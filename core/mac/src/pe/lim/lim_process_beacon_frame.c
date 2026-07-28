/*
 * Copyright (c) 2011-2021 The Linux Foundation. All rights reserved.
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

/*
 *
 * This file lim_process_beacon_frame.cc contains the code
 * for processing Received Beacon Frame.
 * Author:        Chandra Modumudi
 * Date:          03/01/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 *
 */

#include "wni_cfg.h"
#include "ani_global.h"
#include "sch_api.h"
#include "utils_api.h"
#include "lim_types.h"
#include "lim_utils.h"
#include "lim_assoc_utils.h"
#include "lim_prop_exts_utils.h"
#include "lim_ser_des_utils.h"
#include "wlan_mlo_t2lm.h"
#include "wlan_mlo_mgr_roam.h"
#include "lim_mlo.h"
#include "wlan_mlo_mgr_sta.h"
#include "wlan_cm_api.h"
#include "wlan_mlme_api.h"
#include "wlan_objmgr_vdev_obj.h"
#include "wlan_reg_services_api.h"
#ifdef WLAN_FEATURE_11BE_MLO
#include <cds_ieee80211_common.h>
#endif
#include "wlan_t2lm_api.h"
#include "wma.h"

/*Invalid Recommended Max Simultaneous Links value */
#define RESERVED_REC_LINK_VALUE 1

#ifdef WLAN_FEATURE_11BE_MLO

void lim_process_bcn_prb_rsp_t2lm(struct mac_context *mac_ctx,
				  struct pe_session *session,
				  tpSirProbeRespBeacon bcn_ptr)
{
	struct wlan_objmgr_vdev *vdev;
	struct wlan_t2lm_context *t2lm_ctx;

	if (!session || !bcn_ptr || !mac_ctx) {
		pe_err("invalid input parameters");
		return;
	}

	if (!wlan_mlme_get_t2lm_negotiation_supported(mac_ctx->psoc)) {
		pe_err_rl("T2LM negotiation not supported");
		return;
	}

	vdev = session->vdev;
	if (!vdev || !wlan_vdev_mlme_is_mlo_vdev(vdev))
		return;

	if (!wlan_cm_is_vdev_connected(vdev))
		return;

	if (!mlo_check_if_all_links_up(vdev))
		return;

	t2lm_ctx = &vdev->mlo_dev_ctx->t2lm_ctx;

	qdf_mem_copy((uint8_t *)&t2lm_ctx->tsf, (uint8_t *)bcn_ptr->timeStamp,
		     sizeof(uint64_t));
	wlan_update_t2lm_mapping(vdev, &bcn_ptr->t2lm_ctx, t2lm_ctx->tsf);
}

static uint8_t valid_max_rec_links(uint8_t value)
{
	if (value > RESERVED_REC_LINK_VALUE &&
	    value <= WLAN_DEFAULT_REC_LINK_VALUE)
		return value;
	return WLAN_DEFAULT_REC_LINK_VALUE;
}

/**
 * lim_set_link_force_mode_for_cac() - Set link inactive or no_force
 * @vdev: vdev
 * @inactive_mode: inactive mode or not
 *
 * This function is called when radar is detected during or after CAC.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
lim_set_link_force_mode_for_cac(struct wlan_objmgr_vdev *vdev,
				bool inactive_mode)
{
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;
	uint8_t link_vdev_id;

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		pe_err("null psoc");
		return QDF_STATUS_E_INVAL;
	}

	link_vdev_id = wlan_vdev_get_id(vdev);

	if (inactive_mode) {
		pe_debug("vdev %d: Set INACTIVE", link_vdev_id);
		/* Call policy manager to set link inactive */
		status = policy_mgr_mlo_sta_set_link(psoc,
						     MLO_LINK_FORCE_REASON_CONNECT,
						     MLO_LINK_FORCE_MODE_INACTIVE,
						     1,
						     &link_vdev_id);
	} else {
		pe_debug("vdev %d: Set NO_FORCE", link_vdev_id);
		/* Call policy manager to clear force-inactive */
		status = policy_mgr_mlo_sta_set_link(psoc,
						     MLO_LINK_FORCE_REASON_CONNECT,
						     MLO_LINK_FORCE_MODE_NO_FORCE,
						     1,
						     &link_vdev_id);
	}

	if (QDF_IS_STATUS_ERROR(status)) {
		pe_err("vdev %d: Failed to set inactive, status %d",
		       link_vdev_id, status);
		return status;
	}

	pe_debug("vdev %d: Successfully set", link_vdev_id);

	return QDF_STATUS_SUCCESS;
}

/**
 * lim_sync_mlo_sta_cac_info_to_partners() - copy this session's
 * mlo_sta_cac_info to every other link's own pe_session in the same
 * MLO association
 * @session: pe session whose mlo_sta_cac_info was just updated
 *
 * lim_process_mcst_ie_and_csa() only ever updates the mlo_sta_cac_info
 * of the pe_session it was called with, but each MLO link has its own
 * pe_session with its own independent copy of mlo_sta_cac_info. Call
 * this right after a state-changing update so every partner link's
 * pe_session observes the same CAC/CSA state.
 *
 * Return none
 */
static void
lim_sync_mlo_sta_cac_info_to_partners(struct pe_session *session)
{
	struct wlan_objmgr_vdev *wlan_vdev_list[WLAN_UMAC_MLO_MAX_VDEVS];
	uint16_t vdev_count = 0;
	uint16_t i;
	struct mac_context *mac_ctx;
	struct pe_session *link_session;

	if (!session->vdev || !session->vdev->mlo_dev_ctx)
		return;

	mac_ctx = session->mac_ctx;
	if (!mac_ctx)
		return;

	mlo_get_partner_vdev_list(session->vdev, &vdev_count, wlan_vdev_list);
	for (i = 0; i < vdev_count; i++) {
		if (!wlan_vdev_list[i])
			continue;
		if (wlan_vdev_list[i] == session->vdev)
			goto release_ref;

		link_session = pe_find_session_by_vdev_id(mac_ctx,
							  wlan_vdev_get_id(wlan_vdev_list[i]));
		if (!link_session)
			goto release_ref;

		link_session->mlo_sta_cac_info = session->mlo_sta_cac_info;
		pe_debug("vdev %d: synced mlo_sta_cac_info from vdev %d (in_cac=%d state=%d cac_link=%d)",
			 wlan_vdev_get_id(wlan_vdev_list[i]), session->vdev_id,
			 session->mlo_sta_cac_info.mlo_link_in_cac,
			 session->mlo_sta_cac_info.link_state,
			 session->mlo_sta_cac_info.cac_link_id);
release_ref:
		mlo_release_vdev_ref(wlan_vdev_list[i]);
	}
}

