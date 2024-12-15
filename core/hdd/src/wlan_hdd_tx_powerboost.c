/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: wlan_hdd_tx_powerboost.c
 *
 * WLAN Host Device Driver Tx powerboost API implementation
 */
#ifdef FEATURE_WLAN_TX_POWERBOOST
#include "wlan_hdd_main.h"
#include "cfg_ucfg_api.h"
#include "wlan_hdd_tx_powerboost.h"

void hdd_tx_powerboost_target_config(struct hdd_context *hdd_ctx,
				     struct wma_tgt_cfg *tgt_cfg)
{
	bool tx_pb_ini;

	tx_pb_ini = cfg_get(hdd_ctx->psoc, CFG_TX_POWERBOOST);
	hdd_ctx->tx_pb.tx_powerboost_enabled = tx_pb_ini &&
					       tgt_cfg->tx_powerboost;
	hdd_debug("TPB Enable: %d (Host: %d FW: %d)",
		  hdd_ctx->tx_pb.tx_powerboost_enabled, tx_pb_ini,
		  tgt_cfg->tx_powerboost);
}

#endif
