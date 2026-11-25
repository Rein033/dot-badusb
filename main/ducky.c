/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ducky.c
 * @brief DuckyScript -> profile text (see ducky.h).
 */
#include "ducky.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "keys.h"

#define TXT_CHUNK 90   /* longest text in one `type`/`line` (profile c->s is 96) */

/* ---- output buffer --------------------------------------------------------------------- */

typedef struct {
    char   *buf;
    size_t  cap;
    size_t  len;
    bool    of;        /* overflowed */
    /* byte range [last_start, last_start+last_len) of the last instruction line, for REPEAT */
    size_t  last_start;
    size_t  last_len;
    bool    have_last;
    int     cmds;      /* instruction lines emitted (not comments/settings) */
} out_t;

static void out_putc(out_t *o, char c)
{
    if ( o->len + 1 < o->cap )
    {
        o->buf[o->len++] = c;
    }
    else
    {
        o->of = true;
    }
}

static void out_str(out_t *o, const char *s)
{
    for ( ; *s; ++s )
    {
        out_putc(o, *s);
    }
}

/* Emit one line (adds '\n'). is_cmd marks a translatable instruction (counts, repeatable). */
static void emit(out_t *o, const char *s, bool is_cmd)
{
    const size_t start = o->len;
    out_str(o, s);
    out_putc(o, '\n');
    if ( is_cmd )
    {
        o->last_start = start;
        o->last_len = o->len - start;
        o->have_last = true;
        o->cmds++;
    }
}

/* ---- warnings -------------------------------------------------------------------------- */

static void warn(ducky_res_t *r, int line, const char *fmt, const char *a)
{
    if ( !r )
    {
        return;
    }
    if ( r->nwarn < DUCKY_WARNS )
    {
        ducky_warn_t *w = &r->warn[r->nwarn];
        w->line = line;
        (void)snprintf(w->msg, sizeof(w->msg), fmt, a ? a : "");
    }
    r->nwarn++;
}

/* A line that could not be translated: keep it as a comment so nothing is silent. */
static void comment_out(out_t *o, const char *raw)
{
    out_str(o, "# [ducky] ");
    out_str(o, raw);
    out_putc(o, '\n');
}

/* ---- small string helpers -------------------------------------------------------------- */

static const char *skip_ws(const char *s)
{
    while ( *s && isspace((unsigned char)*s) )
    {
        ++s;
    }
    return s;
}

/* Copy the first word of s into w; return the pointer just past it. */
static const char *first_word(const char *s, char *w, size_t cap)
{
    const char *p = skip_ws(s);
    size_t n = 0;
    while ( *p && !isspace((unsigned char)*p) )
    {
        if ( n + 1 < cap )
        {
            w[n++] = *p;
        }
        ++p;
    }
    w[n] = 0;
    return p;
}

static void upper(char *s)
{
    for ( ; *s; ++s )
    {
        *s = (char)toupper((unsigned char)*s);
    }
}

static bool to_num(const char *s, long *out)
{
    s = skip_ws(s);
    if ( !*s )
    {
        return false;
    }
    char *end = NULL;
    const long v = strtol(s, &end, 10);
    end = (char *)skip_ws(end);
    if ( *end || (v < 0) )
    {
        return false;
    }
    *out = v;
    return true;
}

/* ---- key-name mapping ------------------------------------------------------------------ */

/* DuckyScript key name (upper) -> the player's keys.c name. NULL if it passes through. */
static const char *map_key(const char *u)
{
    static const struct { const char *d; const char *k; } T[] = {
        { "WINDOWS", "WIN" }, { "GUI", "WIN" }, { "COMMAND", "WIN" }, { "META", "WIN" },
        { "CONTROL", "CTRL" }, { "OPTION", "ALT" },
        { "RETURN", "ENTER" }, { "ESCAPE", "ESC" },
        { "DELETE", "DEL" }, { "BACKSPACE", "BKSP" },
        { "SPACEBAR", "SPACE" },
        { "PAGEUP", "PGUP" }, { "PAGEDOWN", "PGDN" },
        { "UPARROW", "UP" }, { "DOWNARROW", "DOWN" },
        { "LEFTARROW", "LEFT" }, { "RIGHTARROW", "RIGHT" },
        { "PRINTSCREEN", "PRTSC" }, { "PRINT", "PRTSC" },
        { "BREAK", "PAUSE" }, { "CONTROLBREAK", "PAUSE" },
        { "APP", "MENU" }, { "APPLICATION", "MENU" },
        { "CAPS", "CAPSLOCK" },
    };
    for ( size_t i = 0; i < sizeof(T) / sizeof(T[0]); ++i )
    {
        if ( 0 == strcmp(u, T[i].d) )
        {
            return T[i].k;
        }
    }
    return NULL;
}

