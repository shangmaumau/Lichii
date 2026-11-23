// SPDX-License-Identifier: GPL-2.0-or-later

#include <tuple>
#include <utility>

#include "common/settings_input.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "lichii/util/controller_navigation.h"

namespace {
Qt::Key StickDirection(const Common::Input::StickStatus& stick, Qt::Key down, Qt::Key left,
                       Qt::Key right, Qt::Key up) {
    if (stick.down) {
        return down;
    }
    if (stick.left) {
        return left;
    }
    if (stick.right) {
        return right;
    }
    return stick.up ? up : Qt::Key_unknown;
}
} // namespace

ControllerNavigation::ControllerNavigation(Core::HID::HIDCore& hid_core, QObject* parent)
    : QObject(parent) {
    player1_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
    handheld_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change = [this](Core::HID::ControllerTriggerType type) { ControllerUpdateEvent(type); },
        .is_npad_service = false,
    };
    player1_callback_key = player1_controller->SetCallback(engine_callback);
    handheld_callback_key = handheld_controller->SetCallback(engine_callback);
    is_controller_set = true;
}

ControllerNavigation::~ControllerNavigation() {
    UnloadController();
}

void ControllerNavigation::UnloadController() {
    if (is_controller_set) {
        player1_controller->DeleteCallback(player1_callback_key);
        handheld_controller->DeleteCallback(handheld_callback_key);
        is_controller_set = false;
    }
}

void ControllerNavigation::TriggerButton(Settings::NativeButton::Values native_button,
                                         Qt::Key key) {
    if (button_values[native_button].value && !button_values[native_button].locked) {
        emit TriggerKeyboardEvent(key);
    }
}

void ControllerNavigation::ControllerUpdateEvent(Core::HID::ControllerTriggerType type) {
    std::scoped_lock lock{mutex};
    switch (type) {
    case Core::HID::ControllerTriggerType::Button:
        ControllerUpdateButton();
        break;
    case Core::HID::ControllerTriggerType::Trigger:
        if (player1_controller->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::GameCube) {
            ControllerUpdateButton();
        }
        break;
    case Core::HID::ControllerTriggerType::Stick:
        ControllerUpdateStick();
        break;
    case Core::HID::ControllerTriggerType::Disconnected:
    case Core::HID::ControllerTriggerType::Type:
        stick_direction = Qt::Key_unknown;
        shoulder_key = Qt::Key_unknown;
        button_values = {};
        UpdateHeldNavigation(player1_controller->GetNpadStyleIndex());
        break;
    default:
        break;
    }
}

void ControllerNavigation::ControllerUpdateButton() {
    const auto controller_type = player1_controller->GetNpadStyleIndex();
    const auto& player1_buttons = player1_controller->GetButtonsValues();
    const auto& handheld_buttons = handheld_controller->GetButtonsValues();

    for (std::size_t i = 0; i < player1_buttons.size(); ++i) {
        bool button = player1_buttons[i].value;
        if (controller_type == Core::HID::NpadStyleIndex::GameCube &&
            (i == Settings::NativeButton::ZL || i == Settings::NativeButton::ZR)) {
            const auto trigger = i == Settings::NativeButton::ZL ? Settings::NativeTrigger::LTrigger
                                                                 : Settings::NativeTrigger::RTrigger;
            button = player1_controller->GetTriggersValues()[trigger].pressed.value;
        }
        button = button || handheld_buttons[i].value;
        // Trigger only once
        button_values[i].locked = button == button_values[i].value;
        button_values[i].value = button;
    }

    switch (controller_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::GameCube:
        TriggerButton(Settings::NativeButton::A, Qt::Key_Enter);
        TriggerButton(Settings::NativeButton::B, Qt::Key_Escape);
        TriggerButton(Settings::NativeButton::DDown, Qt::Key_Down);
        TriggerButton(Settings::NativeButton::DLeft, Qt::Key_Left);
        TriggerButton(Settings::NativeButton::DRight, Qt::Key_Right);
        TriggerButton(Settings::NativeButton::DUp, Qt::Key_Up);
        TriggerButton(Settings::NativeButton::X, Qt::Key_Menu);
        TriggerButton(Settings::NativeButton::Plus, Qt::Key_F10);
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        TriggerButton(Settings::NativeButton::DDown, Qt::Key_Enter);
        TriggerButton(Settings::NativeButton::DLeft, Qt::Key_Escape);
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        TriggerButton(Settings::NativeButton::X, Qt::Key_Enter);
        TriggerButton(Settings::NativeButton::A, Qt::Key_Escape);
        TriggerButton(Settings::NativeButton::Plus, Qt::Key_F10);
        break;
    default:
        break;
    }
    shoulder_key = UpdateShoulder(controller_type);
    UpdateHeldNavigation(controller_type);
}

