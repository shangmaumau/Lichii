// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThreadPool>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "qt_common/game_list/worker.h"

namespace {

constexpr quint32 SnapshotVersion = 1;
constexpr qint64 MaximumSnapshotBytes = 128 * 1024 * 1024;

QString SnapshotPath() {
    const auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_list" /
                      "library.snapshot";
    return QString::fromStdString(Common::FS::PathToUTF8String(path));
}

QString SnapshotContext(const QVector<UISettings::GameDir>& game_dirs) {
    QJsonArray context{QString::fromStdString(FileSys::GetConfiguredLanguageTag()),
                       QString::fromStdString(UISettings::values.language.GetValue())};
    for (const auto& dir : game_dirs) {
        context.append(QJsonArray{QString::fromStdString(dir.path), dir.deep_scan});
    }
    return QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact));
}

bool IsDirectoryItem(int type) {
    return type >= static_cast<int>(GameListItemType::CustomDir) &&
           type <= static_cast<int>(GameListItemType::SysNandDir);
}

} // namespace

GameListModel::GameListModel(std::shared_ptr<FileSys::VfsFilesystem> vfs_,
                             FileSys::ManualContentProvider* provider_,
                             const PlayTime::PlayTimeManager& play_time_manager_,
                             Core::System& system_, QObject* parent)
    : QStandardItemModel{parent}, vfs{std::move(vfs_)}, provider{provider_},
      play_time_manager{play_time_manager_}, system{system_} {
    watcher = new QFileSystemWatcher(this);
    external_watcher = new QFileSystemWatcher(this);

    connect(watcher, &QFileSystemWatcher::directoryChanged, this,
            &GameListModel::RefreshGameDirectory);
    connect(external_watcher, &QFileSystemWatcher::directoryChanged, this,
            &GameListModel::RefreshExternalContent);

    ResetExternalWatcher();

    insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

    setSortRole(GameListItemPath::SortRole);

    snapshot_writer.setMaxThreadCount(1);
    snapshot_timer.setSingleShot(true);
    snapshot_timer.setInterval(400);
    connect(&snapshot_timer, &QTimer::timeout, this, &GameListModel::SaveSnapshot);
    connect(this, &QStandardItemModel::dataChanged, this, [this] {
        if (!snapshot_context.isEmpty() && UISettings::values.cache_game_list) {
            snapshot_timer.start();
        }
    });
}

GameListModel::~GameListModel() {
    StopWorker();
    if (snapshot_timer.isActive()) {
        SaveSnapshot();
    }
}

void GameListModel::SetScanEnabled(bool enabled, bool stop_worker) {
    scan_enabled = enabled;
    if (!enabled && stop_worker) {
        StopWorker();
    }
}

void GameListModel::PopulateAsync(QVector<UISettings::GameDir>& game_dirs) {
    if (!scan_enabled || system.IsPoweredOn()) {
        return;
    }
    if (!snapshot_attempted) {
        snapshot_attempted = true;
        RestoreSnapshot(game_dirs);
    }
    StopWorker();
    scanning_context = SnapshotContext(game_dirs);
    emit PopulatingStarted();

    current_worker = std::make_unique<GameListWorker>(vfs, provider, game_dirs,
                                                      play_time_manager, system);

    connect(current_worker.get(), &GameListWorker::DataAvailable, this, &GameListModel::WorkerEvent,
            Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(current_worker.get());
}

void GameListModel::WorkerEvent() {
    if (current_worker) {
        current_worker->ProcessEvents(this);
    }
}

void GameListModel::AddDirEntry(GameListDir* entry_items) {
    scanned_directories.emplace_back(entry_items);
}

void GameListModel::AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent) {
    parent->appendRow(entry_items);
}

void GameListModel::DonePopulating(const QStringList& watch_list) {
    if (scanning_context != SnapshotContext(UISettings::values.game_dirs)) {
        PopulateAsync(UISettings::values.game_dirs);
        return;
    }
    std::erase_if(scanned_directories, [](const auto& directory) {
        return !directory->hasChildren() &&
               directory->type() != static_cast<int>(GameListItemType::CustomDir);
    });
    emit ScanResultsReady();
    ShowDirectories(std::exchange(scanned_directories, {}));
    snapshot_context = scanning_context;
    snapshot_timer.start();
    emit PopulatingCompleted(watch_list);
}

