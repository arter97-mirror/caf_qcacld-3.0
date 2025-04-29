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
 * DOC: wlan_hdd_tx_powerboost.h
 *
 * WLAN Host Device Driver Tx powerboost API implementation
 */
#ifndef __WLAN_HDD_TX_POWERBOOST_H
#define __WLAN_HDD_TX_POWERBOOST_H

#ifdef FEATURE_WLAN_TX_POWERBOOST
/**
 * hdd_tx_powerboost_target_config() - Configure Tx Powerboost feature
 * @hdd_ctx: Pointer to HDD context
 * @tgt_cfg: Pointer to target device capability information
 *
 * Tx powerboost functionality is enabled if it is enabled in
 * .ini file and also supported on target device.
 *
 * Return: None
 */
void hdd_tx_powerboost_target_config(struct hdd_context *hdd_ctx,
				     struct wma_tgt_cfg *tgt_cfg);

/**
 * hdd_tx_powerboost_init() - HDD Tx powerboost initialization
 * @hdd_ctx: Pointer to HDD context
 *
 * Return: QDF_STATUS
 */
QDF_STATUS hdd_tx_powerboost_init(struct hdd_context *hdd_ctx);

/**
 * hdd_tx_powerboost_deinit() - HDD Tx powerboost de-initialization
 * @hdd_ctx: Pointer to HDD context
 *
 * Return: void
 */
void hdd_tx_powerboost_deinit(struct hdd_context *hdd_ctx);
#else
static inline
void hdd_tx_powerboost_target_config(struct hdd_context *hdd_ctx,
				     struct wma_tgt_cfg *tgt_cfg)
{
}

static inline
QDF_STATUS hdd_tx_powerboost_init(struct hdd_context *hdd_ctx)
{
	return QDF_STATUS_SUCCESS;
}

static inline
void hdd_tx_powerboost_deinit(struct hdd_context *hdd_ctx)
{
}
#endif /* FEATURE_WLAN_TX_POWERBOOST */
#endif /* __WLAN_HDD_TX_POWERBOOST_H */