/**
 * lim_process_mcst_ie_and_csa() - Process MCST IE and CSA in beacon
 * @pdev: pointer to pdev object
 * @session: pe session
 * @link_id: Link ID
 * @mcst_found: whether the MCST IE was found by the caller
 * @csa_found: CSA found in ML IE or not
 * @channel: CSA new channel
 * @is_self_link: true if link_id refers to session->vdev itself (the
 *                beacon was received directly on this link), false if
 *                link_id refers to a partner link found via a per-STA
 *                profile in this session's ML IE
 *
 * This function processes MCST IE and CSA IE in beacon and determines
 * the appropriate action based on CAC state.
 *
 * Decision logic based on design table:
 *
 * +-----------------------------+-------------+------------------------+------------------------+
 * | Scenario			 | New channel | 2G Beacon ML IE + 5G	| Action		 |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |1-1.Radar detected during CAC| DFS	       | CSA&MCST IE(2G)	| Do CSA		 |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |1-2.Radar detected during CAC| DFS	       | MCST IE(2G)		| wait CAC complete      |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |1-3.Radar detected during CAC| DFS	       | No MCST IE		| CAC  complete          |
 * |				 |	       |			| set no_force           |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |2.Radar detected during CAC	 | Non-DFS     | CSA(2G)		| Do CSA, set no_force   |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |3-1. Radar detected After CAC| DFS	       | CSA&MCST IE(2+5G)	| Do CSA                 |
 * |                             |             |                        | set force_inactive     |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |3-2. Radar detected After CAC| DFS	       |MCST IE(2G)		| wait CAC complete      |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |3-3. Radar detected After CAC| DFS	       |No MCST IE(2+5G)	| set no-force           |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |4. Radar detected After CAC	 | Non-DFS     | CSA (5G beacon)	| Do CSA		 |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |5-1.without radar detect	 | NA	       | MCST IE(2G)		| Do nothing, in CAC     |
 * | during CAC 		 |	       |			| period		 |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |5-2.without radar detect	 | NA	       | No MCST IE		| CAC  complete          |
 * |during CAC                   |	       |			| set no_force           |
 * +-----------------------------+-------------+------------------------+------------------------+
 * |6.without radar detect	 | NA	       | No MCST IE		| boot with non-dfs      |
 * |after CAC 		 	 |	       |			|                        |
 * +-----------------------------+-------------+------------------------+------------------------+
 *
 * Note: CSA IE only appears in ~10 beacons after radar detection,
 * then disappears. MCST IE persists as long as link is in CAC.
 *
 * Return: QDF_STATUS
 */
