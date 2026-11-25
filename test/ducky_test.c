/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * Host test of the DuckyScript translator and the whole pipeline:
 *   DuckyScript --ducky_to_profile--> profile text --sc_parse--> sc_player --> a fake PC.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -I../main ducky_test.c \
 *      ../main/ducky.c ../main/script.c ../main/keys.c && ./a.out
 */
#include <stdio.h>
#include <string.h>

#include "ducky.h"
#include "keys.h"
#include "script.h"

static int fails;
#define CHECK(c)                                                              \
    do {                                                                      \
        if ( !(c) ) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
    } while ( 0 )

/* ---- a fake PC (as in script_test.c) --------------------------------------------------- */

typedef struct {
    bool up;
    int n;
    kstroke_t k[1024];
    uint16_t down[1024];
    uint32_t now;
    int ev[16];
} pc_t;

static bool f_ready(void *c) { return ((pc_t *)c)->up; }
static int f_pending(void *c) { (void)c; return 0; }
static bool f_send(void *c, kstroke_t k, uint16_t d)
{
    pc_t *pc = c;
    if ( pc->n < 1024 ) { pc->k[pc->n] = k; pc->down[pc->n] = d; pc->n++; }
    return true;
}
static void f_flush(void *c) { (void)c; }
static void f_event(void *c, sc_event_t ev, const char *t) { (void)t; ((pc_t *)c)->ev[ev]++; }

static pc_t pc;
static sc_prog_t prog;
static sc_player_t pl;
static char profile[4096];
static ducky_res_t res;

/* Translate ducky, parse, run to completion; return profile parse-error count. */
static int run_ducky(const char *ducky)
{
    memset(&pc, 0, sizeof(pc));
    pc.up = true;
    ducky_to_profile(ducky, profile, sizeof(profile), &res);
    const int e = sc_parse(&prog, profile);
    if ( e )
    {
        printf("  profile parse error: line %d: %s\n  --profile--\n%s\n",
               prog.err[0].line, prog.err[0].msg, profile);
    }
    const sc_io_t io = { f_ready, f_pending, f_send, f_flush, f_event, &pc };
    sc_player_init(&pl, &io);
    sc_load(&pl, &prog);
    sc_start(&pl, pc.now);
    for ( int t = 0; t < 12000; t += 10 )
    {
        pc.now += 10;
        sc_tick(&pl, pc.now);
    }
    return e;
}

static bool stroke(int i, uint8_t mod, uint8_t key)
{
    return (i >= 0) && (i < pc.n) && (pc.k[i].mod == mod) && (pc.k[i].key == key);
}

/* count strokes of a given key (any mod) */
static int count_key(uint8_t key)
{
    int c = 0;
    for ( int i = 0; i < pc.n; ++i ) { if ( pc.k[i].key == key ) c++; }
    return c;
}

/* ---- tests ----------------------------------------------------------------------------- */

/* The classic BadUSB payload: Run dialog -> powershell. This is the "RCE, not F12" case. */
static void t_classic_rce(void)
{
    const int e = run_ducky(
        "REM open run and launch a shell\n"
        "DEFAULTDELAY 0\n"
        "DELAY 500\n"
        "GUI r\n"
        "DELAY 300\n"
        "STRING powershell\n"
        "ENTER\n");
    CHECK(0 == e);
    CHECK(!res.overflow);
    /* WIN+r , then p-o-w-e-r-s-h-e-l-l (10) , then ENTER = 12 strokes */
    CHECK(12 == pc.n);
    CHECK(stroke(0, KEY_MOD_LGUI, 0x15));        /* WIN+r */
    CHECK(stroke(1, 0, 0x13));                   /* 'p' */
    CHECK(stroke(11, 0, 0x28));                  /* ENTER */
    CHECK(strstr(profile, "keys WIN+r"));
    CHECK(strstr(profile, "type powershell"));
}

/* ducky_parse: DuckyScript is converted, the profile language passes through unchanged. */
static void t_ducky_parse_wrapper(void)
{
    char scratch[4096];
    int e = ducky_parse(&prog, "GUI r\nSTRING cmd\nENTER\n", scratch, sizeof(scratch), &res);
    CHECK(0 == e);
    CHECK(3 == prog.n);            /* keys WIN+r ; type cmd ; keys ENTER */
    e = ducky_parse(&prog, "title T\nkeys F10\nspam F10 10\n", scratch, sizeof(scratch), &res);
    CHECK(0 == e);
    CHECK(0 == strcmp(prog.title, "T"));
    CHECK(0 == res.nwarn);         /* not treated as DuckyScript */
}

/* v1 dash-separated combo. */
static void t_v1_dash_combo(void)
{
    const int e = run_ducky("DEFAULTDELAY 0\nCONTROL-ALT-DELETE\n");
    CHECK(0 == e);
    CHECK(1 == pc.n);
    CHECK(stroke(0, KEY_MOD_LCTRL | KEY_MOD_LALT, 0x4C)); /* CTRL+ALT+DEL */
    CHECK(strstr(profile, "keys CTRL+ALT+DEL"));
}

