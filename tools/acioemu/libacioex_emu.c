/*
 * libacioEx.dll replacement for Disney Tsum Tsum (O26) -- emulates the cabinet I/O so tmac.dll boots
 * without the ACIO boards. Drop in as modules\libacioEx.dll (keep the original as libacioex_orig.dll).
 *
 * tmac.dll imports 47 functions; their contracts come from tmac's ioplugin.cpp state machines
 * (AcioInitializeStep sub_180180210, AcioUpdate sub_1801806B0, AcioUpdateICCCRead sub_180180A70)
 * and the original libacioex.dll:
 *   BMPU  (node 23)     coins/buttons; 56-byte status buffer (u32 seq, 16 raw bytes, decoded words)
 *   ICCC  (devs 30..35) e-amusement card readers; *_isfinished: 0 = done, 1 = busy, -1 = error
 *   O26A                capsule vendor; *_is_finish: 0 = busy, >0 = done, <0 = error (dispenses onto blank NFC tags)
 *
 * Keys: F1 = TEST, F2 = SERVICE, F9 = coin, F11 = e-amusement card (card0.txt), F12 = mascot figure on/off
 * (figure0.txt), left mouse button = touch (see below). An NFC reader (ACR122U + NTAG215, see nfc.h / o26tag.exe)
 * works too: figure tags go on the figure pad, other cards log in, both while they're on the reader.
 * Keys are read globally (GetAsyncKeyState), so they work whichever window has focus.
 * arko26 polls the keyboard itself too (sub_18003D3C0, VK table at 0x1800CB790): F1 TEST, F2 SERVICE, F3..F8, F10,
 * Space, arrows, N, R, M. TEST/SERVICE here use the same keys; coin and card use keys arko26 leaves alone.
 * Writes libacioex_emu.log next to the DLL with the first call of every function.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nfc.h"
#include "figures.h"

#define EXPORT __declspec(dllexport)

static FILE *LOG;
static int COINS;
static uint32_t SEQ;

static void trace(const char *fn, int *seen) {
    if (*seen) return;
    *seen = 1;
    if (LOG) { fprintf(LOG, "%s\n", fn); fflush(LOG); }
}
#define TRACE() do { static int seen; trace(__func__, &seen); } while (0)

static uint8_t BUTTONS;  /* raw BMPU byte 3 (buffer offset 7) */
static DWORD CARD_UNTIL; /* tick count until which the e-amusement card sits on the reader */
static uint8_t CARD_UID[8];

/* card0.txt in the game folder: 16 hex digits (E004...). Made up on first use so the card stays the same. */
static void load_card(void) {
    char hex[17] = {0};
    FILE *f = fopen("card0.txt", "r");
    int i, ok = f && fscanf(f, "%16[0-9A-Fa-f]", hex) == 1 && strlen(hex) == 16;
    if (f) fclose(f);
    if (!ok) {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        sprintf(hex, "E004%012llX", (unsigned long long)(t.QuadPart * 2654435761ull) & 0xffffffffffffull);
        if ((f = fopen("card0.txt", "w"))) { fprintf(f, "%s\n", hex); fclose(f); }
    }
    for (i = 0; i < 8; i++) sscanf(hex + 2 * i, "%2hhx", &CARD_UID[i]);
    if (LOG) { fprintf(LOG, "card: %s\n", hex); fflush(LOG); }
}

/*
 * Mascot figure on the pad (reader path "26"): an ISO15693 tag, UID E0.., IC type 38, 8 blocks read. tmac
 * (sub_1801818C0) accepts data[0..15] when data[8..15] = MD5(key64 || uid || data[0..7])[:8], and data[1..7]
 * unscrambled with an LFSR seeded from uid[6..7] has CRC-8 0. Plain data[3], data[4], data[5] >> 4 pick the figure
 * from tmac's table at 0x1802B8F40 (character, type, variant). figure0.txt: "chara type variant", default Mickey 001.
 * The MD5 key and CRC table are read from the loaded tmac.dll. See work/tools/o26figure.py for the same in Python.
 */
static int FIGURE_ON, FIGURE_QUIET;  /* quiet: already reported since the last reset_to_ready (ISO15693 "stay quiet") */
static uint8_t FIGURE_UID[8], FIGURE_DATA[28];
static CRITICAL_SECTION FIG_CS;      /* the NFC thread sets figures/cards, tmac's I/O thread reads them */