static bool is_mod(const char *k)
{
    return (0 == strcmp(k, "CTRL")) || (0 == strcmp(k, "ALT")) || (0 == strcmp(k, "SHIFT"))
        || (0 == strcmp(k, "WIN")) || (0 == strcmp(k, "RCTRL")) || (0 == strcmp(k, "RALT"))
        || (0 == strcmp(k, "RSHIFT")) || (0 == strcmp(k, "RGUI")) || (0 == strcmp(k, "ALTGR"));
}

/* Normalise one DuckyScript token to a player token in out (cap). A single character keeps
 * its case (so `r` is r, not SHIFT+r); a name is upper-cased and mapped. */
static void norm_token(const char *tok, char *out, size_t cap)
{
    if ( 1 == strlen(tok) )
    {
        (void)snprintf(out, cap, "%s", tok);
        return;
    }
    char u[32];
    (void)snprintf(u, sizeof(u), "%s", tok);
    upper(u);
    const char *m = map_key(u);
    (void)snprintf(out, cap, "%s", m ? m : u);
}

/* Split a key line into tokens: on spaces if any, else on '-' (v1 CTRL-ALT-DEL), else one.
 * Returns the token count; tokens point into a working copy wc. */
static int split_combo(char *wc, char *tok[], int max)
{
    const char sep = strchr(wc, ' ') ? ' ' : (strchr(wc + 1, '-') ? '-' : 0);
    int n = 0;
    if ( 0 == sep )
    {
        tok[n++] = wc;
        return n;
    }
    char *p = wc;
    tok[n++] = p;
    for ( ; *p && (n < max); ++p )
    {
        if ( (*p == sep) || (' ' == sep && isspace((unsigned char)*p)) )
        {
            *p = 0;
            if ( p[1] && (n < max) )
            {
                tok[n++] = p + 1;
            }
        }
    }
    /* drop empties from doubled separators */
    int m = 0;
    for ( int i = 0; i < n; ++i )
    {
        if ( tok[i][0] )
        {
            tok[m++] = tok[i];
        }
    }
    return m;
}

/* Translate a key line into "KEYS combo". false (with a warning) if no valid key. */
static bool key_combo(const char *raw, char *out, size_t cap, ducky_res_t *r, int ln)
{
    char wc[128];
    (void)snprintf(wc, sizeof(wc), "%s", raw);
    char *tok[16];
    const int n = split_combo(wc, tok, 16);

    char mods[96] = { 0 };
    char key[32] = { 0 };
    int nkeys = 0;
    for ( int i = 0; i < n; ++i )
    {
        char t[32];
        norm_token(tok[i], t, sizeof(t));
        if ( is_mod(t) && ((i + 1 < n) || (0 == i && n == 1)) )
        {
            /* a modifier, unless it is the sole token and the lone key (handled below) */
            if ( (i + 1 < n) )
            {
                (void)strncat(mods, t, sizeof(mods) - strlen(mods) - 1);
                (void)strncat(mods, "+", sizeof(mods) - strlen(mods) - 1);
                continue;
            }
        }
        if ( key[0] )
        {
            nkeys = 99; /* more than one non-modifier key: invalid combo */
            break;
        }
        (void)snprintf(key, sizeof(key), "%s", t);
        nkeys++;
    }
    if ( nkeys > 1 )
    {
        warn(r, ln, "not one keystroke: %s", raw);
        return false;
    }

    char combo[128];
    (void)snprintf(combo, sizeof(combo), "%s%s", mods, key);
    /* a mods-only combo keeps a trailing '+': drop it */
    size_t cl = strlen(combo);
    if ( cl && ('+' == combo[cl - 1]) )
    {
        combo[cl - 1] = 0;
    }
    kstroke_t k;
    if ( !combo[0] || !keys_parse(combo, &k) )
    {
        warn(r, ln, "unknown key: %s", raw);
        return false;
    }
    (void)snprintf(out, cap, "keys %s", combo);
    return true;
}

