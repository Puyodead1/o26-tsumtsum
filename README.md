# o26-tsumtsum

Tools to run **Disney Tsum Tsum** arcade (Konami, model `O26`, 2019) on a normal Windows PC, for preservation:

- **`libacioEx.dll`**: a drop-in replacement for the cabinet's I/O library. It emulates the cabinet's I/O boards
  (coins and buttons, e-amusement card readers, the figurine pad, the capsule dispenser), turns the mouse into touch
  input, and can use a real NFC reader (ACS ACR122U with NTAG215 stickers) for login cards and figurines.
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
| e-amusement login, new player registration, PIN, play, saves | yes |
| Figurines: scan, register, ownership, level / skills | yes: keyboard (one fake figurine) or NFC tags |
| "Game and Figurine(s)" (the capsule dispenser) | emulated: each capsule is written onto the next blank NFC tag |
| English | yes (some screens only exist in Japanese) |

## Requirements

- The game data: the `O26-001` contents (`modules\`, `prop\`, `data\`, `arkdata\`), **decrypted**.
  [o26-decrypt](https://github.com/Puyodead1/o26-decrypt) does that from the cabinet's game drive and its license
  dongle.
- [spice2x](https://github.com/spice2x/spice2x.github.io) (`spice64.exe`).
- Asphyxia CORE 1.70b.
- Optional: an ACS ACR122U (or another PC/SC NFC reader) and NTAG215 stickers or cards.

## Setup

1. Build or download `libacioEx.dll` and put it in the game's `modules\` folder, replacing the original (keep a
   backup).
2. Put `spice64.exe` and `runtime\launch.bat` in the game folder.
3. Edit the props in the game's `prop\` folder:
   - `ea3-config.xml`: `<dest __type="str">J</dest>` → `Y`, for English (`J` locks the game to Japanese).
   - `mascotsetting.xml`: `<ya __type="s8">15</ya>` → `18`, so every figurine series is accepted.
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

`libacioEx.dll` uses the first NFC reader it finds (`set NFC_READER=<n>` picks another, `NFC_READER=off` turns it
off).

- A **figurine tag** is on the figurine pad while it lies on the reader. Each sticker is its own figurine with its own
  owner, like the real ones.
- A **login tag** is an e-amusement pass while it lies on the reader.
- Any other card (a blank NTAG, a transit card…) also works as a login card, with an id made from its UID.

Use `o26tag_gui.exe` to write a figurine (pick one from the list) or a login card onto a tag, see what's on a tag, or
erase it.

### Getting new figurines

Choose **Game and Figurine(s)** in game. Each capsule the cabinet would drop is a random figurine, and the next
**blank** NTAG you put on the reader gets it written (the PC beeps). Scan it when the game asks. Paid capsules wait in
`capsules.txt` in the game folder until a blank tag comes along.

A blank tag you use as a login card would get written too, so give login tags a proper record first: in
`o26tag_gui`, click "ID from tag's UID", then "Write login card". The card id stays the same.

## Building

MSVC, x64:

```
cmake -B build -A x64
cmake --build build --config Release
```

## Known issues

- On the name-entry screen the bubbles stay blank while you type. The name is still saved correctly.
- The real figurine pad uses a different tag type (ISO15693), so original Konami figurines won't read on an ACR122U.
- Events, bingo and campaigns are empty.
- Not tested on the cabinet's own PC (Windows Embedded 7).

## Disclaimer

For preservation and personal use with hardware and software you own. Not affiliated with Konami or Disney. Disney
Tsum Tsum is © Disney; the game is © Konami Amusement.