/* Build FIGURE_UID / FIGURE_DATA for a tag UID (must start E0) and a figure-table key. */
static int set_figure(const uint8_t uid[8], int chara, int type, int variant) {
    uint8_t *tmac = (uint8_t *)GetModuleHandleA("tmac.dll"), p[8] = {0}, crc = 0, hash[16], buf[80];
    const uint8_t *crc8, *key;
    int i;
    unsigned v;
    DWORD len = 16;
    HCRYPTPROV prov;
    HCRYPTHASH h;
    if (!tmac) return 0;
    key = tmac + 0x24E6E0;
    crc8 = tmac + 0x1E0870;
    memcpy(FIGURE_UID, uid, 8);
    p[3] = (uint8_t)chara; p[4] = (uint8_t)type; p[5] = (uint8_t)(variant << 4);
    for (i = 0; i < 7; i++) crc = crc8[crc ^ p[i]];
    p[7] = crc;
    memset(FIGURE_DATA, 0, sizeof FIGURE_DATA);
    FIGURE_DATA[0] = p[0];
    v = FIGURE_UID[7] | ((FIGURE_UID[6] | 0x80u) << 8);
    for (i = 1; i < 8; i++) {  /* same 16-bit LFSR as tmac */
        unsigned a = (v ^ (v >> 2)) & 0xffff, b = (v ^ (a >> 1)) & 0xffff, c = (v ^ (b >> 2)) & 0xffff;
        v = ((v << 1) | ((c >> 10) & 1)) & 0xffff;
        FIGURE_DATA[i] = p[i] ^ (uint8_t)v ^ 0x57;
    }
    memcpy(buf, key, 64); memcpy(buf + 64, FIGURE_UID, 8); memcpy(buf + 72, FIGURE_DATA, 8);
    if (!CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return 0;
    if (CryptCreateHash(prov, CALG_MD5, 0, 0, &h)) {
        if (CryptHashData(h, buf, 80, 0) && CryptGetHashParam(h, HP_HASHVAL, hash, &len, 0)) memcpy(FIGURE_DATA + 8, hash, 8);
        CryptDestroyHash(h);
    }
    CryptReleaseContext(prov, 0);
    if (LOG) {
        fprintf(LOG, "figure: chara %d type %d variant %d placed, uid %02X%02X%02X%02X%02X%02X%02X%02X\n", chara, type, variant,
                uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);
        fflush(LOG);
    }
    return 1;
}

/* F12 figure: figure0.txt "chara type variant" (default Mickey 001), UID made up from it. */
static int make_figure(void) {
    uint8_t uid[8] = {0xE0, 0x02, 0x26, 0, 0, 0, 0, 0};
    int chara = 1, type = 1, variant = 1;
    FILE *f = fopen("figure0.txt", "r");
    if (f) { if (fscanf(f, "%d %d %d", &chara, &type, &variant) != 3) chara = type = variant = 1; fclose(f); }
    uid[6] = (uint8_t)chara; uid[7] = (uint8_t)type;
    return set_figure(uid, chara, type, variant);
}

/*
 * O26A capsule vendor. The real one drops a random capsule per ac_io_o26a_pay_vendor_request; IoSyncPayCapsule
 * (tmac sub_1801800A0) waits until vendor status[0..1] (big-endian) equals that call's return value and status[2]
 * is 0 (1 / 7 = busy, anything else = error), and the vendor scene first wants to see 1 (dispensing; it times out
 * after 90 s otherwise). Here a pay reports 1 for 3 s, then 0, and queues a random figurine (one that
 * works in the English region too; Rare ones 1/10 as likely) in capsules.txt in the game folder ("chara type
 * variant  name" per line, so paid capsules survive a restart). The next blank NTAG put on the NFC reader is written
 * with it, like the capsule you then scan.
 */
static uint16_t VEND_SEQ;
static DWORD VEND_BUSY_UNTIL;  /* the vendor scene (tmac sub_180131690) waits for status[2] = 1 (dispensing) before 0 */

static const figure *random_figure(void) {
    int i, total = 0, pick;
    for (i = 0; i < NFIG; i++)
        if (FIG[i].series_en && FIG[i].name[0]) total += FIG[i].type == 2 ? 1 : 10;
    pick = (int)(((unsigned)rand() << 15 | (unsigned)rand()) % (unsigned)total);
    for (i = 0; i < NFIG; i++) {
        int w = FIG[i].series_en && FIG[i].name[0] ? (FIG[i].type == 2 ? 1 : 10) : 0;
        if (pick < w) return &FIG[i];
        pick -= w;
    }
    return &FIG[0];
}

static void capsule_add(int chara, int type, int variant) {
    const figure *f = find_figure(chara, type, variant);
    FILE *out = fopen("capsules.txt", "a");
    if (out) { fprintf(out, "%d %d %d  %s\n", chara, type, variant, f ? figure_name(f) : "?"); fclose(out); }
}

/* Take the first queued capsule (caller holds FIG_CS); returns how many there were. */
static int capsule_take(int *chara, int *type, int *variant) {
    char line[256], rest[64][256];
    int n = 0, i;
    FILE *in = fopen("capsules.txt", "r"), *out;
    if (!in) return 0;
    while (n < 64 && fgets(line, sizeof line, in))
        if (n || sscanf(line, "%d %d %d", chara, type, variant) == 3) strcpy(rest[n++], line);
    fclose(in);
    if (!n) return 0;
    if ((out = fopen("capsules.txt", "w"))) {
        for (i = 1; i < n; i++) fputs(rest[i], out);
        fclose(out);
    }
    return n;
}

EXPORT int ac_io_o26a_pay_vendor_request(void) {
    const figure *f = random_figure();
    TRACE();
    EnterCriticalSection(&FIG_CS);
    capsule_add(f->chara, f->type, f->variant);
    LeaveCriticalSection(&FIG_CS);
    if (LOG) { fprintf(LOG, "vendor: capsule %s (%d %d %d) - put a blank tag on the reader\n", figure_name(f), f->chara, f->type, f->variant); fflush(LOG); }
    VEND_BUSY_UNTIL = GetTickCount() + 3000;
    return ++VEND_SEQ;
}

EXPORT uint8_t ac_io_o26a_get_vendor_status(uint8_t *st) {
    TRACE();
    if (!st) return 0;
    memset(st, 0, 18);
    st[0] = (uint8_t)(VEND_SEQ >> 8); st[1] = (uint8_t)VEND_SEQ;
    st[2] = (int)(VEND_BUSY_UNTIL - GetTickCount()) > 0;  /* 1 = dispensing for 3 s after a pay, then 0 = done */
    return 1;
}

/*
 * NFC reader (PC/SC, e.g. ACR122U with NTAG215 stickers; tag format and o26tag.exe in nfc.h / o26tag.c). A figure
 * tag sits on the figure pad while it's on the reader; anything else is a login card while it's on the reader,
 * except a blank NTAG while a capsule is waiting (see above), which becomes that figure.
 * Uses the first reader; NFC_READER=<n> in the environment picks another, NFC_READER=off disables it.
 */
static int CARD_FROM_NFC, FIGURE_FROM_NFC;

static DWORD WINAPI nfc_thread(LPVOID unused) {
    const char *env = getenv("NFC_READER");
    int index = env ? atoi(env) : 0;
    char readers[2048], *reader = NULL, *r;
    SCARD_READERSTATEA st;
    SCARDCONTEXT ctx;
    uint8_t last_uid[10];
    int last_len = 0, i, logged = 0;
    for (;;) {
        DWORD n = sizeof readers;
        if (SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &ctx) != SCARD_S_SUCCESS) { Sleep(5000); continue; }
        if (SCardListReadersA(ctx, NULL, readers, &n) == SCARD_S_SUCCESS)
            for (r = readers, i = 0; *r; r += strlen(r) + 1, i++)
                if (i == index) { reader = r; break; }
        if (!reader) { SCardReleaseContext(ctx); Sleep(3000); continue; }
        if (LOG && !logged++) { fprintf(LOG, "nfc: using %s\n", reader); fflush(LOG); }
        memset(&st, 0, sizeof st);
        st.szReader = reader;
        st.dwCurrentState = SCARD_STATE_UNAWARE;
        for (;;) {
            LONG rc = SCardGetStatusChangeA(ctx, 500, &st, 1);
            if (rc != SCARD_S_SUCCESS && rc != SCARD_E_TIMEOUT) break;  /* reader unplugged: start over */
            st.dwCurrentState = st.dwEventState & ~SCARD_STATE_CHANGED;
            if (!(st.dwEventState & SCARD_STATE_PRESENT)) {
                if (last_len && LOG) { fprintf(LOG, "nfc: tag removed\n"); fflush(LOG); }
                last_len = 0;
                EnterCriticalSection(&FIG_CS);
                if (FIGURE_FROM_NFC) FIGURE_ON = FIGURE_FROM_NFC = 0;
                LeaveCriticalSection(&FIG_CS);
                continue;
            }
            if (!last_len) {  /* new tag: read it once */
                SCARDHANDLE h;
                DWORD proto;
                nfc_tag t;
                if (SCardConnectA(ctx, reader, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &h, &proto) != SCARD_S_SUCCESS)
                    continue;
                if (nfc_read_tag(h, proto, &t)) {
                    memcpy(last_uid, t.uid, t.uid_len);
                    last_len = t.uid_len;
                    EnterCriticalSection(&FIG_CS);
                    if (!t.kind && t.has_raw) {  /* blank NTAG: a waiting capsule goes onto it */
                        int c, ty, v, left = capsule_take(&c, &ty, &v);
                        uint8_t rec[16];
                        if (left) {
                            nfc_figure_record(rec, c, ty, v);
                            if (nfc_write_record(h, proto, rec)) {
                                t.kind = NFC_FIGURE; t.chara = c; t.type = ty; t.variant = v;
                                MessageBeep(MB_ICONASTERISK);
                            } else capsule_add(c, ty, v);  /* moved too soon: keep it for the next tag */
                            if (LOG) {
                                fprintf(LOG, "vendor: capsule %d %d %d %s, %d left\n", c, ty, v,
                                        t.kind ? "written to the tag" : "NOT written (tag moved?)", left - (t.kind != 0));
                                fflush(LOG);
                            }
                        }
                    }
                    if (t.kind == NFC_FIGURE) {
                        uint8_t fu[8];
                        nfc_figure_uid(&t, fu);
                        FIGURE_ON = FIGURE_FROM_NFC = set_figure(fu, t.chara, t.type, t.variant);
                        if (CARD_FROM_NFC) CARD_UNTIL = CARD_FROM_NFC = 0;  /* else the last login card stays "on the reader" */
                    } else {
                        memcpy(CARD_UID, t.card_id, 8);
                        CARD_FROM_NFC = 1;
                        if (LOG) {
                            fprintf(LOG, "nfc: login card %02X%02X%02X%02X%02X%02X%02X%02X\n", CARD_UID[0], CARD_UID[1],
                                    CARD_UID[2], CARD_UID[3], CARD_UID[4], CARD_UID[5], CARD_UID[6], CARD_UID[7]);
                            fflush(LOG);
                        }
                    }
                    LeaveCriticalSection(&FIG_CS);
                }
                SCardDisconnect(h, SCARD_LEAVE_CARD);
            }
            if (CARD_FROM_NFC && last_len) CARD_UNTIL = GetTickCount() + 1500;  /* on the reader = on the pad */
        }
        SCardReleaseContext(ctx);
        reader = NULL;
        Sleep(1000);
    }
    return 0;
}

