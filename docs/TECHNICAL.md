# Technical notes

Addresses are virtual addresses in the game's `tmac.dll` / `arko26.dll` (image base `0x180000000`).

## Layout

| Path | |
|---|---|
| `src/acioemu/` | `libacioEx.dll` |
| `src/o26tag/` | `o26tag.exe`, `o26tag_gui.exe` |
| `src/common/` | `nfc.h` (tag format, PC/SC), `figures.h` + generated `figure_table.h` (figurine table) |
| `scripts/gen_figure_table.py` | regenerates `figure_table.h`: `python scripts/gen_figure_table.py <tmac.dll>` (needs `pefile`) |
| `server/plugins/o26@asphyxia/` | Asphyxia CORE plugin |
| `runtime/launch.bat` | spice2x launcher |

## libacioEx.dll

Replaces the cabinet's ACIO library; `tmac.dll` imports 47 functions from it. Contracts come from tmac's
`ioplugin.cpp` state machines (`AcioInitializeStep` sub_180180210, `AcioUpdate` sub_1801806B0,
`AcioUpdateICCCRead` sub_180180A70).

| Board | Emulation |
|---|---|
| BMPU (node 23) | coins and buttons. Status byte 7: `0x10` TEST, `0x02` SERVICE |
| ICCC (devs 30, 31) | card readers: the e-amusement path (24-byte status, `[0]=2` card, `[2..9]` UID) and the figurine path "26" |
| O26A | capsule dispenser |

`arko26.dll` also polls the keyboard itself (sub_18003D3C0, VK table at `0x1800CB790`: F1–F8, F10, Space, arrows,
N, R, M), so the DLL's own keys avoid those.

### Touch

tmac doesn't use serial ports for touch: it checks `GetSystemMetrics(SM_DIGITIZER)`, calls `RegisterTouchWindow` and
reads `WM_TOUCH` (sub_180177CD0; x/100, y/100 in 1080×1920 space). The DLL patches tmac's imports:
`GetSystemMetrics` reports a digitizer, `RegisterTouchWindow` subclasses the window, and left-button mouse messages
become `WM_TOUCH` with a fake handle answered by `GetTouchInputInfo` / `CloseTouchInputHandle`.

tmac also hides the cursor; the DLL hooks its `ShowCursor` and sets an arrow on `WM_SETCURSOR`.

### Figurine pad

The figurine is an ISO15693 tag (UID `E0…`, IC type 38); tmac reads 8 blocks. Acceptance (sub_1801818C0), on the
first 16 data bytes:

- `data[8..15] == MD5(key64 @0x18024E6E0 ‖ uid ‖ data[0..7])[:8]`
- `data[1..7] ^=` a 16-bit LFSR keystream seeded with `uid[7] | (uid[6] | 0x80) << 8`, each byte `^ 0x57`
- CRC-8 (table `@0x1801E0870`) over the unscrambled `data[0..7]` must be 0
- unscrambled `data[3]`, `data[4]`, `data[5] >> 4` = chara, type, variant: the key into the figurine table
  (463 × 144 bytes `@0x1802B8F40`)

The DLL builds these bytes itself (MD5 key and CRC table read from the loaded `tmac.dll`), so tags only carry the
figurine's chara/type/variant. A tag is reported once per `reset_to_ready`: a tag that keeps answering sends tmac
straight back to `ucgetuid` and starves the e-amusement path.

### Capsule dispenser

`ac_io_o26a_pay_vendor_request()` drops one capsule and returns a sequence number. IoSyncPayCapsule (sub_1801800A0)
waits until vendor status `[0..1]` (big-endian) equals that number and `[2]` is 0 (`1` / `7` busy, anything else an
error). The dispense scene (sub_180131690) first waits up to 90 s for `[2] = 1` (dispensing), so the DLL reports 1
for 3 s, then 0. `[4..13]` would carry the capsule's 8-byte id, which the game only logs.

Each pay queues a random figurine in `capsules.txt`; the NFC thread writes it onto the next blank NTAG.

### Fonts

The game asks for its fonts (`data/font/*.ttc`) by their Japanese family names (e.g. `Rgシリウス-B`, Shift-JIS).
Windows only matches localized names on a Japanese system, so the DLL hooks tmac's `CreateFontA`,
`CreateFontIndirectA/W` and `D3DXCreateFontA` and swaps in the English family names.

## NFC tags

PC/SC APDUs (ACR122U pass-through): `FF CA 00 00 00` (UID), `FF B0 00 04 10` (read pages 4–7), `FF D6 00 <page> 04`
(write one page).

Record in NTAG pages 4–7 (16 bytes), byte 15 = sum of bytes 0–14:

| Bytes | Figurine | Login card |
|---|---|---|
| 0–3 | `"O26F"` | `"O26C"` |
| 4 | version 1 | version 1 |
| 5–7 / 5–12 | chara, type, variant | 8-byte card id |

- Figurine `funique_id` (the game-side UID): `E0 02 26` + 5 bytes of FNV-1a over the tag UID and chara/type/variant.
  A rewritten sticker is a new figurine. It must not start `E004`: Asphyxia CORE treats any `E004…` id as an
  e-amusement card and rewrites it to `refid|dataid`, which breaks `figure.check`.
- Card id of a tag without a record: `E004` + the last 6 bytes of its UID.

## Server

Implemented calls: `system.convcardnumber`, `eventlog.write`, `common.get` / `campaign` / `anydata`, `shop.save` /
`update`, `ranking.get`, `report.report`, `figure.check` / `attach` / `unattach`, `pc.reg` / `get` / `save` /
`logout`. Each handler documents its response layout. tmac's parsers read fields with `property_node_refer` and fail
the whole call if a required field is missing.

`common.get` `message` entries: `{tag str, ver s16, message1 str, message2 str, from_time u64, till_time u64}`, all
required. The game reads `KEY=a,b;KEY2=…` settings from `message1` / `message2` of a message with a given tag and
`ver` 0, valid while `from_time <= now <= till_time` or both are 0. Tag `MASCOT` (sub_180014180):

- `RELEASE=` figurine series (Japanese numbering) that may be scanned. Without it only series 1 works, and the rest
  say "cannot be used with the current game version".
- `ZERO_RELEASE=` tsum ids allowed from series 0 (Party Mickey = 97).

`figure.check` status: 0 = yours, 1 = unregistered (the game offers to register, then sends `figure.attach`),
2 = someone else's (the name shown comes back in `name`).

Plugin gotchas:

- CORE compiles plugins as ES2017 without Node typings: no `BigInt` global (see `handlers/util.ts`).
- Update profiles with `$set` under the refid. Replacing the document drops `__refid`, and CORE deletes it.
- `asphyxia-core-x64.exe --dev` plus the plugin's "Log requests" option prints every request.

## Region

`<dest>` in `ea3-config.xml` (arko26 sub_180032340) sets the language range: `J` Japanese only, `U` English only,
`A` / `Y` English or a second language (`A` starts in the second language, `Y` in English).

Figurine series: tmac has 20 lists of tsum ids at `0x18021B7F0` (list n = Japanese series n) and a per-region
mapping at `0x1802D6180`. Japanese series 3–5 don't exist in `A` / `Y`; series 6–19 become 3–16 there.
`mascotsetting.xml` caps the series per region (`ja` 18, `ua` 16, `aa` / `ya` 15 by default).
