// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

#include "lichii/util/keyboard_panel.h"

namespace Lichii {
namespace {

void SetStyleProperty(QWidget* widget, const char* name, const QVariant& value) {
    if (widget->property(name) == value) {
        return;
    }
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QList<QPushButton*> SelectableButtons(const QHBoxLayout* row) {
    QList<QPushButton*> buttons;
    for (int i = 0; i < row->count(); ++i) {
        auto* button = qobject_cast<QPushButton*>(row->itemAt(i)->widget());
        if (button && button->isVisible() && button->isEnabled()) {
            buttons.push_back(button);
        }
    }
    return buttons;
}

} // namespace

KeyboardPanel::KeyboardPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("lichiiKeyboardPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setAccessibleName(tr("On-screen keyboard"));
    if (parent) {
        window()->installEventFilter(this);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(5);

    header_row = new QHBoxLayout;
    header_row->setSpacing(5);
    layout->addLayout(header_row, 1);
    input_mode = new QLabel(this);
    input_mode->setObjectName(QStringLiteral("keyboardInputMode"));
    input_mode->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    header_row->addWidget(input_mode);
    input_mode_switch = MakeButton(QStringLiteral("keyboardInputModeSwitch"));
    input_mode_switch->setMinimumWidth(108);
    input_mode_switch->setMaximumWidth(142);
    header_row->addWidget(input_mode_switch, 1);
    connect(input_mode_switch, &QPushButton::clicked, this, &KeyboardPanel::InputModeSwitchRequested);
    header_row->addStretch(1);
    dismiss = MakeButton(QStringLiteral("keyboardDismiss"));
    dismiss->setText(tr("Hide"));
    dismiss->setToolTip(tr("Hide keyboard"));
    dismiss->setMaximumWidth(96);
    header_row->addWidget(dismiss, 1);
    connect(dismiss, &QPushButton::clicked, this, &KeyboardPanel::Dismissed);

    candidate_strip = new QWidget(this);
    auto* strip_layout = new QVBoxLayout(candidate_strip);
    strip_layout->setContentsMargins(0, 0, 0, 0);
    candidate_row = new QHBoxLayout;
    candidate_row->setSpacing(5);
    strip_layout->addLayout(candidate_row);
    layout->addWidget(candidate_strip, 1);

    composition = new QLabel(candidate_strip);
    composition->setObjectName(QStringLiteral("keyboardComposition"));
    composition->setAlignment(Qt::AlignCenter);
    composition->setMaximumWidth(124);
    composition->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    candidate_row->addWidget(composition, 2);

    previous_candidates = MakeButton(QStringLiteral("keyboardCandidate"));
    previous_candidates->setText(QStringLiteral("‹"));
    previous_candidates->setAccessibleName(tr("Previous candidates"));
    previous_candidates->setToolTip(previous_candidates->accessibleName());
    candidate_row->addWidget(previous_candidates, 1);
    connect(previous_candidates, &QPushButton::clicked, this, [this] { ChangeCandidatePage(-1); });

    for (int index = 0; index < CandidateCount; ++index) {
        auto* button = MakeButton(QStringLiteral("keyboardCandidate"));
        candidate_buttons[index] = button;
        candidate_row->addWidget(button, 3);
        connect(button, &QPushButton::clicked, this, [this, index] {
            emit CandidateChosen(candidate_page * candidate_capacity + index);
        });
    }

    next_candidates = MakeButton(QStringLiteral("keyboardCandidate"));
    next_candidates->setText(QStringLiteral("›"));
    next_candidates->setAccessibleName(tr("Next candidates"));
    next_candidates->setToolTip(next_candidates->accessibleName());
    candidate_row->addWidget(next_candidates, 1);
    connect(next_candidates, &QPushButton::clicked, this, [this] { ChangeCandidatePage(1); });

    for (int row = 0; row < KeyboardRows; ++row) {
        auto* strip = new QWidget(this);
        key_strips[row] = strip;
        auto* keys = new QHBoxLayout(strip);
        key_rows[row] = keys;
        keys->setContentsMargins(0, 0, 0, 0);
        keys->setSpacing(5);
        layout->addWidget(strip, 1);
        keys->addStretch();
        for (int column = 0; column < KeyboardColumns; ++column) {
            auto* button = MakeButton(QStringLiteral("keyboardKey"));
            key_buttons[row][column] = button;
            keys->addWidget(button);
            connect(button, &QPushButton::clicked, this,
                    [this, row, column] { ActivateKey(row, column); });
        }
        keys->addStretch();
    }

    UpdateStyle();
    UpdateInputModeLabel();
    UpdateCandidates();
    UpdateKeys();
}

QSize KeyboardPanel::sizeHint() const {
    return {680, numeric || sensitive ? 248 : 282};
}

QSize KeyboardPanel::minimumSizeHint() const {
    return {270, numeric || sensitive ? 170 : 210};
}

void KeyboardPanel::SetInputMode(KeyboardLanguage next_language, KeyboardMode next_mode,
                                 bool can_switch) {
    if (language == next_language && mode == next_mode && can_switch_mode == can_switch) {
        return;
    }
    language = next_language;
    mode = next_mode;
    can_switch_mode = can_switch;
    shifted = false;
    symbols = false;
    SetComposition({}, {});
    UpdateInputModeLabel();
    UpdateKeys();
}

void KeyboardPanel::SetNumeric(bool numeric_, bool decimal_, bool negative_,
                               const QString& decimal_separator_) {
    if (numeric == numeric_ && decimal == decimal_ && negative == negative_ &&
        decimal_separator == decimal_separator_) {
        return;
    }
    numeric = numeric_;
    decimal = decimal_;
    negative = negative_;
    decimal_separator = decimal_separator_;
    shifted = false;
    symbols = false;
    ResetControllerFocus();
    UpdateInputModeLabel();
    UpdateCandidates();
    UpdateKeys();
    updateGeometry();
}

void KeyboardPanel::SetSensitive(bool sensitive_) {
    if (sensitive == sensitive_) {
        return;
    }
    sensitive = sensitive_;
    if (sensitive) {
        preedit.clear();
        candidates.clear();
        candidate_page = 0;
    }
    ResetControllerFocus();
    UpdateInputModeLabel();
    UpdateCandidates();
    updateGeometry();
}

void KeyboardPanel::SetComposition(const QString& preedit_, const QStringList& candidates_) {
    if (sensitive || (preedit == preedit_ && candidates == candidates_)) {
        return;
    }
    preedit = preedit_;
    candidates = candidates_;
    candidate_page = 0;
    UpdateCandidates();
}

bool KeyboardPanel::HandleControllerKey(Qt::Key key) {
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
        MoveControllerFocus(key);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!controller_focus || !controller_focus->isVisible() || !controller_focus->isEnabled()) {
            MoveControllerFocus(Qt::Key_unknown);
        }
        if (controller_focus) {
            controller_focus->click();
        }
        return true;
    case Qt::Key_Menu:
    case Qt::Key_Backspace:
        emit DeletePressed();
        return true;
    case Qt::Key_F10:
        emit Confirmed();
        return true;
    case Qt::Key_Escape:
        emit Dismissed();
        return true;
    case Qt::Key_Backtab:
    case Qt::Key_Tab:
        emit CursorMoved(key == Qt::Key_Tab ? 1 : -1);
        return true;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (!numeric && !sensitive) {
            ChangeCandidatePage(key == Qt::Key_PageDown ? 1 : -1);
        }
        return true;
    default:
        return false;
    }
}

void KeyboardPanel::ResetControllerFocus() {
    SetControllerFocus(nullptr);
    focus_column = -1;
}

bool KeyboardPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window() && (event->type() == QEvent::ApplicationPaletteChange ||
                                event->type() == QEvent::PaletteChange)) {
        UpdateStyle();
    } else if (event->type() == QEvent::MouseButtonPress) {
        ResetControllerFocus();
    }
    return QWidget::eventFilter(watched, event);
}

void KeyboardPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::PaletteChange) {
        UpdateStyle();
    }
    QWidget::changeEvent(event);
}

void KeyboardPanel::resizeEvent(QResizeEvent* event) {
    const int capacity = std::clamp((event->size().width() - 120) / 80, 2, CandidateCount);
    if (capacity != candidate_capacity) {
        candidate_page = candidate_page * candidate_capacity / capacity;
        candidate_capacity = capacity;
    }
    UpdateCandidates();
    QWidget::resizeEvent(event);
}

QPushButton* KeyboardPanel::MakeButton(const QString& name) {
    auto* button = new QPushButton(this);
    button->setObjectName(name);
    button->setAutoDefault(false);
    button->setDefault(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    button->installEventFilter(this);
    return button;
}

void KeyboardPanel::UpdateStyle() {
    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    const auto color = [dark](const char* dark_color, const char* light_color) {
        return QString::fromLatin1(dark ? dark_color : light_color);
    };
    const auto stylesheet = QStringLiteral(R"(
QWidget#lichiiKeyboardPanel {
    background-color: %1;
    border: 1px solid %2;
    border-top-left-radius: 14px;
    border-top-right-radius: 14px;
}
QWidget#lichiiKeyboardPanel QPushButton {
    background-color: %3;
    border: 2px solid transparent;
    border-radius: 7px;
    color: %4;
    min-height: 0;
    min-width: 0;
    padding: 0;
}
QWidget#lichiiKeyboardPanel QLabel#keyboardInputMode {
    color: %4;
    padding-left: 6px;
}
QWidget#lichiiKeyboardPanel QPushButton#keyboardInputModeSwitch {
    color: %6;
    font-weight: 600;
}
QWidget#lichiiKeyboardPanel QPushButton#keyboardDismiss {
    background-color: transparent;
    font-weight: 600;
}
QWidget#lichiiKeyboardPanel QPushButton#keyboardCandidate,
QWidget#lichiiKeyboardPanel QLabel#keyboardComposition {
    background-color: %5;
    color: %4;
    border-radius: 7px;
}
QWidget#lichiiKeyboardPanel QPushButton:hover,
QWidget#lichiiKeyboardPanel QPushButton#keyboardDismiss:hover,
QWidget#lichiiKeyboardPanel QPushButton:pressed {
    background-color: %5;
}
QWidget#lichiiKeyboardPanel QPushButton[oskSelected="true"] {
    background-color: %5;
    color: %6;
}
QWidget#lichiiKeyboardPanel QPushButton[oskPrimary="true"] {
    background-color: %7;
    color: %8;
    font-weight: 600;
}
QWidget#lichiiKeyboardPanel QPushButton[oskPrimary="true"]:hover {
    background-color: %9;
}
QWidget#lichiiKeyboardPanel QPushButton[oskFocus="true"] {
    border-color: %6;
}
QWidget#lichiiKeyboardPanel QPushButton[oskPrimary="true"][oskFocus="true"] {
    border-color: %8;
}
QWidget#lichiiKeyboardPanel QPushButton:disabled,
QWidget#lichiiKeyboardPanel QPushButton#keyboardCandidate:disabled {
    background-color: transparent;
    color: %2;
}
)")
                                .arg(color("#1c1924", "#f5eff4"), color("#40364b", "#d8cbd5"),
                                     color("#2a2534", "#fffaff"), color("#f7eff7", "#2e2331"),
                                     color("#3b3045", "#e5d8e2"), color("#ffacd0", "#7c2f56"),
                                     color("#bd4c82", "#86335e"), color("#ffffff", "#ffffff"),
                                     color("#cc598f", "#9b4774"));
    if (styleSheet() != stylesheet) {
        setStyleSheet(stylesheet);
    }
}

