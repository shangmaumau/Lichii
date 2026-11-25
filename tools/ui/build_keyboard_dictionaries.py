# SPDX-FileCopyrightText: Copyright 2026 Lichii Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Build offline lookup tables from reference dictionaries supplied by the maintainer."""

import argparse
from collections import defaultdict
import gzip
from pathlib import Path
import re
import struct
import unicodedata


def write_dictionary(path, rows):
    records = [
        (key + "\t" + "\t".join(values)).encode("utf-8")
        for key, values in sorted(rows.items())
    ]
    offsets = [0]
    for record in records:
        offsets.append(offsets[-1] + len(record))
    with path.open("wb") as out:
        out.write(b"LKD1")
        out.write(struct.pack("<I", len(records)))
        out.write(struct.pack(f"<{len(offsets)}I", *offsets))
        out.writelines(records)
    print(f"{path.name}: {len(records):,} keys, {path.stat().st_size:,} bytes")


def read_frequencies(path):
    frequencies = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        word, _, count = line.rpartition(" ")
        if count.isdecimal():
            frequencies[word] = int(count)
    return frequencies


def english_words(path, frequencies):
    rows = {}
    for word in path.read_text(encoding="utf-8-sig").splitlines():
        word = unicodedata.normalize("NFC", word.strip()).replace("’", "'")
        if not (2 <= len(word) <= 40 and word.replace("'", "").isalpha()):
            continue
        key = word.lower()
        count = frequencies.get(key, 0)
        if key not in rows or word == key:
            rows[key] = (word, str(count))
    return rows


def pinyin_keys(reading):
    syllables = re.findall(r"[a-z:ü]+", reading.lower().replace("u:", "v").replace("ü", "v"))
    if not syllables:
        return []
    aliases = {"".join(syllables)}
    # Standard pinyin inserts an apostrophe before an internal vowel syllable.
    for index in range(1, len(syllables)):
        if syllables[index][0] in "aeo":
            suffix = "".join(syllables[index:])
            aliases |= {
                prefix[:-len(suffix)] + "'" + suffix
                for prefix in aliases
                if prefix.endswith(suffix)
            }
    return sorted(aliases)