/* tmac (sub_18017FCA0) turns buffer byte 7 into switch flags for arko26: 0x10 -> 1 = TEST, 0x02 -> 2 = SERVICE
   (both checked in test mode INPUT CHECK), 0x01 -> 4 = not shown anywhere */
static void poll_keys(void) {
    static int was_f12, was_f9;
    int f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0, f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f12 && !was_f12) {
        EnterCriticalSection(&FIG_CS);
        FIGURE_ON = !FIGURE_ON && make_figure();
        FIGURE_FROM_NFC = 0;
        LeaveCriticalSection(&FIG_CS);
        if (!FIGURE_ON && LOG) { fprintf(LOG, "figure: removed\n"); fflush(LOG); }
    }
    was_f12 = f12;
    if (f9 && !was_f9) COINS++;
    was_f9 = f9;
    BUTTONS = (GetAsyncKeyState(VK_F1) & 0x8000 ? 0x10 : 0)
            | (GetAsyncKeyState(VK_F2) & 0x8000 ? 0x02 : 0);
    if (GetAsyncKeyState(VK_F11) & 0x8000) {
        EnterCriticalSection(&FIG_CS);
        if (!CARD_UNTIL || CARD_FROM_NFC) load_card();
        CARD_FROM_NFC = 0;
        CARD_UNTIL = GetTickCount() + 3000;
        LeaveCriticalSection(&FIG_CS);
    }
}

