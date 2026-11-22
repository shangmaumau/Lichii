// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <QKeyEvent>
#include <QObject>

#include "common/input.h"
#include "common/settings_input.h"

namespace Core::HID {
using ButtonValues = std::array<Common::Input::ButtonStatus, Settings::NativeButton::NumButtons>;
using SticksValues = std::array<Common::Input::StickStatus, Settings::NativeAnalog::NumAnalogs>;
enum class ControllerTriggerType;
enum class NpadStyleIndex : u8;
class EmulatedController;
class HIDCore;
} // namespace Core::HID

class ControllerNavigation : public QObject {
    Q_OBJECT

public:
    explicit ControllerNavigation(Core::HID::HIDCore& hid_core, QObject* parent);
    ~ControllerNavigation();

    /// Disables events from the emulated controller
    void UnloadController();

signals:
    void TriggerKeyboardEvent(Qt::Key key);
    void DirectionChanged(Qt::Key key);

private:
    void TriggerButton(Settings::NativeButton::Values native_button, Qt::Key key);
    void ControllerUpdateEvent(Core::HID::ControllerTriggerType type);

    void ControllerUpdateButton();

    void ControllerUpdateStick();
    void UpdateHeldDirection(Core::HID::NpadStyleIndex style);

    Core::HID::ButtonValues button_values{};
    Core::HID::SticksValues stick_values{};
    Qt::Key stick_direction = Qt::Key_unknown;
    Qt::Key held_direction = Qt::Key_unknown;

    int player1_callback_key{};
    int handheld_callback_key{};
    bool is_controller_set{};
    mutable std::mutex mutex;
    Core::HID::EmulatedController* player1_controller;
    Core::HID::EmulatedController* handheld_controller;
};