static void
lim_process_mcst_ie_and_csa(struct wlan_objmgr_pdev *pdev,
			    struct pe_session *session,
			    uint8_t link_id,
			    bool mcst_found,
			    uint8_t csa_found,
			    uint8_t channel,
			    bool is_self_link)
{
	qdf_freq_t new_chan_freq = 0;
	bool is_new_chan_dfs = false;
	struct wlan_objmgr_vdev *partner_vdev = NULL;
	bool partner_ref_taken = false;

	if (!session) {
		pe_err("invalid input parameters");
		return;
	}

	if (csa_found && channel) {
		struct mlo_link_info *linfo = NULL;
		struct ch_params ch_params = {0};
		enum channel_state ch_state;

		new_chan_freq = wlan_reg_legacy_chan_to_freq(pdev, channel);
		if (!new_chan_freq) {
			pe_err("Invalid channel %d", channel);
			return;
		}

		/*
		 * A primary-channel-only DFS check is insufficient for bonded
		 * channels: e.g. primary ch36 (5180 MHz) is non-DFS in 20 MHz
		 * but DFS in 160 MHz because the bonded range covers ch52-64.
		 * Use wlan_reg_get_5g_bonded_channel_state_for_pwrmode() with
		 * the link's actual ch_width so every sub-channel is checked.
		 * Fall back to the primary-only check when ch_width is
		 * unavailable or the link is operating at 20 MHz.
		 */
		if (session->vdev && session->vdev->mlo_dev_ctx)
			linfo = mlo_mgr_get_ap_link_by_link_id(session->vdev->mlo_dev_ctx,
							       link_id);
		if (linfo && linfo->link_chan_info &&
		    linfo->link_chan_info->ch_width > CH_WIDTH_20MHZ) {
			ch_params.ch_width = linfo->link_chan_info->ch_width;
			ch_state = wlan_reg_get_5g_bonded_channel_state_for_pwrmode(pdev,
										    new_chan_freq,
										    &ch_params,
										    REG_CURRENT_PWR_MODE);
			is_new_chan_dfs = (ch_state == CHANNEL_STATE_DFS);
		} else {
			is_new_chan_dfs = wlan_reg_is_dfs_for_freq(pdev,
								   new_chan_freq);
		}
	}

	pe_debug("vdev %d link %d: mcst=%d csa=%d new_chan=%d is_dfs=%d in_cac=%d state=%d",
		 session->vdev_id, link_id, mcst_found, csa_found,
		 channel, is_new_chan_dfs,
		 session->mlo_sta_cac_info.mlo_link_in_cac,
		 session->mlo_sta_cac_info.link_state);

	/*
	 * Look up the partner vdev once. The MCST IE is reported for link_id
	 * (the 5G DFS link) inside a 6G beacon; session->vdev is the 6G vdev.
	 * All force-mode operations must target the 5G partner vdev.
	 * Only needed when csa_found is true (Scenario 2 and 3-1).
	 * For a self-link beacon (received directly on the 5G link), link_id
	 * refers to session->vdev itself, so no lookup/ref is needed.
	 */
	if (csa_found) {
		if (is_self_link) {
			partner_vdev = session->vdev;
		} else {
			partner_vdev = mlo_get_vdev_by_link_id(session->vdev,
							       link_id,
							       WLAN_MLO_MGR_ID);
			partner_ref_taken = true;
		}
	}

	/* mlo_link_in_cac= true means during CAC,
	 * mlo_link_in_cac= false means after CAC
	 */
	if (session->mlo_sta_cac_info.mlo_link_in_cac) {
		if (mcst_found) {
			if (csa_found && is_new_chan_dfs) {
				/* Scenario 1-1: During CAC, radar detected
				 * do nothing
				 * keep inactive until new dfs chn CAC complete
				 */
				if (session->mlo_sta_cac_info.link_state ==
				    MLO_LINK_FORCE_DO_CSA) {
					if (partner_vdev && partner_ref_taken)
						mlo_release_vdev_ref(partner_vdev);
					return;
				}
				pe_debug("vdev %d link %d radar to DFS, do CSA",
					 session->vdev_id, link_id);
				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_DO_CSA;
				lim_sync_mlo_sta_cac_info_to_partners(session);
			} else if (!csa_found) {
				/* Scenario 1-2: radar detect to DFS,CSA done
				 * Scenario 5-1: during CAC,no radar detect
				 * do nothing ,wait for CAC completion
				 */
				if (session->mlo_sta_cac_info.link_state ==
				    MLO_LINK_FORCE_WAIT_CAC_DONE)
					return;
				pe_debug("vdev %d link %d chn:%d. during CAC",
					 session->vdev_id, link_id, channel);
				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_WAIT_CAC_DONE;
				lim_sync_mlo_sta_cac_info_to_partners(session);
			} else if (csa_found && !is_new_chan_dfs) {
				/* Scenario 2: During CAC, radar to non-DFS
				 * set no_force
				 */
				if (session->mlo_sta_cac_info.link_state ==
				    MLO_LINK_FORCE_SW2_NON_DFS) {
					if (partner_vdev && partner_ref_taken)
						mlo_release_vdev_ref(partner_vdev);
					return;
				}

				pe_debug("vdev %d link %d: sw2 non-DFS during CAC",
					 session->vdev_id, link_id);

				/* Clear CAC flag and activate link */
				session->mlo_sta_cac_info.mlo_link_in_cac = false;

				/*
				 * Activate the 5G partner link (link_id), not
				 * the 6G session vdev which received the beacon.
				 */
				if (partner_vdev)
					lim_set_link_force_mode_for_cac(partner_vdev,
									false);
				else
					pe_debug("vdev %d: no partner for link %d",
						 session->vdev_id, link_id);
				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_SW2_NON_DFS;
				lim_sync_mlo_sta_cac_info_to_partners(session);

			} else {
				pe_debug("vdev %d link %d chn:%d. unsupported",
					 session->vdev_id, link_id, channel);
			}
		}
	} else {
		/* after CAC or normal beacon*/
		if (mcst_found) {
			if (csa_found && is_new_chan_dfs) {
				/* Scenario 3-1:radar to new dfs chn after CAC
				 * set inactive for new CAC
				 */
				if (session->mlo_sta_cac_info.link_state ==
				    MLO_LINK_FORCE_DO_CSA) {
					if (partner_vdev && partner_ref_taken)
						mlo_release_vdev_ref(partner_vdev);
					return;
				}
				pe_debug("vdev %d link %d: after CAC, set inactive",
					 session->vdev_id, link_id);

				/*
				 * Set the 5G partner link (link_id) inactive.
				 * session->vdev is the 6G vdev receiving this
				 * beacon; the link doing CAC is the partner.
				 * Record cac_link_id so the CAC-complete path
				 * can look up the same partner without link_id.
				 */
				if (!partner_vdev) {
					pe_debug("vdev %d: no partner for link %d",
						 session->vdev_id, link_id);
					return;
				}
				lim_set_link_force_mode_for_cac(partner_vdev,
								true);
				session->mlo_sta_cac_info.mlo_link_in_cac = true;
				session->mlo_sta_cac_info.cac_link_id = link_id;
				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_DO_CSA;
				lim_sync_mlo_sta_cac_info_to_partners(session);
			} else if (!csa_found) {
				/* Scenario 3-2: radar to DFS after CAC
				 * wait CAC completed
				 * no log print
				 */
				if (session->mlo_sta_cac_info.link_state ==
				    MLO_LINK_FORCE_WAIT_CAC_DONE)
					return;
				pe_debug("vdev %d link %d  after CAC,wait CAC done",
					 session->vdev_id, link_id);
				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_WAIT_CAC_DONE;
				lim_sync_mlo_sta_cac_info_to_partners(session);
			} else if (csa_found && !is_new_chan_dfs) {
				/*Scenario 4: After CAC, radar to non-DFS
				 * set no_force
				 */
				if (session->mlo_sta_cac_info.link_state ==
				    MLO_LINK_FORCE_SW2_NON_DFS) {
					if (partner_vdev && partner_ref_taken)
						mlo_release_vdev_ref(partner_vdev);
					return;
				}

				pe_debug("vdev %d link %d: sw2 non-DFS after CAC",
					 session->vdev_id, link_id);

				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_SW2_NON_DFS;
				lim_sync_mlo_sta_cac_info_to_partners(session);

			} else {
				pe_debug("vdev %d link %d. unsupported state",
					 session->vdev_id, link_id);
			}
		}
	}

	if (partner_vdev && partner_ref_taken)
		mlo_release_vdev_ref(partner_vdev);
}

