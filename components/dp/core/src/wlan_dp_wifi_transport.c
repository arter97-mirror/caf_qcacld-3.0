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

#ifdef FEATURE_WIFI_TRANSPORT

#include "wlan_dp_wifi_transport.h"
#include "wlan_dp_priv.h"
#include "dp_internal.h"

/**
 * dp_wifi_transport_ce_msg_pool_init() - allocate CE0 framing buffer pool
 * @wtc: session context
 * @dp_ctx: DP psoc context
 *
 * Allocates a single DMA-coherent region for 32 CE0 framing buffers
 * (32 x 2048 B = 64 KB) and chains them into a free list.  Each buffer
 * holds [HTC_FRAME_HDR (8B)][xpan_msg_hdr (8B)][TLV payload].
 *
 * Return: QDF_STATUS_SUCCESS or QDF_STATUS_E_NOMEM
 */
static inline QDF_STATUS
dp_wifi_transport_ce_msg_pool_init(struct wifi_transport_host_context *wtc,
				   struct wlan_dp_psoc_context *dp_ctx)
{
	qdf_device_t qdf_dev = dp_ctx->qdf_dev;
	qdf_nbuf_t nbuf;
	uint32_t i;

	for (i = 0; i < WIFI_TRANSPORT_CE_MSG_POOL_SIZE; i++) {
		nbuf = qdf_nbuf_alloc(qdf_dev,
				      WIFI_TRANSPORT_CE_MSG_BUF_SIZE,
				      0, 4, false);
		if (!nbuf)
			goto err;
		if (qdf_nbuf_map_single(qdf_dev, nbuf, QDF_DMA_TO_DEVICE) !=
		    QDF_STATUS_SUCCESS) {
			qdf_nbuf_free(nbuf);
			goto err;
		}
		qdf_nbuf_mapped_paddr_set(nbuf,
					  qdf_nbuf_get_frag_paddr(nbuf, 0));
		wtc->ce_msg_pool[i].nbuf       = nbuf;
		wtc->ce_msg_pool[i].bufs_added = 0;
		wtc->ce_msg_pool[i].next       =
			(i + 1 < WIFI_TRANSPORT_CE_MSG_POOL_SIZE) ?
			&wtc->ce_msg_pool[i + 1] : NULL;
	}
	wtc->ce_msg_free_head = &wtc->ce_msg_pool[0];
	return QDF_STATUS_SUCCESS;

err:
	while (i--) {
		qdf_nbuf_unmap_single(qdf_dev, wtc->ce_msg_pool[i].nbuf,
				      QDF_DMA_TO_DEVICE);
		qdf_nbuf_free(wtc->ce_msg_pool[i].nbuf);
		wtc->ce_msg_pool[i].nbuf = NULL;
	}
	return QDF_STATUS_E_NOMEM;
}

/**
 * dp_wifi_transport_ce_msg_pool_deinit() - free CE0 framing buffer pool
 * @wtc: session context
 * @dp_ctx: DP psoc context
 *
 * Safe to call if init was never completed.
 *
 * Return: None
 */
static inline void
dp_wifi_transport_ce_msg_pool_deinit(struct wifi_transport_host_context *wtc,
				     struct wlan_dp_psoc_context *dp_ctx)
{
	qdf_device_t qdf_dev = dp_ctx->qdf_dev;
	uint32_t i;

	for (i = 0; i < WIFI_TRANSPORT_CE_MSG_POOL_SIZE; i++) {
		if (!wtc->ce_msg_pool[i].nbuf)
			continue;
		qdf_nbuf_unmap_single(qdf_dev, wtc->ce_msg_pool[i].nbuf,
				      QDF_DMA_TO_DEVICE);
		qdf_nbuf_free(wtc->ce_msg_pool[i].nbuf);
		wtc->ce_msg_pool[i].nbuf = NULL;
	}
	wtc->ce_msg_free_head = NULL;
}

/**
 * dp_wifi_transport_tx_buf_pool_init() - allocate TX CE buffer pool
 * @wtc: session context
 * @dp_ctx: DP psoc context
 *
 * Allocates a single DMA-coherent region for all 380 TX CE audio buffers
 * (380 x 2048 B = 760 KB) and initialises the circular cookie queue so
 * cookie_array[i] = i at Init. All buffers stay DMA-mapped for the session
 * lifetime; no per-packet mapping is needed on the TX hot path.
 *
 * Return: QDF_STATUS_SUCCESS or QDF_STATUS_E_NOMEM
 */
