// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QByteArrayView>
#include <QFile>
#include <QHash>
#include <QtEndian>

#include "lichii/util/keyboard_input.h"

namespace Lichii {
namespace {

constexpr int CompletionLimit = 12;
constexpr int CandidateLimit = 64;
constexpr qsizetype DraftLimit = 96;
constexpr qsizetype WubiCodeLimit = 4;

class Dictionary {
public:
    explicit Dictionary(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        blob = file.readAll();
        if (blob.size() < HeaderSize + OffsetSize || !blob.startsWith("LKD1")) {
            return;
        }
        const auto count = qFromLittleEndian<quint32>(blob.constData() + 4);
        if (count <= (blob.size() - HeaderSize - OffsetSize) / OffsetSize) {
            row_count = count;
            text_offset = HeaderSize + (row_count + 1) * OffsetSize;
        }
    }

    static const Dictionary& ForLanguage(KeyboardLanguage language) {
        switch (language) {
        case KeyboardLanguage::EnglishUS: {
            static const Dictionary us(QStringLiteral(":/keyboard/dictionaries/en-US.bin"));
            return us;
        }
        case KeyboardLanguage::EnglishUK: {
            static const Dictionary uk(QStringLiteral(":/keyboard/dictionaries/en-GB.bin"));
            return uk;
        }
        case KeyboardLanguage::ChineseSimplified: {
            static const Dictionary hans(QStringLiteral(":/keyboard/dictionaries/zh-Hans.bin"));
            return hans;
        }
        case KeyboardLanguage::ChineseTraditional: {
            static const Dictionary hant(QStringLiteral(":/keyboard/dictionaries/zh-Hant.bin"));
            return hant;
        }
        case KeyboardLanguage::Japanese: {
            static const Dictionary ja(QStringLiteral(":/keyboard/dictionaries/ja.bin"));
            return ja;
        }
        }
        return ForLanguage(KeyboardLanguage::EnglishUS);
    }

    static const Dictionary& ForWubi(KeyboardLanguage language) {
        if (language == KeyboardLanguage::ChineseTraditional) {
            static const Dictionary hant(QStringLiteral(":/keyboard/dictionaries/wubi-Hant.bin"));
            return hant;
        }
        static const Dictionary hans(QStringLiteral(":/keyboard/dictionaries/wubi-Hans.bin"));
        return hans;
    }

    qsizetype Count() const {
        return row_count;
    }

    std::pair<QByteArrayView, QByteArrayView> Record(qsizetype index) const {
        if (index < 0 || index >= row_count) {
            return {};
        }
        const char* entry = blob.constData() + HeaderSize + index * OffsetSize;
        const auto start = qFromLittleEndian<quint32>(entry);
        const auto end = qFromLittleEndian<quint32>(entry + OffsetSize);
        if (start >= end || end > blob.size() - text_offset) {
            return {};
        }
        const QByteArrayView row(blob.constData() + text_offset + start, end - start);
        const auto split = row.indexOf('\t');
        if (split < 0) {
            return {};
        }
        return {row.first(split), row.sliced(split + 1)};
    }

    qsizetype LowerBound(QByteArrayView key) const {
        qsizetype first = 0;
        qsizetype last = row_count;
        while (first < last) {
            const auto middle = first + (last - first) / 2;
            if (Record(middle).first.compare(key) < 0) {
                first = middle + 1;
            } else {
                last = middle;
            }
        }
        return first;
    }

