/*
 * The mascot figure table, built in (figure_table.h, generated from tmac.dll by scripts/gen_figure_table.py),
 * so o26tag / o26tag_gui need no game files. Shared by both; header-only.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* series / series_en: bit n = in figurine series ("vol") n, Japanese / English-region (A, Y) numbering. */
typedef struct { int chara, type, variant; uint32_t id; const char *name; uint32_t series, series_en; } figure;

#include "figure_table.h"

/* "3, 12" (bits of a series mask), or "-" */
static char *series_text(uint32_t mask, char *out) {
    int i, n = 0;
    out[0] = 0;
    for (i = 0; i < NSERIES; i++)
        if (mask >> i & 1) n += sprintf(out + n, n ? ", %d" : "%d", i);
    if (!n) strcpy(out, "-");
    return out;
}

static const figure *find_figure(int c, int t, int v) {
    int i;
    for (i = 0; i < NFIG; i++)
        if (FIG[i].chara == c && FIG[i].type == t && FIG[i].variant == v) return &FIG[i];
    return NULL;
}

static const char *figure_name(const figure *f) {
    return f->name[0] ? f->name : "(unnamed)";
}

static int icontains(const char *hay, const char *needle) {
    size_t i, j, n = strlen(needle);
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < n && hay[i + j] && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j]); j++);
        if (j == n) return 1;
    }
    return !n;
}
