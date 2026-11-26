// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QStyle>
#include <QTabBar>
#include <QTreeView>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "lichii/configuration/configure_dialog.h"
#include "lichii/configuration/configure_per_game.h"
#include "lichii/game/game_list.h"
#include "lichii/util/controller_navigation.h"
#include "lichii/util/controller_ui_navigation.h"
#include "lichii/util/focus_halo.h"
#include "lichii/util/keyboard_controller.h"
#include "lichii/util/tab_bar_underline.h"

namespace {

constexpr auto InputCaptureProperty = "lichiiControllerInputCapture";
constexpr int FocusSoundSampleRate = 48000;

struct NavigationFocus {
    QPointer<QWidget> widget{};
    QPointer<QAction> action{};
    QPersistentModelIndex item{};
    int tab = -1;

    bool operator==(const NavigationFocus&) const = default;
};

bool SendKey(QWidget* target, Qt::Key key, bool repeated = false) {
    QPointer<QWidget> guard(target);
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, {}, repeated);
    QApplication::sendEvent(target, &press);
    if (guard) {
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(guard, &release);
    }
    return press.isAccepted();
}

void MoveFocus(QWidget* start, QWidget* window, bool forward) {
    auto* next = start;
    do {
        next = forward ? next->nextInFocusChain() : next->previousInFocusChain();
        if (next->window() == window && next->isVisible() && next->isEnabled() &&
            (next->focusPolicy() & Qt::TabFocus) && next->focusProxy() != start) {
            QPointer<QWidget> focused(next);
            next->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
            for (auto* parent = focused ? focused->parentWidget() : nullptr; parent;
                 parent = parent->parentWidget()) {
                if (auto* scroller = qobject_cast<QScrollArea*>(parent)) {
                    scroller->ensureWidgetVisible(focused);
                }
            }
            return;
        }
    } while (next != start);
}

bool IsDirection(Qt::Key key) {
    return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
}

void OpenContextMenu(QAbstractItemView* view) {
    const auto index = view->currentIndex();
    const QPoint at = index.isValid() ? view->visualRect(index).center()
                                      : view->viewport()->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Keyboard, at, view->viewport()->mapToGlobal(at));
    QApplication::sendEvent(view->viewport(), &event);
}

void SelectFirstAction(QMenu* menu) {
    if (!menu || menu->activeAction()) {
        return;
    }
    for (auto* action : menu->actions()) {
        if (action->isVisible() && action->isEnabled() && !action->isSeparator()) {
            menu->setActiveAction(action);
            return;
        }
    }
}

} // namespace

ControllerUiNavigation::ControllerUiNavigation(Core::System& system_, QMainWindow* window,
                                               std::function<QWidget*()> default_focus_,
                                               std::function<bool()> navigate_back_,
                                               std::function<bool()> open_game_menu_)
    : QObject(window), system(system_), main_window(window), default_focus(std::move(default_focus_)),
      navigate_back(std::move(navigate_back_)), open_game_menu(std::move(open_game_menu_)),
      controller(new ControllerNavigation(system.HIDCore(), this)),
      keyboard(new Lichii::KeyboardController(this, [] {
          return Settings::values.language_index.GetValue();
      })) {
    connect(controller, &ControllerNavigation::TriggerKeyboardEvent, this,
            [this](Qt::Key key) { Dispatch(key, false); }, Qt::QueuedConnection);
    connect(controller, &ControllerNavigation::HeldNavigationChanged, this,
            &ControllerUiNavigation::SetHeldNavigation, Qt::QueuedConnection);
    qApp->installEventFilter(this);
    focus_update.setSingleShot(true);
    connect(&focus_update, &QTimer::timeout, this, &ControllerUiNavigation::UpdateFocusHalo);
    connect(qApp, &QApplication::focusChanged, this, [this] { focus_update.start(); });
    connect(qApp, &QApplication::applicationStateChanged, this, [this] { focus_update.start(); });
    connect(&repeat, &QTimer::timeout, this, [this] {
        auto* popup = QApplication::activePopupWidget();
        if (!repeat_window || !repeat_target || ActiveWindow() != repeat_window ||
            (popup ? popup : repeat_window.data()) != repeat_target) {
            repeat.stop();
            return;
        }
        if (repeat.interval() != 75) {
            repeat.setInterval(75);
        }
        Dispatch(held_key, true);
    });
}

ControllerUiNavigation::~ControllerUiNavigation() {
    Stop();
}

