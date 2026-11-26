/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hidkb_sim.c
 * @brief hidkb.h for the kode-sim host build (IDF_TARGET linux), where there is no USB.
 *
 * The real keyboard (hidkb.c) needs TinyUSB and the P4 OTG PHY, neither of which the
 * simulator has. This stub stands in so app.c and web.c link and run under the sim: it
 * models a PC that is always attached and accepts every stroke at once, so a payload can
 * be watched running on the glass. Nothing leaves the process. Each stroke is printed, so
 * the sim console shows what a real PC would have been typed.
 */
#include "hidkb.h"

#include <stdio.h>

static hidkb_port_t s_port = HIDKB_PORT_DOCK;
static bool         s_up;

bool hidkb_start(hidkb_port_t port)
{
    s_port = port;
    s_up = true;
    return true;
}

bool hidkb_active(void) { return s_up; }
hidkb_port_t hidkb_port(void) { return s_port; }

/* A PC is always present in the sim, so manual payloads run to the end. */
bool hidkb_ready(void) { return s_up; }

int  hidkb_pending(void) { return 0; }   /* sent at once */
void hidkb_flush(void) {}

bool hidkb_send(kstroke_t k, uint16_t down_ms)
{
    printf("[sim-hid] mod=0x%02x key=0x%02x down=%ums\n", k.mod, k.key, down_ms);
    return true;
}

uint8_t hidkb_leds(void) { return 0; }
