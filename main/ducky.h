/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ducky.h
 * @brief DuckyScript -> profile text. A thin translator so Rubber Ducky / EvilCrow
 *        payloads drive the Dot's existing keyboard player (script.c) and HID (hidkb.c).
 *
 * Pure C, no SDK: it writes profile text that sc_parse() then checks and the player runs,
 * so the whole proven engine (arming, plug, batch, the web editor, the glass) is reused
 * unchanged. The host test drives the full pipeline against a fake PC.
 *
 * What is translated (Hak5 DuckyScript 1.0 and the common 2.0 subset):
 *
 *   REM <text>                      -> "# <text>"              (a comment)
 *   REM_BLOCK .. END_REM            -> dropped                 (block comment)
 *   STRING <text>                   -> "type <text>"           (long text is split)
 *   STRINGLN <text>                 -> "line <text>"           (adds ENTER)
 *   STRING_BLOCK .. END_STRING      -> one "line" per inner row, last row "type"
 *   STRINGLN_BLOCK .. END_STRINGLN  -> one "line" per inner row
 *   DELAY <ms>                      -> "wait <ms>"
 *   DEFAULTDELAY / DEFAULT_DELAY n  -> "gap <ms>"              (approx: between keys)
 *   DEFAULT_CHAR_DELAY / CHARDELAY  -> "gap <ms>"
 *   REPEAT <n>                      -> the previous line, n more times
 *   a key combo (GUI r, CTRL ALT DELETE, ENTER, CTRL-ALT-DEL, ...) -> "keys <combo>"
 *
 * Key names are mapped to the player's (GUI/WINDOWS/COMMAND -> WIN, CONTROL -> CTRL,
 * DELETE -> DEL, DOWNARROW -> DOWN, ESCAPE -> ESC, ...) and validated with keys_parse().
 * A line that cannot be translated becomes "# [ducky] <reason>: <line>" and raises a
 * warning; nothing is dropped silently. Unsupported 2.0 features (VAR, IF, WHILE,
 * FUNCTION, HOLD/RELEASE, LED*, ATTACKMODE, $vars) warn and are commented out.
 */
#ifndef DUCKY_H_
#define DUCKY_H_

#include <stdbool.h>
#include <stddef.h>

#include "script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DUCKY_WARNS 16  /**< warnings kept (the count can be higher) */

typedef struct {
    int  line;          /**< 1-based line in the DuckyScript */
    char msg[64];
} ducky_warn_t;

typedef struct {
    int          nwarn;              /**< warnings in all, even past DUCKY_WARNS */
    ducky_warn_t warn[DUCKY_WARNS];
    bool         overflow;           /**< the output buffer was too small; result truncated */
    int          cmds;              /**< profile command lines emitted (the player caps at 64) */
} ducky_res_t;

/**
 * @brief Translate DuckyScript @p in to profile text in @p out.
 * @param in   NUL-terminated DuckyScript (CRLF is tolerated).
 * @param out  Filled with profile text, NUL-terminated.
 * @param cap  Size of @p out.
 * @param res  Warnings and flags (may be NULL).
 * @return Bytes written to @p out (excluding the NUL). On overflow the text is truncated,
 *         res->overflow is set, and the bytes that fit are still valid profile text.
 */
int ducky_to_profile(const char *in, char *out, size_t cap, ducky_res_t *res);

/** @brief A cheap guess that @p text is DuckyScript, not the profile language: a leading
 *  STRING/STRINGLN/DELAY/GUI/REM/ENTER token. Lets the app pick a loader per file. */
bool ducky_detect(const char *text);

/**
 * @brief Parse @p text into @p p whether it is DuckyScript or the profile language.
 *
 * If @p text looks like DuckyScript it is translated into @p scratch (its warnings go to
 * @p res) and the profile text is parsed; otherwise @p text is parsed as-is. Keeps the card
 * file in whichever language the user wrote, converting only on the way to the player.
 *
 * @param scratch A buffer the translation is written to (STORE_TEXT_MAX is a good size).
 * @return The parse error count (p->nerr).
 */
int ducky_parse(sc_prog_t *p, const char *text, char *scratch, size_t scap, ducky_res_t *res);

#ifdef __cplusplus
}
#endif

#endif /* DUCKY_H_ */