void ControllerUiNavigation::Stop() {
    stopped = true;
    repeat.stop();
    focus_update.stop();
    keyboard->Dismiss();
    UpdateOutlinedMenus(nullptr);
    if (halo) {
        halo->hide();
    }
    if (focus_sound) {
        SDL_DestroyAudioStream(std::exchange(focus_sound, nullptr));
    }
    controller->UnloadController();
}

void ControllerUiNavigation::SetGameLibraryActiveWhilePaused(bool active) {
    game_library_active_while_paused = active;
    if (!active) {
        repeat.stop();
    }
    focus_update.start();
}

void ControllerUiNavigation::SetInputCapture(QWidget* widget, bool capturing) {
    widget->window()->setProperty(InputCaptureProperty, capturing);
}

QWidget* ControllerUiNavigation::ActiveWindow() const {
    if (stopped || !Settings::values.controller_navigation ||
        QApplication::applicationState() != Qt::ApplicationActive || QWidget::keyboardGrabber()) {
        return nullptr;
    }
    auto* window = QApplication::activeModalWidget();
    if (!window) {
        window = QApplication::activeWindow();
        if (auto* popup = QApplication::activePopupWidget(); popup && popup->parentWidget()) {
            window = popup->parentWidget()->window();
            while (window->windowType() == Qt::Popup && window->parentWidget()) {
                window = window->parentWidget()->window();
            }
        }
    }
    if (!window || !window->isVisible() || !window->isEnabled() ||
        window->property(InputCaptureProperty).toBool()) {
        return nullptr;
    }
    const bool library_active = game_library_active_while_paused && window == main_window;
    if (system.IsPoweredOn() && !library_active && !qobject_cast<QDialog*>(window)) {
        return nullptr;
    }
    return window;
}

void ControllerUiNavigation::SetHeldNavigation(Qt::Key direction, Qt::Key shoulder) {
    auto* window = ActiveWindow();
    auto* popup = QApplication::activePopupWidget();
    auto* target = popup ? popup : window;
    const bool repeat_shoulder = shoulder != Qt::Key_unknown && !popup &&
                                 (qobject_cast<ConfigureDialog*>(window) ||
                                  qobject_cast<ConfigurePerGame*>(window));
    const auto key = repeat_shoulder ? shoulder : direction;
    if (key == held_key && window == repeat_window && target == repeat_target) {
        return;
    }
    held_key = key;
    repeat.stop();
    repeat_window = window;
    repeat_target = target;
    if (window && key != Qt::Key_unknown) {
        repeat.start(325);
    }
}

void ControllerUiNavigation::PlaySound(SoundEffect effect) {
    const float volume = Settings::Volume();
    if (volume <= 0.0f) {
        return;
    }
    if (!focus_sound_open_attempted) {
        focus_sound_open_attempted = true;
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const SDL_AudioSpec format{
                .format = SDL_AUDIO_F32, .channels = 1, .freq = FocusSoundSampleRate};
            focus_sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format,
                                                   nullptr, nullptr);
            if (focus_sound && !SDL_ResumeAudioStreamDevice(focus_sound)) {
                SDL_DestroyAudioStream(std::exchange(focus_sound, nullptr));
            }
        }
    }
    if (!focus_sound) {
        return;
    }

    const auto make_sound = [](int duration, float first, float last, float attack = 0.0008f,
                               float overtone = 0.26f, float ratio = 1100.0f / 680.0f) {
        std::vector<float> samples(FocusSoundSampleRate * duration / 1000);
        for (size_t i = 0; i < samples.size(); ++i) {
            const float time = static_cast<float>(i) / FocusSoundSampleRate;
            const float position = static_cast<float>(i) / (samples.size() - 1);
            const float remaining = 1.0f - position;
            const float envelope = std::min(time / attack, 1.0f) * remaining * remaining;
            const float phase =
                2.0f * std::numbers::pi_v<float> * time * std::lerp(first, last, position / 2.0f);
            samples[i] = 0.12f * envelope * (std::sin(phase) + overtone * std::sin(phase * ratio));
        }
        return samples;
    };
    static const std::array sounds{make_sound(8, 680.0f, 680.0f),
                                   make_sound(19, 1020.0f, 1660.0f, 0.00035f, 0.37f, 2.12f),
                                   make_sound(21, 1190.0f, 580.0f, 0.00035f, 0.37f, 2.12f)};
    const auto& sound = sounds[static_cast<size_t>(effect)];

    // Preserve every focus change when several inputs arrive in one audio buffer.
    SDL_SetAudioStreamGain(focus_sound, std::min(volume, 1.0f));
    const auto bytes = static_cast<int>(sound.size() * sizeof(float));
    if (SDL_PutAudioStreamData(focus_sound, sound.data(), bytes)) {
        SDL_FlushAudioStream(focus_sound);
    }
}