/* core */
EXPORT uint8_t ac_io_begin(uint8_t port, const char *ver, int *out, int baud) { TRACE(); if (out) *out = 0; return 1; }
EXPORT uint8_t ac_io_is_active2(int a, int *out, int port) { TRACE(); if (out) *out = 6; return 1; }
EXPORT uint8_t ac_io_is_active_device(int node) { TRACE(); return 1; }
EXPORT uint8_t ac_io_begin_get_status(int a, int b) { TRACE(); return 1; }
EXPORT void ac_io_update(int port) { TRACE(); poll_keys(); }
EXPORT void ac_io_end(int port) { TRACE(); }
EXPORT uint8_t ac_io_soft_watch_dog_off(int port) { TRACE(); return 1; }

/* BMPU */
EXPORT uint8_t ac_io_bmpu_update_control_status_buffer(void) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_get_control_status_buffer(uint8_t *buf) {
    TRACE();
    memset(buf, 0, 0x38);
    *(uint32_t *)buf = ++SEQ;
    buf[7] = BUTTONS;
    return 1;
}
EXPORT uint8_t ac_io_bmpu_set_output_mode(int mode) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_set_watchdog_time(uint8_t t) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_control_coin_blocker_open(int slot) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_control_coin_blocker_close(int slot) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_lock_coincounter(int slot) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_unlock_coincounter(int slot) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_control_led_bright(int a, int b) { TRACE(); return 1; }
EXPORT uint8_t ac_io_bmpu_current_coinstock(int slot, int *out) {
    TRACE();
    if (slot > 1) return 0;
    if (out) *out = slot == 0 ? COINS : 0;
    return 1;
}
EXPORT void ac_io_bmpu_destroy_get_status_thread(void) { TRACE(); }