/* GUI alone is a lone Super tap; linux terminal combo CTRL ALT t. */
static void t_lone_and_letter_combo(void)
{
    const int e = run_ducky("DEFAULTDELAY 0\nGUI\nCTRL ALT t\n");
    CHECK(0 == e);
    CHECK(2 == pc.n);
    CHECK(stroke(0, KEY_MOD_LGUI, 0x00));                       /* WIN tap */
    CHECK(stroke(1, KEY_MOD_LCTRL | KEY_MOD_LALT, 0x17));       /* CTRL+ALT+t */
}

static void t_stringln_enter(void)
{
    const int e = run_ducky("DEFAULTDELAY 0\nSTRINGLN hi\n");
    CHECK(0 == e);
    CHECK(3 == pc.n);                 /* h i ENTER */
    CHECK(stroke(2, 0, 0x28));
    /* empty STRINGLN is just ENTER */
    (void)run_ducky("DEFAULTDELAY 0\nSTRINGLN\n");
    CHECK(1 == pc.n);
    CHECK(stroke(0, 0, 0x28));
}

/* A STRING longer than the per-line text cap is split, loses no characters. */
static void t_long_string_split(void)
{
    char big[300];
    strcpy(big, "DEFAULTDELAY 0\nSTRING ");
    for ( int i = 0; i < 200; ++i ) { strncat(big, "x", sizeof(big) - strlen(big) - 1); }
    strncat(big, "\n", sizeof(big) - strlen(big) - 1);
    const int e = run_ducky(big);
    CHECK(0 == e);
    CHECK(200 == pc.n);
    CHECK(200 == count_key(0x1B)); /* 'x' */
    CHECK(res.cmds >= 3);          /* 200 / 90 -> 3 lines */
}

/* REPEAT re-runs the previous instruction. */
static void t_repeat(void)
{
    const int e = run_ducky("DEFAULTDELAY 0\nSTRING a\nREPEAT 3\n");
    CHECK(0 == e);
    CHECK(4 == pc.n);              /* original + 3 */
    CHECK(4 == count_key(0x04));  /* 'a' */
}

/* Unsupported lines are commented out and warned, never silently executed. */
static void t_unsupported_warns(void)
{
    const int e = run_ducky(
        "DEFAULTDELAY 0\n"
        "ATTACKMODE HID STORAGE\n"
        "STRING ok\n");
    CHECK(0 == e);                 /* the profile still parses */
    CHECK(res.nwarn >= 1);
    CHECK(strstr(profile, "# [ducky] ATTACKMODE HID STORAGE"));
    CHECK(2 == pc.n);              /* only "ok" typed */
}

/* REM_BLOCK body is dropped. */
static void t_rem_block(void)
{
    const int e = run_ducky(
        "DEFAULTDELAY 0\n"
        "REM_BLOCK\n"
        "GUI r\n"              /* inside the block: ignored */
        "secret notes\n"
        "END_REM\n"
        "STRING z\n");
    CHECK(0 == e);
    CHECK(1 == pc.n);          /* only 'z' */
    CHECK(stroke(0, 0, 0x1D)); /* 'z' */
}

/* STRINGLN_BLOCK: each row typed with ENTER. */
static void t_strln_block(void)
{
    const int e = run_ducky(
        "DEFAULTDELAY 0\n"
        "STRINGLN_BLOCK\n"
        "ab\n"
        "cd\n"
        "END_STRINGLN\n");
    CHECK(0 == e);
    CHECK(6 == pc.n);          /* a b ENTER c d ENTER */
    CHECK(stroke(2, 0, 0x28) && stroke(5, 0, 0x28));
}

static void t_detect(void)
{
    CHECK(ducky_detect("REM hi\nGUI r\n"));
    CHECK(ducky_detect("STRING hello\n"));
    CHECK(ducky_detect("DELAY 500\nENTER\n"));
    CHECK(!ducky_detect("title X\nkeys F10\nspam F10 10\n")); /* the profile language */
    CHECK(!ducky_detect("arm plug\nconfirm on\n"));
}

/* A real-world reverse-shell-style payload parses and runs end to end. */
static void t_realistic_payload(void)
{
    const int e = run_ducky(
        "REM Windows hidden powershell one-liner\n"
        "DEFAULTDELAY 50\n"
        "DELAY 1000\n"
        "GUI r\n"
        "DELAY 500\n"
        "STRINGLN powershell -w hidden -nop -c \"IEX(New-Object Net.WebClient)\"\n"
        "DELAY 200\n");
    CHECK(0 == e);
    CHECK(!res.overflow);
    CHECK(0 == res.nwarn);
    CHECK(stroke(0, KEY_MOD_LGUI, 0x15));           /* WIN+r first */
    CHECK(stroke(pc.n - 1, 0, 0x28));               /* ENTER last (STRINGLN) */
    CHECK(strstr(profile, "gap 50"));
}

int main(void)
{
    t_classic_rce();
    t_ducky_parse_wrapper();
    t_v1_dash_combo();
    t_lone_and_letter_combo();
    t_stringln_enter();
    t_long_string_split();
    t_repeat();
    t_unsupported_warns();
    t_rem_block();
    t_strln_block();
    t_detect();
    t_realistic_payload();
    printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
