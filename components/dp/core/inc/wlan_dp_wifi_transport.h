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
#include <wifi_transport.h>
#include "htc_api.h"
#include <qdf_lock.h>
#include <qdf_mem.h>
#include <qdf_types.h>

/* CE0 framing buffer: 2048 B matches host_ce_config nbytes_max */
#define WIFI_TRANSPORT_CE_MSG_BUF_SIZE        2048
/* CE5 message buffer: 256 B matches !FEATURE_PKTLOG CE5 pipe profile */
#define WIFI_TRANSPORT_CE5_MSG_BUF_SIZE       256
/* CE0 pool depth = src_nentries from host_ce_config (32) */
#define WIFI_TRANSPORT_CE_MSG_POOL_SIZE       32
/* CE5 pool depth = dest_nentries (32) minus 2 HW-reserved slots */
#define WIFI_TRANSPORT_CE5_MSG_POOL_SIZE      30
/* TX descriptor slots: covers full 10-bit sw_cookie wire range */
#define WIFI_TRANSPORT_MAX_INFLIGHT_TX        1024

/**
 * enum wifi_transport_init_step - bitmask tracking completed Init steps
 * @WIFI_TRANSPORT_INIT_PWR_VOTE: power-save vote taken
 * @WIFI_TRANSPORT_INIT_HTC_CONNECT: HTC service connected
 * @WIFI_TRANSPORT_INIT_REFILL_RING: LPASS refill SRNG created
 * @WIFI_TRANSPORT_INIT_CE_MSG_POOL: CE0 framing pool allocated
 * @WIFI_TRANSPORT_INIT_CE5_MSG_POOL: CE5 framing pool allocated
 * @WIFI_TRANSPORT_INIT_CE_CB_REG: CE0/CE5 callbacks registered
 * @WIFI_TRANSPORT_INIT_IRQ_CFG: CE IRQs configured
 */
enum wifi_transport_init_step {
	WIFI_TRANSPORT_INIT_PWR_VOTE      = BIT(0),
	WIFI_TRANSPORT_INIT_HTC_CONNECT   = BIT(1),
	WIFI_TRANSPORT_INIT_REFILL_RING   = BIT(2),
	WIFI_TRANSPORT_INIT_CE_MSG_POOL   = BIT(3),
	WIFI_TRANSPORT_INIT_CE5_MSG_POOL  = BIT(4),
	WIFI_TRANSPORT_INIT_CE_CB_REG     = BIT(5),
	WIFI_TRANSPORT_INIT_IRQ_CFG       = BIT(6),
};

/**
 * enum wifi_transport_bmps_state - BMPS power-save state machine
 * @WIFI_TRANSPORT_BMPS_ENABLED: BMPS on, link idle
 * @WIFI_TRANSPORT_BMPS_TRANSIT_ENABLE: QMI enable in flight
 * @WIFI_TRANSPORT_BMPS_TRANSIT_DISABLE: QMI disable in flight
 * @WIFI_TRANSPORT_BMPS_DISABLED: BMPS off, audio active
 */