/*
 * ICCC card readers (AcioUpdateICCCRead sub_180180A70). Two paths:
 *   "ea" (e-amusement pass): req_getuid -> 24-byte uc status: [0] 2 = card, [1]&3 type, [2..9] UID, [10] IC type.
 *        Recorded unless type 0 + IC 38 + UID E0.. (those belong to the other path).
 *   "26" (mascot figures): reset_to_ready -> ucgetuid -> 25-byte status ([0] 4 = no card) -> read_block ->
 *        40-byte status. F12 puts / removes the figure from figure0.txt (see make_figure).
 * arko26 starts one path at a time through tmac's IO vtable (0x18024C6A8); a start fails while the other path is
 * busy. Only the ea path asks for 24 bytes, so that's where the card shows up (F11 = card on the reader for 3 s).
 */
EXPORT void ac_io_iccc_set_num_device(int n) { TRACE(); if (LOG) { fprintf(LOG, "iccc: %d devices\n", n); fflush(LOG); } }
EXPORT uint8_t ac_io_iccc_cardunit_init(int dev) { TRACE(); return 1; }
EXPORT uint8_t ac_io_iccc_cardunit_init_isfinished(int dev, int *st) { TRACE(); if (st) *st = 3; return 1; }
EXPORT uint8_t ac_io_iccc_crypt_init(int dev) { TRACE(); return 1; }
EXPORT uint8_t ac_io_iccc_is_enable_send_command(int dev) { TRACE(); return 1; }
EXPORT uint8_t ac_io_iccc_req_getuid(int dev) { TRACE(); return 1; }
EXPORT int ac_io_iccc_req_getuid_isfinished(int dev) { TRACE(); return 0; }
EXPORT uint8_t ac_io_iccc_get_uc_status_buffer(int dev, uint8_t len, uint8_t *buf) {
    TRACE();
    if (!buf) return 1;
    EnterCriticalSection(&FIG_CS);
    memset(buf, 0, len);  /* ea path: buf[0] != 2 -> no card (gives up after 5 tries) */
    if (len == 25) {      /* 26 path after ucgetuid: [0] 2 + UID + IC 38 = tag; 4 = no card (else it retries forever) */
        buf[0] = 4;
        /* once per reset_to_ready: a known tag sends tmac straight back to ucgetuid, so a tag that kept
           answering would loop forever and starve the e-amusement path */
        if (FIGURE_ON && !FIGURE_QUIET) { buf[0] = 2; memcpy(buf + 2, FIGURE_UID, 8); buf[10] = 38; FIGURE_QUIET = 1; }
    }
    if (len == 40 && FIGURE_ON) {  /* 26 path after read_block: data[0..23] at [0x10..0x27], data[24..27] at [2..5] */
        buf[0] = 2;
        memcpy(buf + 0x10, FIGURE_DATA, 24);
        memcpy(buf + 2, FIGURE_DATA + 24, 4);
    }
    if (len == 24 && CARD_UNTIL && (int)(CARD_UNTIL - GetTickCount()) > 0) {
        static int logged;
        buf[0] = 2;
        memcpy(buf + 2, CARD_UID, 8);
        if (!logged++ && LOG) { fprintf(LOG, "card: reported on dev %d\n", dev); fflush(LOG); }
    }
    LeaveCriticalSection(&FIG_CS);
    return 1;
}
EXPORT int ac_io_iccc_get_uc_status_buffer_isfinished(int dev) { TRACE(); return 0; }
EXPORT uint8_t ac_io_iccc_ucgetuid(int dev, uint8_t cmd, uint8_t *buf) { TRACE(); return 1; }
EXPORT int ac_io_iccc_ucgetuid_isfinished(int dev) { TRACE(); return 0; }
EXPORT uint8_t ac_io_iccc_read_block(int dev, void *buf, uint8_t blk, uint8_t n) { TRACE(); return 1; }
EXPORT int ac_io_iccc_read_block_isfinished(int dev) { TRACE(); return 0; }
EXPORT uint8_t ac_io_iccc_reset_to_ready(int dev) { TRACE(); FIGURE_QUIET = 0; return 1; }
EXPORT int ac_io_iccc_reset_to_ready_isfinished(int dev) { TRACE(); return 0; }
EXPORT uint8_t ac_io_iccc_device_control_iccard_power_supply_off(int dev) { TRACE(); return 1; }
EXPORT uint8_t ac_io_iccc_device_control_isfinished(int dev, int *st) { TRACE(); if (st) *st = 5; return 1; }