bool ControllerUiNavigation::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show) {
        if (auto* tabs = qobject_cast<QTabBar*>(watched)) {
            TabBarUnderline::Attach(tabs);
        }
    } else if (event->type() == QEvent::Enter) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            widget->setMouseTracking(true);
        }
    }
    if (event->type() == QEvent::Show && opening_dialog_from) {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (dialog && dialog->parentWidget() && dialog->parentWidget()->window() == main_window &&
            (!menu_dialog || !menu_dialog->isVisible())) {
            menu_dialog = dialog;
            dialog_origin = opening_dialog_from;
            dialog_item = opening_dialog_item;
        }
    }
    const bool moved_mouse = event->type() == QEvent::MouseMove &&
                             static_cast<QMouseEvent*>(event)->globalPosition().toPoint() !=
                                 controller_mouse_position;
    if (moved_mouse || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel ||
        (event->type() == QEvent::KeyPress && event->spontaneous())) {
        if (controller_active) {
            controller_active = false;
            menu_focus = MenuFocus::None;
            repeat.stop();
            focus_update.start();
        }
    } else if (controller_active &&
               (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        if (watched != halo.data()) {
            focus_update.start();
        }
    }
    return QObject::eventFilter(watched, event);
}

void ControllerUiNavigation::FocusMenuBar(int step) {
    auto* bar = main_window->menuBar();
    QList<QAction*> entries;
    for (auto* action : bar->actions()) {
        if (action->isVisible() && action->isEnabled() && action->menu()) {
            entries.append(action);
        }
    }
    if (entries.isEmpty() || !bar->isVisible()) {
        return;
    }
    const auto current = entries.indexOf(menu_action.data());
    const auto index = current < 0 ? 0 : (current + step + entries.size()) % entries.size();
    menu_action = entries.at(index);
    if (step != 0) {
        ShowMenu(MenuFocus::Preview);
    } else {
        menu_focus = MenuFocus::Bar;
        if (!main_window->isActiveWindow()) {
            main_window->activateWindow();
        }
        bar->setFocus(Qt::OtherFocusReason);
    }
}

void ControllerUiNavigation::ShowMenu(MenuFocus focus) {
    if (!menu_action || !menu_action->menu()) {
        return;
    }
    auto* bar = main_window->menuBar();
    if (top_menu && top_menu != menu_action->menu()) {
        top_menu->close();
    }
    menu_focus = focus;
    top_menu = menu_action->menu();
    if (!top_menu->isVisible()) {
        top_menu->popup(bar->mapToGlobal(bar->actionGeometry(menu_action).bottomLeft() + QPoint(0, 1)));
    }
    if (focus == MenuFocus::Items) {
        SelectFirstAction(top_menu);
    } else {
        top_menu->setActiveAction(nullptr);
    }
}

void ControllerUiNavigation::LeaveMenu() {
    if (top_menu) {
        top_menu->close();
    }
    menu_focus = MenuFocus::None;
    if (auto* target = default_focus(); target && target->isVisible() && target->isEnabled()) {
        if (!main_window->isActiveWindow()) {
            main_window->activateWindow();
        }
        target->setFocus(Qt::OtherFocusReason);
    }
}

bool ControllerUiNavigation::NavigateMainMenu(Qt::Key key, QWidget* window, QWidget* popup) {
    if (window != main_window) {
        return false;
    }
    auto* bar = main_window->menuBar();
    if (popup && popup != top_menu) {
        return false;
    }
    if (!popup && QApplication::focusWidget() != bar) {
        menu_focus = MenuFocus::None;
    }
    if (key == Qt::Key_F10) {
        if (menu_focus == MenuFocus::None) {
            FocusMenuBar();
        } else {
            LeaveMenu();
        }
        return true;
    }
    if (menu_focus == MenuFocus::None) {
        auto* target = QApplication::focusWidget();
        if (!popup && key == Qt::Key_Up && target &&
            target->property("controllerMenuAbove").toBool()) {
            FocusMenuBar();
            return true;
        }
        return false;
    }
    if (menu_focus == MenuFocus::Items && popup) {
        const auto* action = top_menu->activeAction();
        if (key == Qt::Key_Left || (key == Qt::Key_Right && (!action || !action->menu()))) {
            FocusMenuBar(key == Qt::Key_Left ? -1 : 1);
            return true;
        }
        if (key == Qt::Key_Escape) {
            top_menu->close();
            FocusMenuBar();
            return true;
        }
        return false;
    }

    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
        FocusMenuBar(key == Qt::Key_Left ? -1 : 1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Escape:
        LeaveMenu();
        break;
    case Qt::Key_Enter:
        ShowMenu(MenuFocus::Items);
        break;
    default:
        break;
    }
    return true;
}

