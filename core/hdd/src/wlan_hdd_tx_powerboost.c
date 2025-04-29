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
#include <wlan_reg_ucfg_api.h>
#include "reg_services_public_struct.h"

#define TX_PB_DMA_SIZE (100 * 1024)
#define MEMORY_ALIGN      8

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

static QDF_STATUS hdd_tx_powerboost_init_dma(struct hdd_context *hdd_ctx)
{
	qdf_device_t qdf_dev;
	struct reg_pdev_pb_dma_buf dma = {0};
	QDF_STATUS status;

	qdf_dev = wlan_psoc_get_qdf_dev(hdd_ctx->psoc);
	if (!qdf_dev) {
		hdd_err("TPB: Invalid qdf dev");
		return QDF_STATUS_E_FAILURE;
	}

	/*
	 * Set the buffer size to 100KB + 8 bytes for alignment
	 * 1KB = 1024B
	 */
	hdd_ctx->tx_pb.dma.size = TX_PB_DMA_SIZE;
	hdd_ctx->tx_pb.dma.vaddr = qdf_aligned_mem_alloc_consistent(
				qdf_dev, &hdd_ctx->tx_pb.dma.size,
				&hdd_ctx->tx_pb.dma.vaddr_unaligned,
				&hdd_ctx->tx_pb.dma.paddr_unaligned,
				&hdd_ctx->tx_pb.dma.paddr,
				MEMORY_ALIGN);
	if (!hdd_ctx->tx_pb.dma.vaddr) {
		hdd_err("TPB: DMA buffer allocation failed, size: %u",
			hdd_ctx->tx_pb.dma.size);
		return QDF_STATUS_E_NOMEM;
	}

	qdf_mem_set(hdd_ctx->tx_pb.dma.vaddr, hdd_ctx->tx_pb.dma.size, 0);
	dma.size = hdd_ctx->tx_pb.dma.size;
	qdf_dmaaddr_to_32s(hdd_ctx->tx_pb.dma.paddr,
			   &dma.paddr_aligned_lo,
			   &dma.paddr_aligned_hi);
	status = ucfg_reg_txpb_send_dma_addr(hdd_ctx->pdev, &dma);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("TPB: ucfg_reg_txpb_send_dma_addr failed: %d", status);

	hdd_debug("TPB: DMA address sent to firmware");
	return status;
}

static void hdd_tx_powerboost_deinit_dma(struct hdd_context *hdd_ctx)
{
	qdf_device_t qdf_dev;

	qdf_dev = wlan_psoc_get_qdf_dev(hdd_ctx->psoc);
	if (!qdf_dev) {
		hdd_err("TPB: Invalid qdf dev");
		return;
	}

	if (!hdd_ctx->tx_pb.dma.vaddr_unaligned)
		return;

	qdf_mem_free_consistent(qdf_dev, qdf_dev->dev,
				hdd_ctx->tx_pb.dma.size,
				hdd_ctx->tx_pb.dma.vaddr_unaligned,
				hdd_ctx->tx_pb.dma.paddr_unaligned,
				0);
	hdd_ctx->tx_pb.dma.vaddr_unaligned = NULL;
	hdd_debug("TPB: DMA memory freed");
}

QDF_STATUS hdd_tx_powerboost_init(struct hdd_context *hdd_ctx)
{
	QDF_STATUS status;

	if (!hdd_ctx->tx_pb.tx_powerboost_enabled) {
		hdd_warn("TPB: feature not enabled");
		return QDF_STATUS_SUCCESS;
	}

	status = hdd_tx_powerboost_init_dma(hdd_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("TPB: init dma failed: %d", status);
		return status;
	}
	qdf_wake_lock_create(&hdd_ctx->tx_pb.txpb_wake_lock, "txpb_wake_lock");
	qdf_runtime_lock_init(&hdd_ctx->tx_pb.txpb_runtime_lock);
	ucfg_reg_txpb_register_callback(hdd_ctx->psoc,
					wlan_hdd_cfg80211_tx_pb_callback,
					hdd_ctx);

	return QDF_STATUS_SUCCESS;
}

void hdd_tx_powerboost_deinit(struct hdd_context *hdd_ctx)
{
	if (!hdd_ctx->tx_pb.tx_powerboost_enabled) {
		hdd_warn("TPB: feature not enabled");
		return;
	}

	ucfg_reg_txpb_unregister_callback(hdd_ctx->psoc);
	qdf_runtime_lock_deinit(&hdd_ctx->tx_pb.txpb_runtime_lock);
	qdf_wake_lock_destroy(&hdd_ctx->tx_pb.txpb_wake_lock);
	hdd_tx_powerboost_deinit_dma(hdd_ctx);
}

#endif