void KeyboardPanel::UpdateInputModeLabel() {
    const bool show_mode = !numeric && !sensitive;
    input_mode->setVisible(show_mode && !can_switch_mode);
    input_mode_switch->setVisible(show_mode && can_switch_mode);
    QString label;
    QString description;
    switch (language) {
    case KeyboardLanguage::EnglishUS:
        label = QStringLiteral("EN US");
        description = tr("English (United States)");
        break;
    case KeyboardLanguage::EnglishUK:
        label = QStringLiteral("EN UK");
        description = tr("English (United Kingdom)");
        break;
    case KeyboardLanguage::ChineseSimplified:
        label = QStringLiteral("简体拼音");
        description = tr("Simplified Chinese (Pinyin)");
        break;
    case KeyboardLanguage::ChineseTraditional:
        label = QStringLiteral("繁體拼音");
        description = tr("Traditional Chinese (Pinyin)");
        break;
    case KeyboardLanguage::Japanese:
        label = QStringLiteral("日本語");
        description = tr("Japanese (Romaji)");
        break;
    }
    if (mode == KeyboardMode::Wubi) {
        const bool traditional = language == KeyboardLanguage::ChineseTraditional;
        label = traditional ? QStringLiteral("五筆 86") : QStringLiteral("五笔 86");
        description = tr("Wubi (86)");
    } else if (mode == KeyboardMode::DirectLatin) {
        label = QStringLiteral("ABC");
        description = tr("Direct English");
    }
    if (can_switch_mode) {
        input_mode_switch->setText(QStringLiteral("%1  ↻").arg(label));
        const auto switch_description = tr("Switch input mode (%1)").arg(description);
        input_mode_switch->setAccessibleName(switch_description);
        input_mode_switch->setToolTip(switch_description);
    } else {
        input_mode->setText(label);
        input_mode->setAccessibleName(description);
        input_mode->setToolTip(description);
    }
}

