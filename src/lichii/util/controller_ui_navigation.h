// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>

class ControllerNavigation;
class FocusHalo;
class QAction;
class QDialog;
class QMainWindow;
class QMenu;
class QWidget;
struct SDL_AudioStream;

namespace Core {
class System;
}

namespace Lichii {
class KeyboardController;
}

class ControllerUiNavigation final : public QObject {
public:
    ControllerUiNavigation(Core::System& system, QMainWindow* window,
                           std::function<QWidget*()> default_focus,
                           std::function<bool()> navigate_back,
                           std::function<bool()> open_game_menu);
    ~ControllerUiNavigation() override;

    void Stop();
    void SetGameLibraryActiveWhilePaused(bool active);
    static void SetInputCapture(QWidget* widget, bool capturing);

private:
    enum class MenuFocus { None, Bar, Preview, Items };
    enum class SoundEffect { Move, Confirm, Back };

    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* ActiveWindow() const;
    void Dispatch(Qt::Key key, bool repeated);
    void SetHeldNavigation(Qt::Key direction, Qt::Key shoulder);
    void PlaySound(SoundEffect effect);
    bool NavigateMainMenu(Qt::Key key, QWidget* window, QWidget* popup);
    void FocusMenuBar(int step = 0);
    void ShowMenu(MenuFocus focus);
    void LeaveMenu();
    void UpdateOutlinedMenus(QMenu* popup);
    void UpdateFocusHalo();

    Core::System& system;
    QMainWindow* main_window;
    std::function<QWidget*()> default_focus;
    std::function<bool()> navigate_back;
    std::function<bool()> open_game_menu;
    ControllerNavigation* controller;
    Lichii::KeyboardController* keyboard;
    MenuFocus menu_focus = MenuFocus::None;
    QPointer<QAction> menu_action;
    QPointer<QMenu> top_menu;
    QPointer<QAction> opening_dialog_from;
    QPointer<QAction> opening_dialog_item;
    QPointer<QDialog> menu_dialog;
    QPointer<QAction> dialog_origin;
    QPointer<QAction> dialog_item;
    QPointer<FocusHalo> halo;
    QPointer<QWidget> controller_tab_window;
    QList<QPointer<QMenu>> outlined_menus;
    QTimer focus_update;
    QTimer repeat;
    SDL_AudioStream* focus_sound = nullptr;
    QPointer<QWidget> repeat_window;
    QPointer<QWidget> repeat_target;
    Qt::Key held_key = Qt::Key_unknown;
    QPoint controller_mouse_position;
    bool stopped = false;
    bool game_library_active_while_paused = false;
    bool controller_active = false;
    bool focus_sound_open_attempted = false;
};
