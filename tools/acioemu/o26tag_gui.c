/*
 * o26tag_gui: o26tag in a window. Shows the tag on the reader live (figure / login card / blank) and writes figures,
 * login cards, or erases our record. Same tag format and figure table as o26tag (nfc.h, figures.h). Windows 7+.
 */
#include "nfc.h"
#include "figures.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
    ID_READER = 100, ID_REFRESH, ID_STATUS, ID_FIGBOX, ID_FILTER_L, ID_FILTER, ID_SERIES, ID_LIST,
    ID_CHARA_L, ID_CHARA, ID_TYPE_L, ID_TYPE, ID_VARIANT_L, ID_VARIANT, ID_WRITE_FIG,
    ID_CARDBOX, ID_CARD_L, ID_CARDID, ID_FROM_TAG, ID_RANDOM, ID_WRITE_CARD, ID_CARD_NOTE,
    ID_ERASE, ID_LOG, ID_LAST
};
#define CTL(id) C[(id) - ID_READER]
#define WM_TAG (WM_APP + 1)      /* wParam TAG_*, lParam malloc'd nfc_tag (TAG_OK) */
#define WM_READERS (WM_APP + 2)  /* reader plugged / unplugged */
enum { TAG_NOSERVICE, TAG_NOREADER, TAG_NONE, TAG_BAD, TAG_OK };

static HWND WND, C[ID_LAST - ID_READER];
static HFONT FONT, BIG;
static int STATUS_H = 66;             /* 3 lines of BIG + padding */
static CRITICAL_SECTION CS;
static char SEL[256];                 /* selected reader ("" = none), guarded by CS */
static SCARDCONTEXT MON, UI;          /* monitor thread's context / the UI thread's (writes) */
static volatile LONG RESTART;
static nfc_tag LAST;                  /* last tag the monitor saw */
static int HAVE_LAST;

static void logmsg(const char *fmt, ...) {
    char buf[512];
    SYSTEMTIME t;
    int n;
    va_list ap;
    GetLocalTime(&t);
    n = sprintf(buf, "%02d:%02d:%02d  ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    va_end(ap);
    n = (int)SendMessageA(CTL(ID_LOG), LB_ADDSTRING, 0, (LPARAM)buf);
    SendMessageA(CTL(ID_LOG), LB_SETTOPINDEX, n, 0);
}

static char *hexs(const uint8_t *p, int n, char *out) {
    int i;
    for (i = 0; i < n; i++) sprintf(out + 2 * i, "%02X", p[i]);
    out[2 * n] = 0;
    return out;
}

/* ---- reader monitor (own thread): posts WM_TAG whenever the tag on the selected reader changes ---- */

static void report(const char *reader, DWORD state) {
    SCARDHANDLE h;
    DWORD proto;
    nfc_tag *t;
    if (state & SCARD_STATE_EMPTY) { PostMessageA(WND, WM_TAG, TAG_NONE, 0); return; }
    if (!(state & SCARD_STATE_PRESENT)) return;
    t = malloc(sizeof *t);
    if (SCardConnectA(MON, reader, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &h, &proto) == SCARD_S_SUCCESS) {
        int ok = nfc_read_tag(h, proto, t);
        SCardDisconnect(h, SCARD_LEAVE_CARD);
        if (ok) { PostMessageA(WND, WM_TAG, TAG_OK, (LPARAM)t); return; }
    }
    free(t);
    PostMessageA(WND, WM_TAG, TAG_BAD, 0);
}

static DWORD WINAPI monitor(void *arg) {
    SCARD_READERSTATEA st[2];
    char reader[256];
    (void)arg;
    for (;;) {
        DWORD n;
        int first = 1;
        if (!MON && SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &MON) != SCARD_S_SUCCESS) {
            MON = 0;
            PostMessageA(WND, WM_TAG, TAG_NOSERVICE, 0);
            Sleep(2000);
            PostMessageA(WND, WM_READERS, 0, 0);
            continue;
        }
        EnterCriticalSection(&CS);
        strcpy(reader, SEL);
        LeaveCriticalSection(&CS);
        InterlockedExchange(&RESTART, 0);
        if (!reader[0]) PostMessageA(WND, WM_TAG, TAG_NOREADER, 0);
        memset(st, 0, sizeof st);
        st[0].szReader = "\\\\?PnP?\\Notification";
        st[1].szReader = reader;
        n = reader[0] ? 2 : 1;
        for (;;) {
            LONG rc = SCardGetStatusChangeA(MON, 1000, st, n);
            if (RESTART) break;
            if (rc == SCARD_E_TIMEOUT || rc == SCARD_E_CANCELLED) continue;
            if (rc != SCARD_S_SUCCESS) {  /* service stopped (last reader unplugged on Windows 8+) etc. */
                SCardReleaseContext(MON);
                MON = 0;
                PostMessageA(WND, WM_READERS, 0, 0);
                Sleep(1000);
                break;
            }
            if (!first && (st[0].dwEventState & SCARD_STATE_CHANGED)) PostMessageA(WND, WM_READERS, 0, 0);
            if (n == 2 && (first || (st[1].dwEventState & SCARD_STATE_CHANGED))) report(reader, st[1].dwEventState);
            st[0].dwCurrentState = st[0].dwEventState & ~SCARD_STATE_CHANGED;
            st[1].dwCurrentState = st[1].dwEventState & ~SCARD_STATE_CHANGED;
            first = 0;
            if (n == 2 && (st[1].dwEventState & (SCARD_STATE_UNKNOWN | SCARD_STATE_UNAVAILABLE))) {
                PostMessageA(WND, WM_READERS, 0, 0);
                Sleep(500);
                break;
            }
        }
    }
}