void KeyboardPanel::UpdateCandidates() {
    candidate_strip->setVisible(!numeric && !sensitive);
    const int start = candidate_page * candidate_capacity;
    const bool paging = candidates.size() > candidate_capacity;
    const int preedit_width = std::clamp(width() / 5, 45, 122);
    const int candidate_width =
        std::max(18, (width() - 24 - (paging ? 75 : 0) - (preedit.isEmpty() ? 0 : preedit_width)) /
                             candidate_capacity -
                         8);
    composition->setMaximumWidth(preedit_width);
    composition->setText(
        composition->fontMetrics().elidedText(preedit, Qt::ElideLeft, preedit_width - 8));
    composition->setAccessibleName(preedit);
    composition->setVisible(!preedit.isEmpty());

    previous_candidates->setVisible(paging);
    previous_candidates->setEnabled(candidate_page > 0);
    next_candidates->setVisible(paging);
    next_candidates->setEnabled(start + candidate_capacity < candidates.size());
    for (int index = 0; index < CandidateCount; ++index) {
        auto* button = candidate_buttons[index];
        const bool shown = index < candidate_capacity && start + index < candidates.size();
        button->setVisible(shown);
        if (shown) {
            const auto& word = candidates[start + index];
            button->setText(
                button->fontMetrics().elidedText(word, Qt::ElideRight, candidate_width));
            button->setToolTip(word);
            button->setAccessibleName(word);
        }
    }
    if (controller_focus &&
        (!controller_focus->isEnabled() || !controller_focus->isVisibleTo(this))) {
        auto* first = candidate_buttons.front();
        if (controller_focus->parentWidget() == candidate_strip && first->isVisibleTo(this)) {
            SetControllerFocus(first);
            focus_column = -1;
        } else {
            ResetControllerFocus();
        }
    }
}

void KeyboardPanel::UpdateKeys() {
    const auto text_row = [](const QString& text) {
        QList<Key> row;
        for (const auto character : text) {
            row.push_back({QString(character)});
        }
        return row;
    };
    const Key delete_key{QStringLiteral("⌫"), Action::Delete, 2};
    const Key confirm{tr("Done"), Action::Confirm, 3};

    if (numeric) {
        auto row = text_row(QStringLiteral("789"));
        row.push_back(delete_key);
        SetRow(0, row, 1);
        row = text_row(QStringLiteral("456"));
        row.push_back({QStringLiteral("-"), Action::Text, 2, negative});
        SetRow(1, row, 1);
        row = text_row(QStringLiteral("123"));
        row.push_back({decimal_separator, Action::Text, 2, decimal});
        SetRow(2, row, 1);
        SetRow(3, {{QStringLiteral("0"), Action::Text, 2}, confirm}, 1);
        SetRow(4, {});
        return;
    }

    const bool british = language == KeyboardLanguage::EnglishUK;
    auto row =
        text_row(shifted ? (british ? QStringLiteral("!\"£$%^&*()") : QStringLiteral("!@#$%^&*()"))
                         : QStringLiteral("1234567890"));
    row.push_back(delete_key);
    SetRow(0, row);

    if (symbols) {
        SetRow(1, text_row(QStringLiteral("@#£$€%&*()+-")));
        SetRow(2, text_row(QStringLiteral("[]{}=<>_|\\")), 1);
        const QString punctuation = language == KeyboardLanguage::Japanese
                                        ? QStringLiteral("「」『』。、〜・")
                                        : QStringLiteral(";:'\"!,./?~");
        SetRow(3, text_row(punctuation), 1);
    } else {
        SetRow(1, text_row(shifted ? QStringLiteral("QWERTYUIOP") : QStringLiteral("qwertyuiop")));
        SetRow(2, text_row(shifted ? QStringLiteral("ASDFGHJKL") : QStringLiteral("asdfghjkl")), 1);
        row = text_row(shifted ? QStringLiteral("ZXCVBNM") : QStringLiteral("zxcvbnm"));
        row.prepend({QStringLiteral("⇧"), Action::Shift, 2});
        row.push_back({QStringLiteral(",")});
        row.push_back({QStringLiteral(".")});
        SetRow(3, row);
    }

    SetRow(4, {{symbols ? QStringLiteral("ABC") : QStringLiteral("123"), Action::Symbols, 2},
               {QStringLiteral("‹"), Action::Left},
               {tr("Space"), Action::Space, 7},
               {QStringLiteral("›"), Action::Right},
               confirm});
}