enum wifi_transport_bmps_state {
	WIFI_TRANSPORT_BMPS_ENABLED,
	WIFI_TRANSPORT_BMPS_TRANSIT_ENABLE,
	WIFI_TRANSPORT_BMPS_TRANSIT_DISABLE,
	WIFI_TRANSPORT_BMPS_DISABLED,
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
 * struct wifi_transport_tx_desc - single TX descriptor slot
 * @skb: skb from .ko; DMA-mapped on TX, unmapped and freed on completion
 */
struct wifi_transport_tx_desc {
	qdf_nbuf_t skb;
};

/**
 * struct wifi_transport_ce_msg_buf - one CE0 or CE5 framing buffer entry
 * @vaddr: kernel virtual address of the DMA-coherent buffer
 * @dma_addr: DMA address for hardware
 * @bufs_added: xpan_tx_buffer_info TLVs added to this CE0 message (CE0 only)
 * @next: free-list next index; sentinel = pool size constant
 */
struct wifi_transport_ce_msg_buf {
	void           *vaddr;
	qdf_dma_addr_t  dma_addr;
	uint32_t        bufs_added;
	uint32_t        next;
};

/**
 * struct wifi_transport_host_context - per-session core state object
 * @ref_cnt: reference count; last put triggers dp_wifi_transport_release()
 * @lock: BH spinlock protecting pool free-lists (L2 in lock hierarchy)
 * @ce_index_lock: BH spinlock serializing ce_send_nolock() calls (L3)
 * @deregister_done: signalled when ref_cnt reaches zero at teardown
 * @dl_down_completion: signalled on XPAN_DIRECT_LINK_DOWN_RESPONSE
 * @ops: .ko callbacks stored at init time
 * @lpass_ep_id: HTC endpoint ID for the LPASS_DATA_MSG_SVC service
 * @rx_refill_ring: LPASS RXDMA refill SRNG (640 x 8 B descriptors)
 * @ce5_msg_pool: CE5 framing buffer pool (30 x 256 B, DMA-coherent)
 * @ce5_msg_pool_base_vaddr: base virtual address of ce5_msg_pool region
 * @ce5_msg_pool_base_dma: base DMA address of ce5_msg_pool region
 * @host_desc: TX descriptor slots indexed by .ko sw_cookie
 * @free_head: head of TX descriptor free-list
 * @avail_desc: number of free TX descriptor slots
 * @ce_msg_pool: CE0 framing buffer pool (32 x 2048 B, DMA-coherent)
 * @ce_msg_pool_base_vaddr: base virtual address of ce_msg_pool region
 * @ce_msg_pool_base_dma: base DMA address of ce_msg_pool region
 * @ce_msg_free_head: head index of CE0 free-list
 * @ce_msg_ready_head: head index of CE0 ready-to-send list
 * @ce_msg_ready_tail: tail index of CE0 ready-to-send list
 * @msg_seq_num: xpan_msg_hdr sequence counter
 * @init_mask: bitmask of completed init steps (enum wifi_transport_init_step)
 * @bmps_state: current BMPS state; atomic because set_params() runs lockless
 *
 * Lock hierarchy (must acquire in order — never reverse):
 *   L1: dp_ctx->wifi_transport_host_ctx_lock  (pointer guard)
 *   L2: wtc->lock                             (pool free-lists)
 *   L3: wtc->ce_index_lock                    (CE send serialization)
 * L2 and L3 are siblings and must never be nested with each other.
 */
struct wifi_transport_host_context {
	qdf_atomic_t                 ref_cnt;
	qdf_spinlock_t               lock;
	qdf_spinlock_t               ce_index_lock;
	qdf_event_t                  deregister_done;
	qdf_event_t                  dl_down_completion;
	struct wifi_transport_ops    ops;
	HTC_ENDPOINT_ID              lpass_ep_id;
	struct dp_srng              *rx_refill_ring;
	struct wifi_transport_ce_msg_buf
			ce5_msg_pool[WIFI_TRANSPORT_CE5_MSG_POOL_SIZE];
	void                        *ce5_msg_pool_base_vaddr;
	qdf_dma_addr_t               ce5_msg_pool_base_dma;
	struct wifi_transport_tx_desc
			host_desc[WIFI_TRANSPORT_MAX_INFLIGHT_TX];
	uint32_t                     free_head;
	uint32_t                     avail_desc;
	struct wifi_transport_ce_msg_buf
			ce_msg_pool[WIFI_TRANSPORT_CE_MSG_POOL_SIZE];
	void                        *ce_msg_pool_base_vaddr;
	qdf_dma_addr_t               ce_msg_pool_base_dma;
	uint32_t                     ce_msg_free_head;
	uint32_t                     ce_msg_ready_head;
	uint32_t                     ce_msg_ready_tail;
	uint16_t                     msg_seq_num;
	uint32_t                     init_mask;
	qdf_atomic_t                 bmps_state;
};

/**
 * dp_wifi_transport_init() - allocate session context and set up CE path
 * @dp_ctx: DP psoc context
 *
 * Return: QDF_STATUS_SUCCESS or error code
 */
QDF_STATUS
dp_wifi_transport_init(struct wlan_dp_psoc_context *dp_ctx);

/**
 * dp_wifi_transport_deinit() - tear down CE path and free session context
 * @dp_ctx: DP psoc context
 * @is_recovery_stop: true skips FW handshake and frees in-flight TX skbs
 */
void dp_wifi_transport_deinit(struct wlan_dp_psoc_context *dp_ctx,
			      bool is_recovery_stop);

#endif /* FEATURE_WIFI_TRANSPORT */
#endif /* _WLAN_DP_WIFI_TRANSPORT_H_ */
