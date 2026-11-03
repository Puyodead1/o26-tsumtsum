/*
 * o26tag: manage NFC tags (NTAG215 on an ACR122U, or any PC/SC reader) used as Tsum Tsum mascot figures and
 * e-amusement login cards with libacioEx.dll. Tag format: see nfc.h.
 *
 *   o26tag readers                       list PC/SC readers
 *   o26tag info                          show what's on the tag
 *   o26tag list [text | series]          list figures (built-in table) with their series, by name or series number
 *   o26tag figure <chara> <type> <var>   make the tag a figure (e.g. 1 1 1 = Mickey 001)
 *   o26tag card [16 hex digits]          make the tag a login card (default: the id derived from its UID)
 *   o26tag erase                         clear our record (pages 4..7)
 * Options: -r <n> reader number (default 0). o26tag_gui.exe does the same in a window.
 * Commands that need a tag wait up to 15 s for one.
 */
#include "nfc.h"
#include "figures.h"
#include <stdio.h>
#include <stdlib.h>

/* ---- PC/SC ---- */
static SCARDCONTEXT CTX;
static char READERS[2048], *READER;

static int open_reader(int index) {
    DWORD n = sizeof READERS;
    char *r;
    int i = 0;
    if (SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &CTX) != SCARD_S_SUCCESS) {
        fprintf(stderr, "no PC/SC service (is the Smart Card service running?)\n");
        return 0;
    }
    if (SCardListReadersA(CTX, NULL, READERS, &n) != SCARD_S_SUCCESS) { fprintf(stderr, "no reader found\n"); return 0; }
    for (r = READERS; *r; r += strlen(r) + 1, i++)
        if (i == index) { READER = r; return 1; }
    fprintf(stderr, "no reader #%d\n", index);
    return 0;
}

/* Wait for a tag and connect to it. */
static int connect_tag(SCARDHANDLE *h, DWORD *proto) {
    SCARD_READERSTATEA st = {0};
    int waited = 0;
    st.szReader = READER;
    st.dwCurrentState = SCARD_STATE_UNAWARE;
    for (;;) {
        LONG rc = SCardGetStatusChangeA(CTX, 1000, &st, 1);  /* SCARD_E_TIMEOUT after 1 s without a change */
        if (rc != SCARD_S_SUCCESS && rc != SCARD_E_TIMEOUT) { fprintf(stderr, "reader error %08lX\n", rc); return 0; }
        if (st.dwEventState & SCARD_STATE_PRESENT) break;
        st.dwCurrentState = st.dwEventState & ~SCARD_STATE_CHANGED;
        if (!waited++) printf("put a tag on %s ...\n", READER);
        if (waited > 15) { fprintf(stderr, "no tag\n"); return 0; }
    }
    if (SCardConnectA(CTX, READER, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, h, proto) != SCARD_S_SUCCESS) {
        fprintf(stderr, "can't connect to the tag\n");
        return 0;
    }
    return 1;
}

static void hex(const uint8_t *p, int n) {
    while (n--) printf("%02X", *p++);
}

