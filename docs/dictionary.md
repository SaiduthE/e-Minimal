# Dictionary

Look up words while reading an EPUB using an offline StarDict dictionary stored on the SD card.

The setup and file sections apply to every board. The controls differ: e-Minimal has five buttons (no Left/Right, no touch) and uses the flow under "Looking Up a Word". Boards with Left/Right or touch use the labelled subsection there. Key-by-key details for e-Minimal are in [eminimal-controls.md](eminimal-controls.md).

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required). On the SD card it **must be uncompressed**: a `.idx.gz` will not work there. The web Dictionaries page decompresses `.idx.gz` for you; for a manual copy, decompress it on your computer with `gzip -d` first.
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.syn` — synonym index (optional; maps alternate spellings and irregular forms to their headword)
- `.ifo` — metadata (optional)

Not supported: dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo` — rare, and rejected with an error).

## Setting Up a Dictionary

**Web Dictionaries page (fastest).** Open the Dictionaries page of the web interface (`/dictionaries`).

- Upload a `.zip` or `.tar.gz`, or the loose StarDict files together: `.idx`, `.dict` or `.dict.dz`, and optionally `.syn` and `.ifo`.
- The browser unpacks the archive and uploads the folder. `.idx.gz` and `.syn.gz` are decompressed in the browser. `.tar.bz2`, `.7z` and other formats are not unpacked by the page: repack them as `.zip` or `.tar.gz` first.
- The folder name can be up to 31 characters. A name that is already installed is refused.
- Each dictionary in the list has **Use** (or **Stop using**) and **Delete**.
- A **Where to get dictionaries** section links the sources listed at the end of this page. On the eMinimal hotspot the phone has no internet, so download first on another network.

**On the device (e-Minimal).** Open **Settings → Reader → Manage Dictionaries** and choose **Add Dictionary** (or **Add** when none are installed). Pick **Join a Network** or **Create Hotspot**, then scan the QR code with your phone or computer to open the Dictionaries page. Leaving after the radio was on restarts the device back into Settings; open Manage Dictionaries again to see what was added.

**Manual copy.** Copy your dictionary folder(s) to `/dictionaries/` on the SD card — one dictionary per folder, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder (dot-prefixed) works the same way, for keeping it out of the file browser.

**Choosing it.** Open **Settings → Reader → Dictionary** on the device and select a dictionary from the list, or **None** to disable lookups. On e-Minimal that row is **Manage Dictionaries**: Select on a dictionary makes it the default ("In use"); hold Select for Use as default, Rename, Delete and Add More. To turn lookups off without deleting, use **Stop using** on the web Dictionaries page.

If no dictionary is chosen, the first lookup picks for you: one dictionary on the card is used automatically, and several open a **Choose Dictionary** list, whose pick is saved.

The Dictionary setting only appears when at least one usable dictionary folder exists (e-Minimal's Manage Dictionaries row always appears). Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous.

## Starting a Lookup

- **Reader menu:** open it (Select on e-Minimal, Confirm elsewhere) and choose **Look Up**.
- **Hold Select (e-Minimal):** set **Settings → Controls → Hold Select Action** to **Dictionary**, or leave it on **Ask** and choose **Dictionary** from the popup. Hold Select takes about 0.4 s.
- **Long-Press Menu (boards with Left/Right or touch):** set **Settings → Controls → Long-Press Menu** to "Dictionary", then hold Confirm (~0.4s) on the reading page.

## Looking Up a Word

### e-Minimal (five buttons)

The lookup is a two-step picker over the words on the current page:

1. **Pick line.** Up/Down move between the text rows of the page. Select opens the row. Back leaves the picker and returns to the reader.
2. **Pick word.** The highlight opens on the row's longest word. Up/Down move along the row. Select looks up the highlighted word. Back returns to pick line.

Words with fewer than three letters or digits are skipped on this board, so words like "a" and "of" are never stops. Words are not skipped on the highlight picker (see [eminimal-controls.md](eminimal-controls.md)).

If the word is not found, a short popup appears and the picker stays on the same word.

### Boards with Left/Right or touch

One word on the page becomes highlighted:

1. Left/Right move between words in reading order, and the side Up/Down buttons jump between lines. On a touch screen, tap a word to look it up directly.
2. Confirm looks up the highlighted word.
3. Back returns to the reader.

### Indexing and sidecar files

On the very first lookup with a dictionary (and again whenever the `.idx` or `.syn` source file changes), the reader shows *"Indexing dictionary…"* while it builds small sidecar files next to them — a `.qidx` for the word index, and a `.sidx` when a `.syn` synonym file is present. Each sidecar is rebuilt independently, only when its own source changes. This takes a few seconds for large dictionaries and makes all subsequent lookups fast. The sidecars can be deleted safely at any time — they will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word is found as-is (case-insensitive) in the dictionary index. Surrounding punctuation is ignored.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). This step is skipped if the `.sidx` sidecar could not be built (e.g. transient low memory during indexing); the dictionary otherwise stays usable, and the build is retried the next time it is opened.
3. **Stemming** — still no match: common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
4. **Not found** — a short popup appears and you return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions.

HTML dictionaries that declare `sametypesequence=h` use the EPUB text-layout engine for semantic formatting such as headings, bold, italics, lists, and line breaks. Images and CSS styling are ignored. Definitions that are too large or cannot be laid out within the available memory fall back to plain text.

- **e-Minimal:** Up/Down turn the definition's pages. Select returns to the reader. Back returns to word selection.
- **Boards with Left/Right or touch:** Left/Right or side Up/Down turn pages (touch: tap the left third for the previous page, the rest for the next). Confirm returns to the reader. Back returns to word selection.

## Where to find dictionaries

> credit to https://github.com/koreader/koreader/wiki/Dictionary-support for the list.

- The [reader.dict](https://www.reader-dict.com) (ex "BoboTiG/ebook-reader-dict") project provides StarDict version of daily dumps of [Wiktionary](https://www.wiktionary.org/) monolingual dictionaries for a variety of languages. It also provides [non-free multilingual](https://www.reader-dict.com) dictionaries.
- The [WikDict](https://www.wikdict.com) project provides free bilingual dictionaries based on [Wiktionary](https://www.wiktionary.org/) for a lot of language pairs. StarDict versions can be [downloaded from here](https://download.wikdict.com/dictionaries/stardict/).
- The [`Vuizur/Wiktionary-Dictionaries`](https://github.com/Vuizur/Wiktionary-Dictionaries) repository contains dictionaries based on [Wiktionary](https://www.wiktionary.org/) from many languages to English, including English-English.
- The [DictInfo](https://www.dictinfo.com/) website provides outdated monolingual dictionaries based on [Wiktionary](https://www.wiktionary.org/).
- The [Firedict site](https://tuxor1337.frama.io/firedict/dictionaries.html) contains a list of freely available dictionaries.
- [wiktionary_stardict](https://xxyzz.github.io/wiktionary_stardict/): update monthly.
- [Fictionaries](https://fictionary.gumroad.com/) provides dictionaries for various speculative fiction books and series.
- [World Factbooks Archive](https://github.com/MilkMp/CIA-World-Factbooks-Archive-1990-2025) provides 36 years of CIA's World Factbook dictionaries in StarDict format.
- [StarDict-Hebrew](https://github.com/Uri-Tauber/StarDict-Hebrew) Hebrew-English StarDict versions of Babylon dictionaries.
