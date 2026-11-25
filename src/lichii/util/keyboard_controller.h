// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <QHash>
#include <QObject>
#include <QPointer>

#include "common/settings_enums.h"
#include "lichii/util/keyboard_input.h"

class QWidget;

namespace Lichii {

class KeyboardOverlay;
class KeyboardPanel;

class KeyboardController final : public QObject {
public:
    KeyboardController(QObject* parent, std::function<Settings::Language()> system_language);

    bool HandleControllerKey(QWidget* window, Qt::Key key);
    bool IsOpenFor(const QWidget* window) const;
    void Dismiss();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool Open(QWidget* field);
    void SwitchInputMode();
    void Insert(const QString& text);
    void UpdateComposition();
    void FlushComposition();
    void SendEditorKey(Qt::Key key);
    void Confirm();
    KeyboardPanel* Panel() const;
    KeyboardOverlay* OverlayFor(QWidget* window);

    QHash<QWidget*, QPointer<KeyboardOverlay>> overlays;
    QPointer<KeyboardOverlay> active;
    QPointer<QWidget> editor;
    std::function<Settings::Language()> system_language;
    KeyboardInput input;
    KeyboardMode chinese_mode = KeyboardMode::Default;
    bool literal = false;
    bool sending_input = false;
};

} // namespace Lichii