static void select_reader(const char *name) {
    EnterCriticalSection(&CS);
    if (!strcmp(SEL, name)) { LeaveCriticalSection(&CS); return; }
    strcpy(SEL, name);
    LeaveCriticalSection(&CS);
    InterlockedExchange(&RESTART, 1);
    if (MON) SCardCancel(MON);
    if (name[0]) logmsg("reader: %s", name);
}

/* Re-list readers into the combo box, keeping the selection if it is still there. */
static void list_readers(void) {
    HWND cb = CTL(ID_READER);
    char names[4096], cur[256], *r;
    DWORD n = sizeof names;
    int i, sel = 0;
    EnterCriticalSection(&CS);
    strcpy(cur, SEL);
    LeaveCriticalSection(&CS);
    SendMessageA(cb, CB_RESETCONTENT, 0, 0);
    if (!UI && SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &UI) != SCARD_S_SUCCESS) UI = 0;
    if (UI && SCardListReadersA(UI, NULL, names, &n) == SCARD_S_SUCCESS) {
        for (r = names, i = 0; *r; r += strlen(r) + 1, i++) {
            SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)r);
            if (!strcmp(r, cur)) sel = i;
        }
        SendMessageA(cb, CB_SETCURSEL, sel, 0);
        SendMessageA(cb, CB_GETLBTEXT, sel, (LPARAM)cur);
        select_reader(cur);
    } else {
        if (UI) { SCardReleaseContext(UI); UI = 0; }  /* the service may come back with a new context */
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"(no reader - plug in the ACR122U)");
        SendMessageA(cb, CB_SETCURSEL, 0, 0);
        select_reader("");
    }
}

/* ---- tag status ---- */

/* "Vol. 3, 12" / "Promo" (series 0 is a one-off: Party Mickey) / "-" */
static char *vol_text(uint32_t mask, char *out) {
    int i, n = 0, vols = 0;
    if (mask & 1) n = sprintf(out, "Promo");
    for (i = 1; i < NSERIES; i++)
        if (mask >> i & 1) n += sprintf(out + n, vols++ ? ", %d" : n ? ", Vol. %d" : "Vol. %d", i);
    if (!n) strcpy(out, "-");
    return out;
}

/* type 1 = the normal figurines, type 2 = the one rare figurine of each series */
static const char *kind_text(const figure *f) {
    return f->type == 2 ? "Rare" : "Normal";
}

static const char *english_text(const figure *f) {
    return f->series_en ? "Works" : f->series ? "Japan only" : "-";
}

