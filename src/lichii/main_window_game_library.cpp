// main_window_game_library.cpp
// Lichii
// Author: shangmaomao
// Created on 2026-11-26
// SPDX-FileCopyrightText: 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QLayout>
#include <QProgressDialog>
#include <QTimer>

#include "common/logging.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/hle/service/am/am_types.h"
#include "lichii/bootmanager.h"
#include "lichii/game/game_list.h"
#include "lichii/loading_screen.h"
#include "lichii/main_window.h"
#include "lichii/util/controller_ui_navigation.h"
#include "lichii/util/game_scene_transition.h"
#include "qt_common/qt_common.h"
#include "qt_common/render/emu_thread.h"
#include "ui_main.h"
#include "video_core/renderer_base.h"

namespace {

struct PausedFrameCapture {
    enum class Status { Pending, Ready, Inverted };

    QImage pixels;
    std::atomic<Status> status{Status::Pending};
};

} // namespace

// Keep the loaded game in memory while the library takes over the main window.
void MainWindow::PauseToGameLibrary() {
    const auto* modal = QApplication::activeModalWidget();
    if (close_pending || shutdown_game_in_progress || shutdown_dialog ||
        QApplication::applicationState() != Qt::ApplicationActive ||
        (modal && modal != boot_preparation_dialog) || QApplication::activePopupWidget()) {
        return;
    }

    if ((game_scene_transition->IsExpanding() || game_scene_transition->IsShrinking()) &&
        game_scene_transition->Reverse()) {
        return;
    }

    if (boot_in_progress && !emulation_running) {
        return_to_library_during_boot = true;
        if (boot_preparation_dialog) {
            boot_preparation_dialog->hide();
        }
        game_scene_transition->Reverse();
        return;
    }

    if (!emulation_running || game_suspended_to_library || pause_capture_pending ||
        QtCommon::system->IsShuttingDown() || !QtCommon::emu_thread) {
        return;
    }
    if (awaiting_first_game_frame || loading_screen->isVisible()) {
        ReturnToLibraryWhileLoading();
        return;
    }
    if (!render_window->isVisible() || !render_window->IsLoadingComplete() ||
        QApplication::activeWindow() != render_window->window()) {
        return;
    }

    constexpr u64 switch2_home_program_id = 0x0400000000001000ULL;
    const u64 program_id = QtCommon::system->GetApplicationProcessProgramID();
    if (Service::AM::IsAppletProgramId(program_id) || program_id == switch2_home_program_id ||
        current_game_path.isEmpty()) {
        return;
    }

    const auto& viewport = render_window->GetFramebufferLayout();
    auto& renderer = QtCommon::system->Renderer();
    if (!QtCommon::emu_thread->IsRunning() || renderer.IsScreenshotPending() ||
        viewport.width == 0 || viewport.height == 0) {
        FinishPauseToGameLibrary(program_id, {});
        return;
    }

    const QSize extent = QSize(viewport.width, viewport.height).scaled(640, 640, Qt::KeepAspectRatio);
    if (extent.isEmpty()) {
        FinishPauseToGameLibrary(program_id, {});
        return;
    }
    const auto layout = Layout::DefaultFrameLayout(extent.width(), extent.height());
    auto capture = std::make_shared<PausedFrameCapture>();
    capture->pixels = QImage(extent, QImage::Format_RGB32);
    if (capture->pixels.isNull()) {
        FinishPauseToGameLibrary(program_id, {});
        return;
    }

    pause_capture_pending = true;
    const QPointer<EmuThread> session(QtCommon::emu_thread.get());
    renderer.RequestScreenshot(
        capture->pixels.bits(),
        [capture](bool inverted) {
            using Status = PausedFrameCapture::Status;
            capture->status.store(inverted ? Status::Inverted : Status::Ready,
                                  std::memory_order_release);
        },
        layout);

    auto* wait = new QTimer(this);
    wait->setTimerType(Qt::PreciseTimer);
    QElapsedTimer elapsed;
    elapsed.start();
    connect(wait, &QTimer::timeout, this, [this, wait, elapsed, capture, session, program_id] {
        using Status = PausedFrameCapture::Status;
        const bool current_session = session && session.data() == QtCommon::emu_thread.get();
        const auto status = capture->status.load(std::memory_order_acquire);
        if (current_session && !close_pending && !shutdown_game_in_progress && !shutdown_dialog &&
            status == Status::Pending && elapsed.elapsed() < 96) {
            return;
        }

        wait->stop();
        wait->deleteLater();
        pause_capture_pending = false;
        if (!current_session) {
            return;
        }
        QPixmap frame;
        if (status == Status::Ready) {
            frame = QPixmap::fromImage(capture->pixels);
        } else if (status == Status::Inverted) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            frame = QPixmap::fromImage(capture->pixels.flipped(Qt::Vertical));
#else
            frame = QPixmap::fromImage(capture->pixels.mirrored(false, true));
#endif
        }
        FinishPauseToGameLibrary(program_id, std::move(frame));
    });
    wait->start(12);
}

void MainWindow::FinishPauseToGameLibrary(u64 program_id, QPixmap frame) {
    if (!emulation_running || game_suspended_to_library || boot_in_progress || close_pending ||
        shutdown_game_in_progress || shutdown_dialog || !QtCommon::emu_thread ||
        QtCommon::system->IsShuttingDown() || !render_window->isVisible() ||
        QtCommon::system->GetApplicationProcessProgramID() != program_id ||
        QApplication::activeWindow() != render_window->window() ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        return;
    }

    if (QtCommon::emu_thread->IsRunning()) {
        OnPauseGame();
    }

    ShowSuspendedGameLibrary(program_id, std::move(frame));
}