static inline QDF_STATUS
dp_wifi_transport_tx_buf_pool_init(struct wifi_transport_host_context *wtc,
				   struct wlan_dp_psoc_context *dp_ctx)
{
	qdf_device_t qdf_dev = dp_ctx->qdf_dev;
	qdf_nbuf_t nbuf;
	uint32_t i;

	for (i = 0; i < WIFI_TRANSPORT_TX_BUF_POOL_SIZE; i++) {
		nbuf = qdf_nbuf_alloc(qdf_dev,
				      WIFI_TRANSPORT_TX_CE_BUF_SIZE,
				      0, 4, false);
		if (!nbuf)
			goto err;
		if (qdf_nbuf_map_single(qdf_dev, nbuf, QDF_DMA_TO_DEVICE) !=
		    QDF_STATUS_SUCCESS) {
			qdf_nbuf_free(nbuf);
			goto err;
		}
		qdf_nbuf_mapped_paddr_set(nbuf,
					  qdf_nbuf_get_frag_paddr(nbuf, 0));
		wtc->buf_array[i].nbuf  = nbuf;
		wtc->cookie_array[i]    = i;
	}
	wtc->buf_pool_hp         = 0;
	wtc->buf_pool_tp         = 0;
	wtc->buf_pool_free_count = WIFI_TRANSPORT_TX_BUF_POOL_SIZE;
	qdf_spinlock_create(&wtc->buf_pool_lock);
	return QDF_STATUS_SUCCESS;

err:
	while (i--) {
		qdf_nbuf_unmap_single(qdf_dev, wtc->buf_array[i].nbuf,
				      QDF_DMA_TO_DEVICE);
		qdf_nbuf_free(wtc->buf_array[i].nbuf);
		wtc->buf_array[i].nbuf = NULL;
	}
	return QDF_STATUS_E_NOMEM;
}

/**
 * dp_wifi_transport_tx_buf_pool_deinit() - free TX CE buffer pool
 * @wtc: session context
 * @dp_ctx: DP psoc context
 *
 * Releases the DMA-coherent TX CE buffer pool. Safe to call if init
 * was never completed.
 *
 * Return: None
 */
static inline void
dp_wifi_transport_tx_buf_pool_deinit(
				struct wifi_transport_host_context *wtc,
				struct wlan_dp_psoc_context *dp_ctx)
{
	qdf_device_t qdf_dev = dp_ctx->qdf_dev;
	uint32_t i;

	if (!wtc->buf_array[0].nbuf)
		return;

	qdf_spinlock_destroy(&wtc->buf_pool_lock);
	for (i = 0; i < WIFI_TRANSPORT_TX_BUF_POOL_SIZE; i++) {
		if (!wtc->buf_array[i].nbuf)
			continue;
		qdf_nbuf_unmap_single(qdf_dev, wtc->buf_array[i].nbuf,
				      QDF_DMA_TO_DEVICE);
		qdf_nbuf_free(wtc->buf_array[i].nbuf);
		wtc->buf_array[i].nbuf = NULL;
	}
}

/**
 * dp_wifi_transport_htc_tx_complete() - stub HTC TX completion callback
 * @ctx: dp_ctx (unused)
 * @pkt: completed HTC packet (unused)
 *
 * HTC registers this callback but CE0 data path uses dp_wifi_transport_tx_done
 * instead (registered after htc_connect_service overrides HTC's default).
 *
 * Return: None
 */
static inline void
dp_wifi_transport_htc_tx_complete(void *ctx, HTC_PACKET *pkt)
{
	dp_err("htc_tx_complete stub (CE path active)");
}

/**
 * dp_wifi_transport_htc_rx_handler() - stub HTC RX handler
 * @ctx: dp_ctx (unused)
 * @pkt: received HTC packet (unused)
 *
 * HTC registers this callback but CE5 data path uses dp_wifi_transport_rx_ready
 * instead (registered after htc_connect_service overrides HTC's default).
 *
 * Return: None
 */
static inline void
dp_wifi_transport_htc_rx_handler(void *ctx, HTC_PACKET *pkt)
{
	dp_err("htc_rx_handler stub (CE path active)");
}

/**
 * dp_wifi_transport_tx_done() - CE0 TX completion callback
 * @transfer_ctx: msg_buf pointer passed to ce_send()
 * @nbytes: number of bytes transferred (used to sync DMA cache)
 *
 * Return: None
 */
static inline void
dp_wifi_transport_tx_done(void *transfer_ctx,
			  unsigned int nbytes)
{
}

