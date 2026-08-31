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
 * DOC: wlan_dp_wifi_transport.h
 *
 * Internal header for the WiFi Transport core layer.
 * Defines the per-session context struct, pool constants, enums,
 * and the core API consumed by the dispatcher layer only.
 * Not included directly by OS-IF.
 */

#ifndef _WLAN_DP_WIFI_TRANSPORT_H_
#define _WLAN_DP_WIFI_TRANSPORT_H_

#ifdef FEATURE_WIFI_TRANSPORT

#include "wlan_dp_priv.h"
#include "lpass_ce/lpass_ce.h"
#include <wifi_transport_aux.h>
#include "htc_api.h"
#include <qdf_lock.h>
#include <qdf_mem.h>
#include <qdf_types.h>

/* Byte offset from MAC address start to the upper 2 bytes (bytes 4-5) */
#define WIFI_TP_MAC_HI_OFF      4
/*
 * Max xpan_tx_buffer_info TLVs per CE0 HTC message.
 * Formula: (2048 - HTC_HDR(8) - xpan_msg_hdr(8)
 *           - xpan_tx_audio_fixed_param(12))
 *          / xpan_tx_buffer_info(28) = 72
 */
#define WIFI_TRANSPORT_MAX_BUFS_PER_CE_MSG    72
/* Max ms to wait for all session reference holders to drain at deinit */
#define WIFI_TRANSPORT_REF_DRAIN_TIMEOUT_MS      3000
/* Max ms to wait for FW XPAN_DIRECT_LINK_DOWN_RESPONSE ack at deinit */
#define WIFI_TRANSPORT_DL_DOWN_TIMEOUT_MS        1000
/*
 * TX CE buffer pool depth: 5 TWT_SI x 38 PKT_PER_SP x 2 = 380.
 * sw_cookie is HOST-assigned from cookie_array[TP]; range 0-379.
 * Host bounds-checks cookie < WIFI_TRANSPORT_TX_BUF_POOL_SIZE before
 * indexing buf_array[].
 */
#define WIFI_TRANSPORT_TX_BUF_POOL_SIZE       380
/* TX CE buffer size: matches CE0 nbytes_max (2048 B) */
#define WIFI_TRANSPORT_TX_CE_BUF_SIZE         2048
/* CE0 framing buffer: 2048 B matches host_ce_config nbytes_max */
#define WIFI_TRANSPORT_CE_MSG_BUF_SIZE        2048
/* CE0 pool depth = src_nentries from host_ce_config (32) */
#define WIFI_TRANSPORT_CE_MSG_POOL_SIZE       32

/**
 * enum wifi_transport_init_step - bitmask tracking session init steps
 * @WIFI_TRANSPORT_INIT_PWR_VOTE: power-save vote taken
 * @WIFI_TRANSPORT_INIT_TX_BUF_POOL: TX CE buffer pool allocated and mapped
 * @WIFI_TRANSPORT_INIT_CE_MSG_POOL: CE0 control-message pool allocated
 */
enum wifi_transport_init_step {
	WIFI_TRANSPORT_INIT_PWR_VOTE      = BIT(0),
	WIFI_TRANSPORT_INIT_TX_BUF_POOL   = BIT(1),
	WIFI_TRANSPORT_INIT_CE_MSG_POOL   = BIT(2),
};

/**
 * enum wifi_transport_link_step - bitmask tracking link-init steps
 * @WIFI_TRANSPORT_LINK_HTC_CONNECT: HTC connected (provides lpass_ep_id)
 * @WIFI_TRANSPORT_LINK_CE_CALLBACKS: ce_send_cb and ce_recv_cb registered on
 *   CE0/CE5 (overrides HTC's default callbacks for direct CE path)
 * @WIFI_TRANSPORT_LINK_REFILL_RING: LPASS refill SRNG created
 */
enum wifi_transport_link_step {
	WIFI_TRANSPORT_LINK_HTC_CONNECT   = BIT(0),
	WIFI_TRANSPORT_LINK_CE_CALLBACKS  = BIT(1),
	WIFI_TRANSPORT_LINK_REFILL_RING   = BIT(2),
};

