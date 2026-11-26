// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <sstream>
#include <QShortcut>
#include <QTreeWidgetItem>
#include <QtGlobal>

#include "hid_core/frontend/emulated_controller.h"
#include "qt_common/config/uisettings.h"
#include "lichii/hotkeys.h"

HotkeyRegistry::HotkeyRegistry() = default;
HotkeyRegistry::~HotkeyRegistry() = default;

void HotkeyRegistry::SaveHotkeys() {
    UISettings::values.shortcuts.clear();
    for (const auto& group : hotkey_groups) {
        for (const auto& hotkey : group.second) {
            UISettings::values.shortcuts.push_back(
                {hotkey.first, group.first,
                 UISettings::ContextualShortcut({hotkey.second.keyseq.toString().toStdString(),
                                                 hotkey.second.controller_keyseq,
                                                 hotkey.second.context, hotkey.second.repeat})});
        }
    }
}

void HotkeyRegistry::LoadHotkeys() {
    // Make sure NOT to use a reference here because it would become invalid once we call
    // beginGroup()
    for (auto shortcut : UISettings::values.shortcuts) {
        Hotkey& hk = hotkey_groups[shortcut.group][shortcut.name];
        hk.keyseq = QKeySequence::fromString(QString::fromStdString(shortcut.shortcut.keyseq),
                                             QKeySequence::NativeText);
        hk.context = static_cast<Qt::ShortcutContext>(shortcut.shortcut.context);
        hk.controller_keyseq = shortcut.shortcut.controller_keyseq;
        if (hk.shortcut) {
            hk.shortcut->disconnect();
            hk.shortcut->setKey(hk.keyseq);
        }
        if (hk.controller_shortcut) {
            hk.controller_shortcut->disconnect();
            hk.controller_shortcut->SetKey(hk.controller_keyseq);
        }
        hk.repeat = shortcut.shortcut.repeat;
    }
}

QShortcut* HotkeyRegistry::GetHotkey(const std::string& group, const std::string& action,
                                     QWidget* widget) {
    Hotkey& hk = hotkey_groups[group][action];

    if (!hk.shortcut) {
        hk.shortcut = new QShortcut(hk.keyseq, widget, nullptr, nullptr, hk.context);
    }

    hk.shortcut->setAutoRepeat(hk.repeat);
    return hk.shortcut;
}

ControllerShortcut* HotkeyRegistry::GetControllerHotkey(
    const std::string& group, const std::string& action, Core::HID::EmulatedController* controller,
    ControllerShortcutTrigger trigger, Core::HID::EmulatedController* handheld) {
    Hotkey& hk = hotkey_groups[group][action];

    if (!hk.controller_shortcut) {
        hk.controller_shortcut = new ControllerShortcut(controller, trigger, handheld);
        hk.controller_shortcut->SetKey(hk.controller_keyseq);
    }

    return hk.controller_shortcut;
}

QKeySequence HotkeyRegistry::GetKeySequence(const std::string& group, const std::string& action) {
    return hotkey_groups[group][action].keyseq;
}

Qt::ShortcutContext HotkeyRegistry::GetShortcutContext(const std::string& group,
                                                       const std::string& action) {
    return hotkey_groups[group][action].context;
}

ControllerShortcut::ControllerShortcut(Core::HID::EmulatedController* controller,
                                       ControllerShortcutTrigger trigger_,
                                       Core::HID::EmulatedController* handheld)
    : trigger{trigger_}, emulated_controller{controller}, handheld_controller{handheld} {
    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change = [this](Core::HID::ControllerTriggerType type) { ControllerUpdateEvent(type); },
        .is_npad_service = false,
    };
    callback_key = emulated_controller->SetCallback(engine_callback);
    if (handheld_controller) {
        handheld_callback_key = handheld_controller->SetCallback(engine_callback);
    }
}

ControllerShortcut::~ControllerShortcut() {
    emulated_controller->DeleteCallback(callback_key);
    if (handheld_controller) {
        handheld_controller->DeleteCallback(handheld_callback_key);
    }
}

void ControllerShortcut::SetKey(const ControllerButtonSequence& buttons) {
    std::scoped_lock lock{mutex};
    button_sequence = buttons;
    short_press_start.reset();
    short_press_interrupted = false;
}