/**
 * dp_wifi_transport_rx_ready() - CE5 RX callback (FW -> host)
 * @transfer_ctx: vaddr passed to ce_recv_buf_enqueue
 * @buffer: DMA address of the received buffer (used to re-post)
 * @nbytes: bytes received (includes HTC_FRAME_HDR)
 *
 *
 * Return: None
 */
static inline void
dp_wifi_transport_rx_ready(void *transfer_ctx,
			   qdf_dma_addr_t buffer,
			   unsigned int nbytes)
{
}

QDF_STATUS dp_wifi_transport_link_init(struct wlan_dp_psoc_context *dp_ctx)
{
	struct wifi_transport_link_state *ls = &dp_ctx->wifi_transport_link;
	struct hif_opaque_softc *hif_handle =
			cds_get_context(QDF_MODULE_ID_HIF);
	void *htc_handle = cds_get_context(QDF_MODULE_ID_HTC);
	struct htc_service_connect_req conn_req = {0};
	struct htc_service_connect_resp conn_resp = {0};
	QDF_STATUS status;
	uint32_t pdev_id;

	if (!htc_handle || !hif_handle) {
		dp_err("HTC or HIF context is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	if (!pld_is_direct_link_supported(dp_ctx->qdf_dev->dev)) {
		dp_err("direct_link not supported — cannot use LPASS CE");
		return QDF_STATUS_E_NOSUPPORT;
	}

	/*
	 * Step 1: Connect to LPASS_DATA_MSG_SVC to obtain the endpoint ID.
	 * HTC also maps CE0 (PIPEDIR_OUT) for TX and CE5 (PIPEDIR_IN) for RX.
	 * The stub EpTxComplete / EpRecv callbacks are registered here but will
	 * be overridden by step 2 below so they are never invoked on the
	 * data path.
	 */
	conn_req.EpCallbacks.pContext     = dp_ctx;
	conn_req.EpCallbacks.EpTxComplete = dp_wifi_transport_htc_tx_complete;
	conn_req.EpCallbacks.EpRecv       = dp_wifi_transport_htc_rx_handler;
	conn_req.ConnectionFlags |=
			HTC_CONNECT_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
	conn_req.service_id = LPASS_DATA_MSG_SVC;

	status = htc_connect_service(htc_handle, &conn_req, &conn_resp);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("LPASS_DATA_MSG_SVC connect failed: %d", status);
		return status;
	}
	ls->lpass_ep_id = conn_resp.Endpoint;
	ls->link_mask  |= WIFI_TRANSPORT_LINK_HTC_CONNECT;

	/*
	 * Step 2: Register CE0/CE5 callbacks via HIF so Core DP never
	 * touches CE-internal types (struct CE_handle, ce_send, etc.).
	 */
	status = hif_wifi_transport_register_callbacks(
			hif_handle,
			dp_wifi_transport_tx_done,
			dp_wifi_transport_rx_ready);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("CE callback registration failed");
		return status;
	}
	ls->link_mask |= WIFI_TRANSPORT_LINK_CE_CALLBACKS;

	pdev_id = wlan_objmgr_pdev_get_pdev_id(dp_ctx->pdev);
	ls->rx_refill_ring =
		dp_setup_direct_link_refill_ring(dp_ctx->cdp_soc, pdev_id);
	if (!ls->rx_refill_ring) {
		dp_err("Failed to create LPASS refill ring");
		return QDF_STATUS_E_FAILURE;
	}
	ls->link_mask |= WIFI_TRANSPORT_LINK_REFILL_RING;

	dp_info("wifi_transport link init ep_id=%d link_mask=0x%x",
		ls->lpass_ep_id, ls->link_mask);

	return QDF_STATUS_SUCCESS;
}

void dp_wifi_transport_link_deinit(struct wlan_dp_psoc_context *dp_ctx)
{
	struct wifi_transport_link_state *ls = &dp_ctx->wifi_transport_link;
	uint32_t pdev_id;

	dp_info("link_deinit link_mask=0x%x", ls->link_mask);

	if (ls->link_mask & WIFI_TRANSPORT_LINK_REFILL_RING) {
		pdev_id = wlan_objmgr_pdev_get_pdev_id(dp_ctx->pdev);
		dp_info("destroying LPASS refill ring");
		dp_destroy_direct_link_refill_ring(dp_ctx->cdp_soc, pdev_id);
		ls->rx_refill_ring = NULL;
	}

	ls->link_mask = 0;
}

