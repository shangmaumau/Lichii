// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/util/keyboard_controller.h"

#include <utility>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QInputMethodEvent>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>

#include "lichii/util/keyboard_overlay.h"
#include "lichii/util/keyboard_panel.h"

namespace Lichii {
namespace {

QWidget* EditableField(QWidget* widget) {
    for (; widget; widget = widget->parentWidget()) {
        if (auto* line = qobject_cast<QLineEdit*>(widget)) {
            return line->isReadOnly() ? nullptr : line;
        }
        if (auto* text = qobject_cast<QTextEdit*>(widget)) {
            return text->isReadOnly() ? nullptr : text;
        }
        if (auto* text = qobject_cast<QPlainTextEdit*>(widget)) {
            return text->isReadOnly() ? nullptr : text;
        }
        if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<KeyboardPanel*>(widget) ||
            widget->isWindow()) {
            return nullptr;
        }
    }
    return nullptr;
}

QWidget* FocusedEditor(QWidget* widget) {
    if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget); spin && !spin->isReadOnly()) {
        return EditableField(spin->findChild<QLineEdit*>(QString{}, Qt::FindDirectChildrenOnly));
    }
    return EditableField(widget);
}

bool CanOpen(const QWidget* field) {
    return field && field->isVisible() && field->isEnabled() &&
           field->window()->windowType() != Qt::Popup &&
           field->window()->objectName() != QStringLiteral("QtSoftwareKeyboardDialog") &&
           !field->window()->property("lichiiControllerInputCapture").toBool();
}

struct InputOptions {
    bool numeric = false;
    bool decimal = false;
    bool negative = false;
    bool sensitive = false;
    QString separator = QStringLiteral(".");
};

InputOptions OptionsFor(const QWidget& field) {
    const auto hints = field.inputMethodHints();
    InputOptions options;
    options.decimal = hints.testFlag(Qt::ImhFormattedNumbersOnly);
    options.numeric = options.decimal || hints.testFlag(Qt::ImhDigitsOnly);
    options.negative = options.decimal;
    options.sensitive = hints.testFlag(Qt::ImhSensitiveData) || hints.testFlag(Qt::ImhHiddenText);
    options.separator = field.locale().decimalPoint();
    if (const auto* line = qobject_cast<const QLineEdit*>(&field)) {
        options.sensitive |= line->echoMode() != QLineEdit::Normal;
        if (const auto* spin = qobject_cast<QSpinBox*>(line->parentWidget())) {
            options.numeric = true;
            options.decimal = false;
            options.negative = spin->minimum() < 0;
        } else if (const auto* double_spin = qobject_cast<QDoubleSpinBox*>(line->parentWidget())) {
            options.numeric = true;
            options.decimal = double_spin->decimals() > 0;
            options.negative = double_spin->minimum() < 0;
            options.separator = double_spin->locale().decimalPoint();
        } else if (const auto* validator = qobject_cast<const QIntValidator*>(line->validator())) {
            options.numeric = true;
            options.decimal = false;
            options.negative = validator->bottom() < 0;
        } else if (const auto* double_validator =
                       qobject_cast<const QDoubleValidator*>(line->validator())) {
            options.numeric = true;
            options.decimal = double_validator->decimals() != 0;
            options.negative = double_validator->bottom() < 0;
            options.separator = double_validator->locale().decimalPoint();
        }
    }
    return options;
}

KeyboardLanguage KeyboardLanguageFor(Settings::Language language) {
    switch (language) {
    case Settings::Language::Chinese:
    case Settings::Language::ChineseSimplified:
        return KeyboardLanguage::ChineseSimplified;
    case Settings::Language::Taiwanese:
    case Settings::Language::ChineseTraditional:
        return KeyboardLanguage::ChineseTraditional;
    case Settings::Language::Japanese:
        return KeyboardLanguage::Japanese;
    case Settings::Language::EnglishBritish:
        return KeyboardLanguage::EnglishUK;
    default:
        return KeyboardLanguage::EnglishUS;
    }
}

