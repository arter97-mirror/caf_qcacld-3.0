/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * DOC: N79-WLAN 5 GHz antenna sharing coexistence INI configuration
 */

#ifndef _WLAN_N79_COEX_CFG_H_
#define _WLAN_N79_COEX_CFG_H_

#include "cfg_define.h"

#ifdef FEATURE_N79_COEX
/*
 * <ini>
 * n79_ss_hysteresis_timer - Hysteresis delay (seconds) before restoring
 * spatial streams after N79 becomes inactive.
 *
 * @Min: 10
 * @Max: 3600
 * @Default: 1800
 *
 * When N79 deactivates, the WLAN host waits this duration before sending
 * the WMI command to restore the pre-N79 NSS configuration (AP coordination
 * path). A value of 1800 s (30 min) prevents rapid toggling.
 *
 * Supported Feature: N79 Coexistence
 * Usage: Internal
 * </ini>
 */
#define CFG_N79_SS_HYSTERESIS_TIMER \
	CFG_INI_UINT("n79_ss_hysteresis_timer", \
		10, 3600, 1800, \
		CFG_VALUE_OR_DEFAULT, \
		"N79 spatial-stream restore hysteresis (s)")

/*
 * <ini>
 * n79_rx_div_hysteresis_timer - Hysteresis delay (seconds) before restoring
 * Rx-diversity chains after N79 becomes inactive.
 *
 * @Min: 1
 * @Max: 60
 * @Default: 10
 *
 * When N79 deactivates, the WLAN host waits this duration before sending
 * the WMI command to restore full Rx-diversity (num_rx_chains). The shorter
 * default (10 s) allows faster Rx-diversity recovery than NSS recovery.
 *
 * Supported Feature: N79 Coexistence
 * Usage: Internal
 * </ini>
 */
#define CFG_N79_RX_DIV_HYSTERESIS_TIMER \
	CFG_INI_UINT("n79_rx_div_hysteresis_timer", \
		1, 60, 10, \
		CFG_VALUE_OR_DEFAULT, \
		"N79 Rx-diversity restore hysteresis (s)")

/*
 * <ini>
 * gN79CoexPolicy - N79 coexistence NSS/chain restriction policy
 *
 * @Min: 0
 * @Max: 1
 * @Default: 1
 *
 * Controls whether WLAN applies NSS and chain restrictions when N79 is
 * active.
 *   0: Disabled — no WLAN restriction applied when N79 is active.
 *   1: 2x2 policy (default) — limit rx_nss, tx_nss, num_rx_chains and
 *      num_tx_chains to 2 on all 5 GHz vdevs while N79 is active.
 *
 * Supported Feature: N79 Coexistence
 * Usage: Internal
 * </ini>
 */
#define CFG_N79_COEX_POLICY \
	CFG_INI_UINT("gN79CoexPolicy", \
		0, 1, 1, \
		CFG_VALUE_OR_DEFAULT, \
		"N79 coexistence NSS/chain restriction policy")

#define CFG_N79_COEX_ALL \
	CFG(CFG_N79_SS_HYSTERESIS_TIMER) \
	CFG(CFG_N79_RX_DIV_HYSTERESIS_TIMER) \
	CFG(CFG_N79_COEX_POLICY)

#else
#define CFG_N79_COEX_ALL
#endif /* FEATURE_N79_COEX */

#endif /* _WLAN_N79_COEX_CFG_H_ */