void lim_process_beacon_mlo(struct mac_context *mac_ctx,
			    struct pe_session *session,
			    tSchBeaconStruct *bcn_ptr)
{
	struct csa_offload_params csa_param;
	int i;
	uint8_t link_id;
	uint8_t *per_sta_pro;
	uint32_t per_sta_pro_len;
	uint8_t *sta_pro;
	uint32_t sta_pro_len;
	uint16_t stacontrol, chan_space;
	struct ieee80211_channelswitch_ie *csa_ie;
	struct ieee80211_extendedchannelswitch_ie *xcsa_ie;
	struct wlan_objmgr_vdev *vdev;
	struct wlan_objmgr_vdev *p_vdev;
	struct wlan_objmgr_pdev *pdev;
	struct wlan_mlo_dev_context *mlo_ctx;
	uint8_t is_sta_csa_synced;
	struct mlo_link_info *link_info;
	uint8_t sta_info_len = 0;
	uint8_t tmp_rec_value;
	uint8_t country_code[CDS_COUNTRY_CODE_LEN + 1];
	uint16_t opclass_width;
	bool csa_found = false;
	const uint8_t *mcst_ie;
	bool mcst_found = false;
	uint8_t mcst_ext_id = WLAN_EXTN_ELEMID_MAX_CHAN_SWITCH_TIME;

	if (!session || !bcn_ptr || !mac_ctx) {
		pe_err("invalid input parameters");
		return;
	}
	vdev = session->vdev;
	if (!vdev || !wlan_vdev_mlme_is_mlo_vdev(vdev))
		return;

	pdev = wlan_vdev_get_pdev(vdev);
	if (!pdev) {
		pe_err("null pdev");
		return;
	}
	mlo_ctx = vdev->mlo_dev_ctx;
	if (!mlo_ctx) {
		pe_err("null mlo_dev_ctx");
		return;
	}

	if (bcn_ptr->mlo_ie.mlo_ie.medium_sync_delay_info_present) {
		wlan_vdev_mlme_cap_clear(vdev, WLAN_VDEV_C_EMLSR_CAP);
		pe_debug("EMLSR not supported with D2.0 AP");
	}

	wlan_reg_read_current_country(mac_ctx->psoc, country_code);

	/* Check if AP beacons contain Extended MLD cap and op field */
	mlo_ctx->mlo_extmld_cap_advertisement =
		bcn_ptr->mlo_ie.mlo_ie.ext_mld_capab_and_op_present;

	/* Peer link reconfig operation support */
	mlo_ctx->link_recfg_op_support =
		bcn_ptr->mlo_ie.mlo_ie.mld_capab_and_op_info.link_reconfig_operation_support;

	pe_debug("AP caps: ext mld: %d, link reconfig oper support: %d",
		 mlo_ctx->mlo_extmld_cap_advertisement,
		 mlo_ctx->link_recfg_op_support);

	/** max num of active links recommended by AP */
	tmp_rec_value =
	bcn_ptr->mlo_ie.mlo_ie.ext_mld_capab_and_op_info.rec_max_simultaneous_links;
	mlo_ctx->mlo_max_recom_simult_links =
		valid_max_rec_links(tmp_rec_value);

	/* CAC complete or radar detect and new CAC complete
	 * Scenario 1-3, 3-3, 5-2
	 */
	if (session->mlo_sta_cac_info.link_state ==
				MLO_LINK_FORCE_WAIT_CAC_DONE &&
	    bcn_ptr->mlo_ie.mlo_ie.num_sta_profile == 0) {
		pe_debug("vdev %d  CAC completed",
			 session->vdev_id);

		/* Clear CAC flag and activate link */
		session->mlo_sta_cac_info.mlo_link_in_cac = false;

		/*
		 * num_sta_profile==0: per-STA profile (and link_id) is gone.
		 * Use cac_link_id saved when set_inactive was called to find
		 * the 5G partner vdev and send NO_FORCE to re-activate it.
		 */
		p_vdev = mlo_get_vdev_by_link_id(session->vdev,
						 session->mlo_sta_cac_info.cac_link_id,
						 WLAN_MLO_MGR_ID);
		if (p_vdev) {
			QDF_STATUS cac_status;

			cac_status = lim_set_link_force_mode_for_cac(p_vdev,
								     false);
			mlo_release_vdev_ref(p_vdev);
			/*
			 * Only advance to CAC_COMPLETE if the link was
			 * actually re-activated. On failure keep the state as
			 * WAIT_CAC_DONE so the next beacon retries, otherwise
			 * the link could stay force-inactive in firmware.
			 */
			if (QDF_IS_STATUS_SUCCESS(cac_status))
				session->mlo_sta_cac_info.link_state =
						MLO_LINK_FORCE_CAC_COMPLETE;
			else
				pe_err("vdev %d: failed to activate link %d on CAC complete",
				       session->vdev_id,
				       session->mlo_sta_cac_info.cac_link_id);
		} else {
			pe_err("vdev %d: no partner for cac_link_id %d",
			       session->vdev_id,
			       session->mlo_sta_cac_info.cac_link_id);
			session->mlo_sta_cac_info.link_state =
					MLO_LINK_FORCE_CAC_COMPLETE;
		}
		/*
		 * Each MLO link has its own pe_session copy of
		 * mlo_sta_cac_info; propagate this CAC-complete update to the
		 * partner links so every session observes the same state.
		 */
		lim_sync_mlo_sta_cac_info_to_partners(session);
	}

	for (i = 0; i < bcn_ptr->mlo_ie.mlo_ie.num_sta_profile; i++) {
		csa_ie = NULL;
		xcsa_ie = NULL;
		qdf_mem_zero(&csa_param, sizeof(csa_param));
		per_sta_pro = bcn_ptr->mlo_ie.mlo_ie.sta_profile[i].data;
		/* Append one byte to get the element length  */
		per_sta_pro_len = bcn_ptr->mlo_ie.mlo_ie.sta_profile[i].num_data;
		stacontrol = *(uint16_t *)(per_sta_pro + sizeof(struct subelem_header));
		sta_info_len = *(uint8_t *)(per_sta_pro +
				sizeof(struct subelem_header) + WLAN_ML_BV_LINFO_PERSTAPROF_STACTRL_SIZE);
		/* IE ID + LEN + STA control STA info len*/
		sta_pro = per_sta_pro + sizeof(struct subelem_header) +
			WLAN_ML_BV_LINFO_PERSTAPROF_STACTRL_SIZE + sta_info_len;
		sta_pro_len = per_sta_pro_len - sizeof(struct subelem_header) -
				WLAN_ML_BV_LINFO_PERSTAPROF_STACTRL_SIZE - sta_info_len;
		link_id = QDF_GET_BITS(
			    stacontrol,
			    WLAN_ML_BV_LINFO_PERSTAPROF_STACTRL_LINKID_IDX,
			    WLAN_ML_BV_LINFO_PERSTAPROF_STACTRL_LINKID_BITS);
		mcst_ie = wlan_get_ext_ie_ptr_from_ext_id(&mcst_ext_id, 1,
							  sta_pro, sta_pro_len);
		mcst_found = mcst_ie;

		csa_ie = (struct ieee80211_channelswitch_ie *)
				wlan_get_ie_ptr_from_eid(
					DOT11F_EID_CHANSWITCHANN,
					sta_pro, sta_pro_len);
		xcsa_ie = (struct ieee80211_extendedchannelswitch_ie *)
				wlan_get_ie_ptr_from_eid(
					DOT11F_EID_EXT_CHAN_SWITCH_ANN,
					sta_pro, sta_pro_len);
		is_sta_csa_synced = mlo_is_sta_csa_synced(mlo_ctx, link_id);
		link_info = mlo_mgr_get_ap_link_by_link_id(mlo_ctx, link_id);
		if (!link_info) {
			mlo_err("link info null");
			continue;
		}

		if (xcsa_ie) {
			csa_param.channel = xcsa_ie->newchannel;
			csa_param.switch_mode = xcsa_ie->switchmode;
			csa_param.new_op_class = xcsa_ie->newClass;
			if (wlan_reg_is_6ghz_op_class(pdev, xcsa_ie->newClass)) {
				csa_param.csa_chan_freq =
					wlan_reg_chan_band_to_freq(
						pdev, xcsa_ie->newchannel,
						BIT(REG_BAND_6G));
				chan_space =
					wlan_reg_get_op_class_width(pdev,
								    xcsa_ie->newClass,
								    true);
			} else {
				csa_param.csa_chan_freq =
					wlan_reg_legacy_chan_to_freq(
						pdev, xcsa_ie->newchannel);
				chan_space =
					wlan_reg_dmn_get_chanwidth_from_opclass_auto(country_code,
						csa_param.channel, xcsa_ie->newClass);
			}

			wlan_reg_convert_chan_spacing_to_width(chan_space,
							       &opclass_width);
			csa_param.new_ch_width =
				wlan_reg_find_chwidth_from_bw(opclass_width);

			if (!csa_param.csa_chan_freq) {
				pe_nofl_rl_debug("invalid freq from xcsa ie newchannel %d link %d",
						 xcsa_ie->newchannel,
						 link_id);
				return;
			} else if (wlan_reg_is_disable_for_pwrmode(
						pdev, csa_param.csa_chan_freq,
						REG_CURRENT_PWR_MODE)) {
				pe_nofl_rl_debug("reg disable freq %d from xcsa ie newchannel %d link %d",
						 csa_param.csa_chan_freq,
						 xcsa_ie->newchannel,
						 link_id);
				return;
			}
			csa_param.ies_present_flag |= MLME_XCSA_IE_PRESENT;
			mlo_sta_handle_csa_standby_link(mlo_ctx, link_id,
							&csa_param, vdev);
			if (!is_sta_csa_synced)
				mlo_sta_csa_save_params(mlo_ctx, link_id,
							&csa_param);
		} else if (csa_ie) {
			csa_param.channel = csa_ie->newchannel;
			csa_param.csa_chan_freq = wlan_reg_legacy_chan_to_freq(
						pdev, csa_ie->newchannel);
			if (!csa_param.csa_chan_freq) {
				pe_nofl_rl_debug("invalid freq from csa ie newchannel %d link %d",
						 csa_ie->newchannel,
						 link_id);
				return;
			} else if (wlan_reg_is_disable_for_pwrmode(
						pdev, csa_param.csa_chan_freq,
						REG_CURRENT_PWR_MODE)) {
				pe_nofl_rl_debug("reg disable freq %d from csa ie newchannel %d link %d",
						 csa_param.csa_chan_freq,
						 csa_ie->newchannel,
						 link_id);
				return;
			}
			csa_param.switch_mode = csa_ie->switchmode;
			csa_param.ies_present_flag |= MLME_CSA_IE_PRESENT;
			mlo_sta_handle_csa_standby_link(mlo_ctx, link_id,
							&csa_param, vdev);

			if (!is_sta_csa_synced)
				mlo_sta_csa_save_params(mlo_ctx, link_id,
							&csa_param);
		}
		csa_found = csa_ie || xcsa_ie;
		if (wlan_cm_is_vdev_connected(session->vdev))
			lim_process_mcst_ie_and_csa(pdev, session, link_id,
						    mcst_found, csa_found,
						    csa_param.channel, false);
	}
}

