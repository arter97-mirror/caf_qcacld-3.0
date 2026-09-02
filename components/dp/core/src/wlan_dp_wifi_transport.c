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
#include "wlan_dp_main.h"
#include "lpass_ce/lpass_ce.h"

/**
 * dp_wifi_transport_ctx_get() - fetch session context and take a reference
 * @dp_ctx: DP psoc context
 *
 * Atomically fetches the active session context and increments its
 * reference count under the ctx_lock spinlock.
 *
 * Return: pointer to wtc if a session is active, NULL otherwise
 */
static inline struct wifi_transport_host_context *
dp_wifi_transport_ctx_get(struct wlan_dp_psoc_context *dp_ctx)
{
	struct wifi_transport_host_context *wtc;

	qdf_spin_lock_bh(&dp_ctx->wifi_transport_host_ctx_lock);
	wtc = dp_ctx->wifi_transport_host_ctx;
	if (wtc)
		qdf_atomic_inc(&wtc->ref_cnt);
	qdf_spin_unlock_bh(&dp_ctx->wifi_transport_host_ctx_lock);

	return wtc;
}

/**
 * dp_wifi_transport_release() - signal teardown completion
 * @wtc: session context whose ref_cnt just reached zero
 *
 * Called when the last reference is dropped during deinit.
 * Signals deregister_done so dp_wifi_transport_deinit() can
 * proceed with resource teardown.
 *
 * Return: None
 */
static inline void
dp_wifi_transport_release(struct wifi_transport_host_context *wtc)
{
	dp_info("last ref dropped, signalling deregister_done");
	qdf_event_set(&wtc->deregister_done);
}

/**
 * dp_wifi_transport_ctx_put() - release a session context reference
 * @wtc: session context obtained from dp_wifi_transport_ctx_get()
 *
 * No lock needed — we already hold a reference so the object is stable.
 *
 * Return: None
 */
static inline void
dp_wifi_transport_ctx_put(struct wifi_transport_host_context *wtc)
{
	if (wtc && qdf_atomic_dec_and_test(&wtc->ref_cnt))
		dp_wifi_transport_release(wtc);
}

/**
 * dp_wifi_transport_cookie_alloc() - allocate a TX cookie from the ring
 * @wtc: session context
 *
 * Return: cookie index on success, WIFI_TRANSPORT_TX_BUF_POOL_SIZE if
 *         pool is exhausted
 */
static inline uint16_t
dp_wifi_transport_cookie_alloc(struct wifi_transport_host_context *wtc)
{
	uint16_t cookie;

	qdf_spin_lock_bh(&wtc->buf_pool_lock);
	if (!wtc->buf_pool_free_count) {
		qdf_spin_unlock_bh(&wtc->buf_pool_lock);
		return WIFI_TRANSPORT_TX_BUF_POOL_SIZE;
	}
	cookie = wtc->cookie_array[wtc->buf_pool_tp];
	wtc->buf_pool_tp = (wtc->buf_pool_tp + 1) %
			   WIFI_TRANSPORT_TX_BUF_POOL_SIZE;
	wtc->buf_pool_free_count--;
	qdf_spin_unlock_bh(&wtc->buf_pool_lock);
	return cookie;
}

/**
 * dp_wifi_transport_cookie_free() - Free cookie value
 * @wtc: session context
 * @cookie: Cookie value.
 *
 * Return: None
 */
static inline void
dp_wifi_transport_cookie_free(struct wifi_transport_host_context *wtc,
			      uint16_t cookie)
{
	qdf_spin_lock_bh(&wtc->buf_pool_lock);
	wtc->cookie_array[wtc->buf_pool_hp] = cookie;
	wtc->buf_pool_hp = (wtc->buf_pool_hp + 1) %
			   WIFI_TRANSPORT_TX_BUF_POOL_SIZE;
	wtc->buf_pool_free_count++;
	qdf_spin_unlock_bh(&wtc->buf_pool_lock);
}