static void describe(const nfc_tag *t, char *out) {
    char a[40], b[40];
    uint8_t fu[8];
    const figure *f;
    int n = sprintf(out, "UID %s\r\n", hexs(t->uid, t->uid_len, a));
    switch (t->kind) {
    case NFC_FIGURE:
        f = find_figure(t->chara, t->type, t->variant);
        nfc_figure_uid(t, fu);
        n += sprintf(out + n, "FIGURE: %s  (%d %d %d)", f ? figure_name(f) : "NOT IN THE FIGURE TABLE",
                     t->chara, t->type, t->variant);
        if (f) {
            char s[64];
            n += sprintf(out + n, "  -  %s, %s", kind_text(f), vol_text(f->series, s));
            if (!f->series_en) n += sprintf(out + n, " (Japan only)");
        }
        sprintf(out + n, "\r\nfunique_id %s", hexs(fu, 8, b));
        break;
    case NFC_LOGIN:
        sprintf(out + n, "LOGIN CARD %s", hexs(t->card_id, 8, b));
        break;
    default:
        sprintf(out + n, "%s\r\nworks as login card %s",
                t->has_raw ? "blank (no O26 record)" : "not an NTAG - can't be written", hexs(t->card_id, 8, b));
    }
}

static void select_figure(int chara, int type, int variant);

static void show_tag(int what, nfc_tag *t) {
    char buf[256], id[20];
    HAVE_LAST = what == TAG_OK;
    switch (what) {
    case TAG_NOSERVICE: strcpy(buf, "Smart card service not running.\r\nPlug in the reader."); break;
    case TAG_NOREADER: strcpy(buf, "No reader."); break;
    case TAG_NONE: strcpy(buf, "Put a tag on the reader."); break;
    case TAG_BAD: strcpy(buf, "Tag present but unreadable.\r\nHold it still / try again."); break;
    default:
        LAST = *t;
        describe(t, buf);
        if (t->kind == NFC_FIGURE) select_figure(t->chara, t->type, t->variant);
        if (t->kind == NFC_LOGIN) SetWindowTextA(CTL(ID_CARDID), hexs(t->card_id, 8, id));
    }
    SetWindowTextA(CTL(ID_STATUS), buf);
}

/* ---- writing (UI thread, own connection; the monitor doesn't see a change, so we show the result ourselves) ---- */