bool lim_is_same_mld_addr(struct mac_context *mac_ctx,
			  struct pe_session *session,
			  struct sSirProbeRespBeacon *bcn_ptr)
{
	struct qdf_mac_addr mld_mac = {0};
	QDF_STATUS status;

	if (!mlo_is_mld_sta(session->vdev) && !bcn_ptr->mlo_ie.mlo_ie_present)
		return true;
	else if (!mlo_is_mld_sta(session->vdev) || !bcn_ptr->mlo_ie.mlo_ie_present)
		return false;

	status = wlan_vdev_get_bss_peer_mld_mac(session->vdev, &mld_mac);
	if (QDF_IS_STATUS_SUCCESS(status) &&
	    qdf_is_macaddr_equal(&mld_mac,
				 (struct qdf_mac_addr *)bcn_ptr->mlo_ie.mlo_ie.mld_mac_addr))
		return true;

	pe_debug_rl("mld addr mismatch, bss peer " QDF_MAC_ADDR_FMT " bcn "
		    QDF_MAC_ADDR_FMT, QDF_MAC_ADDR_REF(mld_mac.bytes),
		    QDF_MAC_ADDR_REF(bcn_ptr->mlo_ie.mlo_ie.mld_mac_addr));

	return false;
}
#endif

static QDF_STATUS
lim_validate_rsn_ie(const uint8_t *ie_ptr, uint16_t ie_len)
{
	QDF_STATUS status;
	const uint8_t *rsn_ie;
	struct wlan_crypto_params crypto_params;

	rsn_ie = wlan_get_rsn_data_from_ie_ptr(ie_ptr, ie_len);
	if (!rsn_ie)
		return QDF_STATUS_SUCCESS;

	qdf_mem_zero(&crypto_params, sizeof(struct wlan_crypto_params));
	status = wlan_crypto_rsnie_check(&crypto_params, rsn_ie, NULL);
	if (status != QDF_STATUS_SUCCESS) {
		pe_debug_rl("RSN IE check failed %d", status);
		return QDF_STATUS_E_INVAL;
	}

	return QDF_STATUS_SUCCESS;
}

