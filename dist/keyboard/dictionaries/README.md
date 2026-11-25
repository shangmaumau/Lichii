<!--
SPDX-FileCopyrightText: Copyright 2026 Lichii Project
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Offline keyboard dictionaries

The on-screen keyboard candidate tables use the sources below. The tables are
read locally when the corresponding language is selected and carry no saved
input or per-user learning data.

- **US and UK English** use the official ESDB (SCOWL) size-60 `en_US.txt`
  and `en_GB-ise.txt` wordlists, release `2026.02.25`.
  Copyright 2000–2026 Kevin Atkinson. See [SCOWL-Copyright.txt](SCOWL-Copyright.txt)
  for the complete upstream notices. Lichii deduplicates case variants and
  indexes entries for prefix completion.
  [English Speller Database](https://wordlist.aspell.net/dicts/) ·
  [Published wordlists](https://github.com/en-wl/wordlist-diff/tree/rel-2026.02.25)
- **Simplified and Traditional Chinese** use CC-CEDICT published by MDBG
  on 2026-11-24 (125,474 source entries; derived from CEDICT, copyright
  1997–1998 Paul Denisowski). Lichii indexes the Mandarin readings, retains the
  two written forms separately, and omits definitions and non-Han entries.
  The tables are adaptations under CC BY-SA 4.0.
  [CC-CEDICT download and attribution](https://www.mdbg.net/chinese/dictionary?page=cc-cedict)
- **Wubi 86** uses the main Jidian Wubi table maintained by Kyle Bing.
  Lichii retains the upstream short codes, filters the table to Han entries,
  and sorts candidates by upstream weight and row order. Simplified and
  Traditional candidates use the OpenCC character and phrase dictionaries
  by Carbo Kuo and contributors. Traditional input also accepts codes for
  Traditional characters and phrases where the component codes are available.
  The generated tables include about 62,600 distinct multi-character entries
  per script. See [the Jidian MIT license](WUBI-MIT.txt) and the
  [OpenCC Apache 2.0 license](/LICENSES/Apache-2.0.txt), also included as
  `Apache-2.0.txt` alongside this file in standalone packages.
  [Jidian Wubi table](https://github.com/KyleBing/rime-wubi86-jidian/blob/4d08110b8080f6577dad733ada7e5ae5c5e878fd/wubi86_jidian.dict.yaml) ·
  [OpenCC dictionaries](https://github.com/BYVoid/OpenCC/tree/fae2a0a911c30764e669a7d8b0591cd8e852934a/data/dictionary)
- **Candidate frequency** for English and both Chinese forms uses
  FrequencyWords `2018` English, Mainland Chinese and Taiwan Chinese
  50k-word counts, compiled by Hermit Dave from the OpenSubtitles corpus.
  The rank columns and rank-ordered adaptations are licensed under CC BY-SA 4.0.
  [FrequencyWords source and license](https://github.com/hermitdave/FrequencyWords)
- **Japanese** uses `SKK-JISYO.L.unannotated` from the SKK Development Team and
  contributors. Lichii converts the input from EUC-JP to UTF-8, omits
  non-reading keys and rules, and indexes both bare readings and okuri stems
  under the original GPL-2.0-or-later. The upstream gzip and
  [SKK-NOTICE.txt](SKK-NOTICE.txt) provide the source and full copyright notice.
  [SKK official dictionaries](https://skk-dev.github.io/dict/)

See [CC-BY-SA-4.0.txt](CC-BY-SA-4.0.txt) for the full Creative Commons license.
The GNU GPL copy is [GPL-2.0-or-later.txt](/LICENSES/GPL-2.0-or-later.txt)
in the Lichii source tree and `GPL-2.0-or-later.txt` alongside this file in
standalone packages.

## Regenerating the tables

Place the inputs under a source directory using the local names below. Reuse
the checked hashes to reproduce this snapshot. For a planned dictionary
update, record the new upstream revisions and hashes before replacing the
generated binary resources.

| Local name | Upstream path | SHA-256 |
| --- | --- | --- |
| `en-US.txt` | `en_US.txt` at wordlist-diff `rel-2026.02.25` | `4ff7e0b6d86763e1e042ffd746e94cdf4432618702deac303a1669e2a838db04` |
| `en-GB.txt` | `en_GB-ise.txt` at wordlist-diff `rel-2026.02.25` | `82b22843c9958a1efac291e0ea7dbef22ce227dd4e5ca0fd84043cbc62e0a4f1` |
| `EN-Copyright` | `Copyright` at wordlist-diff `rel-2026.02.25` | `090575a131b4260926c7a6b30a90aca0f5db5fbb5c46778e0c5855227bf6ebc3` |
| `en-frequency.txt` | FrequencyWords `content/2018/en/en_50k.txt` | `5351ff405b1126ef555791dd4d9798a48e3e9a501a9fc481a9da957752cfb458` |
| `zh-frequency.txt` | FrequencyWords `content/2018/zh_cn/zh_cn_50k.txt` | `25599e00347b893e55c3058a748819b3fe19403291cd12f7c03af128e7f3fe45` |
| `tw-frequency.txt` | FrequencyWords `content/2018/zh_tw/zh_tw_50k.txt` | `b3bfa426ebd05225b1963cd557ce844d52912c5534c08b40ebeaec8768f36034` |
| `zh.gz` | `cedict_1_0_ts_utf-8_mdbg.txt.gz` dated 2026-11-24 | `9594e10b88f5dde3003133f20f216ff9ae66e9f4441ee94f13b56d14e4043118` |
| `ja.gz` | `SKK-JISYO.L.unannotated.gz` bundled here | `f88b8ed15856e47b5874cd06d5b2d3a7cd841f608b4afa4d24936534e5d159c7` |
| `wubi86-jidian.yaml` | Jidian `wubi86_jidian.dict.yaml` at `4d08110b8080f6577dad733ada7e5ae5c5e878fd` | `89df3685e4a0c3d319a1c56a3a64e3df4ee3bfa3ba3892a6dadc77b21c989c5b` |
| `STCharacters.txt` | OpenCC `data/dictionary/STCharacters.txt` at `fae2a0a911c30764e669a7d8b0591cd8e852934a` | `a0ca1601c70648cf48b33c3c6210ccbecc5c7eead4b4c3daf76587ba2c03582b` |
| `STPhrases.txt` | OpenCC `data/dictionary/STPhrases.txt` at the same revision | `24d23700f4f3d1860b899aa90896dcc7ec56c535a0b3a246bbf5a7048c7bdeb8` |
| `TSCharacters.txt` | OpenCC `data/dictionary/TSCharacters.txt` at the same revision | `9ff46a7d30e5765375eb13d33f2b03a34d298913caf2b120380679f33ae1642d` |
| `TSPhrases.txt` | OpenCC `data/dictionary/TSPhrases.txt` at the same revision | `1b1e60a3d50492bc088bcd4372152570b5f0ccbe2ded0b66c90b6b8eeb5f849f` |

```sh
python tools/ui/build_keyboard_dictionaries.py <source-directory> dist/keyboard/dictionaries
```

Use `--wubi-only` to regenerate the Wubi tables with just the five Wubi and
OpenCC source files above.

The format is `LKD1`, a little-endian 32-bit row count, then one little-endian
32-bit byte offset for each row plus the final end offset. The indexed bytes
are UTF-8 with a tab between the key and each value. Rows are bytewise sorted
by key. English rows hold one word and its frequency count. Chinese and
Japanese rows hold candidates in selection order.