def chinese_words(path, hans_freq, hant_freq):
    hans, hant = defaultdict(dict), defaultdict(dict)
    entries = 0
    with gzip.open(path, "rt", encoding="utf-8-sig") as source:
        for line in source:
            match = re.match(r"^(\S+)\s+(\S+)\s+\[{1,2}([^\]]+)\]{1,2}\s+/(.*)", line)
            if not match:
                continue
            traditional, simplified, reading, gloss = match.groups()
            if any(
                not (ord(ch) >= 0x3400 or ch == "·")
                for word in (traditional, simplified)
                for ch in word
            ):
                continue
            entries += 1
            common = max(hans_freq.get(simplified, 0), hant_freq.get(traditional, 0))
            variant = gloss.lower().startswith(("variant of ", "old variant of ", "surname "))
            penalty = 4 if variant else 1
            for key in pinyin_keys(reading):
                for rows, word, regional in (
                    (hans, simplified, hans_freq.get(simplified, 0)),
                    (hant, traditional, hant_freq.get(traditional, 0)),
                ):
                    rank = (regional + common // 4) // penalty
                    old = rows[key].get(word, (-1, False))
                    rows[key][word] = max(old, (rank, not variant))
    print(f"CC-CEDICT: {entries:,} admitted Han entries")
    return tuple(
        {
            key: sorted(values, key=lambda word: (-values[word][0], not values[word][1], word))[:64]
            for key, values in rows.items()
        }
        for rows in (hans, hant)
    )


def japanese_words(path):
    raw = gzip.decompress(path.read_bytes())
    try:
        source = raw.decode("euc_jp")
    except UnicodeDecodeError:
        source = raw.decode("euc_jis_2004")
    rows = {}
    notices = []
    for line in source.splitlines():
        if line.startswith(";;"):
            notices.append(line)
            continue
        key, space, raw_candidates = line.partition(" ")
        if not space or not re.fullmatch(r"[ぁ-ゖー]+[a-z]?", key):
            continue
        values = list(
            dict.fromkeys(
                item
                for item in raw_candidates.strip("/").split("/")
                if item and not any(ch in item for ch in "[];\t") and not item.startswith("(")
            )
        )
        if values:
            rows[key] = values[:60]
    return rows, "\n".join(notices) + "\n"


def opencc_entries(path):
    entries = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("#"):
            continue
        key, _, values = line.partition("\t")
        if values := values.split():
            entries[key] = values
    return entries


def chinese_conversion(source, direction):
    characters = opencc_entries(source / f"{direction}Characters.txt")
    phrases = opencc_entries(source / f"{direction}Phrases.txt")
    entries = characters | phrases
    longest = max(map(len, entries))

    def convert(word):
        if len(word) == 1:
            return entries.get(word, [word])
        parts = []
        start = 0
        while start < len(word):
            for end in range(min(start + longest, len(word)), start, -1):
                if choices := entries.get(word[start:end]):
                    parts.append(choices[0])
                    start = end
                    break
            else:
                parts.append(word[start])
                start += 1
        return ["".join(parts)]

    return convert


def wubi_phrase_code(word, character_codes):
    codes = [character_codes.get(character, "") for character in word]
    if not all(codes):
        return ""
    if len(codes) == 2:
        code = codes[0][:2] + codes[1][:2]
    elif len(codes) == 3:
        code = codes[0][0] + codes[1][0] + codes[2][:2]
    else:
        code = "".join(part[0] for part in (*codes[:3], codes[-1]))
    return code if len(code) == 4 else ""


def wubi_words(source):
    data = (source / "wubi86-jidian.yaml").read_text(encoding="utf-8-sig")
    _, data = data.split("\n...\n", 1)
    entries = []
    character_codes = {}
    for index, line in enumerate(data.splitlines()):
        columns = line.split("\t")
        if len(columns) < 2 or line.startswith("#"):
            continue
        word, code = columns[:2]
        if not word or not re.fullmatch(r"[a-z]{1,4}", code):
            continue
        if not all(
            unicodedata.name(character, "").startswith(("CJK UNIFIED ", "CJK COMPATIBILITY "))
            for character in word
        ):
            continue
        weight = int(columns[2]) if len(columns) > 2 and columns[2].isdecimal() else 0
        entries.append((word, code, weight, index))
        if len(word) == 1:
            rank = (len(code), weight, -index)
            if word not in character_codes or rank > character_codes[word][0]:
                character_codes[word] = (rank, code)

    full_codes = {character: code for character, (_, code) in character_codes.items()}
    to_simplified = chinese_conversion(source, "TS")
    to_traditional = chinese_conversion(source, "ST")
    simplified, traditional = defaultdict(dict), defaultdict(dict)

    def add(rows, code, word, rank):
        candidates = rows[code]
        if word not in candidates or rank > candidates[word]:
            candidates[word] = rank

    for word, code, weight, index in entries:
        for variant, simplified_word in enumerate(to_simplified(word)):
            add(simplified, code, simplified_word, (True, weight, -index, -variant))
        for variant, traditional_word in enumerate(to_traditional(word)):
            add(traditional, code, traditional_word, (True, weight, -index, -variant))
            if len(traditional_word) > 1 and traditional_word != word:
                native_code = wubi_phrase_code(traditional_word, full_codes)
                if native_code and native_code != code:
                    add(traditional, native_code, traditional_word, (False, weight, -index, -variant))

    return tuple(
        {
            code: sorted(candidates, key=candidates.__getitem__, reverse=True)[:64]
            for code, candidates in rows.items()
        }
        for rows in (simplified, traditional)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--wubi-only", action="store_true", help="regenerate only Wubi 86 tables")
    args = parser.parse_args()
    source = args.source
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    wubi_hans, wubi_hant = wubi_words(source)
    write_dictionary(out / "wubi-Hans.bin", wubi_hans)
    write_dictionary(out / "wubi-Hant.bin", wubi_hant)
    if args.wubi_only:
        return

    en_freq = read_frequencies(source / "en-frequency.txt")
    for region in ("en-US", "en-GB"):
        write_dictionary(out / f"{region}.bin", english_words(source / f"{region}.txt", en_freq))

    hans, hant = chinese_words(
        source / "zh.gz",
        read_frequencies(source / "zh-frequency.txt"),
        read_frequencies(source / "tw-frequency.txt"),
    )
    write_dictionary(out / "zh-Hans.bin", hans)
    write_dictionary(out / "zh-Hant.bin", hant)
    japanese, notices = japanese_words(source / "ja.gz")
    write_dictionary(out / "ja.bin", japanese)
    (out / "SKK-NOTICE.txt").write_text(notices, encoding="utf-8", newline="\n")
    (out / "SCOWL-Copyright.txt").write_bytes((source / "EN-Copyright").read_bytes())


if __name__ == "__main__":
    main()
