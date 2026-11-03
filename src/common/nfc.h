/*
 * NFC tags for Disney Tsum Tsum (O26) through a PC/SC reader (tested target: ACS ACR122U + NTAG215).
 * Shared by libacioEx.dll (the game side) and o26tag.exe (the tag manager). Header-only; link winscard.lib.
 *
 * Tag record: 16 bytes in NTAG pages 4..7 (the first user pages):
 *   [0..3]  "O26F" figure  -> [4] version 1, [5] chara, [6] type, [7] variant          (tmac figure table key)
 *           "O26C" login   -> [4] version 1, [5..12] card id (8 bytes, shown as 16 hex digits)
 *   [15]    sum of bytes 0..14 (mod 256)
 * A figure's game-side UID is E0 02 26 + a hash of the tag's UID and the figure, so every sticker is its own figure.
 * Any other card (no record, or not an NTAG) works as a login card with id E004 + 6 bytes of its UID.
 */
#pragma once
#include <windows.h>
#include <winscard.h>
#include <stdint.h>
#include <string.h>

#define NFC_FIGURE 1
#define NFC_LOGIN 2

typedef struct {
    uint8_t uid[10];
    int uid_len;
    int kind;            /* 0 = blank / foreign card, NFC_FIGURE, NFC_LOGIN */
    int chara, type, variant;
    uint8_t card_id[8];  /* NFC_LOGIN: stored id; otherwise the id derived from the UID */
    uint8_t raw[16];     /* pages 4..7 as read */
    int has_raw;
} nfc_tag;

/* One APDU; returns the data length (without SW 90 00) or -1. */
static int nfc_apdu(SCARDHANDLE h, DWORD proto, const uint8_t *cmd, DWORD n, uint8_t *out, DWORD outmax) {
    uint8_t rx[300];
    DWORD rn = sizeof rx;
    const SCARD_IO_REQUEST *pci = proto == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    if (SCardTransmit(h, pci, cmd, n, NULL, rx, &rn) != SCARD_S_SUCCESS || rn < 2) return -1;
    if (rx[rn - 2] != 0x90 || rx[rn - 1] != 0x00) return -1;
    rn -= 2;
    if (out) memcpy(out, rx, rn < outmax ? rn : outmax);
    return (int)rn;
}

static uint8_t nfc_sum(const uint8_t *p, int n) {
    uint8_t s = 0;
    while (n--) s += *p++;
    return s;
}

/* Login id for a card without our record: E0 04 + the UID's last 6 bytes (7-byte NXP UIDs start with 04). */
static void nfc_derive_card_id(const nfc_tag *t, uint8_t id[8]) {
    int i, n = t->uid_len > 6 ? 6 : t->uid_len, off = t->uid_len - n;
    memset(id, 0, 8);
    id[0] = 0xE0; id[1] = 0x04;
    for (i = 0; i < n; i++) id[2 + (6 - n) + i] = t->uid[off + i];
}

/* Fill t from the card on handle h: UID (FF CA), then pages 4..7 (FF B0, NTAG READ). */
static int nfc_read_tag(SCARDHANDLE h, DWORD proto, nfc_tag *t) {
    static const uint8_t get_uid[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
    static const uint8_t read4[] = {0xFF, 0xB0, 0x00, 0x04, 0x10};
    int n;
    memset(t, 0, sizeof *t);
    n = nfc_apdu(h, proto, get_uid, sizeof get_uid, t->uid, sizeof t->uid);
    if (n <= 0) return 0;
    t->uid_len = n;
    t->has_raw = nfc_apdu(h, proto, read4, sizeof read4, t->raw, 16) == 16;
    nfc_derive_card_id(t, t->card_id);
    if (t->has_raw && t->raw[4] == 1 && nfc_sum(t->raw, 15) == t->raw[15]) {
        if (!memcmp(t->raw, "O26F", 4)) {
            t->kind = NFC_FIGURE;
            t->chara = t->raw[5]; t->type = t->raw[6]; t->variant = t->raw[7];
        } else if (!memcmp(t->raw, "O26C", 4)) {
            t->kind = NFC_LOGIN;
            memcpy(t->card_id, t->raw + 5, 8);
        }
    }
    return 1;
}

/* Write 16 bytes to pages 4..7 (FF D6 = NTAG WRITE, one 4-byte page per APDU), then read back. */
static int nfc_write_record(SCARDHANDLE h, DWORD proto, const uint8_t rec[16]) {
    uint8_t cmd[9] = {0xFF, 0xD6, 0x00, 0, 4}, back[16];
    static const uint8_t read4[] = {0xFF, 0xB0, 0x00, 0x04, 0x10};
    int page;
    for (page = 0; page < 4; page++) {
        cmd[3] = (uint8_t)(4 + page);
        memcpy(cmd + 5, rec + page * 4, 4);
        if (nfc_apdu(h, proto, cmd, sizeof cmd, NULL, 0) < 0) return 0;
    }
    return nfc_apdu(h, proto, read4, sizeof read4, back, 16) == 16 && !memcmp(back, rec, 16);
}

static void nfc_figure_record(uint8_t rec[16], int chara, int type, int variant) {
    memset(rec, 0, 16);
    memcpy(rec, "O26F", 4);
    rec[4] = 1; rec[5] = (uint8_t)chara; rec[6] = (uint8_t)type; rec[7] = (uint8_t)variant;
    rec[15] = nfc_sum(rec, 15);
}

static void nfc_login_record(uint8_t rec[16], const uint8_t id[8]) {
    memset(rec, 0, 16);
    memcpy(rec, "O26C", 4);
    rec[4] = 1;
    memcpy(rec + 5, id, 8);
    rec[15] = nfc_sum(rec, 15);
}

/* The game-side figure UID (funique_id) for a figure tag: E0 02 26 (like the real figurines' ISO15693 tags) + 5 bytes
   of a hash of the tag's UID and the figure on it, so a sticker rewritten as another figure is a new figure (not one
   still registered to whoever owned the old one), while the same figure on the same sticker keeps its id.
   Not E0 04..: Asphyxia CORE takes any E004 id for an e-amusement card number and rewrites it, so the game's
   figure.check got a different funique_id back and said "scan failed". */
static void nfc_figure_uid(const nfc_tag *t, uint8_t uid[8]) {
    uint64_t h = 0xcbf29ce484222325ull;  /* FNV-1a */
    uint8_t fig[3];
    int i;
    fig[0] = (uint8_t)t->chara; fig[1] = (uint8_t)t->type; fig[2] = (uint8_t)t->variant;
    for (i = 0; i < t->uid_len; i++) h = (h ^ t->uid[i]) * 0x100000001b3ull;
    for (i = 0; i < 3; i++) h = (h ^ fig[i]) * 0x100000001b3ull;
    uid[0] = 0xE0; uid[1] = 0x02; uid[2] = 0x26;
    for (i = 3; i < 8; i++, h >>= 8) uid[i] = (uint8_t)h;
}