void ControllerUiNavigation::UpdateFocusHalo() {
    auto* window = controller_active ? ActiveWindow() : nullptr;
    if (window != controller_tab_window) {
        if (controller_tab_window) {
            TabBarUnderline::SetControllerActive(controller_tab_window, false);
        }
        controller_tab_window = window;
        if (window) {
            TabBarUnderline::SetControllerActive(window, true);
        }
    }
    if (!window || keyboard->IsOpenFor(window)) {
        UpdateOutlinedMenus(nullptr);
        if (halo) {
            halo->hide();
        }
        return;
    }

    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    UpdateOutlinedMenus(popup);

    QWidget* widget = QApplication::focusWidget();
    QRect area;
    qreal radius = 6;
    if (popup && popup->activeAction() && popup->activeAction()->isEnabled() &&
        (popup != top_menu || menu_focus != MenuFocus::Preview)) {
        widget = popup;
        area = popup->actionGeometry(popup->activeAction()).adjusted(6, 4, -6, -4);
    } else if (window == main_window && menu_action &&
               (menu_focus == MenuFocus::Bar || menu_focus == MenuFocus::Preview)) {
        auto* bar = main_window->menuBar();
        widget = bar;
        area = bar->actionGeometry(menu_action).adjusted(4, 4, -4, -4);
    } else if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
        area = tabs->tabRect(tabs->currentIndex()).adjusted(4, 3, -4, -3);
    } else if (auto* view = qobject_cast<QAbstractItemView*>(widget);
               view && view->property("controllerHighlightItem").toBool()) {
        widget = view->viewport();
        area = view->visualRect(view->currentIndex()).adjusted(6, 3, -6, -3);
    } else if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget) ||
               qobject_cast<QLineEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) ||
               qobject_cast<QAbstractSlider*>(widget)) {
        area = widget->rect();
        if (qobject_cast<QCheckBox*>(widget) || qobject_cast<QRadioButton*>(widget)) {
            area.setWidth(std::min(area.width(), widget->sizeHint().width()));
        }
        if (const auto corner = widget->property("focusHaloRadius"); corner.isValid()) {
            radius = corner.toReal();
        }
    }
    if (area.isEmpty() || !widget->isVisible()) {
        if (halo) {
            halo->hide();
        }
        return;
    }
    auto* surface = widget->window();
    area.translate(widget->mapTo(surface, QPoint{}));
    for (auto* parent = widget; parent != surface; parent = parent->parentWidget()) {
        area &= QRect(parent->mapTo(surface, QPoint{}), parent->size());
    }
    if (!halo) {
        halo = new FocusHalo(surface);
    } else if (halo->parentWidget() != surface) {
        halo->setParent(surface);
    }
    halo->ShowAround(area, radius);
}

void ControllerUiNavigation::UpdateOutlinedMenus(QMenu* popup) {
    QList<QPointer<QMenu>> menus;
    for (auto* menu = popup; menu; menu = qobject_cast<QMenu*>(menu->parentWidget())) {
        menus.append(menu);
    }
    const auto set_outline = [](QMenu* menu, bool enabled) {
        menu->setProperty("controllerMenuFocus", enabled);
        menu->style()->unpolish(menu);
        menu->style()->polish(menu);
        menu->update();
    };
    for (const auto& menu : outlined_menus) {
        if (menu && !menus.contains(menu)) {
            set_outline(menu, false);
        }
    }
    for (const auto& menu : menus) {
        if (!outlined_menus.contains(menu)) {
            set_outline(menu, true);
        }
    }
    outlined_menus = std::move(menus);
}

