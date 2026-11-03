# o26-tsumtsum

Tools to run **Disney Tsum Tsum** arcade (Konami, model `O26`, 2019) on a normal Windows PC, for preservation:

- **`libacioEx.dll`**: a drop-in replacement for the cabinet's I/O library. It emulates the ACIO boards (coin/buttons,
  e-amusement card readers, the mascot figure pad, the capsule vendor), turns the mouse into touch input, and optionally
  uses a real NFC reader (ACS ACR122U + NTAG215 stickers) for login cards and mascot figurines.
- **`o26tag_gui.exe` / `o26tag.exe`**: write, read and erase figurine and login-card tags.
- **`o26@asphyxia`**: an [Asphyxia CORE](https://asphyxia-core.github.io/) plugin, the e-amusement server for card
  login, registration, profiles, saves, figurine ownership and rankings.

**No game files are included.** You need your own copy of the game data from a cabinet you own.

## What works

| | |
|---|---|
| Boot to attract / title | yes (with spice2x) |
| Touch | mouse (left button) as the touchscreen; real touchscreens pass through |
| Coins, TEST / SERVICE | keyboard |
| e-amusement login, new player registration, PIN, play, saves | yes (Asphyxia CORE + plugin) |
| Mascot figurines: scan, register, ownership, level / skills | yes: keyboard (one fake figure) or NFC tags |
| "Game and Figurine(s)": the capsule vendor | emulated: each capsule is written onto the next blank NFC tag |
| English | yes (region `Y`; some screens only exist in Japanese) |

## Requirements

- The game data: the `O26-001` contents (`modules\`, `prop\`, `data\`, `arkdata\`), **decrypted**.
  [o26-decrypt](https://github.com/Puyodead1/o26-decrypt) does that from the cabinet's game drive and its license
  dongle.
- [spice2x](https://github.com/spice2x/spice2x.github.io) (`spice64.exe`) in the game folder.
- Asphyxia CORE 1.70b.
- Optional: an ACS ACR122U (or any PC/SC NFC reader that passes through the `FF CA` / `FF B0` / `FF D6` APDUs) and
  NTAG215 stickers or cards.

## Setup

1. Build or download `libacioEx.dll` (see "Building") and put it in the game's `modules\` folder, replacing the
   original (keep a backup).
2. Put `spice64.exe` and `runtime\launch.bat` in the game folder.
3. Edit the props in the game's `prop\` folder:
   - `ea3-config.xml`: `<dest __type="str">J</dest>` → `Y` (English; see "Region").
   - `mascotsetting.xml`: `<ya __type="s8">15</ya>` → `18` (so the English region accepts every figurine series).
4. Copy `server\plugins\o26@asphyxia` into CORE's `plugins\` folder and start `asphyxia-core-x64.exe`.
5. Run `launch.bat`. It starts windowed at 540×960. For a portrait 1080×1920 monitor, drop `-w -windowsize -windowpos`.

The first boot may show "game was not terminated normally". Press **F1** to clear it.

### Controls

| Key | |
|---|---|
| Mouse (left button) | touch |
| F9 | insert coin |
| F1 / F2 | TEST / SERVICE (operator menu) |
| F11 | e-amusement card (its id is in `card0.txt`, made up on first use) |
| F12 | put the fake figurine on the pad / take it off (`figure0.txt`: `chara type variant`, default `1 1 1` = Mickey 001) |

## NFC: login cards and figurines

`libacioEx.dll` watches the first PC/SC reader (`set NFC_READER=<n>` picks another, `NFC_READER=off` disables it).

- A **figurine tag** is on the figure pad while it lies on the reader. Each sticker is its own figurine with its own
  owner, like the real ones.
- A **login tag** is an e-amusement pass while it lies on the reader.
- Any other card (a blank NTAG, a transit card…) also works as a login card, with an id made from its UID.

Tags carry a 16-byte record in NTAG pages 4..7 (`src/common/nfc.h`): `"O26F"` + chara/type/variant, or `"O26C"` +
an 8-byte card id. `o26tag_gui.exe` writes and reads them: pick a figurine from the list (filter by name or series,
"English mode" shows whether the English region accepts it), or write a login card, or erase a tag.

### Getting new figurines: the capsule vendor

Choose **Game and Figurine(s)** in game. Each capsule the cabinet would drop is a random figurine (Rare ones less
often), queued in `capsules.txt` in the game folder. **The next blank NTAG put on the reader is written with it**, and
the PC beeps. Scan it when the game asks.

Because a blank tag is fair game for a waiting capsule, give your login tags a real record: in `o26tag_gui`, use
"ID from tag's UID", then "Write login card". The id stays the same.

## Server (Asphyxia plugin)

Implemented calls: `system.convcardnumber`, `eventlog.write`, `common.get` / `campaign` / `anydata`, `shop.save` /
`update`, `ranking.get`, `report.report`, `figure.check` / `attach` / `unattach`, `pc.reg` / `get` / `save` /
`logout`. The response layouts are documented in the handlers.

`common.get` also sends the `MASCOT` setting `RELEASE=1..19`. Without it the game only accepts figurines from series 1,
and everything else says "cannot be used with the current game version".

Run CORE with `--dev` and turn on the plugin's "Log requests" option to see every request.

## Region

The language option in the operator menu is locked by region (`<dest>` in `ea3-config.xml`): `J` = Japanese only,
`U` = English only, `A` / `Y` = English or a second language (`Y` starts in English). The region also decides which
figurine series exist (Japanese series 3–5 don't exist outside Japan) and caps the series via `mascotsetting.xml`.

## Building

CMake project, MSVC x64 (static CRT, so nothing extra is needed on the target PC):

```
cmake -B build -A x64
cmake --build build --config Release
```

`build\Release\` then has `libacioEx.dll`, `o26tag.exe` and `o26tag_gui.exe`. Visual Studio can also open the folder
directly (File → Open → Folder).

| Path | |
|---|---|
| `src/acioemu/` | `libacioEx.dll` |
| `src/o26tag/` | `o26tag.exe` and `o26tag_gui.exe` |
| `src/common/` | shared headers: the NFC tag format (`nfc.h`), the figurine table (`figures.h`, `figure_table.h`) |
| `scripts/gen_figure_table.py` | regenerates `figure_table.h` from `tmac.dll` (`python scripts/gen_figure_table.py <tmac.dll>`, needs `pefile`) |
| `server/plugins/o26@asphyxia/` | the Asphyxia CORE plugin |
| `runtime/launch.bat` | the spice2x launcher for the game folder |

GitHub Actions (`.github/workflows/build.yml`) builds everything on every push, and the run's artifact has the
binaries. Pushing a tag like `v1.0` publishes a release zip with the DLL, the tag tools, `launch.bat`, the plugin and
this README.

## Known issues

- On the name-entry screen the bubbles stay blank while you type. The name is still saved correctly.
- The real figure pad is an ISO15693 reader. The NFC support uses NTAG instead, so original Konami figurines won't
  read on an ACR122U.
- Events, bingo and campaigns are empty.
- Not tested on the cabinet's own PC (Windows Embedded 7).

## Disclaimer

For preservation and personal use with hardware and software you own. Not affiliated with Konami or Disney. Disney
Tsum Tsum is © Disney; the game is © Konami Amusement.