/**
 * dp_wifi_transport_ce_msg_alloc() - pop a buffer from the CE0 framing pool
 * @wtc: session context
 *
 * Takes ce_msg_lock internally (_bh: contested with tx_done BH).
 *
 * Return: pointer to msg_buf, or NULL if pool exhausted
 */
static inline struct wifi_transport_ce_msg_buf *
dp_wifi_transport_ce_msg_alloc(struct wifi_transport_host_context *wtc)
{
	struct wifi_transport_ce_msg_buf *buf;

	qdf_spin_lock_bh(&wtc->ce_msg_lock);
	buf = wtc->ce_msg_free_head;
	if (buf) {
		wtc->ce_msg_free_head = buf->next;
		buf->next = NULL;
		buf->bufs_added = 0;
	}
	qdf_spin_unlock_bh(&wtc->ce_msg_lock);
	return buf;
}

/**
 * dp_wifi_transport_ce_msg_free() - return a buffer to the CE0 framing pool
 * @wtc: session context
 * @buf: msg_buf to return
 *
 * Takes ce_msg_lock internally (_bh: may be called from tx_done BH).
 *
 * Return: None
 */
static inline void
dp_wifi_transport_ce_msg_free(struct wifi_transport_host_context *wtc,
			      struct wifi_transport_ce_msg_buf *buf)
{
	qdf_spin_lock_bh(&wtc->ce_msg_lock);
	buf->bufs_added = 0;
	buf->next = wtc->ce_msg_free_head;
	wtc->ce_msg_free_head = buf;
	qdf_spin_unlock_bh(&wtc->ce_msg_lock);
}

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
 * dp_wifi_transport_msg_send() - fill HTC_FRAME_HDR and submit one TX CE msg
 * @wtc:     session context
 * @hif_ctx: HIF opaque context (hif_wt_tx_send is called internally)
 * @msg_buf: framing buffer containing [HTC_HDR][xpan_msg_hdr][TLV payload];
 *           passed as transfer_ctx so tx_done callback can return it to pool
 * @nbytes:  xpan payload size (sizeof(xpan_msg_hdr) + TLV body);
 *           HIF adds HTC_HDR_LENGTH for the total transfer length
 *
 * Return: 0 on success, -EIO if the CE TX ring is full
 */
static inline int
dp_wifi_transport_msg_send(struct wifi_transport_host_context *wtc,
			   struct hif_opaque_softc *hif_ctx,
			   struct wifi_transport_ce_msg_buf *msg_buf,
			   uint32_t nbytes)
{
	qdf_nbuf_t nbuf = msg_buf->nbuf;
	qdf_dma_addr_t dma_addr = qdf_nbuf_mapped_paddr_get(nbuf);
	HTC_FRAME_HDR *htc_hdr = (HTC_FRAME_HDR *)qdf_nbuf_data(nbuf);
	uint32_t total_len = HTC_HDR_LENGTH + nbytes;
	QDF_STATUS status;
	int ret = 0;

	htc_hdr->EndpointID    = wtc->lpass_ep_id & 0xFF;
	htc_hdr->Flags         = 0;
	htc_hdr->PayloadLen    = nbytes;
	htc_hdr->ControlBytes0 = 0;
	htc_hdr->ControlBytes1 = 0;
	htc_hdr->reserved      = 0;

	qdf_mem_dma_sync_single_for_device(wtc->qdf_dev, dma_addr,
					   total_len, DMA_TO_DEVICE);

	status = hif_wt_tx_send(hif_ctx, msg_buf, dma_addr, total_len);
	if (qdf_unlikely(QDF_IS_STATUS_ERROR(status)))
		ret = qdf_status_to_os_return(status);

	return ret;
}

/**
 * dp_wifi_transport_send() - send a header-only CE0 control message
 * @wtc: session context
 * @msg_type: XPAN_MSG_TYPE_T value (header-only messages)
 *
 * Used for XPAN_DIRECT_LINK_UP_INDICATION and XPAN_DIRECT_LINK_DOWN_REQUEST.
 * Allocates a CE0 framing buffer from the pool, writes HTC_FRAME_HDR and
 * xpan_msg_hdr directly, then calls ce_send() — no qdf_nbuf involved.
 *
 * Return: 0 on success, -ENODEV if no session, -ENOSPC if pool empty
 */