/**
 * enum wifi_transport_bmps_state - BMPS power-save state machine
 * @WIFI_TRANSPORT_BMPS_DISABLED: BMPS off — audio session active
 * @WIFI_TRANSPORT_BMPS_ENABLED: BMPS on — link idle
 * @WIFI_TRANSPORT_BMPS_TRANSIT: QMI in flight — block re-entry
 */
enum wifi_transport_bmps_state {
	WIFI_TRANSPORT_BMPS_DISABLED,
	WIFI_TRANSPORT_BMPS_ENABLED,
	WIFI_TRANSPORT_BMPS_TRANSIT,
};

/**
 * enum wifi_transport_path_state - audio data path state
 * @WIFI_TRANSPORT_PATH_INACTIVE: no active session
 * @WIFI_TRANSPORT_PATH_ACTIVE: session running, CE path active
 */
enum wifi_transport_path_state {
	WIFI_TRANSPORT_PATH_INACTIVE,
	WIFI_TRANSPORT_PATH_ACTIVE,
};

/**
 * struct wifi_transport_ce_msg_buf - CE0 framing buffer (nbuf-backed)
 * @nbuf: pre-allocated, DMA-mapped (TO_DEVICE) nbuf; paddr cached via
 *        qdf_nbuf_mapped_paddr_get().  Data written to qdf_nbuf_data().
 * @bufs_added: number of xpan_tx_buffer_info TLVs accumulated
 * @next: free-list linkage (NULL when in use)
 */
struct wifi_transport_ce_msg_buf {
	qdf_nbuf_t                     nbuf;
	uint32_t                       bufs_added;
	struct wifi_transport_ce_msg_buf *next;
};

/**
 * struct wifi_transport_ce_buf_entry - single TX CE audio buffer (nbuf-backed)
 * @nbuf: pre-allocated, DMA-mapped (TO_DEVICE) nbuf.  Audio payload copied to
 *        qdf_nbuf_data().  DMA address cached via qdf_nbuf_mapped_paddr_get().
 */
struct wifi_transport_ce_buf_entry {
	qdf_nbuf_t nbuf;
};

/**
 * struct wifi_transport_stats - WiFi Transport TX/RX path statistics
 * @tx_enqueued: packets submitted to CE0 TX path via dp_wifi_transport_tx
 * @tx_completed: total TX completions received from FW
 * @tx_status: TX completions status stats
 * @tx_pool_exhausted: TX buf pool was full at packet submit time
 * @tx_ce_msg_exhausted: CE0 framing pool was full at TX or control-msg time
 * @tx_deinit_drop: packets dropped because session deinit is in progress
 * @tx_ce_send_fail: CE0 ring full during flush; cookies returned to pool
 * @rx_indications: XPAN_RX_INDICATION messages received from FW
 * @rx_packets: audio buffers delivered to ops.process_rx
 * @rx_drop_invalid_cookie: buffers dropped due to unresolvable sw_cookie
 * @rx_refill_ring_full: refill ring full; descriptor returned to MAC pool
 * @rx_peer_stats: XPAN_PEER_STATS messages received and delivered
 * @ctrl_msg_fail: control message (mode/burst/CCE/link) CE0 send failures
 * @direct_link_up_sent: XPAN_DIRECT_LINK_UP_INDICATION control messages sent
 * @direct_link_down_sent: XPAN_DIRECT_LINK_DOWN_REQUEST control messages sent
 */
struct wifi_transport_stats {
	uint32_t tx_enqueued;
	uint32_t tx_completed;
	uint32_t tx_status[XPAN_PKT_DROP + 1];
	uint32_t tx_pool_exhausted;
	uint32_t tx_ce_msg_exhausted;
	uint32_t tx_deinit_drop;
	uint32_t tx_ce_send_fail;

	uint32_t rx_indications;
	uint32_t rx_packets;
	uint32_t rx_drop_invalid_cookie;
	uint32_t rx_refill_ring_full;
	uint32_t rx_peer_stats;

	uint32_t ctrl_msg_fail;
	uint32_t direct_link_up_sent;
	uint32_t direct_link_down_sent;
};