/* ---- STRING chunking ------------------------------------------------------------------- */

/* Emit STRING/STRINGLN text, split so no profile line is over the player's text cap.
 * newline=true adds ENTER after the last chunk (STRINGLN). */
static void emit_text(out_t *o, const char *text, bool newline, ducky_res_t *r, int ln)
{
    if ( ' ' == text[0] )
    {
        warn(r, ln, "leading spaces dropped; use STRING_BLOCK", NULL);
    }
    if ( !text[0] )
    {
        if ( newline )
        {
            emit(o, "keys ENTER", true);
        }
        return;
    }
    const size_t total = strlen(text);
    size_t i = 0;
    char line[8 + TXT_CHUNK];
    while ( i < total )
    {
        size_t take = total - i;
        if ( take > TXT_CHUNK )
        {
            take = TXT_CHUNK;
        }
        const bool last = (i + take >= total);
        const char *verb = (last && newline) ? "line " : "type ";
        (void)snprintf(line, sizeof(line), "%s%.*s", verb, (int)take, text + i);
        emit(o, line, true);
        i += take;
    }
}

/* ---- the translator -------------------------------------------------------------------- */

/* The literal argument of a command: everything after the keyword and exactly one space. */
static const char *arg_after(const char *line, const char *past_word)
{
    (void)line;
    if ( ' ' == *past_word || '\t' == *past_word )
    {
        return past_word + 1;
    }
    return past_word; /* no argument */
}