Qt::Key ControllerNavigation::UpdateShoulder(Core::HID::NpadStyleIndex style) {
    using enum Core::HID::NpadStyleIndex;
    const bool both = style == Fullkey || style == JoyconDual || style == Handheld ||
                      style == GameCube;
    const bool left = both || style == JoyconLeft;
    const bool right = both || style == JoyconRight;
    Qt::Key pressed = Qt::Key_unknown;
    Qt::Key held = Qt::Key_unknown;
    for (const auto& [button, key, supported] :
         {std::tuple{Settings::NativeButton::L, Qt::Key_Backtab, left},
          std::tuple{Settings::NativeButton::R, Qt::Key_Tab, right},
          std::tuple{Settings::NativeButton::ZL, Qt::Key_PageUp, left},
          std::tuple{Settings::NativeButton::ZR, Qt::Key_PageDown, right}}) {
        if (!supported || !button_values[button].value) {
            continue;
        }
        if (!button_values[button].locked) {
            emit TriggerKeyboardEvent(key);
            pressed = key;
        }
        if (held == Qt::Key_unknown || key == shoulder_key) {
            held = key;
        }
    }
    return pressed != Qt::Key_unknown ? pressed : held;
}

void ControllerNavigation::ControllerUpdateStick() {
    const auto controller_type = player1_controller->GetNpadStyleIndex();
    const auto& player1_sticks = player1_controller->GetSticksValues();
    const auto& handheld_sticks = handheld_controller->GetSticksValues();

    for (std::size_t i = 0; i < player1_sticks.size(); ++i) {
        stick_values[i] = {
            .left = player1_sticks[i].left || handheld_sticks[i].left,
            .right = player1_sticks[i].right || handheld_sticks[i].right,
            .up = player1_sticks[i].up || handheld_sticks[i].up,
            .down = player1_sticks[i].down || handheld_sticks[i].down,
        };
    }

    Qt::Key direction = Qt::Key_unknown;
    const auto& left_stick = stick_values[Settings::NativeAnalog::LStick];
    const auto& right_stick = stick_values[Settings::NativeAnalog::RStick];
    switch (controller_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::GameCube:
        direction = StickDirection(left_stick, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
                                   Qt::Key_Up);
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        direction = StickDirection(left_stick, Qt::Key_Right, Qt::Key_Down, Qt::Key_Up,
                                   Qt::Key_Left);
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        direction = StickDirection(right_stick, Qt::Key_Left, Qt::Key_Up, Qt::Key_Down,
                                   Qt::Key_Right);
        break;
    default:
        break;
    }
    if (direction != stick_direction) {
        stick_direction = direction;
        if (direction != Qt::Key_unknown) {
            emit TriggerKeyboardEvent(direction);
        }
        UpdateHeldNavigation(controller_type);
    }
}

void ControllerNavigation::UpdateHeldNavigation(Core::HID::NpadStyleIndex style) {
    Qt::Key key = stick_direction;
    switch (style) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::GameCube:
        for (const auto& [button, direction] :
             {std::pair{Settings::NativeButton::DDown, Qt::Key_Down},
              std::pair{Settings::NativeButton::DLeft, Qt::Key_Left},
              std::pair{Settings::NativeButton::DRight, Qt::Key_Right},
              std::pair{Settings::NativeButton::DUp, Qt::Key_Up}}) {
            if (button_values[button].value) {
                key = direction;
                break;
            }
        }
        break;
    default:
        break;
    }
    if (key != held_direction || shoulder_key != held_shoulder) {
        held_direction = key;
        held_shoulder = shoulder_key;
        emit HeldNavigationChanged(held_direction, held_shoulder);
    }
}