    QByteArrayView Find(QByteArrayView key) const {
        if (key.empty()) {
            return {};
        }
        const auto [candidate, values] = Record(LowerBound(key));
        return candidate == key ? values : QByteArrayView{};
    }

private:
    static constexpr qsizetype HeaderSize = 8;
    static constexpr qsizetype OffsetSize = 4;
    QByteArray blob;
    qsizetype row_count = 0;
    qsizetype text_offset = 0;
};

bool IsEnglish(KeyboardLanguage language) {
    return language == KeyboardLanguage::EnglishUS || language == KeyboardLanguage::EnglishUK;
}

bool IsChinese(KeyboardLanguage language) {
    return language == KeyboardLanguage::ChineseSimplified ||
           language == KeyboardLanguage::ChineseTraditional;
}

bool IsRoman(QChar ch) {
    return ch >= u'a' && ch <= u'z';
}

bool IsLatinLetter(QChar ch) {
    return ch.isLetter() && ch.script() == QChar::Script_Latin;
}

bool IsKana(QChar ch) {
    return (ch >= u'ぁ' && ch <= u'ゖ') || (ch >= u'ァ' && ch <= u'ヶ') || ch == u'ー';
}

QString PinyinKey(QStringView pinyin) {
    QString lower = pinyin.toString().toLower();
    lower.replace(QStringLiteral("u:"), QStringLiteral("v"));
    lower.replace(u'ü', u'v');
    QString result;
    for (const QChar ch : lower.normalized(QString::NormalizationForm_D)) {
        if (IsRoman(ch) || ch == u'\'') {
            result.append(ch);
        } else if (ch == u'\u0308' && result.endsWith(u'u')) {
            result[result.size() - 1] = u'v';
        }
    }
    return result;
}

const QHash<QStringView, QStringView>& RomajiTable() {
    static const QHash<QStringView, QStringView> table{
        {u"a", u"あ"},     {u"i", u"い"},     {u"u", u"う"},     {u"e", u"え"},     {u"o", u"お"},
        {u"ka", u"か"},   {u"ki", u"き"},   {u"ku", u"く"},   {u"ke", u"け"},   {u"ko", u"こ"},
        {u"ga", u"が"},   {u"gi", u"ぎ"},   {u"gu", u"ぐ"},   {u"ge", u"げ"},   {u"go", u"ご"},
        {u"sa", u"さ"},   {u"si", u"し"},   {u"su", u"す"},   {u"se", u"せ"},   {u"so", u"そ"},
        {u"za", u"ざ"},   {u"zi", u"じ"},   {u"zu", u"ず"},   {u"ze", u"ぜ"},   {u"zo", u"ぞ"},
        {u"ta", u"た"},   {u"ti", u"ち"},   {u"tu", u"つ"},   {u"te", u"て"},   {u"to", u"と"},
        {u"da", u"だ"},   {u"di", u"ぢ"},   {u"du", u"づ"},   {u"de", u"で"},   {u"do", u"ど"},
        {u"na", u"な"},   {u"ni", u"に"},   {u"nu", u"ぬ"},   {u"ne", u"ね"},   {u"no", u"の"},
        {u"ha", u"は"},   {u"hi", u"ひ"},   {u"hu", u"ふ"},   {u"he", u"へ"},   {u"ho", u"ほ"},
        {u"ba", u"ば"},   {u"bi", u"び"},   {u"bu", u"ぶ"},   {u"be", u"べ"},   {u"bo", u"ぼ"},
        {u"pa", u"ぱ"},   {u"pi", u"ぴ"},   {u"pu", u"ぷ"},   {u"pe", u"ぺ"},   {u"po", u"ぽ"},
        {u"ma", u"ま"},   {u"mi", u"み"},   {u"mu", u"む"},   {u"me", u"め"},   {u"mo", u"も"},
        {u"ya", u"や"},   {u"yu", u"ゆ"},   {u"yo", u"よ"},   {u"ra", u"ら"},   {u"ri", u"り"},
        {u"ru", u"る"},   {u"re", u"れ"},   {u"ro", u"ろ"},   {u"wa", u"わ"},   {u"wo", u"を"},
        {u"wi", u"うぃ"}, {u"we", u"うぇ"}, {u"ye", u"いぇ"}, {u"shi", u"し"},  {u"ji", u"じ"},
        {u"chi", u"ち"},  {u"tsu", u"つ"},  {u"fu", u"ふ"},   {u"vu", u"ゔ"},   {u"va", u"ゔぁ"},
        {u"vi", u"ゔぃ"}, {u"ve", u"ゔぇ"}, {u"vo", u"ゔぉ"}, {u"fa", u"ふぁ"}, {u"fi", u"ふぃ"},
        {u"fe", u"ふぇ"}, {u"fo", u"ふぉ"}, {u"fy", u"ふぃ"}, {u"fyu", u"ふゅ"},
        {u"kya", u"きゃ"}, {u"kyu", u"きゅ"}, {u"kyo", u"きょ"},
        {u"gya", u"ぎゃ"}, {u"gyu", u"ぎゅ"}, {u"gyo", u"ぎょ"},
        {u"sha", u"しゃ"}, {u"shu", u"しゅ"}, {u"sho", u"しょ"}, {u"she", u"しぇ"},
        {u"sya", u"しゃ"}, {u"syu", u"しゅ"}, {u"syo", u"しょ"},
        {u"ja", u"じゃ"},  {u"ju", u"じゅ"},  {u"jo", u"じょ"},  {u"je", u"じぇ"},
        {u"jya", u"じゃ"}, {u"jyu", u"じゅ"}, {u"jyo", u"じょ"},
        {u"zya", u"じゃ"}, {u"zyu", u"じゅ"}, {u"zyo", u"じょ"},
        {u"cha", u"ちゃ"}, {u"chu", u"ちゅ"}, {u"cho", u"ちょ"}, {u"che", u"ちぇ"},
        {u"cya", u"ちゃ"}, {u"cyu", u"ちゅ"}, {u"cyo", u"ちょ"},
        {u"tya", u"ちゃ"}, {u"tyu", u"ちゅ"}, {u"tyo", u"ちょ"},
        {u"dya", u"ぢゃ"}, {u"dyu", u"ぢゅ"}, {u"dyo", u"ぢょ"},
        {u"nya", u"にゃ"}, {u"nyu", u"にゅ"}, {u"nyo", u"にょ"},
        {u"hya", u"ひゃ"}, {u"hyu", u"ひゅ"}, {u"hyo", u"ひょ"},
        {u"bya", u"びゃ"}, {u"byu", u"びゅ"}, {u"byo", u"びょ"},
        {u"pya", u"ぴゃ"}, {u"pyu", u"ぴゅ"}, {u"pyo", u"ぴょ"},
        {u"mya", u"みゃ"}, {u"myu", u"みゅ"}, {u"myo", u"みょ"},
        {u"rya", u"りゃ"}, {u"ryu", u"りゅ"}, {u"ryo", u"りょ"},
        {u"tsa", u"つぁ"}, {u"tsi", u"つぃ"}, {u"tse", u"つぇ"}, {u"tso", u"つぉ"},
        {u"thi", u"てぃ"}, {u"thu", u"てゅ"}, {u"dhi", u"でぃ"}, {u"dhu", u"でゅ"},
        {u"twu", u"とぅ"}, {u"dwu", u"どぅ"}, {u"kwa", u"くぁ"}, {u"gwa", u"ぐぁ"},
        {u"la", u"ぁ"}, {u"li", u"ぃ"}, {u"lu", u"ぅ"}, {u"le", u"ぇ"}, {u"lo", u"ぉ"},
        {u"xa", u"ぁ"}, {u"xi", u"ぃ"}, {u"xu", u"ぅ"}, {u"xe", u"ぇ"}, {u"xo", u"ぉ"},
        {u"lya", u"ゃ"}, {u"lyu", u"ゅ"}, {u"lyo", u"ょ"}, {u"lwa", u"ゎ"},
        {u"xya", u"ゃ"}, {u"xyu", u"ゅ"}, {u"xyo", u"ょ"}, {u"xwa", u"ゎ"},
        {u"ltu", u"っ"}, {u"xtu", u"っ"}, {u"ltsu", u"っ"}, {u"xtsu", u"っ"},
    };
    return table;
}

QString ToKana(QStringView source, bool final) {
    const QString lower = source.toString().toLower();
    const QStringView input(lower);
    const auto& table = RomajiTable();
    constexpr QStringView vowels_and_y = u"aiueoy";
    QString result;
    for (qsizetype pos = 0; pos < input.size();) {
        const auto current = input[pos];
        const auto next = pos + 1 < input.size() ? input[pos + 1] : QChar{};
        if (current == u'n') {
            if (next == u'\'') {
                result.append(u'ん');
                pos += 2;
                continue;
            }
            if (next == u'n') {
                result.append(u'ん');
                const bool next_n_starts_syllable =
                    pos + 2 < input.size() && vowels_and_y.contains(input[pos + 2]);
                pos += next_n_starts_syllable ? 1 : 2;
                continue;
            }
            if ((!next.isNull() && !vowels_and_y.contains(next)) || (next.isNull() && final)) {
                result.append(u'ん');
                ++pos;
                continue;
            }
        }
        if (IsRoman(current) && !vowels_and_y.contains(current) &&
            (current == next || (current == u't' && input.sliced(pos).startsWith(u"tch")))) {
            result.append(u'っ');
            ++pos;
            continue;
        }
        qsizetype size = std::min<qsizetype>(4, input.size() - pos);
        for (; size > 0; --size) {
            const auto entry = table.constFind(input.sliced(pos, size));
            if (entry != table.cend()) {
                result.append(entry.value());
                pos += size;
                break;
            }
        }
        if (size == 0) {
            result.append(current == u'-' ? u'ー' : source[pos]);
            ++pos;
        }
    }
    return result;
}

QString ShiftKana(QString kana, bool katakana) {
    for (auto& ch : kana) {
        if (katakana && ch >= u'ぁ' && ch <= u'ゖ') {
            ch = QChar(ch.unicode() + 0x60);
        } else if (!katakana && ch >= u'ァ' && ch <= u'ヶ') {
            ch = QChar(ch.unicode() - 0x60);
        }
    }
    return kana;
}

QChar KanaOnset(QChar kana) {
    static constexpr std::pair<QStringView, QChar> groups[]{
        {u"あぁ", u'a'}, {u"いぃ", u'i'}, {u"うぅ", u'u'}, {u"えぇ", u'e'}, {u"おぉ", u'o'},
        {u"かきくけこ", u'k'}, {u"がぎぐげご", u'g'}, {u"さしすせそ", u's'},
        {u"ざじずぜぞ", u'z'}, {u"たちつてとっ", u't'}, {u"だぢづでど", u'd'},
        {u"なにぬねのん", u'n'}, {u"はひふへほ", u'h'}, {u"ばびぶべぼ", u'b'},
        {u"ぱぴぷぺぽ", u'p'}, {u"まみむめも", u'm'}, {u"やゆよゃゅょ", u'y'},
        {u"らりるれろ", u'r'}, {u"わをゎ", u'w'}, {u"ゔ", u'v'},
    };
    for (const auto& [set, onset] : groups) {
        if (set.contains(kana)) {
            return onset;
        }
    }
    return {};
}

void AppendUnique(QStringList& candidates, const QString& value) {
    if (!value.isEmpty() && !candidates.contains(value) && candidates.size() < CandidateLimit) {
        candidates.append(value);
    }
}

void AppendValues(QStringList& candidates, QByteArrayView values, QStringView suffix = {}) {
    while (!values.empty() && candidates.size() < CandidateLimit) {
        const auto next = values.indexOf('\t');
        const auto data = next < 0 ? values : values.first(next);
        auto value = QString::fromUtf8(data);
        value.append(suffix);
        AppendUnique(candidates, value);
        if (next < 0) {
            break;
        }
        values = values.sliced(next + 1);
    }
}

QStringList CompleteEnglish(const Dictionary& dictionary, QStringView draft) {
    if (draft.size() < 2) {
        return {};
    }
    const bool upper = draft.size() > 1 &&
                       std::ranges::all_of(draft, [](QChar ch) {
                           return !ch.isLetter() || ch.isUpper();
                       });
    const auto prefix = draft.toString().toLower().toUtf8();
    struct Word {
        QByteArrayView value;
        int frequency;
    };
    std::vector<Word> words;
    for (auto row = dictionary.LowerBound(prefix); row < dictionary.Count(); ++row) {
        const auto [key, values] = dictionary.Record(row);
        if (!key.startsWith(prefix)) {
            break;
        }
        if (key.size() == prefix.size()) {
            continue;
        }
        const auto split = values.indexOf('\t');
        if (split >= 0) {
            const auto count = values.sliced(split + 1).toInt();
            words.push_back({values.first(split), count});
        }
    }
    QStringList result;
    const auto count = std::min<std::size_t>(CompletionLimit, words.size());
    std::partial_sort(words.begin(), words.begin() + count, words.end(), [](const auto& a, const auto& b) {
        if (a.frequency != b.frequency) {
            return a.frequency > b.frequency;
        }
        if (a.value.size() != b.value.size()) {
            return a.value.size() < b.value.size();
        }
        return a.value.compare(b.value) < 0;
    });
    for (std::size_t i = 0; i < count; ++i) {
        auto word = QString::fromUtf8(words[i].value);
        if (upper) {
            word = word.toUpper();
        }
        if (word.size() > draft.size()) {
            word.replace(0, draft.size(), draft.toString());
            AppendUnique(result, word);
        }
    }
    return result;
}

QStringList ConvertJapanese(const Dictionary& dictionary, QStringView draft) {
    const auto kana = ToKana(draft, true);
    const auto hiragana = ShiftKana(kana, false);
    QStringList result;
    AppendValues(result, dictionary.Find(hiragana.toUtf8()));
    // SKK okuri entries store the word stem under the consonant of the first suffix kana.
    for (qsizetype split = hiragana.size() - 1; split > 0 && result.size() < CompletionLimit; --split) {
        const auto onset = KanaOnset(hiragana[split]);
        if (onset.isNull()) {
            continue;
        }
        auto stem = hiragana.first(split);
        stem.append(onset);
        const auto suffix = QStringView(hiragana).sliced(split);
        AppendValues(result, dictionary.Find(stem.toUtf8()), suffix);
    }
    for (qsizetype split = hiragana.size() - 1; split > 1 && result.isEmpty(); --split) {
        const auto reading = QStringView(hiragana);
        AppendValues(result, dictionary.Find(reading.first(split).toUtf8()), reading.sliced(split));
    }
    result.resize(std::min<qsizetype>(result.size(), CandidateLimit - 2));
    AppendUnique(result, kana);
    AppendUnique(result, ShiftKana(hiragana, true));
    return result;
}

} // namespace

KeyboardLanguage KeyboardInput::Language() const {
    return language;
}

void KeyboardInput::SetLanguage(KeyboardLanguage new_language) {
    if (language != new_language) {
        Reset();
        language = new_language;
    }
}

void KeyboardInput::SetMode(KeyboardMode new_mode) {
    if (mode != new_mode) {
        Reset();
        mode = new_mode;
    }
}

void KeyboardInput::Reset() {
    draft.clear();
    candidates.clear();
    candidate_length = 0;
}

QString KeyboardInput::Preedit() const {
    if (IsEnglish(language)) {
        return {};
    }
    return language == KeyboardLanguage::Japanese ? ToKana(draft, false) : draft;
}

const QStringList& KeyboardInput::Candidates() const {
    return candidates;
}

QString KeyboardInput::Feed(QStringView text) {
    if (mode == KeyboardMode::DirectLatin) {
        return text.toString();
    }
    if (IsEnglish(language)) {
        for (const auto ch : text) {
            if (IsLatinLetter(ch) || (ch == u'\'' && !draft.isEmpty())) {
                if (draft.size() == DraftLimit) {
                    draft.clear();
                }
                draft.append(ch);
            } else {
                draft.clear();
            }
        }
        UpdateCandidates();
        return text.toString();
    }

    QString commit;
    const bool chinese = IsChinese(language);
    const bool wubi = chinese && mode == KeyboardMode::Wubi;
    const auto code_limit = wubi ? WubiCodeLimit : DraftLimit;
    for (const auto ch : text) {
        const bool composing =
            wubi ? (IsRoman(ch) || (ch >= u'A' && ch <= u'Z'))
                 : IsLatinLetter(ch) || (!draft.isEmpty() && ch == u'\'') ||
                       (chinese && !draft.isEmpty() && ch == u':') ||
                       (!chinese && (IsKana(ch) || (!draft.isEmpty() && ch == u'-')));
        if (composing) {
            if (draft.size() == code_limit) {
                commit.append(Flush());
            }
            draft.append(ch);
            continue;
        }
        if (ch == u' ' && !draft.isEmpty()) {
            UpdateCandidates();
            if (!candidates.isEmpty()) {
                commit.append(AcceptCandidate(0));
                continue;
            }
        }
        commit.append(Flush());
        commit.append(ch);
    }
    UpdateCandidates();
    return commit;
}

bool KeyboardInput::Backspace() {
    if (draft.isEmpty()) {
        return false;
    }
    draft.chop(1);
    UpdateCandidates();
    return !IsEnglish(language);
}

QString KeyboardInput::AcceptCandidate(int index) {
    if (index < 0 || index >= candidates.size()) {
        return {};
    }
    const auto selected = candidates.at(index);
    if (IsEnglish(language)) {
        auto suffix = selected.sliced(draft.size());
        draft = selected;
        UpdateCandidates();
        return suffix;
    }
    draft.remove(0, candidate_length);
    if (draft.startsWith(u'\'')) {
        draft.remove(0, 1);
    }
    UpdateCandidates();
    return selected;
}

QString KeyboardInput::Flush() {
    QString commit;
    if (language == KeyboardLanguage::Japanese) {
        commit = ToKana(draft, true);
    } else if (IsChinese(language)) {
        while (!draft.isEmpty()) {
            UpdateCandidates();
            if (candidates.isEmpty()) {
                commit.append(draft);
                break;
            }
            commit.append(AcceptCandidate(0));
        }
    }
    Reset();
    return commit;
}

void KeyboardInput::UpdateCandidates() {
    candidates.clear();
    candidate_length = 0;
    if (draft.isEmpty()) {
        return;
    }
    if (mode == KeyboardMode::Wubi && IsChinese(language)) {
        const auto& dictionary = Dictionary::ForWubi(language);
        AppendValues(candidates, dictionary.Find(draft.toLower().toLatin1()));
        candidate_length = draft.size();
        return;
    }
    const auto& dictionary = Dictionary::ForLanguage(language);
    if (IsEnglish(language)) {
        candidates = CompleteEnglish(dictionary, draft);
        return;
    }
    if (language == KeyboardLanguage::Japanese) {
        candidates = ConvertJapanese(dictionary, draft);
        candidate_length = draft.size();
        return;
    }
    for (auto length = draft.size(); length > 0; --length) {
        const auto key = PinyinKey(QStringView(draft).first(length)).toUtf8();
        AppendValues(candidates, dictionary.Find(key));
        if (!candidates.isEmpty()) {
            candidate_length = length;
            return;
        }
    }
}

} // namespace Lichii