/* O26A capsule vendor: idle, never empty */
EXPORT uint8_t ac_io_o26a_create_get_status_thread(void) { TRACE(); return 1; }
EXPORT uint8_t ac_io_o26a_vendor_init(void) { TRACE(); return 1; }
EXPORT int ac_io_o26a_vendor_init_is_finish(void) { TRACE(); return 1; }
EXPORT uint8_t ac_io_o26a_update_control_status_buffer(void) { TRACE(); return 1; }
EXPORT uint8_t ac_io_o26a_get_control_status_buffer(uint8_t *buf) {
    TRACE();
    memset(buf, 0, 0x38);
    *(uint32_t *)buf = SEQ;
    return 1;
}
EXPORT uint8_t ac_io_o26a_get_vendor_status_begin(void) { TRACE(); return 1; }
EXPORT int ac_io_o26a_get_vendor_status_is_finish(void) { TRACE(); return 1; }
EXPORT int ac_io_o26a_pay_vendor_request_is_finish(void) { TRACE(); return 1; }
EXPORT int ac_io_o26a_release_error(void) { TRACE(); return 1; }
EXPORT int ac_io_o26a_release_error_is_finish(void) { TRACE(); return 1; }

/*
 * Touch: tmac doesn't use the serial ports for touch. It checks SM_DIGITIZER, calls RegisterTouchWindow on its D3D window and
 * reads WM_TOUCH (wndproc sub_180177C20 -> sub_180177CD0): x/100, y/100 in 1080x1920 screen pixels,
 * DOWN/MOVE/UP -> touch panel ring (sub_180161EF0). Windows doesn't turn a mouse into WM_TOUCH, so we patch
 * tmac's user32 imports: RegisterTouchWindow also subclasses the window, left-button mouse messages become
 * WM_TOUCH with a fake handle, and GetTouchInputInfo / CloseTouchInputHandle answer for that handle.
 * Real touch input (handles from Windows, mouse messages synthesized from touch) passes through untouched.
 */
#define MOUSE_TOUCH ((HTOUCHINPUT)(uintptr_t)0x4f323654)  /* "O26T" */
#define FROM_TOUCH(extra) (((extra) & 0xffffff00) == 0xff515700)

static BOOL (WINAPI *RegisterTouchWindow_orig)(HWND, ULONG);
static BOOL (WINAPI *GetTouchInputInfo_orig)(HTOUCHINPUT, UINT, PTOUCHINPUT, int);
static BOOL (WINAPI *CloseTouchInputHandle_orig)(HTOUCHINPUT);
static WNDPROC game_wndproc;
static TOUCHINPUT mouse_touch;
static DWORD touch_id;

/* CTouchPanelDevice init (sub_180161A80) wants SM_DIGITIZER = ready | multi-input | integrated touch,
   else TPINIT error 5-1541-0000 and WM_TOUCH stays ignored. */
static int WINAPI GetSystemMetrics_hook(int index) {
    int v = GetSystemMetrics(index);
    if (index == SM_DIGITIZER) return v | NID_READY | NID_MULTI_INPUT | NID_INTEGRATED_TOUCH;
    if (index == SM_MAXIMUMTOUCHES && v < 1) return 1;
    return v;
}

/* tmac hides the pointer (it expects a touchscreen); ignore that so the mouse stays visible. */
static int WINAPI ShowCursor_hook(BOOL show) {
    int n = ShowCursor(TRUE);  /* show or hide: either way the display count ends up at 0 or above */
    (void)show;
    if (n > 0) n = ShowCursor(FALSE);
    return n;
}

static BOOL WINAPI GetTouchInputInfo_hook(HTOUCHINPUT h, UINT n, PTOUCHINPUT out, int size) {
    if (h != MOUSE_TOUCH) return GetTouchInputInfo_orig(h, n, out, size);
    if (n < 1 || size != sizeof(TOUCHINPUT)) return FALSE;
    out[0] = mouse_touch;
    return TRUE;
}

static BOOL WINAPI CloseTouchInputHandle_hook(HTOUCHINPUT h) {
    return h == MOUSE_TOUCH ? TRUE : CloseTouchInputHandle_orig(h);
}

static LRESULT CALLBACK touch_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DWORD flags = 0;
    switch (msg) {
    case WM_LBUTTONDOWN: flags = TOUCHEVENTF_DOWN; touch_id++; SetCapture(hwnd); break;
    case WM_MOUSEMOVE: if (wp & MK_LBUTTON) flags = TOUCHEVENTF_MOVE; break;
    case WM_LBUTTONUP: flags = TOUCHEVENTF_UP; ReleaseCapture(); break;
    case WM_SETCURSOR:  /* the game hides the pointer (it expects a touchscreen); keep an arrow for the mouse */
        if (LOWORD(lp) == HTCLIENT) {
            while (ShowCursor(TRUE) < 0);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    }
    if (flags && !FROM_TOUCH(GetMessageExtraInfo())) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            memset(&mouse_touch, 0, sizeof mouse_touch);
            /* client coords scaled to the game's 1080x1920 space, so windowed mode works too */
            mouse_touch.x = MulDiv((short)LOWORD(lp), 108000, rc.right);
            mouse_touch.y = MulDiv((short)HIWORD(lp), 192000, rc.bottom);
            mouse_touch.dwID = touch_id;
            mouse_touch.dwFlags = flags | TOUCHEVENTF_PRIMARY | (flags == TOUCHEVENTF_UP ? 0 : TOUCHEVENTF_INRANGE);
            mouse_touch.dwTime = GetTickCount();
            CallWindowProcA(game_wndproc, hwnd, WM_TOUCH, 1, (LPARAM)MOUSE_TOUCH);
        }
    }
    return CallWindowProcA(game_wndproc, hwnd, msg, wp, lp);
}