void GameListModel::SetFolderSize(const QString& game_path, u64 size_bytes) {
    const auto update_children = [&](QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            // Favorites rows are plain QStandardItem clones (AddFavorite), so match the stored
            // type and set the size text directly instead of relying on the item classes.
            const QStandardItem* path_item = parent->child(row, COLUMN_NAME);
            if (path_item == nullptr ||
                path_item->data(GameListItem::TypeRole).toInt() !=
                    static_cast<int>(GameListItemType::Game) ||
                path_item->data(GameListItemPath::FullPathRole).toString() != game_path) {
                continue;
            }
            if (QStandardItem* size_item = parent->child(row, COLUMN_SIZE)) {
                size_item->setData(qulonglong{size_bytes}, GameListItemSize::SizeRole);
                size_item->setData(QtCommon::ReadableByteSize(size_bytes), Qt::DisplayRole);
                size_item->setToolTip({});
            }
        }
    };
    // Games sit at the top level in the flat list, and under their folder (or Favorites) in
    // the tree.
    update_children(invisibleRootItem());
    for (int row = 0; row < rowCount(); ++row) {
        update_children(item(row));
    }
}

bool GameListModel::IsEmpty() const {
    return m_flat ? rowCount() == 0 : rowCount() <= 2;
}

bool GameListModel::HasGames() const {
    if (m_flat) {
        return rowCount() > 0;
    }
    for (int row = 0; row < rowCount(); ++row) {
        if (const auto* folder = item(row); IsDirectoryItem(folder->type()) && folder->hasChildren()) {
            return true;
        }
    }
    return false;
}

void GameListModel::ToggleFavorite(u64 program_id) {
    if (!UISettings::values.favorited_ids.contains(program_id)) {
        UISettings::values.favorited_ids.append(program_id);
        if (!m_flat && rowCount() > 0) {
            AddFavorite(program_id);
        }
    } else {
        UISettings::values.favorited_ids.removeOne(program_id);
        if (!m_flat && rowCount() > 0) {
            RemoveFavorite(program_id);
        }
    }
    emit SaveConfig();
}

void GameListModel::AddFavorite(u64 program_id) {
    auto* favorites_row = item(0);

    for (int i = 1; i < rowCount() - 1; i++) {
        const auto* folder = item(i);
        for (int j = 0; j < folder->rowCount(); j++) {
            if (folder->child(j)->data(GameListItemPath::ProgramIdRole).toULongLong() ==
                program_id) {
                QList<QStandardItem*> list;
                for (int k = 0; k < COLUMN_COUNT; k++) {
                    list.append(folder->child(j, k)->clone());
                }
                list[0]->setData(folder->child(j)->data(GameListItem::SortRole),
                                 GameListItem::SortRole);
                list[0]->setText(folder->child(j)->data(Qt::DisplayRole).toString());

                favorites_row->appendRow(list);
                return;
            }
        }
    }
}

void GameListModel::RemoveFavorite(u64 program_id) {
    auto* favorites_row = item(0);

    for (int i = 0; i < favorites_row->rowCount(); i++) {
        const auto* game = favorites_row->child(i);
        if (game->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
            favorites_row->removeRow(i);
            return;
        }
    }
}

void GameListModel::StopWorker() {
    if (!current_worker) {
        return;
    }

    // The worker may still be inside ProcessEvents further up the stack (a nested event loop
    // can trigger a repopulate), so stop it now but defer freeing it to the event loop.
    current_worker->disconnect();
    current_worker->Stop();
    current_worker.release()->deleteLater();
    scanned_directories.clear();
}

void GameListModel::Repopulate() {
    // CreateFactories replaces the content providers a running game reads from. Stopping
    // emulation repopulates the list anyway, so skip watcher-driven reloads until then.
    if (!scan_enabled || QtCommon::system->IsPoweredOn()) {
        LOG_INFO(Frontend, "Emulation is running, deferring game list reload.");
        return;
    }

    StopWorker();
    QtCommon::system->GetFileSystemController().CreateFactories(*QtCommon::vfs);
    PopulateAsync(UISettings::values.game_dirs);
}