/**
 * struct wifi_transport_host_context - per-session core state object
 * @ref_cnt: reference count; last put triggers dp_wifi_transport_release()
 * @ce_msg_lock: Spinlock protecting ce_msg_free_head
 * @deregister_done: signalled when ref_cnt reaches zero at teardown
 * @dl_down_completion: signalled on XPAN_DIRECT_LINK_DOWN_RESPONSE
 * @ops: .ko callbacks stored at init time
 * @lpass_ep_id: HTC endpoint ID for the LPASS_DATA_MSG_SVC service
 * @rx_refill_ring: LPASS RXDMA refill SRNG (640 x 8 B descriptors)
 * @qdf_dev: qdf_dev handle
 * @hif_handle: hif handle
 * @buf_array: 380 pre-allocated DMA-mapped TX CE audio buffers
 * @cookie_array: circular free-index queue; cookie_array[i]=i at Init
 * @buf_pool_hp: cookie ring head — written by TX completion (BH) and error
 *        paths; protected by buf_pool_lock
 * @buf_pool_tp: cookie ring tail — written by TX alloc (process); protected
 *        by buf_pool_lock
 * @buf_pool_free_count: available slots; protected by buf_pool_lock
 * @buf_pool_lock: _bh spinlock serialising TX alloc (process) vs TX
 *        completion (BH) and SSR reset
 * @ce_msg_pool: 32 pre-allocated DMA-mapped TX framing buffers
 * @ce_msg_free_head: head of msg_buf free list (protected by ce_msg_lock)
 * @msg_seq_num: xpan_msg_hdr sequence counter
 * @init_mask: bitmask of completed init steps (enum wifi_transport_init_step)
 * @bmps_state: current BMPS state; atomic because set_params() runs lockless
 * @cur_msg_buf: TX burst buffer currently being assembled; TX-only state
 *        touched exclusively from dp_wifi_transport_tx() and the deinit
 *        path; WiFi Transport driver contract is single-threaded TX, so
 *        no BH lock is needed
 * @ready_head: head of the queue of fully-assembled CE0 framing buffers
 *        waiting to be flushed to FW, in submission order
 * @ready_tail: tail pointer for the ready queue; NULL when queue is empty
 * @stats: Tx/Rx Path statistics
 *
 * Lock hierarchy (must acquire in order — never reverse):
 *   L1: dp_ctx->wifi_transport_host_ctx_lock  (pointer guard)
 *   L2: wtc->ce_msg_lock                      (pool free-lists)
 *   L3: wtc->ce_index_lock                    (CE send serialization)
 * L2 and L3 are siblings and must never be nested with each other.
 */
struct wifi_transport_host_context {
	qdf_atomic_t                 ref_cnt;
	qdf_spinlock_t               ce_msg_lock;
	qdf_event_t                  deregister_done;
	qdf_event_t                  dl_down_completion;
	struct wifi_transport_ops    ops;
	HTC_ENDPOINT_ID              lpass_ep_id;
	struct dp_srng              *rx_refill_ring;
	qdf_device_t                 qdf_dev;
	struct hif_opaque_softc     *hif_handle;
	struct wifi_transport_ce_buf_entry
			buf_array[WIFI_TRANSPORT_TX_BUF_POOL_SIZE];
	uint16_t  cookie_array[WIFI_TRANSPORT_TX_BUF_POOL_SIZE];
	uint32_t                     buf_pool_hp;
	uint32_t                     buf_pool_tp;
	uint32_t                     buf_pool_free_count;
	qdf_spinlock_t               buf_pool_lock;
	struct wifi_transport_ce_msg_buf
			ce_msg_pool[WIFI_TRANSPORT_CE_MSG_POOL_SIZE];
	struct wifi_transport_ce_msg_buf *ce_msg_free_head;
	uint16_t                     msg_seq_num;
	uint32_t                     init_mask;
	qdf_atomic_t                 bmps_state;
	struct wifi_transport_ce_msg_buf *cur_msg_buf;
	struct wifi_transport_ce_msg_buf *ready_head;
	struct wifi_transport_ce_msg_buf *ready_tail;
	struct wifi_transport_stats      stats;
};