QDF_STATUS dp_wifi_transport_init(struct wlan_dp_psoc_context *dp_ctx,
				  const struct wifi_transport_ops *ops)
{
	struct wifi_transport_host_context *wtc;
	struct wifi_transport_link_state *ls =
			&dp_ctx->wifi_transport_link;
	void *htc_handle;
	QDF_STATUS status;

	/* Guard: link layer must be initialised first */
	if (!ls->link_mask) {
		dp_err("link_init not called");
		return QDF_STATUS_E_FAILURE;
	}

	/* Double-init guard */
	qdf_spin_lock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	if (dp_ctx->wifi_transport_host_ctx) {
		qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
		dp_err("init called while session active");
		return QDF_STATUS_E_ALREADY;
	}
	qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);

	htc_handle = cds_get_context(QDF_MODULE_ID_HTC);
	if (!htc_handle) {
		dp_err("HTC context is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	wtc = qdf_mem_malloc(sizeof(*wtc));
	if (!wtc) {
		dp_err("Unable to allocate wtc");
		return QDF_STATUS_E_NOMEM;
	}

	/* Store WiFi Transport driver callbacks before any init step
	 * that may fire them.
	 */
	wtc->ops = *ops;
	wtc->lpass_ep_id = ls->lpass_ep_id;
	wtc->rx_refill_ring = ls->rx_refill_ring;
	wtc->qdf_dev = dp_ctx->qdf_dev;
	wtc->hif_handle = cds_get_context(QDF_MODULE_ID_HIF);

	qdf_spin_lock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	dp_ctx->wifi_transport_host_ctx = wtc;
	qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);

	htc_vote_link_up(htc_handle, HTC_LINK_VOTE_DIRECT_LINK_USER_ID);
	wtc->init_mask |= WIFI_TRANSPORT_INIT_PWR_VOTE;

	status = dp_wifi_transport_tx_buf_pool_init(wtc, dp_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("TX CE buf pool init failed: %d", status);
		goto err;
	}
	wtc->init_mask |= WIFI_TRANSPORT_INIT_TX_BUF_POOL;

	status = dp_wifi_transport_ce_msg_pool_init(wtc, dp_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("CE msg pool init failed: %d", status);
		goto err;
	}
	wtc->init_mask |= WIFI_TRANSPORT_INIT_CE_MSG_POOL;

	qdf_atomic_set(&wtc->bmps_state, WIFI_TRANSPORT_BMPS_ENABLED);
	qdf_atomic_set(&dp_ctx->wifi_transport_active_path,
		       WIFI_TRANSPORT_PATH_ACTIVE);

	dp_info("wifi_transport init complete, ep_id=%d",
		wtc->lpass_ep_id);

	return QDF_STATUS_SUCCESS;

err:
	if (wtc->init_mask & WIFI_TRANSPORT_INIT_TX_BUF_POOL)
		dp_wifi_transport_tx_buf_pool_deinit(wtc, dp_ctx);
	if (wtc->init_mask & WIFI_TRANSPORT_INIT_PWR_VOTE)
		htc_vote_link_down(htc_handle,
				   HTC_LINK_VOTE_DIRECT_LINK_USER_ID);

	qdf_spin_lock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	dp_ctx->wifi_transport_host_ctx = NULL;
	qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	qdf_mem_free(wtc);

	return status;
}

void dp_wifi_transport_deinit(struct wlan_dp_psoc_context *dp_ctx,
			      bool is_recovery_stop)
{
	struct wifi_transport_host_context *wtc;
	void *htc_handle;

	qdf_spin_lock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	wtc = dp_ctx->wifi_transport_host_ctx;
	qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);

	dp_info("deinit, recovery=%d init_mask=0x%x",
		is_recovery_stop, wtc ? wtc->init_mask : 0);

	if (!wtc || !wtc->init_mask)
		return;

	qdf_atomic_set(&dp_ctx->wifi_transport_active_path,
		       WIFI_TRANSPORT_PATH_INACTIVE);

	/* Clear pointer now — prevents new ctx_get() from succeeding.
	 * For SSR the FW is gone so no link-down send is needed.
	 */
	qdf_spin_lock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	dp_ctx->wifi_transport_host_ctx = NULL;
	qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);

	dp_wifi_transport_ce_msg_pool_deinit(wtc, dp_ctx);
	dp_wifi_transport_tx_buf_pool_deinit(wtc, dp_ctx);

	if (wtc->init_mask & WIFI_TRANSPORT_INIT_PWR_VOTE) {
		htc_handle = cds_get_context(QDF_MODULE_ID_HTC);
		if (htc_handle)
			htc_vote_link_down(htc_handle,
					   HTC_LINK_VOTE_DIRECT_LINK_USER_ID);
	}

	qdf_mem_free(wtc);
	dp_info("deinit complete");

}

#endif /* FEATURE_WIFI_TRANSPORT */
