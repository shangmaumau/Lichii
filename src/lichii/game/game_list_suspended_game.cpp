// game_list_suspended_game.cpp
// Lichii
// Author: shangmaomao
// Created on 2026-11-26
// SPDX-FileCopyrightText: 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAbstractItemView>
#include <QFileInfo>
#include <QListView>

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
           (running_file.fileName() == QStringLiteral("main") &&
            library_absolute.compare(running_file.absolutePath(), sensitivity) == 0);
}

} // namespace

void GameList::SetSuspendedGame(QString path, u64 program_id) {
    for (const auto& row : item_model->match(item_model->index(0, 0),
                                            GameListItemPath::SuspendedRole, true, -1,
                                            Qt::MatchExactly | Qt::MatchRecursive)) {
        item_model->setData(row, {}, GameListItemPath::SuspendedRole);
        item_model->setData(row, {}, Qt::AccessibleDescriptionRole);
    }

    suspended_game_path = std::move(path);
    suspended_program_id = program_id;
    UpdateSuspendedGameRows();

    if (current_page == home_view) {
        home_view->Refresh();
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
            if (path.isEmpty() || !IsSameGamePath(path, suspended_game_path)) {
                continue;
            }

            item_model->setData(row, true, GameListItemPath::SuspendedRole);
            item_model->setData(row, tr("Game in progress"), Qt::AccessibleDescriptionRole);
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
    const auto selected = UpdateSuspendedGameRows();
    if (!selected.isValid()) {
        resume_unlisted_game->setFocus();
        return;
    }

    if (current_page == home_view) {
        home_view->ReturnToShelf();
        home_view->Refresh();
        home_view->FocusSuspendedGame();
        return;
    }

    if (m_isTreeMode) {
        tree_view->expand(selected.parent());
    }
    m_currentView->setCurrentIndex(selected);
    m_currentView->scrollTo(selected);
    RestoreFocus();
}
