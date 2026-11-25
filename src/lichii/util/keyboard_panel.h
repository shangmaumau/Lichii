// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QStringList>
#include <QWidget>

#include "lichii/util/keyboard_input.h"

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace Lichii {

class KeyboardPanel final : public QWidget {
    Q_OBJECT

public:
    explicit KeyboardPanel(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void SetInputMode(KeyboardLanguage language, KeyboardMode mode, bool can_switch);
    void SetNumeric(bool numeric, bool decimal = false, bool negative = false,
                    const QString& decimal_separator = QStringLiteral("."));
    void SetSensitive(bool sensitive);
    void SetComposition(const QString& preedit, const QStringList& candidates);
    bool HandleControllerKey(Qt::Key key);
    void ResetControllerFocus();

signals:
    void TextTyped(const QString& text);
    void DeletePressed();
    void SpacePressed();
    void CursorMoved(int direction);
    void Confirmed();
    void Dismissed();
    void CandidateChosen(int index);
    void InputModeSwitchRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Action { Text, Shift, Symbols, Delete, Space, Left, Right, Confirm };
    struct Key {
        QString label;
        Action action = Action::Text;
        int width = 1;
        bool enabled = true;
    };

    static constexpr int CandidateCount = 6;
    static constexpr int KeyboardRows = 5;
    static constexpr int KeyboardColumns = 11;

    QPushButton* MakeButton(const QString& name);
    void UpdateStyle();
    void UpdateInputModeLabel();
    void UpdateCandidates();
    void UpdateKeys();
    void SetRow(int index, const QList<Key>& keys, int padding = 0);
    void ActivateKey(int row, int column);
    void ChangeCandidatePage(int step);
    void SetControllerFocus(QPushButton* button);
    void MoveControllerFocus(Qt::Key key);

    QLabel* input_mode{};
    QPushButton* input_mode_switch{};
    QPushButton* dismiss{};
    QWidget* candidate_strip{};
    QLabel* composition{};
    std::array<QPushButton*, CandidateCount> candidate_buttons{};
    QPushButton* previous_candidates{};
    QPushButton* next_candidates{};
    QHBoxLayout* header_row{};
    QHBoxLayout* candidate_row{};
    std::array<QWidget*, KeyboardRows> key_strips{};
    std::array<QHBoxLayout*, KeyboardRows> key_rows{};
    std::array<std::array<QPushButton*, KeyboardColumns>, KeyboardRows> key_buttons{};
    std::array<QList<Key>, KeyboardRows> visible_keys;
    QPushButton* controller_focus{};
    int focus_column = -1;

    KeyboardLanguage language = KeyboardLanguage::EnglishUS;
    KeyboardMode mode = KeyboardMode::Default;
    QString preedit;
    QStringList candidates;
    QString decimal_separator = QStringLiteral(".");
    int candidate_page = 0;
    int candidate_capacity = CandidateCount;
    bool numeric = false;
    bool can_switch_mode = false;
    bool decimal = false;
    bool negative = false;
    bool sensitive = false;
    bool shifted = false;
    bool symbols = false;
};

} // namespace Lichii