void GameListModel::RefreshGameDirectory() {
    ResetExternalWatcher();
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "Change detected in the games directory. Reloading game list.");
        Repopulate();
    }
}

void GameListModel::RefreshExternalContent() {
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "External content directory changed. Clearing metadata cache.");
        QtCommon::Game::ResetMetadata(false);
        Repopulate();
    }
}

void GameListModel::ResetExternalWatcher() {
    auto watch_dirs = external_watcher->directories();
    if (!watch_dirs.isEmpty()) {
        external_watcher->removePaths(watch_dirs);
    }

    for (const std::string& dir : Settings::values.external_content_dirs) {
        external_watcher->addPath(QString::fromStdString(dir));
    }
}

void GameListModel::RetranslateUI() {
    setHeaderData(COLUMN_NAME, Qt::Horizontal, tr("Name"));
    setHeaderData(COLUMN_ADD_ONS, Qt::Horizontal, tr("Add-ons"));
    setHeaderData(COLUMN_FILE_TYPE, Qt::Horizontal, tr("File type"));
    setHeaderData(COLUMN_SIZE, Qt::Horizontal, tr("Size"));
    setHeaderData(COLUMN_PLAY_TIME, Qt::Horizontal, tr("Play time"));
}

QFileSystemWatcher* GameListModel::GetWatcher() const {
    return watcher;
}

void GameListModel::SetFlat(bool flat) {
    if (m_flat == flat) {
        return;
    }
    std::vector<std::unique_ptr<GameListDir>> directories;
    if (m_flat) {
        directories = std::move(flat_directories);
        while (rowCount() > 0) {
            auto row = takeRow(0);
            const int directory = row.front()->data(GameListItemPath::DirectoryRole).toInt();
            directories.at(directory)->appendRow(row);
        }
    } else {
        while (rowCount() > 0) {
            const auto row = takeRow(0);
            if (IsDirectoryItem(row.front()->type())) {
                directories.emplace_back(static_cast<GameListDir*>(row.front()));
            } else {
                delete row.front();
            }
        }
    }
    m_flat = flat;
    ShowDirectories(std::move(directories));
}

void GameListModel::ShowDirectories(std::vector<std::unique_ptr<GameListDir>> directories) {
    removeRows(0, rowCount());
    flat_directories.clear();
    if (m_flat) {
        flat_directories = std::move(directories);
        for (int i = 0; i < static_cast<int>(flat_directories.size()); ++i) {
            auto& folder = flat_directories[i];
            while (folder->hasChildren()) {
                auto row = folder->takeRow(0);
                row.front()->setData(i, GameListItemPath::DirectoryRole);
                invisibleRootItem()->appendRow(row);
            }
        }
        return;
    }
    invisibleRootItem()->appendRow(new GameListFavorites());
    for (auto& directory : directories) {
        invisibleRootItem()->appendRow(directory.release());
    }
    invisibleRootItem()->appendRow(new GameListAddDir());
    for (const auto id : std::as_const(UISettings::values.favorited_ids)) {
        AddFavorite(id);
    }
}