void KeyboardPanel::SetRow(int index, const QList<Key>& keys, int padding) {
    auto* const layout = key_rows[index];
    visible_keys[index] = keys;
    key_strips[index]->setVisible(!keys.isEmpty());
    const int space = keys.isEmpty() ? 0 : padding;
    layout->setStretch(0, space);
    layout->setStretch(KeyboardColumns + 1, space);
    for (int column = 0; column < KeyboardColumns; ++column) {
        auto* const button = key_buttons[index][column];
        if (column >= keys.size()) {
            button->hide();
            continue;
        }
        const auto& key = keys[column];
        button->setText(key.label);
        button->setEnabled(key.enabled);
        button->setAutoRepeat(key.action == Action::Delete);
        button->setVisible(true);
        layout->setStretch(column + 1, key.width);
        const auto label = [this, &key] {
            switch (key.action) {
            case Action::Delete:
                return tr("Backspace");
            case Action::Shift:
                return tr("Shift");
            case Action::Symbols:
                return symbols ? tr("Letters") : tr("Numbers and symbols");
            case Action::Left:
                return tr("Move cursor left");
            case Action::Right:
                return tr("Move cursor right");
            default:
                return key.label;
            }
        }();
        button->setAccessibleName(label);
        button->setToolTip(label == key.label ? QString{} : label);
        SetStyleProperty(button, "oskPrimary", key.action == Action::Confirm);
        SetStyleProperty(button, "oskSelected", key.action == Action::Shift && shifted);
    }
    if (controller_focus &&
        (!controller_focus->isEnabled() || !controller_focus->isVisibleTo(this))) {
        ResetControllerFocus();
    }
}

void KeyboardPanel::ActivateKey(int row, int column) {
    const auto key = visible_keys[row].at(column);
    switch (key.action) {
    case Action::Text:
        if (shifted) {
            shifted = false;
            UpdateKeys();
        }
        emit TextTyped(key.label);
        break;
    case Action::Shift:
        shifted = !shifted;
        UpdateKeys();
        break;
    case Action::Symbols:
        symbols = !symbols;
        shifted = false;
        UpdateKeys();
        break;
    case Action::Delete:
        emit DeletePressed();
        break;
    case Action::Space:
        emit SpacePressed();
        break;
    case Action::Left:
        emit CursorMoved(-1);
        break;
    case Action::Right:
        emit CursorMoved(1);
        break;
    case Action::Confirm:
        emit Confirmed();
        break;
    }
}

void KeyboardPanel::ChangeCandidatePage(int step) {
    if (candidates.isEmpty()) {
        return;
    }
    const int last = static_cast<int>(candidates.size() - 1) / candidate_capacity;
    candidate_page = std::clamp(candidate_page + step, 0, last);
    UpdateCandidates();
}

void KeyboardPanel::SetControllerFocus(QPushButton* button) {
    if (controller_focus == button) {
        return;
    }
    if (controller_focus) {
        SetStyleProperty(controller_focus, "oskFocus", false);
    }
    controller_focus = button;
    if (button) {
        SetStyleProperty(button, "oskFocus", true);
    }
}

void KeyboardPanel::MoveControllerFocus(Qt::Key key) {
    if (!controller_focus || !controller_focus->isEnabled() || !controller_focus->isVisible()) {
        const auto start_row = SelectableButtons(key_rows[1]);
        if (!start_row.isEmpty()) {
            SetControllerFocus(start_row.at((start_row.size() - 1) / 2));
        }
        return;
    }

    std::array<QHBoxLayout*, KeyboardRows + 2> layout_rows{};
    layout_rows[0] = header_row;
    layout_rows[1] = candidate_row;
    std::ranges::copy(key_rows, layout_rows.begin() + 2);
    for (int row = 0; row < static_cast<int>(layout_rows.size()); ++row) {
        const auto buttons = SelectableButtons(layout_rows[row]);
        const auto index = buttons.indexOf(controller_focus);
        if (index < 0) {
            continue;
        }
        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            const auto next = std::clamp(index + (key == Qt::Key_Right ? 1 : -1), qsizetype{0},
                                         buttons.size() - 1);
            SetControllerFocus(buttons[next]);
            focus_column = -1;
        } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
            if (focus_column < 0) {
                focus_column = controller_focus->mapTo(this, controller_focus->rect().center()).x();
            }
            const int step = key == Qt::Key_Down ? 1 : -1;
            for (int next = row + step; next >= 0 && next < static_cast<int>(layout_rows.size());
                 next += step) {
                const auto items = SelectableButtons(layout_rows[next]);
                if (!items.isEmpty()) {
                    const auto distance = [this](const QPushButton* button) {
                        return std::abs(button->mapTo(this, button->rect().center()).x() -
                                        focus_column);
                    };
                    SetControllerFocus(*std::ranges::min_element(items, {}, distance));
                    break;
                }
            }
        }
        return;
    }
}

} // namespace Lichii