static inline int
dp_wifi_transport_send(struct wifi_transport_host_context *wtc,
		       XPAN_MSG_TYPE_T msg_type)
{
	struct wifi_transport_ce_msg_buf *msg_buf;
	xpan_msg_hdr *hdr;
	uint32_t nbytes = sizeof(xpan_msg_hdr);
	int ret;

	msg_buf = dp_wifi_transport_ce_msg_alloc(wtc);

	if (!msg_buf) {
		dp_err("ce_msg_pool exhausted for send");
		ret = -ENOSPC;
		goto error;
	}

	hdr = (xpan_msg_hdr *)
		((uint8_t *)qdf_nbuf_data(msg_buf->nbuf) + HTC_HDR_LENGTH);
	hdr->msg_type = msg_type;
	hdr->msg_id   = wtc->msg_seq_num++;
	hdr->dbg_indx = 0;

	ret = dp_wifi_transport_msg_send(wtc, wtc->hif_handle, msg_buf, nbytes);
	if (ret)
		dp_wifi_transport_ce_msg_free(wtc, msg_buf);

error:
	return ret;
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
	struct wlan_dp_psoc_context *dp_ctx = dp_get_context();
	struct wifi_transport_host_context *wtc =
		dp_wifi_transport_ctx_get(dp_ctx);
	struct wifi_transport_ce_msg_buf *msg_buf = transfer_ctx;

	if (qdf_unlikely(!wtc)) {
		dp_err("wtc NULL");
		return;
	}

	/* Sync to CPU: framing buf safe to rewrite for next TX */
	qdf_mem_dma_sync_single_for_cpu(
			dp_ctx->qdf_dev,
			qdf_nbuf_mapped_paddr_get(msg_buf->nbuf),
			nbytes, DMA_TO_DEVICE);

	dp_wifi_transport_ce_msg_free(wtc, msg_buf);
	dp_wifi_transport_ctx_put(wtc);
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

int dp_wifi_transport_tx(struct wlan_dp_psoc_context *dp_ctx,
			 qdf_nbuf_t nbuf,
			 struct wifi_transport_tx_metadata *meta,
			 bool flush)
{
	struct wifi_transport_host_context *wtc =
				dp_wifi_transport_ctx_get(dp_ctx);
	xpan_tx_audio_fixed_param *fixed;
	xpan_tx_buffer_info *buf_info;
	xpan_msg_hdr *hdr;
	qdf_nbuf_t tx_nbuf;
	qdf_dma_addr_t buf_dma;
	uint32_t nbuf_len, n;
	uint16_t cookie;
	const uint8_t *dst_addr, *src_addr;
	int ret = 0;

	if (qdf_unlikely(!wtc)) {
		ret = -ENODEV;
		goto err_no_ctx;
	}

	if (qdf_unlikely(qdf_atomic_read(&dp_ctx->wifi_transport_active_path) ==
			    WIFI_TRANSPORT_PATH_INACTIVE)) {
		dp_err("TX dropped — deinit in progress");
		ret = -ESHUTDOWN;
		goto err_ctx;
	}

	if (qdf_unlikely(!nbuf))
		goto err_ctx;

	cookie = dp_wifi_transport_cookie_alloc(wtc);
	if (qdf_unlikely(cookie >= WIFI_TRANSPORT_TX_BUF_POOL_SIZE)) {
		dp_err("TX buf pool exhausted");
		ret = -ENOSPC;
		goto err_ctx;
	}

	tx_nbuf = wtc->buf_array[cookie].nbuf;
	nbuf_len = (uint32_t)qdf_nbuf_len(nbuf);
	qdf_mem_copy(qdf_nbuf_data(tx_nbuf),
		     qdf_nbuf_data(nbuf), nbuf_len);
	qdf_mem_dma_sync_single_for_device(
			dp_ctx->qdf_dev,
			qdf_nbuf_mapped_paddr_get(tx_nbuf),
			nbuf_len, DMA_TO_DEVICE);

	dst_addr = qdf_nbuf_get_ether_dest_addr(tx_nbuf);
	src_addr = qdf_nbuf_get_ether_src_addr(tx_nbuf);

	if (!wtc->cur_msg_buf ||
	    wtc->cur_msg_buf->bufs_added >=
	    WIFI_TRANSPORT_MAX_BUFS_PER_CE_MSG) {
		if (wtc->cur_msg_buf) {
			wtc->cur_msg_buf->next = NULL;
			if (!wtc->ready_tail) {
				wtc->ready_head = wtc->cur_msg_buf;
				wtc->ready_tail = wtc->cur_msg_buf;
			} else {
				wtc->ready_tail->next = wtc->cur_msg_buf;
				wtc->ready_tail = wtc->cur_msg_buf;
			}
			wtc->cur_msg_buf = NULL;
		}

		wtc->cur_msg_buf = dp_wifi_transport_ce_msg_alloc(wtc);
		if (!wtc->cur_msg_buf) {
			dp_err("ce_msg_pool exhausted");
			dp_wifi_transport_cookie_free(wtc, cookie);
			ret = -ENOSPC;
			goto err_ctx;
		}

		hdr = (xpan_msg_hdr *)
			((uint8_t *)qdf_nbuf_data(wtc->cur_msg_buf->nbuf) +
			HTC_HDR_LENGTH);
		fixed = (xpan_tx_audio_fixed_param *)
			((uint8_t *)qdf_nbuf_data(wtc->cur_msg_buf->nbuf) +
			HTC_HDR_LENGTH + sizeof(xpan_msg_hdr));

		hdr->msg_type = XPAN_TX_AUDIO_PACKET;
		hdr->msg_id   = wtc->msg_seq_num++;
		hdr->dbg_indx = 0;

		LPASSCETLV_SET_HDR(
			fixed,
			LPASSCETLV_TAG_STRUC_xpan_tx_audio_fixed_param,
			LPASSCETLV_GET_STRUCT_TLVLEN(
				xpan_tx_audio_fixed_param));
		fixed->src_mac_addr_31_0  =
			*(const uint32_t *)src_addr;
		fixed->src_mac_addr_47_32 =
			*(const uint16_t *)(src_addr + WIFI_TP_MAC_HI_OFF);
		fixed->num_tx_buffers = 0;
	}

	n = wtc->cur_msg_buf->bufs_added;
	buf_dma = qdf_nbuf_mapped_paddr_get(tx_nbuf);
	buf_info = (xpan_tx_buffer_info *)
		((uint8_t *)qdf_nbuf_data(wtc->cur_msg_buf->nbuf) +
		 HTC_HDR_LENGTH + sizeof(xpan_msg_hdr) +
		 sizeof(xpan_tx_audio_fixed_param) +
		 (n * sizeof(xpan_tx_buffer_info)));

	LPASSCETLV_SET_HDR(
		buf_info,
		LPASSCETLV_TAG_STRUC_xpan_tx_buffer_info_array,
		LPASSCETLV_GET_STRUCT_TLVLEN(xpan_tx_buffer_info));
	buf_info->buffer_addr_lo = (uint32_t)(buf_dma & 0xFFFFFFFF);
	buf_info->buffer_addr_hi = (uint32_t)(buf_dma >> 32) & 0xFF;
	buf_info->sw_cookie         = cookie;
	buf_info->buffer_len        = nbuf_len;
	buf_info->dest_mac_addr_31_0  = *(const uint32_t *)dst_addr;
	buf_info->dest_mac_addr_47_32 =
		*(const uint16_t *)(dst_addr + WIFI_TP_MAC_HI_OFF);
	buf_info->qos               = meta->qos;
	buf_info->checksum_en       = meta->checksum_en;
	buf_info->is_non_audio_pkt  = meta->is_non_audio_pkt;
	buf_info->ttp_lo           = (uint32_t)(meta->ttp & 0xFFFFFFFF);
	buf_info->ttp_hi            = (uint32_t)(meta->ttp >> 32);
	wtc->cur_msg_buf->bufs_added++;
	ret = cookie;

err_ctx:
		dp_wifi_transport_ctx_put(wtc);
err_no_ctx:
		if (nbuf)
			qdf_nbuf_kfree(nbuf);

	return ret;
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
	qdf_spinlock_create(&wtc->ce_msg_lock);
	qdf_event_create(&wtc->deregister_done);

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

	qdf_atomic_init(&wtc->ref_cnt);
	qdf_atomic_inc(&wtc->ref_cnt);
	qdf_atomic_set(&wtc->bmps_state, WIFI_TRANSPORT_BMPS_ENABLED);
	dp_wifi_transport_send(wtc, XPAN_DIRECT_LINK_UP_INDICATION);
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
	qdf_spinlock_destroy(&wtc->ce_msg_lock);
	qdf_event_destroy(&wtc->deregister_done);
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

	if (qdf_atomic_dec_and_test(&wtc->ref_cnt))
		dp_wifi_transport_release(wtc);
	if (QDF_IS_STATUS_ERROR(
		qdf_wait_single_event(&wtc->deregister_done,
				      WIFI_TRANSPORT_REF_DRAIN_TIMEOUT_MS)))
		dp_err("timed out waiting for session refs to drain");

	dp_wifi_transport_ce_msg_pool_deinit(wtc, dp_ctx);
	dp_wifi_transport_tx_buf_pool_deinit(wtc, dp_ctx);

	if (wtc->init_mask & WIFI_TRANSPORT_INIT_PWR_VOTE) {
		htc_handle = cds_get_context(QDF_MODULE_ID_HTC);
		if (htc_handle)
			htc_vote_link_down(htc_handle,
					   HTC_LINK_VOTE_DIRECT_LINK_USER_ID);
	}

	qdf_event_destroy(&wtc->deregister_done);
	qdf_spinlock_destroy(&wtc->ce_msg_lock);
	qdf_mem_free(wtc);
	dp_info("deinit complete");

}

/** - enable or disable BMPS with idempotency guard
 * @dp_ctx: DP psoc context
 * @wtc: session context
 * @disable: true = disable BMPS (audio active), false = enable BMPS (link idle)
 *
 * Return: 0 on success, -ENODEV if no session, -EBUSY if transition in flight
 */
static inline int
dp_wifi_transport_bmps_set(struct wlan_dp_psoc_context *dp_ctx,
			   struct wifi_transport_host_context *wtc,
			   bool disable)
{
	enum wifi_transport_bmps_state requested, cur_state;
	int rc;

	dp_info("bmps_set disable=%d", disable);

	requested = disable ? WIFI_TRANSPORT_BMPS_DISABLED :
			      WIFI_TRANSPORT_BMPS_ENABLED;

	cur_state = (enum wifi_transport_bmps_state)
			qdf_atomic_read(&wtc->bmps_state);

	if (cur_state == requested)
		return 0;

	if (cur_state == WIFI_TRANSPORT_BMPS_TRANSIT)
		return -EBUSY;

	qdf_atomic_set(&wtc->bmps_state, (int)WIFI_TRANSPORT_BMPS_TRANSIT);

	rc = pld_set_bmps(dp_ctx->qdf_dev->dev, disable);
	qdf_atomic_set(&wtc->bmps_state,
		       rc == 0 ? (int)requested : (int)cur_state);

	return rc;
}

/**
 * dp_wifi_transport_ctrl_msg_alloc() - allocate and header-fill a CE0
 *   control message buffer
 * @wtc: session context
 * @msg_type: XPAN message type to write into xpan_msg_hdr
 * @msg_buf_out: on success, set to the allocated CE msg buffer
 * @hdr_out: on success, set to the xpan_msg_hdr inside the buffer
 *
 * Handles the repeated pattern: check hif_handle, alloc from pool,
 * fill type/seq_num/dbg_indx in the header.
 *
 * Return: 0 on success, -ENODEV if hif not ready, -ENOSPC if pool full
 */
static inline int
dp_wifi_transport_ctrl_msg_alloc(struct wifi_transport_host_context *wtc,
				 uint32_t msg_type,
				 struct wifi_transport_ce_msg_buf **msg_buf_out,
				 xpan_msg_hdr **hdr_out)
{
	struct wifi_transport_ce_msg_buf *msg_buf;
	xpan_msg_hdr *hdr;

	msg_buf = dp_wifi_transport_ce_msg_alloc(wtc);
	if (!msg_buf) {
		dp_err("Unable to allocate ms_buf");
		return -ENOSPC;
	}

	hdr = (xpan_msg_hdr *)((uint8_t *)qdf_nbuf_data(msg_buf->nbuf) +
			       HTC_HDR_LENGTH);
	hdr->msg_type = msg_type;
	hdr->msg_id   = wtc->msg_seq_num++;
	hdr->dbg_indx = 0;

	*msg_buf_out = msg_buf;
	*hdr_out     = hdr;

	return 0;
}

/**
 * dp_wifi_transport_ctrl_msg_send() - send a control message, freeing on error
 * @wtc: session context
 * @msg_buf: CE msg buffer to send
 * @nbytes: xpan payload size (excluding HTC header)
 *
 * Return: 0 on success, -EIO if CE ring is full (msg_buf freed)
 */
static inline int
dp_wifi_transport_ctrl_msg_send(struct wifi_transport_host_context *wtc,
				struct wifi_transport_ce_msg_buf *msg_buf,
				uint32_t nbytes)
{
	int ret = dp_wifi_transport_msg_send(wtc, wtc->hif_handle,
					     msg_buf, nbytes);

	if (ret)
		dp_wifi_transport_ce_msg_free(wtc, msg_buf);
	return ret;
}

/**
 * dp_wifi_transport_wt_to_xpan_filter_type() - Convert wifi_transport param
 *     type to XPAN_RX_FILTER_TYPE for the CE0 CCE filter TLV
 * @type: WT_PARAM_SET_CCE_FILTER_* value
 *
 * Return: XPAN_RX_FILTER_TYPE value
 */
static inline uint8_t
dp_wifi_transport_wt_to_xpan_filter_type(enum wifi_transport_param_type type)
{
	switch (type) {
	case WT_PARAM_SET_CCE_FILTER_UDP_DATA:
		return XPAN_UDP_DATA;
	case WT_PARAM_SET_CCE_FILTER_UDP_TIME_SYNC:
		return XPAN_UDP_TIME_SYNC;
	case WT_PARAM_SET_CCE_FILTER_ETHERTYPE:
		return XPAN_ETHERTYPE;
	case WT_PARAM_SET_CCE_FILTER_UDP_HEARTBEAT:
		return XPAN_UDP_HEARTBEAT;
	default:
		return XPAN_UDP_DATA;
	}
}

int dp_wifi_transport_set_params(struct wlan_dp_psoc_context *dp_ctx,
				 enum wifi_transport_param_type type,
				 const union wifi_transport_param_data *data)
{
	struct wifi_transport_host_context *wtc =
				dp_wifi_transport_ctx_get(dp_ctx);
	struct wifi_transport_ce_msg_buf *msg_buf;
	xpan_set_burst_interval_info *msg_burst;
	xpan_set_rx_filter_info *msg_cce;
	xpan_mode *msg_mode;
	xpan_msg_hdr *hdr;
	uint32_t nbytes;
	int ret = 0;

	if (!wtc) {
		dp_err("No active session");
		return -ENODEV;
	}

	switch (type) {
	case WT_PARAM_BMPS_DISABLE:
		ret = dp_wifi_transport_bmps_set(dp_ctx, wtc,
						 data->bmps_disable);
		break;
	case WT_PARAM_SET_MODE:
		dp_info("SET_MODE mode=%u", data->mode);
		nbytes = sizeof(xpan_msg_hdr) + sizeof(xpan_mode);
		ret = dp_wifi_transport_ctrl_msg_alloc(wtc, XPAN_MODE,
						       &msg_buf, &hdr);
		if (ret)
			break;
		msg_mode = (xpan_mode *)((uint8_t *)hdr + sizeof(xpan_msg_hdr));
		LPASSCETLV_SET_HDR(msg_mode,
				   LPASSCETLV_TAG_STRUC_xpan_mode_fixed_param,
				   LPASSCETLV_GET_STRUCT_TLVLEN(xpan_mode));
		msg_mode->link_mode = data->mode;
		ret = dp_wifi_transport_ctrl_msg_send(wtc, msg_buf, nbytes);
		break;
	case WT_PARAM_SET_BURST_INTERVAL:
		dp_info("SET_BURST_INTERVAL ms=%u", data->burst_interval_ms);
		nbytes = sizeof(xpan_msg_hdr) +
			 sizeof(xpan_set_burst_interval_info);
		ret = dp_wifi_transport_ctrl_msg_alloc(
				wtc, XPAN_SET_BURST_INTERVAL,
				&msg_buf, &hdr);
		if (ret)
			break;
		msg_burst = (xpan_set_burst_interval_info *)
			((uint8_t *)hdr + sizeof(xpan_msg_hdr));
		LPASSCETLV_SET_HDR(
		msg_burst,
		LPASSCETLV_TAG_STRUC_xpan_set_burst_interval_fixed_param,
		LPASSCETLV_GET_STRUCT_TLVLEN(
			xpan_set_burst_interval_info));
		msg_burst->burst_interval_ms = data->burst_interval_ms;
		ret = dp_wifi_transport_ctrl_msg_send(wtc, msg_buf, nbytes);
		break;
	case WT_PARAM_SET_CCE_FILTER_UDP_DATA:
	case WT_PARAM_SET_CCE_FILTER_UDP_TIME_SYNC:
	case WT_PARAM_SET_CCE_FILTER_ETHERTYPE:
	case WT_PARAM_SET_CCE_FILTER_UDP_HEARTBEAT:
		dp_info("SET_CCE_FILTER type=%d port=%u", type, data->port);
		nbytes = sizeof(xpan_msg_hdr) + sizeof(xpan_set_rx_filter_info);
		ret = dp_wifi_transport_ctrl_msg_alloc(
				wtc, XPAN_SET_RX_CCE_FILTER,
				&msg_buf, &hdr);
		if (ret)
			break;
		msg_cce = (xpan_set_rx_filter_info *)
			((uint8_t *)hdr + sizeof(xpan_msg_hdr));
		LPASSCETLV_SET_HDR(
			msg_cce,
			LPASSCETLV_TAG_STRUC_xpan_set_rx_cce_filter_fixed_param,
			LPASSCETLV_GET_STRUCT_TLVLEN(xpan_set_rx_filter_info));
		msg_cce->filter_value = data->port;
		msg_cce->filter_type  =
			dp_wifi_transport_wt_to_xpan_filter_type(type);
		msg_cce->enable       = 1;
		ret = dp_wifi_transport_ctrl_msg_send(wtc, msg_buf, nbytes);
		break;
	default:
		dp_err("unknown param type=%d", type);
		ret = -EINVAL;
		break;
	}
	dp_wifi_transport_ctx_put(wtc);

	return ret;
}

int dp_wifi_transport_get_params(struct wlan_dp_psoc_context *dp_ctx,
				 enum wifi_transport_param_type type,
				 union wifi_transport_param_data *data)
{
	int ret = 0;

	dp_info("get_params type=%d", type);

	switch (type) {
	case WT_PARAM_GET_FW_QTIMER:
		data->qtimer_val = hal_get_fw_qtime(dp_ctx->hal_soc);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#endif /* FEATURE_WIFI_TRANSPORT */
