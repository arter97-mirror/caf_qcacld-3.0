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
 * DOC: N79-WLAN 5 GHz antenna sharing coexistence — core internal API
 */

#ifndef _WLAN_N79_COEX_H_
#define _WLAN_N79_COEX_H_

#include "wlan_coex_main.h"
#include "wlan_objmgr_vdev_obj.h"

#ifdef FEATURE_N79_COEX

/**
 * wlan_coex_n79_psoc_init() - initialise N79 state in a newly created psoc obj
 * @psoc_obj: coex psoc object
 * @psoc: owning psoc (stored as back-pointer for timer softirq callbacks)
 *
 * Called from wlan_coex_psoc_created_notification(). Stores the psoc
 * back-pointer, clears n79_coex_active, and initialises both hysteresis
 * timers. Timer durations are read from CFG_INT at first start time.
 *
 * Return: QDF_STATUS_SUCCESS on success; error otherwise
 */
QDF_STATUS wlan_coex_n79_psoc_init(struct coex_psoc_obj *psoc_obj,
				   struct wlan_objmgr_psoc *psoc);

/**
 * wlan_coex_n79_psoc_deinit() - stop and free N79 timers before psoc detach
 * @psoc_obj: coex psoc object
 *
 * Called from wlan_coex_psoc_destroyed_notification() before qdf_mem_free().
 */
void wlan_coex_n79_psoc_deinit(struct coex_psoc_obj *psoc_obj);

/**
 * wlan_coex_n79_vdev_init() - zero-initialise per-vdev N79 state
 * @vdev_obj: coex vdev object
 *
 * Called from wlan_coex_vdev_created_notification() after allocating the
 * vdev object.
 */
void wlan_coex_n79_vdev_init(struct coex_vdev_obj *vdev_obj);

/**
 * wlan_coex_n79_vdev_deinit() - clear per-vdev N79 state on vdev destroy
 * @vdev_obj: coex vdev object
 */
static inline void
wlan_coex_n79_vdev_deinit(struct coex_vdev_obj *vdev_obj)
{
}

/**
 * wlan_coex_n79_apply_active_vdev() - apply 2x2 NSS/chain constraint
 * @psoc: psoc object
 * @vdev: vdev object
 * @arg: unused iterator argument
 */
void wlan_coex_n79_apply_active_vdev(struct wlan_objmgr_psoc *psoc,
				     struct wlan_objmgr_vdev *vdev,
				     void *arg);

/**
 * wlan_coex_n79_restore_vdev() - restore pre-N79 NSS/chains on one vdev
 * @psoc: psoc object
 * @vdev: vdev object
 * @arg: unused iterator argument
 */
void wlan_coex_n79_restore_vdev(struct wlan_objmgr_psoc *psoc,
				struct wlan_objmgr_vdev *vdev,
				void *arg);

/**
 * wlan_coex_n79_activate_vdev() - per-vdev iterator for N79-active path
 * @psoc: psoc object (passed by wlan_objmgr_iterate_obj_list)
 * @object: vdev object (cast from void * per wlan_objmgr_op_handler)
 * @arg: unused iterator argument
 *
 * Called on the NL80211/vendor-cmd thread via iterate_obj_list. For each
 * 5 GHz vdev where rx_nss, tx_nss, num_rx_chains, or num_tx_chains exceeds
 * the N79 limit, applies 2x2 constraint via WMI.
 */
void wlan_coex_n79_activate_vdev(struct wlan_objmgr_psoc *psoc,
				 void *object,
				 void *arg);

/**
 * wlan_coex_n79_active() - apply N79 active state to all 5 GHz vdevs
 * @psoc: psoc object
 *
 * Stops any pending restore timers, sets n79_coex_active=true, then
 * iterates all vdevs via wlan_coex_n79_activate_vdev() (TDLS teardown +
 * per-vdev WMI post on the vendor-cmd thread).
 */
void wlan_coex_n79_active(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_coex_n79_inactive() - start N79 hysteresis timers for restore
 * @psoc: psoc object
 *
 * Clears n79_coex_active and starts both SS and RxDiv hysteresis timers.
 * Restore WMI is sent when the timers fire.
 */
void wlan_coex_n79_inactive(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_coex_n79_event() - dispatch a WLAN connection-state event
 * @psoc: psoc object
 * @vdev: vdev object
 * @evt: event type (connect/disconnect/start/stop)
 *
 * On connect/start: if N79 is active, applies 2x2 to the new vdev.
 * On disconnect/stop: clears per-vdev saved NSS state.
 *
 * Return: QDF_STATUS_SUCCESS on success; QDF_STATUS_E_INVAL if psoc
 *         coex object is unavailable
 */
QDF_STATUS wlan_coex_n79_event(struct wlan_objmgr_psoc *psoc,
			       struct wlan_objmgr_vdev *vdev,
			       enum wlan_coex_n79_event evt);

/**
 * wlan_coex_n79_nss_chain_vdev_up_req() - handle NSS/chains config update
 *   when N79 may be active
 * @vdev: vdev object
 * @params: requested NSS/chains config (5 GHz fields used)
 *
 * If N79 is inactive or the vdev is not on 5 GHz, returns false.
 * If N79 is active and the requested rx_nss or num_rx_chains exceeds the
 * N79 limit, saves the values as the restore target and returns true
 * (request deferred — caller must not apply the config now).
 *
 * Return: true if request was deferred; false to proceed with normal apply
 */
bool wlan_coex_n79_nss_chain_vdev_up_req(
			struct wlan_objmgr_vdev *vdev,
			const struct wlan_mlme_nss_chains *params);

#else /* !FEATURE_N79_COEX */

static inline QDF_STATUS
wlan_coex_n79_psoc_init(struct coex_psoc_obj *psoc_obj,
			struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline void
wlan_coex_n79_psoc_deinit(struct coex_psoc_obj *psoc_obj)
{
}

#endif /* FEATURE_N79_COEX */
#endif /* _WLAN_N79_COEX_H_ */