bool SupportsInputModeSwitch(KeyboardLanguage language) {
    return language == KeyboardLanguage::ChineseSimplified ||
           language == KeyboardLanguage::ChineseTraditional;
}

} // namespace

KeyboardController::KeyboardController(QObject* parent,
                                       std::function<Settings::Language()> system_language_)
    : QObject(parent), system_language(std::move(system_language_)) {
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* next) {
        if (!editor || !next || sending_input || QApplication::activePopupWidget()) {
            return;
        }
        auto* field = FocusedEditor(next);
        if (field == editor) {
            return;
        }
        if (!CanOpen(field) || field->window() != editor->window()) {
            FlushComposition();
            Dismiss();
        } else {
            Open(field);
        }
    });
}

bool KeyboardController::IsOpenFor(const QWidget* window) const {
    return editor && active && active->IsOpen() && editor->window() == window;
}

KeyboardPanel* KeyboardController::Panel() const {
    return active ? static_cast<KeyboardPanel*>(active->Panel()) : nullptr;
}

KeyboardOverlay* KeyboardController::OverlayFor(QWidget* window) {
    if (auto* overlay = overlays.value(window).data()) {
        return overlay;
    }
    auto* panel = new KeyboardPanel(window);
    auto* overlay = new KeyboardOverlay(window, panel);
    overlays.insert(window, overlay);
    connect(window, &QObject::destroyed, this, [this, window] { overlays.remove(window); });
    connect(panel, &KeyboardPanel::TextTyped, this, [this](const QString& text) {
        Insert(literal ? text : input.Feed(text));
        UpdateComposition();
    });
    connect(panel, &KeyboardPanel::SpacePressed, this, [this] {
        Insert(literal ? QStringLiteral(" ") : input.Feed(QStringLiteral(" ")));
        UpdateComposition();
    });
    connect(panel, &KeyboardPanel::DeletePressed, this, [this] {
        if (literal || !input.Backspace()) {
            SendEditorKey(Qt::Key_Backspace);
        }
        UpdateComposition();
    });
    connect(panel, &KeyboardPanel::CursorMoved, this, [this](int direction) {
        FlushComposition();
        SendEditorKey(direction < 0 ? Qt::Key_Left : Qt::Key_Right);
    });
    connect(panel, &KeyboardPanel::Confirmed, this, &KeyboardController::Confirm);
    connect(panel, &KeyboardPanel::Dismissed, this, &KeyboardController::Dismiss);
    connect(panel, &KeyboardPanel::InputModeSwitchRequested, this,
            &KeyboardController::SwitchInputMode);
    connect(panel, &KeyboardPanel::CandidateChosen, this, [this](int index) {
        Insert(input.AcceptCandidate(index));
        UpdateComposition();
    });
    return overlay;
}

bool KeyboardController::Open(QWidget* field) {
    if (!CanOpen(field)) {
        return false;
    }
    const QPointer<QWidget> target(field);
    if (editor == field && IsOpenFor(field->window())) {
        return true;
    }
    if (editor) {
        FlushComposition();
    }
    if (!CanOpen(target)) {
        return false;
    }
    auto* window = target->window();
    if (active && active->Panel()->window() != window) {
        Dismiss();
    }
    if (!CanOpen(target)) {
        return false;
    }
    active = OverlayFor(window);
    editor = field;
    const auto options = OptionsFor(*field);
    literal = options.numeric || options.sensitive ||
              field->inputMethodHints().testFlag(Qt::ImhNoPredictiveText);
    input.Reset();
    const auto language = KeyboardLanguageFor(system_language());
    if (input.Language() != language) {
        chinese_mode = KeyboardMode::Default;
    }
    input.SetLanguage(language);
    const bool can_switch = !literal && SupportsInputModeSwitch(language);
    const auto mode = can_switch ? chinese_mode : KeyboardMode::Default;
    input.SetMode(mode);
    auto* panel = Panel();
    panel->SetSensitive(options.sensitive);
    panel->SetNumeric(options.numeric, options.decimal, options.negative, options.separator);
    panel->SetInputMode(options.sensitive ? KeyboardLanguage::EnglishUS : language, mode, can_switch);
    UpdateComposition();
    panel->ResetControllerFocus();
    active->Open(field);
    return true;
}