void ControllerUiNavigation::Dispatch(Qt::Key key, bool repeated) {
    auto* window = ActiveWindow();
    if (!window) {
        return;
    }
    controller_active = true;
    controller_mouse_position = QCursor::pos();
    focus_update.start();

    auto* popup = QApplication::activePopupWidget();
    const bool main_menu_focused = window == main_window && menu_focus != MenuFocus::None &&
                                   QApplication::focusWidget() == main_window->menuBar();
    if (!popup && (!main_menu_focused || keyboard->IsOpenFor(window)) &&
        keyboard->HandleControllerKey(window, key)) {
        if (key == Qt::Key_Enter || key == Qt::Key_F10 || key == Qt::Key_Escape) {
            PlaySound(key == Qt::Key_Escape ? SoundEffect::Back : SoundEffect::Confirm);
        }
        return;
    }
    if (key == Qt::Key_F10 && window == main_window && !popup && open_game_menu()) {
        SelectFirstAction(qobject_cast<QMenu*>(QApplication::activePopupWidget()));
        PlaySound(SoundEffect::Confirm);
        return;
    }

    const bool navigation_key = IsDirection(key) || key == Qt::Key_Tab || key == Qt::Key_Backtab ||
                                key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_F10;
    const auto selection = [this, window] {
        if (window == main_window && menu_action &&
            (menu_focus == MenuFocus::Bar || menu_focus == MenuFocus::Preview)) {
            return NavigationFocus{main_window->menuBar(), menu_action};
        }
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            return NavigationFocus{menu, menu->activeAction()};
        }

        auto* widget = QApplication::focusWidget();
        NavigationFocus focus{widget};
        if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
            focus.item = view->currentIndex();
        } else if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
            focus.tab = tabs->currentIndex();
        }
        return focus;
    };
    const auto previous = navigation_key ? selection() : NavigationFocus{};
    SCOPE_EXIT {
        if (navigation_key && controller_active && ActiveWindow()) {
            const auto current = selection();
            if (current.widget && current != previous) {
                PlaySound(SoundEffect::Move);
            }
        }
    };

    // File dialogs opened by a controller action need a Qt focus chain.
    const bool qt_dialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    if (key == Qt::Key_Enter) {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    SCOPE_EXIT {
        if (key == Qt::Key_Enter) {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, qt_dialogs);
        }
    };

    if (NavigateMainMenu(key, window, popup)) {
        if (key == Qt::Key_Enter || key == Qt::Key_Escape) {
            PlaySound(key == Qt::Key_Enter ? SoundEffect::Confirm : SoundEffect::Back);
        }
        return;
    }
    if (popup) {
        if (key == Qt::Key_F10) {
            popup->close();
        } else if (key != Qt::Key_Tab && key != Qt::Key_Backtab && key != Qt::Key_Menu) {
            auto* focus = QApplication::focusWidget();
            if (key == Qt::Key_Escape) {
                PlaySound(SoundEffect::Back);
            } else if (key == Qt::Key_Enter) {
                auto* menu = qobject_cast<QMenu*>(popup);
                if (!menu || (menu->activeAction() && menu->activeAction()->isEnabled())) {
                    PlaySound(SoundEffect::Confirm);
                }
            }
            const auto origin = key == Qt::Key_Enter && window == main_window && top_menu &&
                                        top_menu->isVisible() && menu_focus == MenuFocus::Items
                                    ? menu_action
                                    : QPointer<QAction>{};
            QScopedValueRollback remember(opening_dialog_from, origin);
            QScopedValueRollback remember_item(opening_dialog_item,
                                               QPointer<QAction>(origin ? top_menu->activeAction()
                                                                        : nullptr));
            SendKey(focus && focus->window() == popup ? focus : popup, key);
            if (key == Qt::Key_Right || key == Qt::Key_Enter) {
                QTimer::singleShot(0, this, [] {
                    SelectFirstAction(qobject_cast<QMenu*>(QApplication::activePopupWidget()));
                });
            }
        }
        return;
    }
    if (key == Qt::Key_F10) {
        return;
    }
    auto* settings = qobject_cast<ConfigureDialog*>(window);
    auto* per_game = qobject_cast<ConfigurePerGame*>(window);
    if ((settings && settings->HandleControllerNavigation(key)) ||
        (per_game && per_game->HandleControllerNavigation(key, repeated))) {
        if (key == Qt::Key_Enter || key == Qt::Key_Escape) {
            PlaySound(key == Qt::Key_Enter ? SoundEffect::Confirm : SoundEffect::Back);
        }
        return;
    }
    if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        return;
    }
    if (key == Qt::Key_Escape && window == main_window && navigate_back && navigate_back()) {
        PlaySound(SoundEffect::Back);
        return;
    }
    if (key == Qt::Key_Escape && menu_dialog && window == menu_dialog) {
        const auto dialog = menu_dialog;
        const auto origin = dialog_origin;
        const auto item = dialog_item;
        if (SendKey(dialog, key)) {
            PlaySound(SoundEffect::Back);
        }
        if (!dialog || !dialog->isVisible()) {
            menu_dialog.clear();
            dialog_origin.clear();
            dialog_item.clear();
            QTimer::singleShot(0, this, [this, origin, item] {
                if (origin && !stopped && main_window->isVisible() && main_window->isEnabled() &&
                    QApplication::applicationState() == Qt::ApplicationActive &&
                    !QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
                    menu_action = origin;
                    FocusMenuBar();
                    ShowMenu(MenuFocus::Items);
                    if (item && item->isVisible() && item->isEnabled() && top_menu &&
                        top_menu->actions().contains(item)) {
                        top_menu->setActiveAction(item);
                    }
                }
            });
        }
        return;
    }

    auto* target = QApplication::focusWidget();
    if (!target || !target->isVisible() || !target->isEnabled() || target->window() != window ||
        target == window) {
        target = window == main_window ? default_focus() : nullptr;
        if (!target || !target->isVisible() || !target->isEnabled()) {
            target = window;
        } else {
            target->setFocus(Qt::OtherFocusReason);
        }
    }

    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        MoveFocus(target, window, key == Qt::Key_Tab);
        return;
    }

    for (const auto* parent = target; parent; parent = parent->parentWidget()) {
        if (qobject_cast<const QKeySequenceEdit*>(parent)) {
            if (key == Qt::Key_Escape) {
                SendKey(window, key);
            }
            return;
        }
    }

    auto* view = qobject_cast<QAbstractItemView*>(target);
    if (window == main_window && (IsDirection(key) || key == Qt::Key_Enter)) {
        for (auto* parent = target; parent; parent = parent->parentWidget()) {
            if (auto* library = qobject_cast<GameList*>(parent)) {
                switch (library->HandleControllerNavigation(*target, key, repeated)) {
                case GameList::ControllerNavigation::FocusMenu:
                    FocusMenuBar();
                    return;
                case GameList::ControllerNavigation::Handled:
                    if (key == Qt::Key_Enter) {
                        PlaySound(SoundEffect::Confirm);
                    }
                    return;
                case GameList::ControllerNavigation::Unhandled:
                    break;
                }
                break;
            }
        }
    }
    if (IsDirection(key)) {
        const bool uses_focus =
            qobject_cast<QComboBox*>(target) || qobject_cast<QLineEdit*>(target);
        if (!uses_focus && SendKey(target, key, repeated)) {
            return;
        }
        MoveFocus(target, window, key == Qt::Key_Right || key == Qt::Key_Down);
    } else if (key == Qt::Key_Enter) {
        if (auto* button = qobject_cast<QAbstractButton*>(target)) {
            auto* box = qobject_cast<QDialogButtonBox*>(button->parentWidget());
            const bool back = button->property("controllerBackButton").toBool() ||
                              (box && box->buttonRole(button) == QDialogButtonBox::RejectRole);
            PlaySound(back ? SoundEffect::Back : SoundEffect::Confirm);
            SendKey(button, Qt::Key_Space);
        } else if (auto* combo = qobject_cast<QComboBox*>(target)) {
            PlaySound(SoundEffect::Confirm);
            combo->showPopup();
        } else if (auto* tree = qobject_cast<QTreeView*>(view);
                   tree && tree->currentIndex().isValid() &&
                   tree->model()->hasChildren(tree->currentIndex())) {
            const auto index = tree->currentIndex();
            PlaySound(SoundEffect::Confirm);
            tree->setExpanded(index, !tree->isExpanded(index));
        } else if (view && (view->currentIndex().flags() & Qt::ItemIsUserCheckable)) {
            PlaySound(SoundEffect::Confirm);
            SendKey(view, Qt::Key_Space);
        } else if (view && view->currentIndex().isValid()) {
            PlaySound(SoundEffect::Confirm);
            SendKey(view, key);
        } else {
            if (SendKey(target, key)) {
                PlaySound(SoundEffect::Confirm);
            }
        }
    } else if (key == Qt::Key_Menu) {
        QPointer<QAbstractItemView> current_view(view);
        if (!SendKey(target, key) && current_view) {
            OpenContextMenu(current_view);
        }
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            SelectFirstAction(menu);
            PlaySound(SoundEffect::Confirm);
        }
    } else {
        if (SendKey(target, key) && key == Qt::Key_Escape) {
            PlaySound(SoundEffect::Back);
        }
    }
}