static BOOL WINAPI RegisterTouchWindow_hook(HWND hwnd, ULONG flags) {
    BOOL ok = RegisterTouchWindow_orig(hwnd, flags);
    if (!game_wndproc) {
        game_wndproc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)touch_wndproc);
        if (LOG) { fprintf(LOG, "touch: subclassed window %p (RegisterTouchWindow=%d)\n", hwnd, ok); fflush(LOG); }
    }
    return TRUE;  /* fine without a digitizer, the mouse path doesn't need it */
}

/* Swap one import of `mod` (by name, any DLL) and return the old pointer. */
static void *iat_hook(HMODULE mod, const char *name, void *fn) {
    uint8_t *base = (uint8_t *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); dir.VirtualAddress && imp->Name; imp++) {
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            DWORD old;
            void *prev;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, name)) continue;
            VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            prev = (void *)iat->u1.Function;
            iat->u1.Function = (ULONGLONG)fn;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            return prev;
        }
    }
    return NULL;
}

/*
 * The game asks for its fonts (data/font/*.ttc, loaded with AddFontMemResourceEx) by their Japanese family names,
 * e.g. "Rgシリウス-B". Windows only matches localized font names on a Japanese system, so elsewhere it falls back to a
 * font without the full-width letters player names are stored in, and text like the name bubbles stays blank. Swap
 * in the English family name (from the same fonts' name tables); that works everywhere. Each face is logged once.
 */
static const WCHAR *font_name_w(const WCHAR *face) {
    static const struct { const WCHAR *jp, *en; } map[] = {
        {L"\xff24\xff26\x592a\x4e38\x30b4\x30b7\x30c3\x30af\x4f53", L"DFMaruGothic-SB"},  /* df-futomarugothic-w9.ttc */
        {L"\xff24\xff26\xff30\x592a\x4e38\x30b4\x30b7\x30c3\x30af\x4f53", L"DFPMaruGothic-SB"},
        {L"\xff24\xff26\xff27\x592a\x4e38\x30b4\x30b7\x30c3\x30af\x4f53", L"DFGMaruGothic-SB"},
        {L"\xff24\xff26\x30ed\x30de\x30f3\x96eaW9", L"DFRomanYuki-W9"},  /* df-romanyuki-w9.ttc */
        {L"\xff24\xff26\xff30\x30ed\x30de\x30f3\x96eaW9", L"DFPRomanYuki-W9"},
        {L"\xff24\xff26\xff27\x30ed\x30de\x30f3\x96eaW9", L"DFGRomanYuki-W9"},
        {L"Rg\x30b7\x30ea\x30a6\x30b9-B", L"RgSirius-Bd"},  /* t3sirib.ttc */
        {L"RgP\x30b7\x30ea\x30a6\x30b9-B", L"RgPSirius-Bd"},
        {L"RgK\x30b7\x30ea\x30a6\x30b9-B", L"RgKSirius-Bd"},
        {L"Rg\x30b7\x30ea\x30a6\x30b9-E", L"RgSirius-XB"},  /* t3sirie.ttc */
        {L"RgP\x30b7\x30ea\x30a6\x30b9-E", L"RgPSirius-XB"},
        {L"RgK\x30b7\x30ea\x30a6\x30b9-E", L"RgKSirius-XB"},
        {L"Rg\x30b7\x30ea\x30a6\x30b9-M", L"RgSirius-Md"},  /* t3sirim.ttc */
        {L"RgP\x30b7\x30ea\x30a6\x30b9-M", L"RgPSirius-Md"},
        {L"RgK\x30b7\x30ea\x30a6\x30b9-M", L"RgKSirius-Md"},
        {L"Rg\x30b7\x30ea\x30a6\x30b9-U", L"RgSirius-UB"},  /* t3siriu.ttc */
        {L"RgP\x30b7\x30ea\x30a6\x30b9-U", L"RgPSirius-UB"},
        {L"RgK\x30b7\x30ea\x30a6\x30b9-U", L"RgKSirius-UB"},
    };
    static WCHAR seen[32][LF_FACESIZE];
    static int nseen;
    const WCHAR *out = face;
    int i;
    for (i = 0; face && i < (int)(sizeof map / sizeof map[0]); i++)
        if (!wcscmp(face, map[i].jp)) { out = map[i].en; break; }
    for (i = 0; face && i < nseen && wcscmp(seen[i], face); i++);
    if (face && i == nseen && nseen < 32) {
        char a[LF_FACESIZE * 3], b[LF_FACESIZE * 3];
        wcsncpy(seen[nseen++], face, LF_FACESIZE - 1);
        WideCharToMultiByte(CP_UTF8, 0, face, -1, a, sizeof a, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, out, -1, b, sizeof b, NULL, NULL);
        if (LOG) { fprintf(LOG, "font: %s -> %s\n", a, b); fflush(LOG); }
    }
    return out;
}

