// game_list_suspended_game.cpp
// Lichii
// Author: shangmaomao
// Created on 2026-11-26
// SPDX-FileCopyrightText: 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QFileInfo>
#include <QListView>
#include <QPushButton>

#include "lichii/game/game_card.h"
#include "lichii/game/game_home.h"
#include "lichii/game/game_list.h"
#include "lichii/game/game_tree.h"
#include "qt_common/game_list/game_list_p.h"

namespace {

// Loose game folders are represented by their directory in the library and by main at runtime.
bool IsSameGamePath(const QString& library_path, const QString& running_path) {
    const auto library_absolute = QFileInfo(library_path).absoluteFilePath();
    const QFileInfo running_file(running_path);
#ifdef _WIN32
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return library_absolute.compare(running_file.absoluteFilePath(), sensitivity) == 0 ||
           (running_file.fileName().compare(QStringLiteral("main"), sensitivity) == 0 &&
            library_absolute.compare(running_file.absolutePath(), sensitivity) == 0);
}

} // namespace

bool GameList::IsSuspendedGame(const QString& path, u64 program_id) const {
    return !path.isEmpty() && !suspended_game_path.isEmpty() &&
           (program_id == 0 || program_id == suspended_program_id) &&
           IsSameGamePath(path, suspended_game_path);
}

QPair<QRect, QPixmap> GameList::TransitionArtworkForGame(const QString& filename, u64 program_id,
                                                        const QWidget& surface) const {
    if (!m_currentView || filename.isEmpty()) {
        return {};
    }
    if (resume_unlisted_game->isVisible() && IsSuspendedGame(filename, program_id)) {
        return {QRect(resume_unlisted_game->mapTo(&surface, QPoint{}), resume_unlisted_game->size()),
                resume_unlisted_game->grab()};
    }
    if (current_page == home_view) {
        return home_view->CoverForGame(filename, program_id, surface);
    }
    const auto* model = m_currentView->model();
    if (!model || !m_currentView->isVisible()) {
        return {};
    }

    auto* viewport = m_currentView->viewport();
    const auto cover_for = [&](const QModelIndex& index) -> QPair<QRect, QPixmap> {
        if (!index.isValid()) {
            return {};
        }
        const auto path = index.data(GameListItemPath::FullPathRole).toString();
        if (path.isEmpty() || !IsSameGamePath(path, filename) ||
            (program_id != 0 && index.data(GameListItemPath::ProgramIdRole).toULongLong() != program_id)) {
            return {};
        }

        const QRect cell = m_currentView->visualRect(index);
        if (cell.intersected(viewport->rect()).isEmpty()) {
            return {};
        }
        QRect cover;
        QRect item = cell;
        if (m_isTreeMode) {
            cover = tree_view->CoverRect(index);
        } else if (const auto* card = qobject_cast<const GameCard*>(
                       m_currentView->itemDelegateForIndex(index))) {
            QStyleOptionViewItem option;
            option.rect = cell;
            cover = card->CoverRect(option, index);
            item = card->getCardRect(option, index);
        }
        auto artwork = index.data(Qt::DecorationRole).value<QPixmap>();
        const bool use_cover = !cover.isEmpty() && !artwork.isNull();
        const QRect bounds = use_cover ? cover : item;
        const QRect visible = bounds.intersected(viewport->rect());
        if (visible.isEmpty()) {
            return {};
        }
        if (!use_cover) {
            artwork = viewport->grab(visible);
        } else if (visible != bounds) {
            const qreal x_scale = qreal(artwork.width()) / bounds.width();
            const qreal y_scale = qreal(artwork.height()) / bounds.height();
            const QRectF source((visible.x() - bounds.x()) * x_scale,
                                (visible.y() - bounds.y()) * y_scale,
                                visible.width() * x_scale, visible.height() * y_scale);
            artwork = artwork.copy(source.toAlignedRect().intersected(artwork.rect()));
        }
        return {QRect(viewport->mapTo(&surface, visible.topLeft()), visible.size()),
                std::move(artwork)};
    };

    auto selected = cover_for(m_currentView->currentIndex().siblingAtColumn(0));
    if (!selected.first.isEmpty()) {
        return selected;
    }
    const auto root = model->index(0, 0, m_currentView->rootIndex());
    const auto candidates = model->match(
        root, program_id != 0 ? GameListItemPath::ProgramIdRole : GameListItemPath::FullPathRole,
        program_id != 0 ? QVariant::fromValue(qulonglong(program_id)) : QVariant(filename), -1,
        Qt::MatchExactly | Qt::MatchRecursive);
    for (const auto& index : candidates) {
        auto result = cover_for(index);
        if (!result.first.isEmpty()) {
            return result;
        }
    }
    return {};
}

void GameList::SetSuspendedGame(QString path, u64 program_id, bool loading) {
    for (const auto& row : item_model->match(item_model->index(0, 0),
                                            GameListItemPath::SuspendedRole, true, -1,
                                            Qt::MatchExactly | Qt::MatchRecursive)) {
        item_model->setData(row, {}, GameListItemPath::SuspendedRole);
        item_model->setData(row, {}, Qt::AccessibleDescriptionRole);
    }

    suspended_game_path = std::move(path);
    suspended_program_id = program_id;
    suspended_game_loading = loading;
    UpdateSuspendedGameRows();

    if (current_page == home_view) {
        home_view->Refresh();
        home_view->View()->viewport()->update();
    }
}

void GameList::SetSuspendedGameLoading(bool loading) {
    if (suspended_game_path.isEmpty() || suspended_game_loading == loading) {
        return;
    }
    suspended_game_loading = loading;
    UpdateSuspendedGameRows();
    if (current_page == home_view) {
        home_view->View()->viewport()->update();
    }
}

QModelIndex GameList::UpdateSuspendedGameRows() {
    QModelIndex selected;
    if (!suspended_game_path.isEmpty()) {
        const auto matches = item_model->match(item_model->index(0, 0),
                                               GameListItemPath::ProgramIdRole,
                                               qulonglong(suspended_program_id), -1,
                                               Qt::MatchExactly | Qt::MatchRecursive);
        for (const auto& row : matches) {
            const auto path = row.data(GameListItemPath::FullPathRole).toString();
            if (!IsSuspendedGame(path)) {
                continue;
            }

            item_model->setData(row, true, GameListItemPath::SuspendedRole);
            const auto status = suspended_game_loading
                                    ? QCoreApplication::translate("LoadingScreen", "Loading...")
                                    : tr("Playing");
            item_model->setData(row, status, Qt::AccessibleDescriptionRole);
            if (!selected.isValid() || (m_isTreeMode && row.parent().row() != 0)) {
                selected = row;
            }
        }
    }

    resume_unlisted_game->setVisible(!suspended_game_path.isEmpty() && !selected.isValid());

    return selected;
}

void GameList::FocusSuspendedGame() {
    ClearFilter();
    auto selected = UpdateSuspendedGameRows();
    if (!selected.isValid()) {
        resume_unlisted_game->setFocus();
        return;
    }

    if (current_page == home_view) {
        home_view->ReturnToShelf(false);
        home_view->Refresh();
        home_view->FocusSuspendedGame();
        return;
    }

    const auto current = m_currentView->currentIndex().siblingAtColumn(0);
    if (current.data(GameListItemPath::SuspendedRole).toBool()) {
        selected = current;
    }
    if (m_isTreeMode) {
        tree_view->expand(selected.parent());
    }
    m_currentView->setCurrentIndex(selected);
    m_currentView->scrollTo(selected);
    RestoreFocus();
}