/**
 * dp_wifi_transport_tx() - submit one audio buffer for TX
 * @dp_ctx: DP psoc context
 * @nbuf: audio skb from the WiFi Transport driver; payload is copied into
 *       the pre-mapped TX pool and skb is freed immediately; caller must
 *       not dereference after return
 * @meta: TX metadata (dest MAC, QoS, TTP, checksum)
 * @flush: if true, flush all pending TX CE messages immediately
 *
 * Return: 0 on success, -ENOSPC if TX pool or msg pool exhausted,
 *         -ENODEV if session inactive
 */
int dp_wifi_transport_tx(struct wlan_dp_psoc_context *dp_ctx,
			 qdf_nbuf_t nbuf,
			 struct wifi_transport_tx_metadata *meta,
			 bool flush);

/**
 * dp_wifi_transport_link_init() - connect HTC service and register CE callbacks
 * @dp_ctx: DP psoc context
 *
 * Connects LPASS_DATA_MSG_SVC via HTC to obtain lpass_ep_id, registers
 * WiFi Transport TX/RX CE callbacks via hif_wifi_transport_register_callbacks,
 * and creates the LPASS RXDMA refill ring.
 *
 * Return: QDF_STATUS_SUCCESS or error code
 */
QDF_STATUS
dp_wifi_transport_link_init(struct wlan_dp_psoc_context *dp_ctx);

/**
 * dp_wifi_transport_link_deinit() - deregister CE callbacks, destroy ring
 * @dp_ctx: DP psoc context
 *
 * Return: None
 */
void
dp_wifi_transport_link_deinit(struct wlan_dp_psoc_context *dp_ctx);

/**
 * dp_wifi_transport_init() - allocate session context and set up TX/RX pools
 * @dp_ctx: DP psoc context
 * @ops: WiFi Transport driver callbacks for TX completion and RX indication;
 *       copied into wtc at init time; caller owns the struct but it must
 *       remain valid for the session lifetime
 *
 * Return: QDF_STATUS_SUCCESS or error code
 */
QDF_STATUS
dp_wifi_transport_init(struct wlan_dp_psoc_context *dp_ctx,
		       const struct wifi_transport_ops *ops);

/**
 * dp_wifi_transport_deinit() - tear down CE path and free session context
 * @dp_ctx: DP psoc context
 * @is_recovery_stop: true skips FW handshake and frees in-flight TX skbs
 */
void dp_wifi_transport_deinit(struct wlan_dp_psoc_context *dp_ctx,
			      bool is_recovery_stop);

/**
 * dp_wifi_transport_set_params() - apply a control parameter
 * @dp_ctx: DP psoc context
 * @type: parameter type (see enum wifi_transport_param_type)
 * @data: parameter payload
 *
 * Return: 0 on success, negative errno on failure
 */
int dp_wifi_transport_set_params(struct wlan_dp_psoc_context *dp_ctx,
				 enum wifi_transport_param_type type,
				 const union wifi_transport_param_data *data);

/**
 * dp_wifi_transport_get_params() - retrieve a control parameter
 * @dp_ctx: DP psoc context
 * @type: parameter type (see enum wifi_transport_param_type)
 * @data: output buffer
 *
 * Return: 0 on success, negative errno on failure
 */
int dp_wifi_transport_get_params(struct wlan_dp_psoc_context *dp_ctx,
				 enum wifi_transport_param_type type,
				 union wifi_transport_param_data *data);

/**
 * dp_wifi_transport_display_stats() - Print WiFi Transport TX/RX stats
 * @dp_ctx: DP psoc context
 *
 * Return: None
 */
void dp_wifi_transport_display_stats(struct wlan_dp_psoc_context *dp_ctx);
#else
static inline void
dp_wifi_transport_display_stats(struct wlan_dp_psoc_context *dp_ctx)
{
}

#endif /* FEATURE_WIFI_TRANSPORT */
#endif /* _WLAN_DP_WIFI_TRANSPORT_H_ */