#ifdef WLAN_FEATURE_11BE
/**
 * lim_get_update_eht_bw_puncture_allow() - whether bw and puncture can be
 *                                          sent to target directly
 * @session: pe session
 * @ori_bw: bandwidth from beacon
 * @new_bw: bandwidth intersection between reference AP and STA
 * @update_allow: return true if bw and puncture can be updated directly
 * @phy_mode: update the value of phy_mode
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
lim_get_update_eht_bw_puncture_allow(struct pe_session *session,
				     enum phy_ch_width ori_bw,
				     enum phy_ch_width *new_bw,
				     bool *update_allow,
				     enum wlan_phymode *phy_mode)
{
	enum phy_ch_width ch_width;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	*update_allow = false;

	psoc = wlan_vdev_get_psoc(session->vdev);
	if (!psoc) {
		pe_err("psoc object invalid");
		return QDF_STATUS_E_INVAL;
	}
	status = mlme_get_peer_phymode(psoc, session->bssId, phy_mode);
	if (QDF_IS_STATUS_ERROR(status)) {
		pe_err("failed to get phy_mode %d mac: " QDF_MAC_ADDR_FMT,
		       status, QDF_MAC_ADDR_REF(session->bssId));
		return QDF_STATUS_E_INVAL;
	}
	ch_width = wlan_mlme_get_ch_width_from_phymode(*phy_mode);

	if (ori_bw <= ch_width) {
		*new_bw = ori_bw;
		*update_allow = true;
		return QDF_STATUS_SUCCESS;
	}

	if ((ori_bw == CH_WIDTH_320MHZ) &&
	    !session->eht_config.support_320mhz_6ghz) {
		if (ch_width == CH_WIDTH_160MHZ) {
			*new_bw = CH_WIDTH_160MHZ;
			*update_allow = true;
			return QDF_STATUS_SUCCESS;
		}
	}

	return QDF_STATUS_SUCCESS;
}

void lim_process_beacon_eht_op(struct pe_session *session,
			       struct sSirProbeRespBeacon *bcn_ptr)
{
	uint16_t ori_punc = 0;
	enum phy_ch_width ori_bw = CH_WIDTH_INVALID;
	enum phy_ch_width ori_bw_no_punct = CH_WIDTH_INVALID;
	uint8_t cb_mode;
	enum phy_ch_width new_bw;
	bool update_allow;
	QDF_STATUS status;
	struct mac_context *mac_ctx;
	struct wlan_objmgr_vdev *vdev;
	struct wlan_channel *des_chan;
	struct csa_offload_params *csa_param;
	uint8_t             ccfs0;
	uint8_t             ccfs1;
	tDot11fIEeht_op *eht_op;
	tDot11fIEhe_op *he_op;
	uint8_t chan_id;
	struct wlan_channel bss_chan = {0};
	struct wlan_channel *current_chan = NULL;
	uint8_t band_mask;
	uint32_t ch_cfreq2 = 0;
	enum wlan_phymode phy_mode;

	if (!bcn_ptr || !session || !session->mac_ctx || !session->vdev) {
		pe_err("invalid input parameters");
		return;
	}

	eht_op = &bcn_ptr->eht_op;
	he_op = &bcn_ptr->he_op;
	mac_ctx = session->mac_ctx;
	vdev = session->vdev;

	chan_id = wlan_reg_freq_to_chan(wlan_vdev_get_pdev(vdev),
					bcn_ptr->chan_freq);

	cb_mode = lim_get_cb_mode_for_freq(mac_ctx, session,
					   session->curr_op_freq);
	if (cb_mode == WNI_CFG_CHANNEL_BONDING_MODE_DISABLE) {
		/*
		 * if channel bonding is disabled from INI do not
		 * update the chan width
		 */
		pe_debug_rl("chan banding is disabled skip bw update");

		return;
	}
	/* handle beacon IE for 11be non-mlo case */
	if (eht_op->eht_op_information_present) {
		ori_bw = wlan_mlme_convert_eht_op_bw_to_phy_ch_width(
						eht_op->channel_width);
		pe_debug("update bcn ch width from eht op");
		ccfs0 = eht_op->ccfs0;
		ccfs1 = eht_op->ccfs1;
		if (eht_op->disabled_sub_chan_bitmap_present) {
			ori_punc = QDF_GET_BITS(eht_op->disabled_sub_chan_bitmap[0][0], 0, 8);
			ori_punc |= QDF_GET_BITS(eht_op->disabled_sub_chan_bitmap[0][1], 0, 8) << 8;

			if (!wlan_mlme_get_eht_disable_punct_in_us_lpi(mac_ctx->psoc))
				goto update_bw;

			bss_chan.ch_freq = bcn_ptr->chan_freq;
			bss_chan.puncture_bitmap = ori_punc;
			bss_chan.ch_width = ori_bw;
			bss_chan.ch_phymode = WLAN_PHYMODE_11BEA_EHT160;

			if (ori_bw == CH_WIDTH_320MHZ &&
			    WLAN_REG_IS_6GHZ_CHAN_FREQ(bcn_ptr->chan_freq)) {
				band_mask = BIT(REG_BAND_6G);
				ch_cfreq2 = wlan_reg_chan_band_to_freq(mac_ctx->pdev,
								       ccfs1,
								       band_mask);
				bss_chan.ch_cfreq2 = ch_cfreq2;
			}

			status = wlan_mlme_get_bw_no_punct(mac_ctx->psoc,
							   vdev,
							   &bss_chan,
							   &ori_bw_no_punct);
			current_chan = wlan_vdev_mlme_get_bss_chan(vdev);
			if (QDF_IS_STATUS_SUCCESS(status)) {
				if (ori_bw_no_punct != current_chan->ch_width) {
					status = wlan_mlme_send_ch_width_update_with_notify(mac_ctx->psoc,
											    vdev,
											    session->vdev_id,
											    ori_bw_no_punct);
				}
				return;
			}
		}
	} else {
		return;
	}

update_bw:
	status = lim_get_update_eht_bw_puncture_allow(session, ori_bw,
						      &new_bw,
						      &update_allow,
						      &phy_mode);
	if (QDF_IS_STATUS_ERROR(status))
		return;

	if (update_allow) {
		status = wlan_cm_sta_update_bw_puncture(vdev, session->bssId,
							ori_punc, ori_bw,
							ccfs0,
							ccfs1,
							new_bw);
		if (QDF_IS_STATUS_SUCCESS(status))
			wma_send_peer_phy_mode(session->bssId,
					       session->vdev_id,
					       phy_mode);
	} else {
		csa_param = qdf_mem_malloc(sizeof(*csa_param));
		if (!csa_param) {
			pe_err("csa_param allocation fails");
			return;
		}
		des_chan = wlan_vdev_mlme_get_des_chan(vdev);
		csa_param->channel = des_chan->ch_ieee;
		csa_param->csa_chan_freq = des_chan->ch_freq;
		csa_param->new_ch_width = ori_bw;
		csa_param->new_punct_bitmap = ori_punc;
		csa_param->new_ch_freq_seg1 = ccfs0;
		csa_param->new_ch_freq_seg2 = ccfs1;
		qdf_copy_macaddr(&csa_param->bssid,
				 (struct qdf_mac_addr *)session->bssId);
		lim_handle_sta_csa_param(session->mac_ctx, csa_param, false);
	}
}

/**
 * lim_process_beacon_mlo_self_link() - process CSA/eCSA and MCST IE
 * carried directly in a 5G MLO STA link's own beacon (top-level IEs,
 * not a per-STA profile)
 * @mac_ctx: global mac context
 * @session: pe session for the 5G link
 * @bcn_ptr: pointer to tSchBeaconStruct
 * @rx_pkt_info: pointer to RX packet info structure, used to scan the
 *               beacon's raw top-level IE list for the MCST extension IE
 *
 * Return none
 */