int ducky_to_profile(const char *in, char *out, size_t cap, ducky_res_t *res)
{
    out_t o = { out, cap, 0, false, 0, 0, false, 0 };
    if ( res )
    {
        memset(res, 0, sizeof(*res));
    }

    bool in_rem_block = false;
    bool in_str_block = false;   /* STRING_BLOCK: last row is `type` */
    bool in_strln_block = false; /* STRINGLN_BLOCK: every row is `line` */

    int ln = 0;
    const char *p = in;
    while ( *p )
    {
        /* one raw line into buf (strip CR) */
        char raw[256];
        size_t rn = 0;
        while ( *p && ('\n' != *p) )
        {
            if ( ('\r' != *p) && (rn + 1 < sizeof(raw)) )
            {
                raw[rn++] = *p;
            }
            ++p;
        }
        raw[rn] = 0;
        if ( '\n' == *p )
        {
            ++p;
        }
        ln++;

        /* block bodies first */
        if ( in_rem_block )
        {
            char w[32];
            (void)first_word(raw, w, sizeof(w));
            upper(w);
            if ( 0 == strcmp(w, "END_REM") )
            {
                in_rem_block = false;
            }
            continue;
        }
        if ( in_str_block || in_strln_block )
        {
            char w[32];
            (void)first_word(raw, w, sizeof(w));
            char wu[32];
            (void)snprintf(wu, sizeof(wu), "%s", w);
            upper(wu);
            if ( (in_str_block && 0 == strcmp(wu, "END_STRING"))
              || (in_strln_block && 0 == strcmp(wu, "END_STRINGLN")) )
            {
                in_str_block = in_strln_block = false;
                continue;
            }
            emit_text(&o, raw, true, res, ln); /* every block row ends with ENTER */
            continue;
        }

        const char *rest0 = skip_ws(raw);
        if ( !*rest0 )
        {
            continue; /* blank */
        }

        char cmd[20];
        const char *past = first_word(rest0, cmd, sizeof(cmd));
        char cu[20];
        (void)snprintf(cu, sizeof(cu), "%s", cmd);
        upper(cu);
        const char *arg = arg_after(rest0, past);

        if ( 0 == strcmp(cu, "REM") )
        {
            out_str(&o, "# ");
            out_str(&o, arg);
            out_putc(&o, '\n');
        }
        else if ( 0 == strcmp(cu, "REM_BLOCK") )
        {
            in_rem_block = true;
        }
        else if ( 0 == strcmp(cu, "STRING") )
        {
            emit_text(&o, arg, false, res, ln);
        }
        else if ( 0 == strcmp(cu, "STRINGLN") )
        {
            emit_text(&o, arg, true, res, ln);
        }
        else if ( 0 == strcmp(cu, "STRING_BLOCK") )
        {
            in_str_block = true;
        }
        else if ( 0 == strcmp(cu, "STRINGLN_BLOCK") )
        {
            in_strln_block = true;
        }
        else if ( 0 == strcmp(cu, "DELAY") )
        {
            long ms = 0;
            if ( !to_num(arg, &ms) || (ms > 600000) )
            {
                warn(res, ln, "DELAY needs 0..600000 ms", NULL);
                comment_out(&o, raw);
            }
            else
            {
                char l[32];
                (void)snprintf(l, sizeof(l), "wait %ld", ms);
                emit(&o, l, true);
            }
        }
        else if ( (0 == strcmp(cu, "DEFAULTDELAY")) || (0 == strcmp(cu, "DEFAULT_DELAY"))
               || (0 == strcmp(cu, "DEFAULT_CHAR_DELAY")) || (0 == strcmp(cu, "DEFAULTCHARDELAY"))
               || (0 == strcmp(cu, "CHARDELAY")) )
        {
            long ms = 0;
            if ( !to_num(arg, &ms) || (ms > 5000) )
            {
                warn(res, ln, "default delay needs 0..5000 ms", NULL);
                comment_out(&o, raw);
            }
            else
            {
                char l[32];
                (void)snprintf(l, sizeof(l), "gap %ld", ms);
                emit(&o, l, false); /* a setting, not a repeatable instruction */
            }
        }
        else if ( 0 == strcmp(cu, "REPEAT") )
        {
            long nrep = 0;
            if ( !to_num(arg, &nrep) || (nrep < 1) || (nrep > 999) )
            {
                warn(res, ln, "REPEAT needs 1..999", NULL);
            }
            else if ( !o.have_last )
            {
                warn(res, ln, "REPEAT with nothing before it", NULL);
            }
            else
            {
                /* copy the stored last line out first (the buffer may move under us: it does
                 * not here, but copy to be safe), then re-append it nrep times */
                char prev[128];
                size_t pl = o.last_len;
                if ( pl >= sizeof(prev) )
                {
                    pl = sizeof(prev) - 1;
                }
                memcpy(prev, o.buf + o.last_start, pl);
                prev[pl] = 0;
                for ( long i = 0; i < nrep; ++i )
                {
                    out_str(&o, prev); /* prev already ends in '\n' */
                    o.cmds++;
                }
            }
        }
        else
        {
            /* a key / combo line */
            char l[160];
            if ( key_combo(rest0, l, sizeof(l), res, ln) )
            {
                emit(&o, l, true);
            }
            else
            {
                comment_out(&o, raw);
            }
        }
    }

    o.buf[(o.len < o.cap) ? o.len : (o.cap ? o.cap - 1 : 0)] = 0;
    if ( res )
    {
        res->overflow = o.of;
        res->cmds = o.cmds;
    }
    return (int)o.len;
}

int ducky_parse(sc_prog_t *p, const char *text, char *scratch, size_t scap, ducky_res_t *res)
{
    if ( ducky_detect(text) && scratch && scap )
    {
        ducky_to_profile(text, scratch, scap, res);
        return sc_parse(p, scratch);
    }
    if ( res )
    {
        memset(res, 0, sizeof(*res));
    }
    return sc_parse(p, text);
}

bool ducky_detect(const char *text)
{
    static const char *HINTS[] = {
        "STRING", "STRINGLN", "DELAY", "GUI", "WINDOWS", "REM", "ENTER",
        "DEFAULTDELAY", "CONTROL", "STRING_BLOCK", "REPEAT",
    };
    /* look at the first few non-blank lines */
    const char *p = text;
    for ( int seen = 0; *p && (seen < 5); )
    {
        const char *s = skip_ws(p);
        char w[20];
        const char *past = first_word(s, w, sizeof(w));
        if ( w[0] )
        {
            seen++;
            upper(w);
            for ( size_t i = 0; i < sizeof(HINTS) / sizeof(HINTS[0]); ++i )
            {
                if ( 0 == strcmp(w, HINTS[i]) )
                {
                    return true;
                }
            }
        }
        /* next line */
        p = past;
        while ( *p && ('\n' != *p) )
        {
            ++p;
        }
        if ( '\n' == *p )
        {
            ++p;
        }
    }
    return false;
}