void MainWindow::ReturnToLibraryWhileLoading() {
    if (!emulation_running || game_suspended_to_library || close_pending ||
        shutdown_game_in_progress || shutdown_dialog || !QtCommon::emu_thread ||
        QtCommon::system->IsShuttingDown()) {
        return;
    }
    constexpr u64 switch2_home_program_id = 0x0400000000001000ULL;
    const u64 program_id = QtCommon::system->GetApplicationProcessProgramID();
    if (Service::AM::IsAppletProgramId(program_id) || program_id == switch2_home_program_id ||
        current_game_path.isEmpty()) {
        return;
    }

    if (!awaiting_first_game_frame && QtCommon::emu_thread->IsRunning()) {
        OnPauseGame();
    }
    ShowSuspendedGameLibrary(program_id, {}, true);
}

void MainWindow::ShowSuspendedGameLibrary(u64 program_id, QPixmap frame, bool use_loading_frame) {
    game_suspended_to_library = true;
    auto_paused = false;
    auto* surface = centralWidget();
    if (use_loading_frame) {
        frame = loading_screen->TransitionFrame(surface->size());
    } else if (frame.isNull()) {
        frame = QPixmap(1, 1);
        frame.fill(surface->palette().color(QPalette::Window));
    }
    const bool already_shrinking = game_scene_transition->IsShrinking();
    const bool held = !already_shrinking && game_scene_transition->HoldFrame(frame);

    const bool single_window = ui->action_Single_Window_Mode->isChecked();
    const bool leave_fullscreen = ui->action_Fullscreen->isChecked() &&
                                  (render_window->isVisible() ||
                                   (single_window && loading_screen->isVisible()));
    auto* layout = surface->layout();
    const bool pause_layout = single_window && layout && layout->isEnabled();
    if (pause_layout) {
        // Prepare the library without resizing the outgoing native render window.
        layout->setEnabled(false);
    }
    game_list->SetSuspendedGame(current_game_path, program_id, awaiting_first_game_frame);
    game_list->setEnabled(true);
    OnGameListShowList(true);
    controller_ui_navigation->SetGameLibraryActiveWhilePaused(true);
    if (leave_fullscreen) {
        HideFullscreen();
    }
    OnTogglePerfOverlay();
    render_window->hide();
    loading_screen->hide();
    if (pause_layout) {
        layout->setEnabled(true);
        layout->invalidate();
    }
    if (layout) {
        layout->activate();
    }
    if (held) {
        game_scene_transition->RefreshHeldFrame();
    }
    if (QApplication::activeWindow() != this) {
        raise();
        activateWindow();
    }

    game_list->FocusSuspendedGame();
    auto [cover, artwork] = game_list->HomeCoverForGame(current_game_path, program_id, *surface);
    suspended_transition_frame = std::move(frame);
    if (cover.isEmpty()) {
        const QRect area = surface->rect();
        const int side = std::min({128, area.width() / 3, area.height() / 3});
        cover = QRect(0, 0, side, side);
        cover.moveCenter(area.center());
    }
    if (!already_shrinking) {
        game_scene_transition->Shrink(cover, suspended_transition_frame, std::move(artwork));
    }

    LOG_INFO(Frontend, "Paused game {:016X} and returned to the game library", program_id);
}

// Restore the render window and continue the existing emulation thread from its pause point.
void MainWindow::ResumeSuspendedGame() {
    if (!game_suspended_to_library || !emulation_running || !QtCommon::emu_thread ||
        boot_in_progress || game_scene_transition->IsExpanding() || close_pending ||
        shutdown_game_in_progress || shutdown_dialog ||
        QtCommon::system->IsShuttingDown()) {
        return;
    }

    const auto program_id = QtCommon::system->GetApplicationProcessProgramID();
    auto [cover, artwork] = game_list->HomeCoverForGame(current_game_path, program_id, *centralWidget());
    const auto after_zoom = [this] {
        if (QApplication::activeModalWidget()) {
            game_scene_transition->Clear();
            return;
        }
        FinishResumeSuspendedGame();
    };
    if (game_scene_transition->IsShrinking() && game_scene_transition->Reverse(after_zoom)) {
        return;
    }
    if (!game_scene_transition->Expand(cover, std::move(artwork), after_zoom,
                                      suspended_transition_frame)) {
        FinishResumeSuspendedGame();
    }
}

void MainWindow::FinishResumeSuspendedGame() {
    if (!game_suspended_to_library || !emulation_running || !QtCommon::emu_thread ||
        close_pending || shutdown_game_in_progress || shutdown_dialog ||
        QtCommon::system->IsShuttingDown()) {
        game_scene_transition->Clear();
        return;
    }
    suspended_transition_frame = {};
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

    if (awaiting_first_game_frame) {
        loading_screen->show();
        if (ui->action_Fullscreen->isChecked() && ui->action_Single_Window_Mode->isChecked()) {
            ShowFullscreen();
        }
        game_scene_transition->Release();
        if (!QtCommon::emu_thread->IsRunning()) {
            OnStartGame();
        }
        return;
    }

    game_scene_transition->Clear();
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