/* ANSI face names are Shift-JIS (code page 932) whatever the system code page is. */
static const char *font_name(const char *face, char *buf) {
    WCHAR w[LF_FACESIZE];
    const WCHAR *en;
    if (!face || !MultiByteToWideChar(932, 0, face, -1, w, LF_FACESIZE)) return face;
    en = font_name_w(w);
    if (en == w) return face;
    WideCharToMultiByte(CP_ACP, 0, en, -1, buf, LF_FACESIZE, NULL, NULL);
    return buf;
}

typedef HRESULT (WINAPI *d3dx_create_font_fn)(void *, INT, UINT, UINT, UINT, BOOL, DWORD, DWORD, DWORD, DWORD, LPCSTR, void **);
static d3dx_create_font_fn D3DXCreateFontA_orig;

static HRESULT WINAPI D3DXCreateFontA_hook(void *dev, INT h, UINT w, UINT wt, UINT mip, BOOL it, DWORD cs, DWORD op,
                                           DWORD q, DWORD pf, LPCSTR face, void **out) {
    char buf[LF_FACESIZE];
    return D3DXCreateFontA_orig(dev, h, w, wt, mip, it, cs, op, q, pf, font_name(face, buf), out);
}

static HFONT WINAPI CreateFontA_hook(int h, int w, int esc, int ori, int wt, DWORD it, DWORD ul, DWORD so, DWORD cs,
                                     DWORD op, DWORD cp, DWORD q, DWORD pf, LPCSTR face) {
    char buf[LF_FACESIZE];
    return CreateFontA(h, w, esc, ori, wt, it, ul, so, cs, op, cp, q, pf, font_name(face, buf));
}

static HFONT WINAPI CreateFontIndirectA_hook(const LOGFONTA *lf) {
    LOGFONTA l = *lf;
    char buf[LF_FACESIZE];
    strncpy(l.lfFaceName, font_name(lf->lfFaceName, buf), LF_FACESIZE - 1);
    l.lfFaceName[LF_FACESIZE - 1] = 0;
    return CreateFontIndirectA(&l);
}

static HFONT WINAPI CreateFontIndirectW_hook(const LOGFONTW *lf) {
    LOGFONTW l = *lf;
    wcsncpy(l.lfFaceName, font_name_w(lf->lfFaceName), LF_FACESIZE - 1);
    l.lfFaceName[LF_FACESIZE - 1] = 0;
    return CreateFontIndirectW(&l);
}

static void hook_touch(void) {
    HMODULE tmac = GetModuleHandleA("tmac.dll");
    if (!tmac) { if (LOG) fprintf(LOG, "touch: tmac.dll not loaded, no mouse touch\n"); return; }
    RegisterTouchWindow_orig = iat_hook(tmac, "RegisterTouchWindow", RegisterTouchWindow_hook);
    GetTouchInputInfo_orig = iat_hook(tmac, "GetTouchInputInfo", GetTouchInputInfo_hook);
    CloseTouchInputHandle_orig = iat_hook(tmac, "CloseTouchInputHandle", CloseTouchInputHandle_hook);
    void *metrics = iat_hook(tmac, "GetSystemMetrics", GetSystemMetrics_hook);
    iat_hook(tmac, "ShowCursor", ShowCursor_hook);
    D3DXCreateFontA_orig = iat_hook(tmac, "D3DXCreateFontA", D3DXCreateFontA_hook);
    iat_hook(tmac, "CreateFontA", CreateFontA_hook);
    iat_hook(tmac, "CreateFontIndirectA", CreateFontIndirectA_hook);
    iat_hook(tmac, "CreateFontIndirectW", CreateFontIndirectW_hook);
    if (LOG) {
        fprintf(LOG, "touch: hooks %s\n", RegisterTouchWindow_orig && GetTouchInputInfo_orig && CloseTouchInputHandle_orig && metrics ? "ok" : "MISSING");
        fflush(LOG);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, path, MAX_PATH);
        strcpy(strrchr(path, '\\') + 1, "libacioex_emu.log");
        LOG = fopen(path, "w");
        /* tmac.dll imports us, so it is mapped and its imports are bound by now; its window comes later */
        hook_touch();
        InitializeCriticalSection(&FIG_CS);
        srand(GetTickCount());
        if (!getenv("NFC_READER") || strcmp(getenv("NFC_READER"), "off")) CloseHandle(CreateThread(NULL, 0, nfc_thread, NULL, 0, NULL));
    }
    return TRUE;
}
