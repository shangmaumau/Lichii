// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Standalone Qt Core check. Register the RCC built from src/lichii/keyboard.qrc.
#include <algorithm>
#include <array>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QResource>

#include "lichii/util/keyboard_input.h"

using Lichii::KeyboardInput;
using Lichii::KeyboardLanguage;
using Lichii::KeyboardMode;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2 || !QResource::registerResource(QString::fromLocal8Bit(argv[1]))) {
        std::cerr << "Pass a binary RCC built from src/lichii/keyboard.qrc\n";
        return 2;
    }
    constexpr std::array first_lookups{
        std::pair{KeyboardLanguage::EnglishUS, "en-US"},
        std::pair{KeyboardLanguage::EnglishUK, "en-GB"},
        std::pair{KeyboardLanguage::ChineseSimplified, "zh-Hans"},
        std::pair{KeyboardLanguage::ChineseTraditional, "zh-Hant"},
        std::pair{KeyboardLanguage::Japanese, "ja"},
    };
    QElapsedTimer timer;
    for (const auto& [language, locale] : first_lookups) {
        KeyboardInput first;
        first.SetLanguage(language);
        timer.start();
        first.Feed(u"ni");
        std::cout << "First " << locale << " lookup " << timer.nsecsElapsed() / 1e6 << " ms\n";
    }
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool passed, const char* name) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    };

    KeyboardInput keyboard;
    check(keyboard.Feed(u"colo") == u"colo", "English immediately commits the typed prefix");
    check(keyboard.Preedit().isEmpty(), "English does not duplicate committed text in preedit");
    check(keyboard.Candidates().contains(QStringLiteral("color")), "US dictionary offers color");
    check(!keyboard.Candidates().contains(QStringLiteral("colour")), "US excludes the UK spelling");
    check(keyboard.AcceptCandidate(keyboard.Candidates().indexOf(QStringLiteral("color"))) == u"r",
          "English candidate commits the suffix only");
    check(!keyboard.Backspace(), "English backspace delegates the editor deletion");
    check(keyboard.Candidates().contains(QStringLiteral("color")), "English backspace shortens the prefix");
    keyboard.SetLanguage(KeyboardLanguage::EnglishUK);
    check(keyboard.Candidates().isEmpty(), "Language change clears the old prefix");
    check(keyboard.Feed(u"COLO") == u"COLO", "UK preserves uppercase direct text");
    check(keyboard.Candidates().contains(QStringLiteral("COLOUR")), "UK suggestion preserves uppercase");
    check(!keyboard.Candidates().contains(QStringLiteral("COLOR")), "UK excludes the US spelling");
    check(keyboard.AcceptCandidate(keyboard.Candidates().indexOf(QStringLiteral("COLOUR"))) == u"UR",
          "UK completion preserves uppercase suffix");
    check(keyboard.Flush().isEmpty(), "English flush does not duplicate direct text");
    keyboard.Feed(u"organis");
    check(keyboard.Candidates().contains(QStringLiteral("organisation")), "British -ise variant");
    keyboard.Feed(u" ");
    check(keyboard.Candidates().isEmpty(), "A literal space clears the Latin prefix");
    check(keyboard.Feed(u"-12.34") == u"-12.34", "Numeric values and punctuation are literal");

    struct Case {
        KeyboardLanguage language;
        QStringView typed;
        QStringView expected;
        const char* label;
    };
    constexpr Case cases[]{
        {KeyboardLanguage::ChineseSimplified, u"nihao", u"你好", "Simplified Chinese greeting"},
        {KeyboardLanguage::ChineseSimplified, u"zhongguo", u"中国", "Simplified Chinese country"},
        {KeyboardLanguage::ChineseSimplified, u"xuexi", u"学习", "Simplified Chinese study"},
        {KeyboardLanguage::ChineseSimplified, u"gonggongqiche", u"公共汽车", "Pinyin multi-character phrase"},
        {KeyboardLanguage::ChineseSimplified, u"shijieheping", u"世界和平", "Pinyin whole-phrase lookup"},
        {KeyboardLanguage::ChineseSimplified, u"lǜse", u"绿色", "Pinyin ü with tone accent"},
        {KeyboardLanguage::ChineseSimplified, u"lu:se", u"绿色", "Pinyin u-colon form"},
        {KeyboardLanguage::ChineseSimplified, u"xi'an", u"西安", "Explicit pinyin apostrophe boundary"},
        {KeyboardLanguage::ChineseTraditional, u"zhongguo", u"中國", "Traditional Chinese country"},
        {KeyboardLanguage::ChineseTraditional, u"xuexi", u"學習", "Traditional Chinese study"},
        {KeyboardLanguage::ChineseTraditional, u"jianpan", u"鍵盤", "Traditional Chinese keyboard"},
        {KeyboardLanguage::ChineseTraditional, u"nver", u"女兒", "Traditional Chinese ü alias"},
        {KeyboardLanguage::ChineseTraditional, u"lvse", u"綠色", "Traditional Chinese regional form"},
        {KeyboardLanguage::Japanese, u"nihon", u"日本", "Japanese hiragana to kanji"},
        {KeyboardLanguage::Japanese, u"nihongo", u"日本語", "Japanese whole-word candidate"},
        {KeyboardLanguage::Japanese, u"nyuuryoku", u"入力", "Japanese yōon word"},
        {KeyboardLanguage::Japanese, u"toukyou", u"東京", "Japanese proper place name"},
        {KeyboardLanguage::Japanese, u"kouen", u"公園", "Japanese first homophone"},
        {KeyboardLanguage::Japanese, u"kouen", u"講演", "Japanese alternative homophone"},
        {KeyboardLanguage::Japanese, u"taberu", u"食べる", "Japanese okuri stem"},
        {KeyboardLanguage::Japanese, u"watashiha", u"私は", "Japanese word plus kana suffix"},
        {KeyboardLanguage::Japanese, u"ge-mu", u"ゲーム", "Japanese katakana and long-vowel mark"},
        {KeyboardLanguage::Japanese, u"かいはつ", u"開発", "Direct kana to kanji"},
    };
    for (const auto& test : cases) {
        keyboard.SetLanguage(test.language);
        keyboard.Reset();
        const auto direct = keyboard.Feed(test.typed);
        const auto pick = keyboard.Candidates().indexOf(test.expected.toString());
        check(direct.isEmpty() && pick >= 0 && keyboard.AcceptCandidate(pick) == test.expected &&
                  keyboard.Preedit().isEmpty(),
              test.label);
    }

    keyboard.SetLanguage(KeyboardLanguage::ChineseSimplified);
    keyboard.Feed(u"xuexi");
    check(keyboard.Flush() == u"学习", "Frequency ranking gives the usual Simplified word first");
    keyboard.Feed(u"nihaoqw");
    check(keyboard.AcceptCandidate(0) == u"你好" && keyboard.Preedit() == u"qw",
          "Converting a Chinese prefix preserves the pending tail");
    check(keyboard.Flush() == u"qw", "Unmatched letters are preserved on completion");
    keyboard.Feed(u"niha");
    check(keyboard.Backspace() && keyboard.Preedit() == u"nih", "CJK backspace only edits composition");
    check(keyboard.AcceptCandidate(-1).isEmpty() && keyboard.Preedit() == u"nih",
          "Out-of-range candidates leave composition alone");
    keyboard.Reset();
    check(keyboard.Feed(u"nihao ") == u"你好", "Space chooses the current Chinese candidate");
    check(keyboard.Feed(u" ") == u" ", "Space is literal without composition");

    keyboard.Feed(u"nihao");
    check(keyboard.Flush() == u"你好", "Mode switch can commit the current Pinyin word");
    keyboard.SetMode(KeyboardMode::DirectLatin);
    check(keyboard.Feed(u"Lichii123") == u"Lichii123" && keyboard.Preedit().isEmpty() &&
              keyboard.Candidates().isEmpty() && !keyboard.Backspace() && keyboard.Flush().isEmpty(),
          "Direct Latin mode inserts characters without composition or suggestions");

    keyboard.SetMode(KeyboardMode::Wubi);
    check(keyboard.Feed(u"WQVB").isEmpty() && keyboard.Preedit() == u"WQVB" &&
              keyboard.Candidates().contains(QStringLiteral("你好")),
          "Wubi accepts an uppercase four-letter phrase code");
    const auto next_phrase = keyboard.Candidates().value(0);
    check(!next_phrase.isEmpty() && keyboard.Feed(u"a") == next_phrase && keyboard.Preedit() == u"a",
          "The fifth letter commits the previous Wubi code and starts a new one");
    check(keyboard.Candidates().value(0) == u"工" && keyboard.Candidates().value(1) == u"戈" &&
              keyboard.Feed(u" ") == u"工" && keyboard.Preedit().isEmpty(),
          "A one-letter Wubi code ranks candidates and Space commits the first");
    keyboard.Feed(u"khlg");
    check(keyboard.Candidates().contains(QStringLiteral("中国")), "Wubi offers a Simplified candidate");
    check(keyboard.Backspace() && keyboard.Preedit() == u"khl", "Wubi backspace edits the active code");

    keyboard.SetLanguage(KeyboardLanguage::ChineseTraditional);
    keyboard.Feed(u"khlg");
    const auto traditional = keyboard.Candidates().indexOf(QStringLiteral("中國"));
    check(keyboard.AcceptCandidate(traditional) == u"中國" && keyboard.Preedit().isEmpty(),
          "Wubi commits a Traditional candidate for the same code");
    keyboard.Feed(u"lakg");
    const auto native_traditional = keyboard.Candidates().indexOf(QStringLiteral("國"));
    check(keyboard.AcceptCandidate(native_traditional) == u"國",
          "Traditional Wubi also recognizes native shape codes");
    keyboard.Feed(u"ipnu");
    const auto study = keyboard.Candidates().indexOf(QStringLiteral("學習"));
    check(keyboard.AcceptCandidate(study) == u"學習", "Wubi supports multi-character vocabulary");
    check(keyboard.Feed(u"123!") == u"123!" && keyboard.Candidates().isEmpty(),
          "Wubi passes numbers and punctuation through without composition");
    check(keyboard.Feed(u"zzzz").isEmpty() && keyboard.Candidates().isEmpty() &&
              keyboard.Feed(u"!") == u"zzzz!",
          "An unrecognized Wubi code survives a literal punctuation boundary");
    keyboard.SetMode(KeyboardMode::Default);
    check(keyboard.Feed(u"zhongguo ") == u"中國", "Switching back restores Traditional Pinyin");

    keyboard.SetLanguage(KeyboardLanguage::Japanese);
    keyboard.Feed(u"gakkou");
    check(keyboard.Preedit() == u"がっこう" && keyboard.Candidates().contains(QStringLiteral("学校")),
          "Japanese doubled consonant and kanji suggestion");
    check(keyboard.Flush() == u"がっこう", "Unconverted Japanese Enter preserves the kana reading");
    keyboard.Feed(u"kan'i");
    check(keyboard.Flush() == u"かんい", "Japanese n-apostrophe");
    keyboard.Feed(u"nihon");
    check(keyboard.Preedit() == u"にほn" && keyboard.Flush() == u"にほん",
          "Japanese trailing n resolves on commit");
    keyboard.Feed(u"tch");
    check(keyboard.Backspace() && keyboard.Preedit() == u"tc", "Japanese backspace removes a romaji key");
    keyboard.Reset();
    check(keyboard.Feed(u"😀42") == u"😀42", "Uncomposed Unicode and numeric text survives intact");

    constexpr std::array timings{
        std::pair{KeyboardLanguage::EnglishUS, QStringView(u"organization")},
        std::pair{KeyboardLanguage::EnglishUK, QStringView(u"colourful")},
        std::pair{KeyboardLanguage::ChineseSimplified, QStringView(u"gonggongqiche")},
        std::pair{KeyboardLanguage::ChineseTraditional, QStringView(u"jianpan")},
        std::pair{KeyboardLanguage::Japanese, QStringView(u"nyuuryoku")},
    };
    QList<qint64> nanoseconds;
    for (const auto& [language, phrase] : timings) {
        keyboard.SetLanguage(language);
        for (int run = 0; run < 32; ++run) {
            keyboard.Reset();
            for (qsizetype offset = 0; offset < phrase.size(); ++offset) {
                timer.start();
                keyboard.Feed(phrase.sliced(offset, 1));
                nanoseconds.append(timer.nsecsElapsed());
            }
        }
    }
    std::ranges::sort(nanoseconds);
    std::cout << checks - failures << "/" << checks << " checks passed\n"
              << "Warm lookup " << nanoseconds.size() << " keys, "
              << "median " << nanoseconds[nanoseconds.size() / 2] / 1e3 << " us, "
              << "P95 " << nanoseconds[nanoseconds.size() * 95 / 100] / 1e3 << " us, "
              << "max " << nanoseconds.back() / 1e3 << " us\n";
    return failures ? 1 : 0;
}
