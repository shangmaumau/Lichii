// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>
#include <QStringView>

namespace Lichii {

enum class KeyboardLanguage { EnglishUS, EnglishUK, ChineseSimplified, ChineseTraditional, Japanese };
enum class KeyboardMode { Default, DirectLatin, Wubi };

class KeyboardInput {
public:
    KeyboardLanguage Language() const;
    void SetLanguage(KeyboardLanguage language);
    void SetMode(KeyboardMode mode);
    void Reset();

    QString Preedit() const;
    const QStringList& Candidates() const;
    QString Feed(QStringView text);
    bool Backspace();
    QString AcceptCandidate(int index);
    QString Flush();

private:
    void UpdateCandidates();

    KeyboardLanguage language = KeyboardLanguage::EnglishUS;
    KeyboardMode mode = KeyboardMode::Default;
    QString draft;
    QStringList candidates;
    qsizetype candidate_length = 0;
};

} // namespace Lichii