static void
lim_process_beacon_mlo_self_link(struct mac_context *mac_ctx,
				 struct pe_session *session,
				 tSchBeaconStruct *bcn_ptr,
				 uint8_t *rx_pkt_info)
{
	struct wlan_objmgr_vdev *vdev = session->vdev;
	struct wlan_objmgr_pdev *pdev;
	struct wlan_mlo_dev_context *mlo_ctx;
	uint8_t link_id;
	uint8_t is_sta_csa_synced;
	struct csa_offload_params csa_param;
	bool csa_found = false;
	bool mcst_found = false;
	uint8_t *frame;
	uint16_t frame_len;
	uint8_t mcst_ext_id = WLAN_EXTN_ELEMID_MAX_CHAN_SWITCH_TIME;

	qdf_mem_zero(&csa_param, sizeof(csa_param));

	if (!vdev || !wlan_vdev_mlme_is_mlo_vdev(vdev))
		return;

	pdev = wlan_vdev_get_pdev(vdev);
	mlo_ctx = vdev->mlo_dev_ctx;
	if (!pdev || !mlo_ctx) {
		pe_err("null pdev/mlo_dev_ctx");
		return;
	}

	link_id = wlan_vdev_get_link_id(vdev);

	/* MCST ext IE at the beacon's own top level (not per-STA-profile) */
	frame = WMA_GET_RX_MPDU_DATA(rx_pkt_info);
	frame_len = WMA_GET_RX_PAYLOAD_LEN(rx_pkt_info);
	if (frame_len > SIR_MAC_B_PR_SSID_OFFSET)
		mcst_found = wlan_get_ext_ie_ptr_from_ext_id(&mcst_ext_id, 1,
							     frame + SIR_MAC_B_PR_SSID_OFFSET,
							     frame_len - SIR_MAC_B_PR_SSID_OFFSET) != NULL;

	csa_found = bcn_ptr->ext_chan_switch_present ||
		    bcn_ptr->channelSwitchPresent;

	/*
	 * CAC complete: this link went inactive waiting for CAC (Scenario
	 * 1-2/3-2 in lim_process_mcst_ie_and_csa) and its own beacon no
	 * longer carries the CSA/eCSA IE nor the MCST IE - CAC finished
	 * clean, so re-activate this link. Unlike the partner-link path in
	 * lim_process_beacon_mlo(), which looks up the partner vdev via
	 * cac_link_id, session->vdev here already IS the link to activate -
	 * but link_state/cac_link_id may have been broadcast onto this
	 * session by lim_sync_mlo_sta_cac_info_to_partners() from a
	 * different link's CAC, so only act if cac_link_id names this
	 * link, not some other link this session merely observed.
	 */
	if (session->mlo_sta_cac_info.link_state ==
				MLO_LINK_FORCE_WAIT_CAC_DONE &&
	    session->mlo_sta_cac_info.cac_link_id == link_id &&
	    !csa_found && !mcst_found) {
		pe_debug("vdev %d: CAC completed", session->vdev_id);

		session->mlo_sta_cac_info.mlo_link_in_cac = false;
		lim_set_link_force_mode_for_cac(vdev, false);
		session->mlo_sta_cac_info.link_state =
				MLO_LINK_FORCE_CAC_COMPLETE;
		lim_sync_mlo_sta_cac_info_to_partners(session);
		return;
	}

	/*
	 * Top-level (not per-STA-profile) CSA/eCSA IE of this link's own
	 * beacon, already parsed by the generic dot11f beacon parser.
	 */
	if (bcn_ptr->ext_chan_switch_present) {
		csa_param.channel = bcn_ptr->ext_chan_switch.new_channel;
		csa_param.switch_mode = bcn_ptr->ext_chan_switch.switch_mode;
		csa_param.new_op_class = bcn_ptr->ext_chan_switch.new_reg_class;
		if (wlan_reg_is_6ghz_op_class(pdev, csa_param.new_op_class))
			csa_param.csa_chan_freq = wlan_reg_chan_band_to_freq(pdev,
									     csa_param.channel,
									     BIT(REG_BAND_6G));
		else
			csa_param.csa_chan_freq = wlan_reg_legacy_chan_to_freq(pdev,
									       csa_param.channel);
		csa_param.ies_present_flag |= MLME_XCSA_IE_PRESENT;
	} else if (bcn_ptr->channelSwitchPresent) {
		csa_param.channel = bcn_ptr->channelSwitchIE.newChannel;
		csa_param.switch_mode = bcn_ptr->channelSwitchIE.switchMode;
		csa_param.csa_chan_freq = wlan_reg_legacy_chan_to_freq(pdev,
								       csa_param.channel);
		csa_param.ies_present_flag |= MLME_CSA_IE_PRESENT;
	}

	if (csa_found) {
		is_sta_csa_synced = mlo_is_sta_csa_synced(mlo_ctx, link_id);
		mlo_sta_handle_csa_standby_link(mlo_ctx, link_id, &csa_param,
						vdev);
		if (!is_sta_csa_synced)
			mlo_sta_csa_save_params(mlo_ctx, link_id, &csa_param);
	}

	if (wlan_cm_is_vdev_connected(vdev))
		lim_process_mcst_ie_and_csa(pdev, session,
					    link_id,
					    mcst_found,
					    csa_found,
					    csa_param.channel,
					    true);
}

void lim_process_beacon_eht(struct mac_context *mac_ctx,
			    struct pe_session *session,
			    tSchBeaconStruct *bcn_ptr,
			    uint8_t *rx_pkt_info)
{
	struct wlan_objmgr_vdev *vdev;
	struct wlan_channel *des_chan;

	if (!session || !bcn_ptr || !mac_ctx) {
		pe_err("invalid input parameters");
		return;
	}
	vdev = session->vdev;
	if (!vdev || wlan_vdev_mlme_get_opmode(vdev) != QDF_STA_MODE ||
	    !qdf_is_macaddr_equal((struct qdf_mac_addr *)session->bssId,
				  (struct qdf_mac_addr *)bcn_ptr->bssid))
		return;
	des_chan = wlan_vdev_mlme_get_des_chan(vdev);
	if (!des_chan || !IS_WLAN_PHYMODE_EHT(des_chan->ch_phymode))
		return;

	if (!mlo_is_mld_sta(vdev))
		return;

	/* handle beacon IE for 802.11be mlo case */
	if (wlan_reg_is_5ghz_ch_freq(bcn_ptr->chan_freq))
		lim_process_beacon_mlo_self_link(mac_ctx, session, bcn_ptr,
						 rx_pkt_info);
	else
		lim_process_beacon_mlo(mac_ctx, session, bcn_ptr);
}

void
lim_process_ml_reconfig(struct mac_context *mac_ctx,
			struct pe_session *session,
			uint8_t *rx_pkt_info)
{
	uint8_t *frame;
	uint16_t frame_len;

	if (!session->vdev)
		return;

	frame = WMA_GET_RX_MPDU_DATA(rx_pkt_info);
	frame_len = WMA_GET_RX_PAYLOAD_LEN(rx_pkt_info);
	if (frame_len < SIR_MAC_B_PR_SSID_OFFSET)
		return;

	mlo_process_ml_reconfig_ie(session->vdev, NULL,
				   frame + SIR_MAC_B_PR_SSID_OFFSET,
				   frame_len - SIR_MAC_B_PR_SSID_OFFSET, NULL);
}
#endif

/**
 * lim_process_beacon_frame() - to process beacon frames
 * @mac_ctx: Pointer to Global MAC structure
 * @rx_pkt_info: A pointer to RX packet info structure
 * @session: A pointer to session
 *
 * This function is called by limProcessMessageQueue() upon Beacon
 * frame reception.
 * Note:
 * 1. Beacons received in 'normal' state in IBSS are handled by
 *    Beacon Processing module.
 *
 * Return: none
 */