void ControllerShortcut::SetKey(const std::string& buttons_shortcut) {
    ControllerButtonSequence sequence{};
    std::istringstream command_line(buttons_shortcut);
    std::string line;
    while (std::getline(command_line, line, '+')) {
        if (line.empty()) {
            continue;
        }
        if (line == "A") {
            sequence.npad.a.Assign(1);
        }
        if (line == "B") {
            sequence.npad.b.Assign(1);
        }
        if (line == "X") {
            sequence.npad.x.Assign(1);
        }
        if (line == "Y") {
            sequence.npad.y.Assign(1);
        }
        if (line == "L") {
            sequence.npad.l.Assign(1);
        }
        if (line == "R") {
            sequence.npad.r.Assign(1);
        }
        if (line == "ZL") {
            sequence.npad.zl.Assign(1);
        }
        if (line == "ZR") {
            sequence.npad.zr.Assign(1);
        }
        if (line == "Dpad_Left") {
            sequence.npad.left.Assign(1);
        }
        if (line == "Dpad_Right") {
            sequence.npad.right.Assign(1);
        }
        if (line == "Dpad_Up") {
            sequence.npad.up.Assign(1);
        }
        if (line == "Dpad_Down") {
            sequence.npad.down.Assign(1);
        }
        if (line == "Left_Stick") {
            sequence.npad.stick_l.Assign(1);
        }
        if (line == "Right_Stick") {
            sequence.npad.stick_r.Assign(1);
        }
        if (line == "Minus") {
            sequence.npad.minus.Assign(1);
        }
        if (line == "Plus") {
            sequence.npad.plus.Assign(1);
        }
        if (line == "Home") {
            sequence.home.home.Assign(1);
        }
        if (line == "Screenshot") {
            sequence.capture.capture.Assign(1);
        }
    }

    SetKey(sequence);
}

ControllerButtonSequence ControllerShortcut::ButtonSequence() const {
    std::scoped_lock lock{mutex};

    return button_sequence;
}

void ControllerShortcut::SetEnabled(bool enable) {
    std::scoped_lock lock{mutex};
    is_enabled = enable;
    short_press_start.reset();
    short_press_interrupted = false;
}

bool ControllerShortcut::IsEnabled() const {
    std::scoped_lock lock{mutex};

    return is_enabled;
}

void ControllerShortcut::ControllerUpdateEvent(Core::HID::ControllerTriggerType type) {
    std::unique_lock lock{mutex};
    if (!is_enabled) {
        return;
    }
    if (type == Core::HID::ControllerTriggerType::Disconnected ||
        type == Core::HID::ControllerTriggerType::Type) {
        short_press_start.reset();
        short_press_interrupted = false;
    }
    if (type != Core::HID::ControllerTriggerType::Button) {
        return;
    }
    if (button_sequence.npad.raw == Core::HID::NpadButton::None &&
        button_sequence.capture.raw == 0 && button_sequence.home.raw == 0) {
        return;
    }

    ControllerButtonSequence pressed{
        .capture = emulated_controller->GetCaptureButtons(),
        .home = emulated_controller->GetHomeButtons(),
        .npad = emulated_controller->GetNpadButtons(),
    };
    if (handheld_controller) {
        pressed.capture.raw |= handheld_controller->GetCaptureButtons().raw;
        pressed.home.raw |= handheld_controller->GetHomeButtons().raw;
        pressed.npad.raw |= handheld_controller->GetNpadButtons().raw;
    }
    if (trigger == ControllerShortcutTrigger::ShortPress) {
        const bool activate = HandleShortPress(pressed);
        lock.unlock();
        if (activate) {
            emit Activated();
        }
        return;
    }

    const auto player_npad_buttons = pressed.npad.raw & button_sequence.npad.raw;
    const u64 player_capture_buttons = pressed.capture.raw & button_sequence.capture.raw;
    const u64 player_home_buttons = pressed.home.raw & button_sequence.home.raw;

    if (player_npad_buttons == button_sequence.npad.raw &&
        player_capture_buttons == button_sequence.capture.raw &&
        player_home_buttons == button_sequence.home.raw && !active) {
        // Force user to press the home or capture button again
        active = true;
        lock.unlock();
        emit Activated();
        return;
    }
    active = false;
}

bool ControllerShortcut::HandleShortPress(ControllerButtonSequence pressed) {
    using Core::HID::NpadButton;
    constexpr auto stick_directions = NpadButton::StickLLeft | NpadButton::StickLUp |
                                      NpadButton::StickLRight | NpadButton::StickLDown |
                                      NpadButton::StickRLeft | NpadButton::StickRUp |
                                      NpadButton::StickRRight | NpadButton::StickRDown;
    const auto held_buttons = pressed.npad.raw & ~stick_directions;
    const auto npad = held_buttons & button_sequence.npad.raw;
    const auto home = pressed.home.raw & button_sequence.home.raw;
    const auto capture = pressed.capture.raw & button_sequence.capture.raw;
    const bool all_bound = npad == button_sequence.npad.raw && home == button_sequence.home.raw &&
                           capture == button_sequence.capture.raw;
    const bool any_bound = npad != NpadButton::None || home != 0 || capture != 0;
    const bool extra = held_buttons != npad || pressed.home.raw != home || pressed.capture.raw != capture;

    if (all_bound && !short_press_start) {
        short_press_start = std::chrono::steady_clock::now();
    }
    if (short_press_start && extra) {
        short_press_interrupted = true;
    }
    if (any_bound) {
        return false;
    }

    // Match the emulated HOME button's short-press interval.
    constexpr auto max_duration = std::chrono::milliseconds(400);
    const bool activate = short_press_start && !short_press_interrupted &&
                          std::chrono::steady_clock::now() - *short_press_start < max_duration;
    short_press_start.reset();
    short_press_interrupted = false;

    return activate;
}