static int ask(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return MessageBoxA(WND, buf, "o26tag", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

static void fail(const char *msg) {
    logmsg("%s", msg);
    MessageBoxA(WND, msg, "o26tag", MB_OK | MB_ICONWARNING);
}

static void write_tag(const uint8_t rec[16], const char *what) {
    char reader[256], desc[256];
    SCARDHANDLE h;
    DWORD proto;
    nfc_tag t;
    int ok;
    EnterCriticalSection(&CS);
    strcpy(reader, SEL);
    LeaveCriticalSection(&CS);
    if (!reader[0] || !UI) { fail("No reader."); return; }
    if (SCardConnectA(UI, reader, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &h, &proto) != SCARD_S_SUCCESS) {
        fail("No tag on the reader. Put one on and try again.");
        return;
    }
    if (!nfc_read_tag(h, proto, &t)) { fail("Can't read the tag."); goto out; }
    if (!t.has_raw) { fail("This tag has no writable NTAG pages (not an NTAG215?)."); goto out; }
    if (!memcmp(t.raw, rec, 16)) { logmsg("tag already holds %s", what); goto out; }
    describe(&t, desc);
    if (t.kind && !ask("The tag currently holds:\n\n%s\n\nReplace it with %s?", desc, what)) goto out;
    ok = nfc_write_record(h, proto, rec);
    logmsg(ok ? "wrote %s" : "WRITE FAILED: %s (locked tag? moved?)", what);
    if (!ok) MessageBoxA(WND, "Write failed (locked tag, or it moved). Check the tag and try again.", "o26tag", MB_OK | MB_ICONERROR);
    if (nfc_read_tag(h, proto, &t)) show_tag(TAG_OK, &t);
out:
    SCardDisconnect(h, SCARD_LEAVE_CARD);
}

static int get_int(int id) {
    char buf[16];
    char *end;
    long v;
    GetWindowTextA(CTL(id), buf, sizeof buf);
    v = strtol(buf, &end, 10);
    return buf[0] && !*end && v >= 0 && v <= 255 ? (int)v : -1;
}

static void write_figure(void) {
    int c = get_int(ID_CHARA), t = get_int(ID_TYPE), v = get_int(ID_VARIANT);
    const figure *f = find_figure(c, t, v);
    char what[128];
    uint8_t rec[16];
    if (!f) { fail("Pick a figure from the list (that chara / type / variant isn't in the figure table)."); return; }
    sprintf(what, "figure %s (%d %d %d)", figure_name(f), c, t, v);
    nfc_figure_record(rec, c, t, v);
    write_tag(rec, what);
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

static void write_card(void) {
    char buf[64], what[64];
    uint8_t id[8], rec[16];
    GetWindowTextA(CTL(ID_CARDID), buf, sizeof buf);
    if (!parse_hex(buf, id, 8)) { fail("The card ID must be 16 hex digits, e.g. E004012345678901."); return; }
    sprintf(what, "login card %s", hexs(id, 8, buf));
    nfc_login_record(rec, id);
    write_tag(rec, what);
}

static void erase(void) {
    uint8_t rec[16] = {0};
    if (!ask("Erase pages 4..7 of the tag on the reader?\nIt goes back to being a plain login card (ID from its UID).")) return;
    write_tag(rec, "nothing (erased)");
}

/* ---- figure list ---- */

static void fill_list(void) {
    HWND lv = CTL(ID_LIST);
    char filter[64], num[64];
    LVITEMA it = {0};
    int i, row = 0;
    int vol = (int)SendMessageA(CTL(ID_SERIES), CB_GETCURSEL, 0, 0) - 1;  /* -1 all, 0..NSERIES-1, NSERIES = none */
    GetWindowTextA(CTL(ID_FILTER), filter, sizeof filter);
    SendMessageA(lv, WM_SETREDRAW, FALSE, 0);
    SendMessageA(lv, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < NFIG; i++) {
        const figure *f = &FIG[i];
        if (!f->name[0] || !icontains(f->name, filter)) continue;  /* unnamed rows are unused / not real figures */
        if (vol >= 0 && (vol < NSERIES ? !(f->series >> vol & 1) : f->series != 0)) continue;
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = row++;
        it.iSubItem = 0;
        it.pszText = (char *)f->name;
        it.lParam = i;
        SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
        it.mask = LVIF_TEXT;
        it.iSubItem = 1; it.pszText = (char *)kind_text(f); SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
        it.iSubItem = 2; it.pszText = vol_text(f->series, num); SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
        it.iSubItem = 3; it.pszText = (char *)english_text(f); SendMessageA(lv, LVM_SETITEMA, 0, (LPARAM)&it);
    }
    SendMessageA(lv, WM_SETREDRAW, TRUE, 0);
}

static void set_fields(const figure *f) {
    char num[16];
    sprintf(num, "%d", f->chara); SetWindowTextA(CTL(ID_CHARA), num);
    sprintf(num, "%d", f->type); SetWindowTextA(CTL(ID_TYPE), num);
    sprintf(num, "%d", f->variant); SetWindowTextA(CTL(ID_VARIANT), num);
}

static void select_figure(int chara, int type, int variant) {
    HWND lv = CTL(ID_LIST);
    LVITEMA it = {0};
    LVFINDINFOA fi = {0};
    const figure *f = find_figure(chara, type, variant);
    int row;
    if (!f) return;
    set_fields(f);
    fi.flags = LVFI_PARAM;
    fi.lParam = f - FIG;
    row = (int)SendMessageA(lv, LVM_FINDITEMA, (WPARAM)-1, (LPARAM)&fi);
    if (row < 0) return;
    it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    it.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageA(lv, LVM_SETITEMSTATE, row, (LPARAM)&it);
    SendMessageA(lv, LVM_ENSUREVISIBLE, row, FALSE);
}

/* ---- window ---- */

static HWND add(const char *cls, const char *text, DWORD style, int id) {
    HWND h = CreateWindowExA(!strcmp(cls, "EDIT") || !strcmp(cls, WC_LISTVIEWA) || !strcmp(cls, "LISTBOX") ? WS_EX_CLIENTEDGE : 0,
                             cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 10, 10, WND, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)(id == ID_STATUS ? BIG : FONT), TRUE);
    CTL(id) = h;
    return h;
}

static void place(int id, int x, int y, int w, int h) {
    MoveWindow(CTL(id), x, y, w, h, TRUE);
}

static void layout(int W, int H) {
    const int m = 10, rw = 300, rx = W - m - rw, lw = rx - 2 * m, bot = H - m;
    int y;
    place(ID_READER, m, m, W - 3 * m - 90, 200);
    place(ID_REFRESH, W - m - 90, m - 1, 90, 25);
    place(ID_STATUS, m, 45, W - 2 * m, STATUS_H);

    /* left: figures */
    y = 45 + STATUS_H + 10;
    place(ID_FIGBOX, m, y, lw + m, bot - y);
    place(ID_FILTER_L, 2 * m, y + 24, 40, 20);
    place(ID_FILTER, 2 * m + 42, y + 21, lw - 52 - 140, 22);
    place(ID_SERIES, 2 * m + lw - 140, y + 21, 130, 300);
    place(ID_LIST, 2 * m, y + 50, lw - m, bot - y - 50 - 44);
    place(ID_CHARA_L, 2 * m, bot - 33, 38, 20);
    place(ID_CHARA, 2 * m + 38, bot - 36, 40, 22);
    place(ID_TYPE_L, 2 * m + 86, bot - 33, 32, 20);
    place(ID_TYPE, 2 * m + 118, bot - 36, 40, 22);
    place(ID_VARIANT_L, 2 * m + 166, bot - 33, 42, 20);
    place(ID_VARIANT, 2 * m + 208, bot - 36, 40, 22);
    place(ID_WRITE_FIG, rx - 2 * m - 120, bot - 38, 120, 26);

    /* right: login card, erase, log */
    place(ID_CARDBOX, rx, y, rw, 162);
    place(ID_CARD_L, rx + m, y + 24, rw - 2 * m, 18);
    place(ID_CARDID, rx + m, y + 44, rw - 2 * m, 22);
    place(ID_FROM_TAG, rx + m, y + 72, 135, 25);
    place(ID_RANDOM, rx + rw - m - 135, y + 72, 135, 25);
    place(ID_WRITE_CARD, rx + m, y + 103, rw - 2 * m, 26);
    place(ID_CARD_NOTE, rx + m, y + 133, rw - 2 * m, 24);
    y += 172;
    place(ID_ERASE, rx, y, rw, 28);
    y += 38;
    place(ID_LOG, rx, y, rw, bot - y);
    SendMessageA(CTL(ID_LIST), LVM_ENSUREVISIBLE, SendMessageA(CTL(ID_LIST), LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED), FALSE);
}

static void random_card(void) {
    uint8_t id[8];
    char buf[20];
    int i;
    id[0] = 0xE0; id[1] = 0x04;
    for (i = 2; i < 8; i++) id[i] = (uint8_t)(rand() >> 3);
    SetWindowTextA(CTL(ID_CARDID), hexs(id, 8, buf));
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        LVCOLUMNA col = {0};
        static const char *cols[] = {"Figurine", "Kind", "Series", "English mode"};
        static const int widths[] = {180, 56, 84, 84};
        int i;
        WND = w;
        add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_READER);
        add("BUTTON", "Refresh", BS_PUSHBUTTON | WS_TABSTOP, ID_REFRESH);
        add("EDIT", "", ES_MULTILINE | ES_READONLY, ID_STATUS);
        add("BUTTON", "Write a figure", BS_GROUPBOX, ID_FIGBOX);
        add("STATIC", "Find:", SS_LEFT, ID_FILTER_L);
        add("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_FILTER);
        add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_SERIES);
        SendMessageA(CTL(ID_SERIES), CB_ADDSTRING, 0, (LPARAM)"All series");
        SendMessageA(CTL(ID_SERIES), CB_ADDSTRING, 0, (LPARAM)"Promo");
        for (i = 1; i < NSERIES; i++) {
            char s[24];
            sprintf(s, "Vol. %d%s", i, i >= 3 && i <= 5 ? " (Japan only)" : "");
            SendMessageA(CTL(ID_SERIES), CB_ADDSTRING, 0, (LPARAM)s);
        }
        SendMessageA(CTL(ID_SERIES), CB_ADDSTRING, 0, (LPARAM)"Not in any series");
        SendMessageA(CTL(ID_SERIES), CB_SETCURSEL, 0, 0);
        add(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, ID_LIST);
        SendMessageA(CTL(ID_LIST), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        for (i = 0; i < 4; i++) {
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.cx = widths[i];
            col.pszText = (char *)cols[i];
            SendMessageA(CTL(ID_LIST), LVM_INSERTCOLUMNA, i, (LPARAM)&col);
        }
        add("STATIC", "Chara", SS_LEFT, ID_CHARA_L);
        add("EDIT", "1", ES_NUMBER | WS_TABSTOP, ID_CHARA);
        add("STATIC", "Type", SS_LEFT, ID_TYPE_L);
        add("EDIT", "1", ES_NUMBER | WS_TABSTOP, ID_TYPE);
        add("STATIC", "Variant", SS_LEFT, ID_VARIANT_L);
        add("EDIT", "1", ES_NUMBER | WS_TABSTOP, ID_VARIANT);
        add("BUTTON", "Write figure", BS_PUSHBUTTON | WS_TABSTOP, ID_WRITE_FIG);
        add("BUTTON", "Login card", BS_GROUPBOX, ID_CARDBOX);
        add("STATIC", "Card ID (16 hex digits):", SS_LEFT, ID_CARD_L);
        add("EDIT", "", ES_UPPERCASE | ES_AUTOHSCROLL | WS_TABSTOP, ID_CARDID);
        SendMessageA(CTL(ID_CARDID), EM_SETLIMITTEXT, 16, 0);
        add("BUTTON", "ID from tag's UID", BS_PUSHBUTTON | WS_TABSTOP, ID_FROM_TAG);
        add("BUTTON", "Random ID", BS_PUSHBUTTON | WS_TABSTOP, ID_RANDOM);
        add("BUTTON", "Write login card", BS_PUSHBUTTON | WS_TABSTOP, ID_WRITE_CARD);
        add("STATIC", "Blank tags already log in with the UID's ID.", SS_LEFT, ID_CARD_NOTE);
        add("BUTTON", "Erase tag", BS_PUSHBUTTON | WS_TABSTOP, ID_ERASE);
        add("LISTBOX", "", LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL, ID_LOG);
        /* group boxes under everything (new windows go to the bottom, so do this last) */
        SetWindowPos(CTL(ID_FIGBOX), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(CTL(ID_CARDBOX), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        fill_list();
        select_figure(1, 1, 1);
        show_tag(TAG_NOREADER, NULL);
        list_readers();
        CloseHandle(CreateThread(NULL, 0, monitor, NULL, 0, NULL));
        return 0;
    }
    case WM_SIZE:
        layout(LOWORD(lp), HIWORD(lp));
        RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);  /* group boxes leave ghosts otherwise */
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 760;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 520;
        return 0;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == CTL(ID_STATUS)) {  /* status box: white, like an edit */
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_TAG:
        show_tag((int)wp, (nfc_tag *)lp);
        free((void *)lp);
        return 0;
    case WM_READERS:
        list_readers();
        return 0;
    case WM_NOTIFY: {
        NMLISTVIEW *nm = (NMLISTVIEW *)lp;
        if (nm->hdr.idFrom == ID_LIST && nm->hdr.code == LVN_ITEMCHANGED && (nm->uNewState & LVIS_SELECTED))
            set_fields(&FIG[nm->lParam]);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_READER:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                char name[256] = "";
                int i = (int)SendMessageA(CTL(ID_READER), CB_GETCURSEL, 0, 0);
                SendMessageA(CTL(ID_READER), CB_GETLBTEXT, i, (LPARAM)name);
                if (name[0] != '(') select_reader(name);
            }
            break;
        case ID_REFRESH: list_readers(); break;
        case ID_FILTER: if (HIWORD(wp) == EN_CHANGE) fill_list(); break;
        case ID_SERIES: if (HIWORD(wp) == CBN_SELCHANGE) fill_list(); break;
        case ID_WRITE_FIG: write_figure(); break;
        case ID_WRITE_CARD: write_card(); break;
        case ID_ERASE: erase(); break;
        case ID_RANDOM: random_card(); break;
        case ID_FROM_TAG:
            if (HAVE_LAST) {
                uint8_t id[8];
                char buf[20];
                nfc_derive_card_id(&LAST, id);
                SetWindowTextA(CTL(ID_CARDID), hexs(id, 8, buf));
            } else fail("Put a tag on the reader first.");
            break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    NONCLIENTMETRICSA ncm = {sizeof ncm};
    WNDCLASSA wc = {0};
    MSG m;
    HWND w;
    (void)prev; (void)cmd;
    InitCommonControlsEx(&icc);
    InitializeCriticalSection(&CS);
    srand(GetTickCount());
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    FONT = CreateFontIndirectA(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 4 / 3;
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    BIG = CreateFontIndirectA(&ncm.lfMessageFont);
    {
        HDC dc = GetDC(NULL);
        TEXTMETRICA tm;
        HGDIOBJ old = SelectObject(dc, BIG);
        GetTextMetricsA(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(NULL, dc);
        STATUS_H = 3 * tm.tmHeight + 10;
    }
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = "o26tag";
    RegisterClassA(&wc);
    w = CreateWindowExA(0, "o26tag", "o26tag - Tsum Tsum NFC tags", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, 800, 640, NULL, NULL, inst, NULL);
    ShowWindow(w, show);
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