void
lim_process_beacon_frame(struct mac_context *mac_ctx, uint8_t *rx_pkt_info,
			 struct pe_session *session)
{
	tpSirMacMgmtHdr mac_hdr;
	tSchBeaconStruct *bcn_ptr;
	uint8_t *frame;
	const uint8_t *owe_transition_ie;
	uint16_t frame_len;
	uint8_t bpcc;
	bool cu_flag = true;
	QDF_STATUS status;
	struct bss_description *bss = NULL;
	struct vdev_mlme_obj *mlme_obj;
	struct wlan_lmac_if_reg_tx_ops *tx_ops;
	bool tpe_change = false;


	/*
	 * here is it required to increment session specific heartBeat
	 * beacon counter
	 */
	mac_hdr = WMA_GET_RX_MAC_HEADER(rx_pkt_info);
	frame = WMA_GET_RX_MPDU_DATA(rx_pkt_info);
	frame_len = WMA_GET_RX_PAYLOAD_LEN(rx_pkt_info);

	if (frame_len < SIR_MAC_B_PR_SSID_OFFSET) {
		pe_debug_rl("payload invalid len %d", frame_len);
		return;
	}
	if (lim_validate_rsn_ie(frame + SIR_MAC_B_PR_SSID_OFFSET,
				frame_len - SIR_MAC_B_PR_SSID_OFFSET) !=
			QDF_STATUS_SUCCESS)
		return;
	/* Expect Beacon in any state as Scan is independent of LIM state */
	bcn_ptr = qdf_mem_malloc(sizeof(*bcn_ptr));
	if (!bcn_ptr)
		return;

	/* Parse received Beacon */
	if (sir_convert_beacon_frame2_struct(mac_ctx,
			rx_pkt_info, bcn_ptr) !=
			QDF_STATUS_SUCCESS) {
		/*
		 * Received wrongly formatted/invalid Beacon.
		 * Ignore it and move on.
		 */
		pe_warn("Received invalid Beacon in state: %X",
			session->limMlmState);
		lim_print_mlm_state(mac_ctx, LOGW,
			session->limMlmState);
		qdf_mem_free(bcn_ptr);
		return;
	}

	if (mlo_is_mld_sta(session->vdev)) {
		cu_flag = false;
		status = lim_get_bpcc_from_mlo_ie(bcn_ptr, &bpcc);
		if (QDF_IS_STATUS_SUCCESS(status)) {
			uint8_t link_id = wlan_vdev_get_link_id(session->vdev);

			cu_flag = lim_check_cu_happens(session->vdev, link_id,
						       bpcc);
		}
		lim_process_ml_reconfig(mac_ctx, session, rx_pkt_info);
	}

	lim_process_bcn_prb_rsp_t2lm(mac_ctx, session, bcn_ptr);
	if (QDF_IS_STATUS_SUCCESS(lim_check_for_ml_probe_req(session)))
		goto end;

	/*
	 * during scanning, when any session is active, and
	 * beacon/Pr belongs to one of the session, fill up the
	 * following, TBD - HB counter
	 */
	if (sir_compare_mac_addr(session->bssId,
				bcn_ptr->bssid)) {
		qdf_mem_copy((uint8_t *)&session->lastBeaconTimeStamp,
			(uint8_t *) bcn_ptr->timeStamp,
			sizeof(uint64_t));
		session->currentBssBeaconCnt++;
	}
	MTRACE(mac_trace(mac_ctx,
		TRACE_CODE_RX_MGMT_TSF, 0, bcn_ptr->timeStamp[0]));
	MTRACE(mac_trace(mac_ctx, TRACE_CODE_RX_MGMT_TSF, 0,
		bcn_ptr->timeStamp[1]));

	if (session->limMlmState ==
			eLIM_MLM_WT_JOIN_BEACON_STATE) {
		owe_transition_ie = wlan_get_vendor_ie_ptr_from_oui(
					OWE_TRANSITION_OUI_TYPE,
					OWE_TRANSITION_OUI_SIZE,
					frame + SIR_MAC_B_PR_SSID_OFFSET,
					frame_len - SIR_MAC_B_PR_SSID_OFFSET);
		if (session->connected_akm == ANI_AKM_TYPE_OWE &&
		    owe_transition_ie) {
			pe_debug("vdev:%d Drop OWE rx beacon. Wait for probe for join success",
				 session->vdev_id);
			qdf_mem_free(bcn_ptr);
			return;
		}

		if (session->beacon) {
			qdf_mem_free(session->beacon);
			session->beacon = NULL;
			session->bcnLen = 0;
		}

		mac_ctx->lim.bss_rssi =
			(int8_t)WMA_GET_RX_RSSI_NORMALIZED(rx_pkt_info);
		session->bcnLen = WMA_GET_RX_MPDU_LEN(rx_pkt_info);
		session->beacon = qdf_mem_malloc(session->bcnLen);
		if (session->beacon)
			/*
			 * Store the whole Beacon frame. This is sent to
			 * csr/hdd in join cnf response.
			 */
			qdf_mem_copy(session->beacon,
				WMA_GET_RX_MAC_HEADER(rx_pkt_info),
				session->bcnLen);
		mgmt_txrx_frame_hex_dump((uint8_t *)mac_hdr,
					 WMA_GET_RX_MPDU_LEN(rx_pkt_info),
					 false);
		lim_check_and_announce_join_success(mac_ctx, bcn_ptr,
				mac_hdr, session);
	}

	if (wlan_cm_is_vdev_connected(session->vdev))
		lim_process_beacon_eht_op(session, bcn_ptr);

	if (cu_flag) {
		lim_process_beacon_eht(mac_ctx, session, bcn_ptr, rx_pkt_info);
		if (session->lim_join_req)
			bss = &session->lim_join_req->bssDescription;
		tx_ops = wlan_reg_get_tx_ops(mac_ctx->psoc);
		if (!tx_ops)
			goto end;

		if (!bss) {
			pe_err("bss descriptor is NULL");
			goto end;
		}

		lim_parse_tpe_ie(mac_ctx, session,
				 bss->bcn_ies.transmit_power_env,
				 bss->bcn_ies.num_transmit_power_env,
				 &bss->bcn_ies.he_op, &tpe_change);
		if (!tpe_change) {
			pe_nofl_rl_debug("no change in TPE IE");
			goto end;
		}
		mlme_obj =
			wlan_vdev_mlme_get_cmpt_obj(session->vdev);
		if (!mlme_obj) {
			pe_err("vdev component object is NULL");
			goto end;
		}

		lim_calculate_tpc(mac_ctx, session, false, 0);

		if (tx_ops->set_tpc_power)
			tx_ops->set_tpc_power(mac_ctx->psoc,
					      session->vdev_id,
					      &mlme_obj->reg_tpc_obj);
       }

end:
	qdf_mem_free(bcn_ptr);
	return;
}
