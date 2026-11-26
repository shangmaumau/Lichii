// main_window_game_library.cpp
// Lichii
// Author: shangmaomao
// Created on 2026-11-26
// SPDX-FileCopyrightText: 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/service/am/am_types.h"
#include "lichii/bootmanager.h"
#include "lichii/game/game_list.h"
#include "lichii/main_window.h"
#include "lichii/util/controller_ui_navigation.h"
#include "qt_common/qt_common.h"
#include "qt_common/render/emu_thread.h"
#include "ui_main.h"

// Keep the loaded game in memory while the library takes over the main window.
void MainWindow::PauseToGameLibrary() {
    if (!emulation_running || game_suspended_to_library || boot_in_progress || close_pending ||
        shutdown_game_in_progress || shutdown_dialog || QtCommon::system->IsShuttingDown() ||
        !QtCommon::emu_thread || !render_window->isVisible() || !render_window->IsLoadingComplete()) {
        return;
    }

    if (QApplication::applicationState() != Qt::ApplicationActive ||
        QApplication::activeWindow() != render_window->window() ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        return;
    }

    constexpr u64 switch2_home_program_id = 0x0400000000001000ULL;
    const u64 program_id = QtCommon::system->GetApplicationProcessProgramID();
    if (Service::AM::IsAppletProgramId(program_id) || program_id == switch2_home_program_id ||
        current_game_path.isEmpty()) {
        return;
    }

    game_suspended_to_library = true;
    auto_paused = false;
    if (QtCommon::emu_thread->IsRunning()) {
        OnPauseGame();
    }

    if (ui->action_Fullscreen->isChecked()) {
        HideFullscreen();
    }
    OnTogglePerfOverlay();
    render_window->hide();

    game_list->SetSuspendedGame(current_game_path, program_id);
    game_list->setEnabled(true);
    OnGameListShowList(true);
    controller_ui_navigation->SetGameLibraryActiveWhilePaused(true);
    raise();
    activateWindow();
    game_list->FocusSuspendedGame();

    LOG_INFO(Frontend, "Paused game {:016X} and returned to the game library", program_id);
}

// Restore the render window and continue the existing emulation thread from its pause point.
void MainWindow::ResumeSuspendedGame() {
    if (!game_suspended_to_library || !emulation_running || !QtCommon::emu_thread ||
        boot_in_progress || close_pending || shutdown_game_in_progress || shutdown_dialog ||
        QtCommon::system->IsShuttingDown()) {
        return;
    }

    game_suspended_to_library = false;
    controller_ui_navigation->SetGameLibraryActiveWhilePaused(false);
    game_list->setEnabled(false);
    game_list->SetSuspendedGame();

    if (ui->action_Single_Window_Mode->isChecked()) {
        game_list->hide();
        game_list_placeholder->hide();
    } else {
        OnGameListShowList(!game_list->IsEmpty());
    }

    render_window->show();
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    }
    render_window->window()->raise();
    render_window->window()->activateWindow();
    render_window->setFocus();
    OnTogglePerfOverlay();
    OnStartGame();

    LOG_INFO(Frontend, "Resumed game from the game library");
}
