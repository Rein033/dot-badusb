/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file sim_stubs.c
 * @brief Stubs for the kode-sim host build (IDF_TARGET linux) only.
 *
 * The simulator has no radio, so the SDK sources that define the Wi-Fi requests are not
 * built. app.c calls them when the web editor is toggled; here they are no-ops so the app
 * links and runs. On a real chip these come from the SDK and this file is not compiled.
 */
void wifi_start_request(void) {}
void wifi_state_request(void) {}