void KeyboardController::SwitchInputMode() {
    const auto language = input.Language();
    if (!editor || literal || !SupportsInputModeSwitch(language)) {
        return;
    }
    FlushComposition();
    if (!editor) {
        return;
    }
    switch (chinese_mode) {
    case KeyboardMode::Default:
        chinese_mode = KeyboardMode::DirectLatin;
        break;
    case KeyboardMode::DirectLatin:
        chinese_mode = KeyboardMode::Wubi;
        break;
    case KeyboardMode::Wubi:
        chinese_mode = KeyboardMode::Default;
        break;
    }
    input.SetMode(chinese_mode);
    Panel()->SetInputMode(language, chinese_mode, true);
    UpdateComposition();
}

void KeyboardController::Dismiss() {
    editor.clear();
    input.Reset();
    if (active) {
        active->Close();
        active.clear();
    }
}

void KeyboardController::Insert(const QString& text) {
    if (!editor || text.isEmpty()) {
        return;
    }
    const QScopedValueRollback guard(sending_input, true);
    QInputMethodEvent event;
    event.setCommitString(text);
    QApplication::sendEvent(editor, &event);
}

void KeyboardController::UpdateComposition() {
    if (auto* panel = Panel()) {
        panel->SetComposition(input.Preedit(), input.Candidates());
    }
}

void KeyboardController::FlushComposition() {
    if (!literal) {
        Insert(input.Flush());
    }
    UpdateComposition();
}

void KeyboardController::SendEditorKey(Qt::Key key) {
    if (!editor) {
        return;
    }
    const QScopedValueRollback guard(sending_input, true);
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(editor, &press);
    if (editor) {
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(editor, &release);
    }
}

void KeyboardController::Confirm() {
    FlushComposition();
    if (auto* line = qobject_cast<QLineEdit*>(editor.data())) {
        if (!line->hasAcceptableInput()) {
            return;
        }
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(line->parentWidget())) {
            spin->interpretText();
        }
    }
    Dismiss();
}

bool KeyboardController::HandleControllerKey(QWidget* window, Qt::Key key) {
    if (IsOpenFor(window)) {
        Panel()->HandleControllerKey(key);
        return true;
    }
    auto* focus = QApplication::focusWidget();
    if (key != Qt::Key_Enter || !focus || focus->window() != window) {
        return false;
    }
    return Open(FocusedEditor(focus));
}

bool KeyboardController::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || sending_input) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (editor && EditableField(widget) == editor) {
            FlushComposition();
        }
        break;
    case QEvent::MouseButtonRelease: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) {
            break;
        }
        if (auto* field = EditableField(widget); CanOpen(field)) {
            const QPointer<QWidget> candidate(field);
            QTimer::singleShot(0, this, [this, candidate] {
                if (candidate && FocusedEditor(QApplication::focusWidget()) == candidate) {
                    Open(candidate);
                }
            });
        } else if (auto* panel = Panel(); panel && widget != panel && !panel->isAncestorOf(widget) &&
                   !QApplication::activePopupWidget()) {
            FlushComposition();
            Dismiss();
        }
        break;
    }
    case QEvent::KeyPress: {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_F2 && key->modifiers() == Qt::NoModifier &&
            Open(FocusedEditor(widget))) {
            return true;
        }
        if (!editor || FocusedEditor(widget) != editor) {
            break;
        }
        if (key->key() == Qt::Key_Escape) {
            Dismiss();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            qobject_cast<QLineEdit*>(editor.data())) {
            Confirm();
            return true;
        }
        if (key->key() == Qt::Key_Backspace && !literal && input.Backspace()) {
            UpdateComposition();
            return true;
        }
        FlushComposition();
        break;
    }
    case QEvent::InputMethod:
        if (editor == widget) {
            FlushComposition();
        }
        break;
    case QEvent::Hide:
    case QEvent::Close:
        if (editor == widget || (editor && editor->window() == widget)) {
            Dismiss();
        }
        break;
    default:
        break;
    }
    return false;
}

} // namespace Lichii