static int parse_hex(const char *s, uint8_t *out, int n) {
    int i;
    if ((int)strlen(s) != 2 * n) return 0;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (!isxdigit((unsigned char)s[2 * i]) || !isxdigit((unsigned char)s[2 * i + 1]) || sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

static void show(const nfc_tag *t) {
    uint8_t fu[8];
    printf("UID        "); hex(t->uid, t->uid_len); printf(" (%d bytes)\n", t->uid_len);
    if (!t->has_raw) printf("pages 4..7 not readable (not an NTAG/Ultralight?) - works as a login card only\n");
    switch (t->kind) {
    case NFC_FIGURE: {
        const figure *f = find_figure(t->chara, t->type, t->variant);
        nfc_figure_uid(t, fu);
        printf("FIGURE     chara %d type %d variant %d", t->chara, t->type, t->variant);
        if (f) {
            char s[64], e[64];
            printf("  = %s (skin id %u)\nseries     %s (English region: %s)", figure_name(f), f->id,
                   series_text(f->series, s), series_text(f->series_en, e));
        } else printf("  = not in the figure table!");
        printf("\nfunique_id "); hex(fu, 8); printf("\n");
        break;
    }
    case NFC_LOGIN:
        printf("LOGIN CARD "); hex(t->card_id, 8); printf("\n");
        break;
    default:
        printf("no O26 record - acts as a login card with id "); hex(t->card_id, 8); printf("\n");
        if (t->has_raw) { printf("pages 4..7 "); hex(t->raw, 16); printf("\n"); }
    }
}

static int usage(void) {
    puts("o26tag readers | info | list [text | series] | figure <chara> <type> <variant> | card [16 hex] | erase\n"
         "options: -r <reader number>");
    return 1;
}

int main(int argc, char **argv) {
    const char *cmd;
    int reader = 0, i, ok = 1;
    char *args[8];
    int nargs = 0;
    SCARDHANDLE h;
    DWORD proto;
    nfc_tag t;
    uint8_t rec[16];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) reader = atoi(argv[++i]);
        else if (nargs < 8) args[nargs++] = argv[i];
    }
    if (!nargs) return usage();
    cmd = args[0];
    if (!strcmp(cmd, "list")) {  /* list [text | series number] */
        char s[64], e[64];
        const char *arg = nargs > 1 ? args[1] : "";
        int vol = isdigit((unsigned char)arg[0]) ? atoi(arg) : -1;
        puts("chr typ var  series     EN series  name");
        for (i = 0; i < NFIG; i++)
            if (vol >= 0 ? FIG[i].series >> vol & 1 : icontains(FIG[i].name, arg))
                printf("%3d %3d %2d   %-10s %-10s %s\n", FIG[i].chara, FIG[i].type, FIG[i].variant,
                       series_text(FIG[i].series, s), series_text(FIG[i].series_en, e), figure_name(&FIG[i]));
        return 0;
    }
    if (!strcmp(cmd, "readers")) {
        DWORD n = sizeof READERS;
        char *r;
        if (SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &CTX) != SCARD_S_SUCCESS ||
            SCardListReadersA(CTX, NULL, READERS, &n) != SCARD_S_SUCCESS) { puts("no readers"); return 1; }
        for (r = READERS, i = 0; *r; r += strlen(r) + 1, i++) printf("%d: %s\n", i, r);
        return 0;
    }
    if (strcmp(cmd, "info") && strcmp(cmd, "figure") && strcmp(cmd, "card") && strcmp(cmd, "erase")) return usage();
    if (!strcmp(cmd, "figure") && nargs != 4) return usage();
    if (!strcmp(cmd, "figure") && !find_figure(atoi(args[1]), atoi(args[2]), atoi(args[3]))) {
        fprintf(stderr, "%s %s %s is not in the figure table (see 'list')\n", args[1], args[2], args[3]);
        return 1;
    }
    if (!strcmp(cmd, "card") && nargs > 1 && strlen(args[1]) != 16) { fprintf(stderr, "card id must be 16 hex digits\n"); return 1; }
    if (!open_reader(reader) || !connect_tag(&h, &proto)) return 1;
    if (!nfc_read_tag(h, proto, &t)) { fprintf(stderr, "can't read the tag UID\n"); return 1; }

    if (!strcmp(cmd, "figure")) {
        nfc_figure_record(rec, atoi(args[1]), atoi(args[2]), atoi(args[3]));
    } else if (!strcmp(cmd, "card")) {
        uint8_t id[8];
        memcpy(id, t.card_id, 8);
        if (t.kind != NFC_LOGIN) nfc_derive_card_id(&t, id);
        if (nargs > 1 && !parse_hex(args[1], id, 8)) { fprintf(stderr, "card id must be 16 hex digits\n"); return 1; }
        nfc_login_record(rec, id);
    } else if (!strcmp(cmd, "erase")) {
        memset(rec, 0, 16);
    }
    if (strcmp(cmd, "info")) {
        if (!t.has_raw) { fprintf(stderr, "this tag has no writable NTAG pages\n"); return 1; }
        ok = nfc_write_record(h, proto, rec);
        printf(ok ? "written\n" : "WRITE FAILED (locked tag?)\n");
        nfc_read_tag(h, proto, &t);
    }
    show(&t);
    SCardDisconnect(h, SCARD_LEAVE_CARD);
    SCardReleaseContext(CTX);
    return ok ? 0 : 1;
}