void GameListModel::RestoreSnapshot(QVector<UISettings::GameDir>& game_dirs) {
    if (!UISettings::values.cache_game_list) {
        return;
    }
    QFile file(SnapshotPath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumSnapshotBytes) {
        return;
    }
    QDataStream input(&file);
    input.setVersion(QDataStream::Qt_6_0);
    quint32 version{};
    quint32 folder_count{};
    QString context;
    input >> version >> context >> folder_count;
    if (input.status() != QDataStream::Ok || version != SnapshotVersion ||
        context != SnapshotContext(game_dirs) || folder_count > game_dirs.size()) {
        return;
    }

    std::vector<std::unique_ptr<GameListDir>> directories;
    for (quint32 folder = 0; folder < folder_count; ++folder) {
        qint32 index{};
        qint32 type{};
        quint32 count{};
        input >> index >> type >> count;
        if (input.status() != QDataStream::Ok || index < 0 || index >= game_dirs.size() ||
            !IsDirectoryItem(type) || count > 100000) {
            return;
        }
        auto dir = std::make_unique<GameListDir>(game_dirs[index], index,
                                                 static_cast<GameListItemType>(type), true);
        for (quint32 row = 0; row < count; ++row) {
            auto game = std::make_unique<GameListItemPath>();
            auto file_type = std::make_unique<GameListItem>();
            auto size = std::make_unique<GameListItemSize>();
            auto play_time = std::make_unique<GameListItemPlayTime>();
            auto addons = std::make_unique<GameListItem>();
            input >> *game >> *file_type >> *size >> *play_time >> *addons;
            if (input.status() != QDataStream::Ok ||
                game->data(GameListItemPath::FullPathRole).toString().isEmpty()) {
                return;
            }
            auto picture = game->data(Qt::DecorationRole).value<QPixmap>();
            if (!picture.isNull()) {
                picture.setDevicePixelRatio(GameIconDpr);
                game->setData(picture, Qt::DecorationRole);
            }
            dir->appendRow({game.release(), file_type.release(), size.release(),
                            play_time.release(), addons.release()});
        }
        directories.push_back(std::move(dir));
    }
    if (!input.atEnd()) {
        return;
    }
    ShowDirectories(std::move(directories));
    emit SnapshotRestored();
}

void GameListModel::SaveSnapshot() {
    if (!UISettings::values.cache_game_list || snapshot_context.isEmpty() ||
        snapshot_context != SnapshotContext(UISettings::values.game_dirs)) {
        return;
    }
    struct SnapshotFolder {
        qint32 index;
        qint32 type;
        quint32 games{};
        std::vector<std::unique_ptr<QStandardItem>> cells;
    };
    const auto make_folder = [](const QStandardItem& directory) {
        return SnapshotFolder{directory.data(GameListDir::GameDirRole).toInt(), directory.type(), 0,
                              {}};
    };
    const auto capture_game = [this](SnapshotFolder& folder, const QModelIndex& game) {
        for (int column = 0; column < COLUMN_COUNT; ++column) {
            auto item = std::unique_ptr<QStandardItem>(
                itemFromIndex(game.siblingAtColumn(column))->clone());
            // A paused session belongs to this process, so the next launch must not restore it.
            if (item->data(GameListItemPath::SuspendedRole).toBool()) {
                item->setData({}, GameListItemPath::SuspendedRole);
                item->setData({}, Qt::AccessibleDescriptionRole);
            }

            const auto decoration = item->data(Qt::DecorationRole);
            if (decoration.metaType().id() == QMetaType::QPixmap) {
                item->setData(decoration.value<QPixmap>().toImage(), Qt::DecorationRole);
            }
            folder.cells.push_back(std::move(item));
        }
        ++folder.games;
    };

    std::vector<SnapshotFolder> folders;
    if (m_flat) {
        for (const auto& directory : flat_directories) {
            folders.push_back(make_folder(*directory));
        }
        for (int row = 0; row < rowCount(); ++row) {
            const auto game = index(row, 0);
            capture_game(folders[game.data(GameListItemPath::DirectoryRole).toInt()], game);
        }
    } else {
        for (int row = 0; row < rowCount(); ++row) {
            if (const auto* directory = item(row); IsDirectoryItem(directory->type())) {
                auto& folder = folders.emplace_back(make_folder(*directory));
                for (int game = 0; game < directory->rowCount(); ++game) {
                    capture_game(folder, directory->child(game)->index());
                }
            }
        }
    }

    // Keep at most one pending write. The detached items contain QImages instead of QPixmaps.
    snapshot_writer.clear();
    snapshot_writer.start([path = SnapshotPath(), context = snapshot_context,
                           folders = std::move(folders)] {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            return;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return;
        }
        QDataStream output(&file);
        output.setVersion(QDataStream::Qt_6_0);
        output << SnapshotVersion << context << static_cast<quint32>(folders.size());
        for (const auto& folder : folders) {
            output << folder.index << folder.type << folder.games;
            for (const auto& item : folder.cells) {
                output << *item;
            }
        }
        if (output.status() == QDataStream::Ok && file.size() <= MaximumSnapshotBytes) {
            file.commit();
        }
    });
}
