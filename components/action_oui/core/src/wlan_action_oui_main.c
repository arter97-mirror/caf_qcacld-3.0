/*
 * Copyright (c) 2012-2018, 2020 The Linux Foundation. All rights reserved.
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
 * DOC: Implement various notification handlers which are accessed
 * internally in action_oui component only.
 */
#include "cfg_ucfg_api.h"
#include "wlan_action_oui_cfg.h"
#include "wlan_action_oui_main.h"
#include "wlan_action_oui_public_struct.h"
#include "wlan_action_oui_tgt_api.h"
#include "target_if_action_oui.h"

/**
 * action_oui_allocate() - Allocates memory for various actions.
 * @psoc_priv: pointer to action_oui psoc priv obj
 *
 * This function allocates memory for all the action_oui types
 * and initializes the respective lists to store extensions
 * extracted from action_oui_extract().
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
action_oui_allocate(struct action_oui_psoc_priv *psoc_priv)
{
	struct action_oui_priv *oui_priv;
	uint32_t i;
	uint32_t j;

	for (i = 0; i < ACTION_OUI_MAXIMUM_ID; i++) {
		if (!wlan_action_oui_id_valid(i)) {
			psoc_priv->oui_priv[i] = NULL;
			continue;
		}

		oui_priv = qdf_mem_malloc(sizeof(*oui_priv));
		if (!oui_priv) {
			action_oui_err("Mem alloc failed for oui_priv id: %u",
					i);
			goto free_mem;
		}
		oui_priv->id = i;
		qdf_list_create(&oui_priv->extension_list,
				wlan_action_oui_max_ext_num(i));
		qdf_mutex_create(&oui_priv->extension_lock);
		psoc_priv->oui_priv[i] = oui_priv;
	}

	return QDF_STATUS_SUCCESS;

free_mem:
	for (j = 0; j < i; j++) {
		oui_priv = psoc_priv->oui_priv[j];
		if (!oui_priv)
			continue;

		qdf_list_destroy(&oui_priv->extension_list);
		qdf_mutex_destroy(&oui_priv->extension_lock);
		psoc_priv->oui_priv[j] = NULL;
	}

	return QDF_STATUS_E_NOMEM;
}

/**
 * action_oui_destroy() - Deallocates memory for various actions.
 * @psoc_priv: pointer to action_oui psoc priv obj
 *
 * This function Deallocates memory for all the action_oui types.
 * As a part of deallocate, all extensions are destroyed.
 *
 * Return: None
 */
static void
action_oui_destroy(struct action_oui_psoc_priv *psoc_priv)
{
	struct action_oui_priv *oui_priv;
	struct action_oui_extension_priv *ext_priv;
	qdf_list_t *ext_list;
	QDF_STATUS status;
	qdf_list_node_t *node = NULL;
	uint32_t i;

	psoc_priv->total_extensions = 0;
	psoc_priv->max_extensions = 0;
	psoc_priv->host_only_extensions = 0;

	for (i = 0; i < ACTION_OUI_MAXIMUM_ID; i++) {
		/* Only destroy if it's a valid action OUI ID */
		if (!wlan_action_oui_id_valid(i))
			continue;

		oui_priv = psoc_priv->oui_priv[i];
		psoc_priv->oui_priv[i] = NULL;
		if (!oui_priv)
			continue;

		ext_list = &oui_priv->extension_list;
		qdf_mutex_acquire(&oui_priv->extension_lock);
		while (!qdf_list_empty(ext_list)) {
			status = qdf_list_remove_front(ext_list, &node);
			if (!QDF_IS_STATUS_SUCCESS(status)) {
				action_oui_err("Invalid delete in action: %u",
						oui_priv->id);
				break;
			}
			ext_priv = qdf_container_of(node,
					struct action_oui_extension_priv,
					item);
			qdf_mem_free(ext_priv);
			ext_priv = NULL;
		}

		qdf_list_destroy(ext_list);
		qdf_mutex_release(&oui_priv->extension_lock);
		qdf_mutex_destroy(&oui_priv->extension_lock);
		qdf_mem_free(oui_priv);
		oui_priv = NULL;
	}
}

/**
 * action_oui_get_nss_action_id() - Get NSS action ID based on list type
 *
 * This function reads the NSS list type configuration and returns the
 * appropriate action ID (ALLOW or DISALLOW).
 *
 * Return: enum action_oui_id
 */
static enum action_oui_id action_oui_get_nss_action_id(void)
{
	uint32_t nss_list_type;

	nss_list_type = cfg_default(CFG_ACTION_OUI_DEFAULT_NSS_LIST_TYPE);
	if (nss_list_type == ACTION_OUI_NSS_LIST_ALLOWLIST)
		return ACTION_OUI_ALLOW_NSS_GREATER_THAN_2;
	else if (nss_list_type == ACTION_OUI_NSS_LIST_DENYLIST)
		return ACTION_OUI_DISALLOW_NSS_GREATER_THAN_2;

	/* Default to allow if invalid */
	return ACTION_OUI_ALLOW_NSS_GREATER_THAN_2;
}

static void action_oui_load_config(struct action_oui_psoc_priv *psoc_priv)
{
	struct wlan_objmgr_psoc *psoc = psoc_priv->psoc;
	enum action_oui_id nss_action_id;

	psoc_priv->is_action_oui_v2_enabled =
		wlan_action_oui_v2_enabled(psoc_priv->psoc);

	/* Get NSS action ID based on list type configuration */
	nss_action_id = action_oui_get_nss_action_id();

	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_CONNECT_1X1],
		      cfg_get(psoc, CFG_ACTION_OUI_CONNECT_1X1),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_ITO_EXTENSION],
		      cfg_get(psoc, CFG_ACTION_OUI_ITO_EXTENSION),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_CCKM_1X1],
		      cfg_get(psoc, CFG_ACTION_OUI_CCKM_1X1),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_ITO_ALTERNATE],
		      cfg_get(psoc, CFG_ACTION_OUI_ITO_ALTERNATE),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_SWITCH_TO_11N_MODE],
		      cfg_get(psoc, CFG_ACTION_OUI_SWITCH_TO_11N_MODE),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_CONNECT_1X1_WITH_1_CHAIN],
		      cfg_get(psoc,
			      CFG_ACTION_OUI_CONNECT_1X1_WITH_1_CHAIN),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_DISABLE_AGGRESSIVE_TX],
		      cfg_get(psoc,
			      CFG_ACTION_OUI_DISABLE_AGGRESSIVE_TX),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
					  [ACTION_OUI_DISABLE_AGGRESSIVE_EDCA],
		      cfg_get(psoc,
			      CFG_ACTION_OUI_DISABLE_AGGRESSIVE_EDCA),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_EXTEND_WOW_ITO],
		      cfg_get(psoc, CFG_ACTION_OUI_EXTEND_WOW_ITO),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_DISABLE_TWT],
		      cfg_get(psoc, CFG_ACTION_OUI_DISABLE_TWT),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_HOST_RECONN],
		      cfg_get(psoc, CFG_ACTION_OUI_RECONN_ASSOCTIMEOUT),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_TAKE_ALL_BAND_INFO],
		      cfg_get(psoc, CFG_ACTION_OUI_TAKE_ALL_BAND_INFO),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_11BE_OUI_ALLOW],
		      cfg_get(psoc, CFG_ACTION_OUI_11BE_ALLOW_LIST),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
			[ACTION_OUI_DISABLE_DYNAMIC_QOS_NULL_TX_RATE],
		      cfg_get(psoc,
			      CFG_ACTION_OUI_DISABLE_DYNAMIC_QOS_NULL_TX_RATE),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
			[ACTION_OUI_ENABLE_CTS2SELF_WITH_QOS_NULL],
		      cfg_get(psoc,
			      CFG_ACTION_OUI_ENABLE_CTS2SELF_WITH_QOS_NULL),
		      ACTION_OUI_MAX_STR_LEN);

	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_ENABLE_CTS2SELF],
		      cfg_get(psoc, CFG_ACTION_OUI_ENABLE_CTS2SELF),
		      ACTION_OUI_MAX_STR_LEN);

	qdf_str_lcopy(psoc_priv->action_oui_str
			[ACTION_OUI_SEND_SMPS_FRAME_WITH_OMN],
		      cfg_get(psoc,
			      CFG_ACTION_OUI_SEND_SMPS_FRAME_WITH_OMN),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
			[ACTION_OUI_RESTRICT_MAX_MLO_LINKS],
		      cfg_get(psoc, CFG_ACTION_OUI_RESTRICT_MAX_MLO_LINKS),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
			[ACTION_OUI_RESTRICT_SLO],
		      cfg_get(psoc, CFG_ACTION_OUI_RESTRICT_SLO),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
			[ACTION_OUI_AUTH_ASSOC_6MBPS_2GHZ],
		      cfg_get(psoc, CFG_ACTION_OUI_AUTH_ASSOC_6MBPS_2GHZ),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_DISABLE_BFORMEE],
		      cfg_get(psoc, CFG_ACTION_OUI_DISABLE_BFORMEE),
			      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_LIMIT_BW],
		      cfg_get(psoc, CFG_ACTION_OUI_LIMIT_BW),
			      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[ACTION_OUI_DISABLE_AUX_LISTEN],
		      cfg_get(psoc, CFG_ACTION_OUI_DISABLE_AUX_LISTEN),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
		      [ACTION_OUI_EXT_MLD_CAP_OP],
		      cfg_get(psoc, CFG_ACTION_OUI_EXT_MLD_CAP_OP),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
		      [ACTION_OUI_FORCE_TX_NULL_FRAME_ON_P2P],
		      cfg_get(psoc, CFG_ACTION_OUI_FORCE_TX_NULL_FRAME_ON_P2P),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
		      [ACTION_OUI_SKIP_BCN_CH_MISMATCH_CHK],
		      cfg_get(psoc, CFG_ACTION_OUI_SKIP_BCN_CH_MISMATCH_CHK),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
		      [ACTION_OUI_ENABLE_AMSDU_2G],
		      cfg_get(psoc, CFG_ACTION_OUI_ENABLE_AMSDU_2G),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str
		      [ACTION_OUI_EARLY_RX],
		      cfg_get(psoc, CFG_ACTION_OUI_EARLY_RX),
		      ACTION_OUI_MAX_STR_LEN);
	qdf_str_lcopy(psoc_priv->action_oui_str[nss_action_id],
		      cfg_default(CFG_ACTION_OUI_DEFAULT_NSS_LIST),
		      ACTION_OUI_MAX_STR_LEN);
	if (psoc_priv->is_action_oui_v2_enabled) {
		qdf_str_lcopy(psoc_priv->action_oui_str
			      [ACTION_OUI_DISABLE_DYNAMIC_SMPS],
			      cfg_get(psoc, CFG_ACTION_OUI_DISABLE_DYNAMIC_SMPS_V2),
			      ACTION_OUI_MAX_STR_LEN);
		psoc_priv->is_action_oui_v2_used[ACTION_OUI_DISABLE_DYNAMIC_SMPS] = true;
	} else {
		qdf_str_lcopy(psoc_priv->action_oui_str
			      [ACTION_OUI_DISABLE_DYNAMIC_SMPS],
			      cfg_get(psoc, CFG_ACTION_OUI_DISABLE_DYNAMIC_SMPS),
			      ACTION_OUI_MAX_STR_LEN);
	}
}

static void action_oui_parse_config(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS status;
	uint32_t id;
	uint8_t *str;
	struct action_oui_psoc_priv *psoc_priv;

	if (!psoc) {
		action_oui_err("Invalid psoc");
		return;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		return;
	}
	if (!psoc_priv->action_oui_enable) {
		action_oui_debug("action_oui is not enable");
		return;
	}
	for (id = 0; id < ACTION_OUI_MAXIMUM_ID; id++) {
		/* Only parse for valid action OUI IDs */
		if (!wlan_action_oui_id_valid(id))
			continue;

		str = psoc_priv->action_oui_str[id];
		if (!qdf_str_len(str))
			continue;

		status = action_oui_parse_string(psoc, str, id);
		if (!QDF_IS_STATUS_SUCCESS(status))
			action_oui_err("Failed to parse action_oui str: %u",
				       id);
	}

	/* FW allocates memory for the extensions only during init time.
	 * Therefore, send additional legspace for configuring new
	 * extensions during runtime.
	 * The current max value is default extensions count + 10.
	 */
	psoc_priv->max_extensions = psoc_priv->total_extensions -
					psoc_priv->host_only_extensions +
					ACTION_OUI_MAX_ADDNL_EXTENSIONS;
	action_oui_debug("Extensions - Max: %d Total: %d host_only %d",
			 psoc_priv->max_extensions, psoc_priv->total_extensions,
			 psoc_priv->host_only_extensions);
}

static QDF_STATUS action_oui_send_config(struct wlan_objmgr_psoc *psoc)
{
	struct action_oui_psoc_priv *psoc_priv;
	QDF_STATUS status = QDF_STATUS_E_INVAL;
	uint32_t id;

	if (!psoc) {
		action_oui_err("psoc is NULL");
		goto exit;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		goto exit;
	}
	if (!psoc_priv->action_oui_enable) {
		action_oui_debug("action_oui is not enable");
		return QDF_STATUS_SUCCESS;
	}

	for (id = 0; id < ACTION_OUI_MAXIMUM_ID; id++) {
		if (!wlan_action_oui_id_valid(id))
			continue;

		if (id >= ACTION_OUI_HOST_ONLY)
			continue;
		if (id == ACTION_OUI_CONNECT_1X1 &&
		    policy_mgr_is_hw_dbs_2x2_capable(psoc)) {
			continue;
		}
		status = action_oui_send(psoc_priv, id);
		if (!QDF_IS_STATUS_SUCCESS(status))
			action_oui_debug("Failed to send: %u", id);
	}

exit:
	return status;
}

QDF_STATUS
action_oui_psoc_create_notification(struct wlan_objmgr_psoc *psoc, void *arg)
{
	struct action_oui_psoc_priv *psoc_priv;
	QDF_STATUS status;

	ACTION_OUI_ENTER();

	psoc_priv = qdf_mem_malloc(sizeof(*psoc_priv));
	if (!psoc_priv) {
		status = QDF_STATUS_E_NOMEM;
		goto exit;
	}

	status = wlan_objmgr_psoc_component_obj_attach(psoc,
				WLAN_UMAC_COMP_ACTION_OUI,
				(void *)psoc_priv, QDF_STATUS_SUCCESS);
	if (!QDF_IS_STATUS_SUCCESS(status)) {
		action_oui_err("Failed to attach priv with psoc");
		goto free_psoc_priv;
	}

	target_if_action_oui_register_tx_ops(&psoc_priv->tx_ops);
	psoc_priv->psoc = psoc;
	psoc_priv->action_oui_enable = cfg_get(psoc, CFG_ENABLE_ACTION_OUI);
	action_oui_debug("psoc priv attached");
	goto exit;
free_psoc_priv:
	qdf_mem_free(psoc_priv);
	status = QDF_STATUS_E_INVAL;
exit:
	ACTION_OUI_EXIT();
	return status;
}

QDF_STATUS
action_oui_psoc_destroy_notification(struct wlan_objmgr_psoc *psoc, void *arg)
{
	struct action_oui_psoc_priv *psoc_priv = NULL;
	QDF_STATUS status = QDF_STATUS_E_FAILURE;

	ACTION_OUI_ENTER();

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		goto exit;
	}

	status = wlan_objmgr_psoc_component_obj_detach(psoc,
					WLAN_UMAC_COMP_ACTION_OUI,
					(void *)psoc_priv);
	if (!QDF_IS_STATUS_SUCCESS(status))
		action_oui_err("Failed to detach priv with psoc");

	qdf_mem_free(psoc_priv);

exit:
	ACTION_OUI_EXIT();
	return status;
}

void action_oui_psoc_enable(struct wlan_objmgr_psoc *psoc,
			    bool load_default_config)
{
	struct action_oui_psoc_priv *psoc_priv;
	QDF_STATUS status = QDF_STATUS_E_FAILURE;

	ACTION_OUI_ENTER();

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		goto exit;
	}

	if (load_default_config) {
		action_oui_load_config(psoc_priv);

		status = action_oui_allocate(psoc_priv);
		if (!QDF_IS_STATUS_SUCCESS(status)) {
			action_oui_err("Failed to alloc action_oui");
			goto exit;
		}
		action_oui_parse_config(psoc);
	}
	action_oui_send_config(psoc);
exit:
	ACTION_OUI_EXIT();
}

void action_oui_psoc_disable(struct wlan_objmgr_psoc *psoc)
{
	struct action_oui_psoc_priv *psoc_priv;

	ACTION_OUI_ENTER();

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		goto exit;
	}

	action_oui_destroy(psoc_priv);
exit:
	ACTION_OUI_EXIT();
}

bool wlan_action_oui_search(struct wlan_objmgr_psoc *psoc,
			    struct action_oui_search_attr *attr,
			    enum action_oui_id action_id)
{
	struct action_oui_psoc_priv *psoc_priv;
	bool found = false;

	if (!psoc || !attr) {
		action_oui_err("Invalid psoc or search attrs");
		goto exit;
	}

	if (!wlan_action_oui_id_valid(action_id)) {
		action_oui_err("Invalid action_oui id: %u", action_id);
		goto exit;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		goto exit;
	}

	found = action_oui_search(psoc_priv, attr, action_id);

exit:
	return found;
}

static QDF_STATUS
__wlan_action_oui_cleanup(struct action_oui_psoc_priv *psoc_priv,
			  enum action_oui_id action_id)
{
	struct action_oui_priv *oui_priv;
	struct action_oui_extension_priv *ext_priv;
	qdf_list_t *ext_list;
	QDF_STATUS status;
	qdf_list_node_t *node = NULL;

	oui_priv = psoc_priv->oui_priv[action_id];
	ext_list = &oui_priv->extension_list;
	qdf_mutex_acquire(&oui_priv->extension_lock);
	while (!qdf_list_empty(ext_list)) {
		status = qdf_list_remove_front(ext_list, &node);
		if (!QDF_IS_STATUS_SUCCESS(status)) {
			action_oui_err("Invalid delete in action: %u",
				       oui_priv->id);
			qdf_mutex_release(&oui_priv->extension_lock);
			return QDF_STATUS_E_FAILURE;
		}
		ext_priv = qdf_container_of(
				node,
				struct action_oui_extension_priv,
				item);
		qdf_mem_free(ext_priv);
		ext_priv = NULL;

		if (!wlan_action_oui_is_dynamic(oui_priv->id)) {
			if (psoc_priv->total_extensions)
				psoc_priv->total_extensions--;
			else
				action_oui_err("unexpected total_extensions 0");
		}

		if (action_id >= ACTION_OUI_HOST_ONLY) {
			if (!psoc_priv->host_only_extensions)
				action_oui_err("unexpected total host extensions");
			else
				psoc_priv->host_only_extensions--;
		}
	}
	qdf_mutex_release(&oui_priv->extension_lock);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
wlan_action_oui_cleanup(struct action_oui_psoc_priv *psoc_priv,
			enum action_oui_id action_id)
{
	QDF_STATUS status;

	if (!psoc_priv)
		return QDF_STATUS_E_INVAL;

	if (!wlan_action_oui_id_valid(action_id))
		return QDF_STATUS_E_INVAL;

	switch (action_id) {
	case ACTION_OUI_ALLOW_NSS_GREATER_THAN_2:
	case ACTION_OUI_DISALLOW_NSS_GREATER_THAN_2:
		/* Clear NSS allowlist*/
		status = __wlan_action_oui_cleanup(
					psoc_priv,
					ACTION_OUI_ALLOW_NSS_GREATER_THAN_2);
		if (QDF_IS_STATUS_ERROR(status)) {
			action_oui_debug("action oui cleanup failure for NSS allowlist");
			return status;
		}

		/* Clear NSS denylist*/
		status = __wlan_action_oui_cleanup(
				     psoc_priv,
				     ACTION_OUI_DISALLOW_NSS_GREATER_THAN_2);
		if (QDF_IS_STATUS_ERROR(status)) {
			action_oui_debug("action oui cleanup failure for NSS denylist");
			return status;
		}
		break;

	default:
		/* For other action OUI IDs, cleanup directly */
		status = __wlan_action_oui_cleanup(psoc_priv, action_id);
		break;
	}

	return status;
}

QDF_STATUS
wlan_action_oui_restore_default_and_send(struct action_oui_psoc_priv *psoc_priv,
					 enum action_oui_id action_id)
{
	QDF_STATUS status;

	ACTION_OUI_ENTER();

	if (!psoc_priv) {
		action_oui_err("psoc_priv is NULL");
		status = QDF_STATUS_E_INVAL;
		goto exit;
	}

	if (!wlan_action_oui_id_valid(action_id)) {
		action_oui_err("Invalid action_oui id: %u", action_id);
		status = QDF_STATUS_E_INVAL;
		goto exit;
	}

	/* First cleanup the action OUI configuration */
	status = wlan_action_oui_cleanup(psoc_priv, action_id);
	if (QDF_IS_STATUS_ERROR(status)) {
		action_oui_err("Failed to cleanup action_oui id: %u",
			       action_id);
		goto exit;
	}

	/* Parse the default list if it's not empty */
	if (qdf_str_len(psoc_priv->action_oui_str[action_id]) > 0) {
		status = action_oui_parse_string(
					psoc_priv->psoc,
					psoc_priv->action_oui_str[action_id],
					action_id);
		if (QDF_IS_STATUS_ERROR(status)) {
			action_oui_err("Failed to parse default list for action_oui id: %u",
				       action_id);
			goto exit;
		}
		action_oui_debug("Parsed default list for action_oui id: %u",
				 action_id);
	}

	/* Then send the configuration to firmware */
	if (action_id < ACTION_OUI_HOST_ONLY) {
		status = action_oui_send(psoc_priv, action_id);
		if (QDF_IS_STATUS_ERROR(status))
			action_oui_err("Failed to send action_oui id: %u",
				       action_id);
	} else {
		action_oui_debug("action_oui id %u is host only, skip send",
				 action_id);
	}

exit:
	ACTION_OUI_EXIT();
	return status;
}

bool wlan_action_oui_is_empty(struct wlan_objmgr_psoc *psoc,
			      enum action_oui_id action_id)
{
	struct action_oui_psoc_priv *psoc_priv;
	bool empty = true;

	if (!psoc) {
		action_oui_err("Invalid psoc");
		goto exit;
	}

	if (!wlan_action_oui_id_valid(action_id)) {
		action_oui_err("Invalid action_oui id: %u", action_id);
		goto exit;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		goto exit;
	}

	empty = action_oui_is_empty(psoc_priv, action_id);

exit:
	return empty;
}

bool wlan_action_oui_v2_enabled(struct wlan_objmgr_psoc *psoc)
{
	struct action_oui_psoc_priv *psoc_priv;
	bool v2_enabled = false;

	if (!psoc) {
		action_oui_err("Invalid psoc");
		return false;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		return false;
	}

	v2_enabled = psoc_priv->action_oui_enable == 2 &&
		     target_if_get_action_oui_v2_cap(psoc_priv->psoc);

	return v2_enabled;
}

bool
wlan_is_nss_allowlist_denylist_config_supported(struct wlan_objmgr_psoc *psoc)
{
	wmi_unified_t wmi_hdl;

	if (!psoc) {
		action_oui_err("Invalid psoc");
		return false;
	}

	wmi_hdl = GET_WMI_HDL_FROM_PSOC(psoc);
	if (!wmi_hdl) {
		action_oui_err("wmi handle is NULL");
		return false;
	}

	return wmi_service_enabled(wmi_hdl,
				   wmi_service_supported_ext_oui_action_ids) &&
	       wmi_service_enabled(
			wmi_hdl,
			wmi_service_support_whitelist_blacklist_ap_config);
}

/**
 * wlan_action_oui_convert_bit_to_byte_mask() - Convert bit mask to byte mask
 * @bit_mask_value: input, bit mask value, use 1 bit to mask 1 bit
 * @bit_mask_len: input, bit mask len
 * @byte_mask_value: output, byte mask value, use 1 bit to mask 1 byte
 * @byte_mask_len: output, byte mask len
 *
 * Return: QDF_STATUS.
 */
static QDF_STATUS
wlan_action_oui_convert_bit_to_byte_mask(uint8_t *bit_mask_value,
					 uint32_t bit_mask_len,
					 uint8_t *byte_mask_value,
					 uint32_t *byte_mask_len)
{
	uint8_t data_mask = 0, bit;
	uint8_t *mask_value = byte_mask_value;
	uint32_t i;

	*byte_mask_len = (bit_mask_len + 7) / 8;
	for (i = 0; i < bit_mask_len; i++) {
		if (bit_mask_value[i])
			bit = 1 << (7 - i % 8);
		else
			bit = 0;
		data_mask += bit;
		if (i == bit_mask_len - 1) {
			*mask_value = data_mask;
		} else if ((i + 1) % 8 == 0) {
			*mask_value = data_mask;
			mask_value++;
			data_mask = 0;
		}
	}

	return QDF_STATUS_SUCCESS;
}

#ifdef ACTION_OUI_OP_ATTR
static QDF_STATUS
wlan_action_oui_add_token_opt(enum action_oui_token_type action_token,
			      uint8_t *value,
			      uint32_t value_len,
			      struct action_oui_extension *ext)
{
	uint8_t byte_mask_value[ACTION_OUI_MAX_DATA_MASK_LENGTH_HOST_ONLY] = {0};
	uint32_t byte_mask_len = 0;

	switch (action_token) {
	case ACTION_OUI_MAC_ADDR_TOKEN:
		if (value_len != QDF_MAC_ADDR_SIZE) {
			action_oui_err("Invalid mac addr len %u", value_len);
			return QDF_STATUS_E_INVAL;
		}
		qdf_mem_copy(ext->mac_addr, value, value_len);
		ext->mac_addr_length = value_len;
		ext->info_mask = ext->info_mask | ACTION_OUI_INFO_MAC_ADDRESS;
		break;
	case ACTION_OUI_MAC_MASK_TOKEN:
		if (value_len > ACTION_OUI_MAC_MASK_LENGTH) {
			action_oui_err("Invalid mac mask len %u", value_len);
			return QDF_STATUS_E_INVAL;
		}
		qdf_mem_copy(ext->mac_mask, value, value_len);
		ext->mac_mask_length = value_len;
		break;
	case ACTION_OUI_MAC_BIT_MASK_TOKEN:
		if (value_len > QDF_MAC_ADDR_SIZE) {
			action_oui_err("Invalid mac mask len %u", value_len);
			return QDF_STATUS_E_INVAL;
		}
		wlan_action_oui_convert_bit_to_byte_mask(value,
							 value_len,
							 byte_mask_value,
							 &byte_mask_len);
		qdf_mem_copy(ext->mac_mask, byte_mask_value, byte_mask_len);
		ext->mac_mask_length = byte_mask_len;
		break;
	case ACTION_OUI_CAPABILITY_TOKEN:
		if (value_len > ACTION_OUI_MAX_CAPABILITY_LENGTH) {
			action_oui_err("Invalid capability len %d", value_len);
			return QDF_STATUS_E_INVAL;
		}
		qdf_mem_copy(ext->capability, value, value_len);
		ext->capability_length = value_len;
		if (*value & ACTION_OUI_CAPABILITY_NSS_MASK)
			ext->info_mask = ext->info_mask |
					 ACTION_OUI_INFO_AP_CAPABILITY_NSS;
		if (*value & ACTION_OUI_CAPABILITY_HT_ENABLE_MASK)
			ext->info_mask = ext->info_mask |
					 ACTION_OUI_INFO_AP_CAPABILITY_HT;
		if (*value & ACTION_OUI_CAPABILITY_VHT_ENABLE_MASK)
			ext->info_mask = ext->info_mask |
					 ACTION_OUI_INFO_AP_CAPABILITY_VHT;
		if (*value & ACTION_CAPABILITY_5G_BAND_MASK ||
		    *value & ACTION_OUI_CAPABILITY_2G_BAND_MASK)
			ext->info_mask = ext->info_mask |
					 ACTION_OUI_INFO_AP_CAPABILITY_BAND;
		break;
	default:
		break;
	}

	return QDF_STATUS_SUCCESS;
}

wlan_action_oui_add_cap(uint8_t nss_bitmap,
			bool ht,
			bool vht,
			uint8_t band_bitmap,
			struct action_oui_extension *oui_ext)
{
	union action_oui_capability cap;

	if (nss_bitmap > ACTION_OUI_CAPABILITY_NSS_MASK) {
		action_oui_err("Invalid nss bitmap %u", nss_bitmap);
		return QDF_STATUS_E_INVAL;
	}

	if (band_bitmap > 3) {
		action_oui_err("Invalid band bitmap %u", band_bitmap);
		return QDF_STATUS_E_INVAL;
	}

	cap.bitmap.nss_bitmap = nss_bitmap;
	cap.bitmap.ht = ht ? 1 : 0;
	cap.bitmap.vht = vht ? 1 : 0;
	cap.bitmap.band_bitmap =
		band_bitmap << ACTION_OUI_CAPABILITY_BAND_OFFSET;
	oui_ext->capability[0] = cap.val;

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
wlan_action_oui_add_token_opt(enum action_oui_token_type action_token,
			      uint8_t *value,
			      uint32_t value_len,
			      struct action_oui_extension *ext)
{
	return QDF_STATUS_SUCCESS;
}
#endif

QDF_STATUS
wlan_action_oui_add_token(enum action_oui_token_type action_token,
			  uint8_t *value,
			  uint32_t value_len,
			  struct action_oui_extension *ext)
{
	uint8_t byte_mask_value[ACTION_OUI_MAX_DATA_MASK_LENGTH_HOST_ONLY] = {0};
	uint32_t byte_mask_len = 0;

	switch (action_token) {
	case ACTION_OUI_TOKEN:
		if (value_len != 3 && value_len != 5) {
			action_oui_err("Invalid oui len %u", value_len);
			QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
					   QDF_TRACE_LEVEL_DEBUG,
					   value, value_len);
			return QDF_STATUS_E_INVAL;
		}
		qdf_mem_copy(ext->oui, value, value_len);
		ext->oui_length = value_len;
		ext->info_mask = ext->info_mask | ACTION_OUI_INFO_OUI;
		break;
	case ACTION_OUI_DATA_TOKEN:
		if (value_len > ACTION_OUI_MAX_DATA_LENGTH_HOST_ONLY) {
			action_oui_err("Invalid data len %u", value_len);
			QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
					   QDF_TRACE_LEVEL_DEBUG,
					   value, value_len);
			return QDF_STATUS_E_INVAL;
		}
		qdf_mem_copy(ext->data, value, value_len);
		ext->data_length = value_len;
		break;
	case ACTION_OUI_DATA_MASK_TOKEN:
		if (value_len > ACTION_OUI_MAX_DATA_MASK_LENGTH_HOST_ONLY) {
			action_oui_err("Invalid data mask len %u", value_len);
			return QDF_STATUS_E_INVAL;
		}
		qdf_mem_copy(ext->data_mask, value, value_len);
		ext->data_mask_length = value_len;
		break;
	case ACTION_OUI_DATA_BIT_MASK_TOKEN:
		if (value_len > ACTION_OUI_MAX_DATA_LENGTH_HOST_ONLY) {
			action_oui_err("Invalid data mask len %u", value_len);
			QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
					   QDF_TRACE_LEVEL_DEBUG,
					   value, value_len);
			return QDF_STATUS_E_INVAL;
		}
		wlan_action_oui_convert_bit_to_byte_mask(value,
							 value_len,
							 byte_mask_value,
							 &byte_mask_len);
		qdf_mem_copy(ext->data_mask, byte_mask_value, byte_mask_len);
		ext->data_mask_length = byte_mask_len;
		break;
	default:
		return wlan_action_oui_add_token_opt(action_token, value,
						     value_len, ext);
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
wlan_action_oui_extension_store(struct wlan_objmgr_psoc *psoc,
				enum action_oui_id action_id,
				struct action_oui_extension *oui_ext,
				uint8_t oui_ext_num)
{
	struct action_oui_psoc_priv *psoc_priv;
	struct action_oui_priv *oui_priv;
	QDF_STATUS status;


	if (!psoc) {
		action_oui_err("Invalid psoc");
		return QDF_STATUS_E_INVAL;
	}

	if (!wlan_action_oui_id_valid(action_id)) {
		action_oui_err("Invalid action_oui id: %u", action_id);
		return QDF_STATUS_E_INVAL;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc priv is NULL");
		return QDF_STATUS_E_INVAL;
	}
	oui_priv = psoc_priv->oui_priv[action_id];
	if (!oui_priv) {
		action_oui_err("action oui priv not allocated");
		return QDF_STATUS_E_INVAL;
	}


	status = action_oui_extension_store(psoc_priv, oui_priv, oui_ext,
					    oui_ext_num);

	return status;
}

void wlan_action_oui_extension_dump(struct action_oui_extension *oui_ext)
{
	action_oui_trace("oui len %u", oui_ext->oui_length);
	if (oui_ext->oui_length)
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   oui_ext->oui, oui_ext->oui_length);

	action_oui_trace("oui data len %u", oui_ext->data_length);
	if (oui_ext->data_length)
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   oui_ext->data, oui_ext->data_length);

	action_oui_trace("oui data mask len %u", oui_ext->data_mask_length);
	if (oui_ext->data_mask_length)
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   oui_ext->data_mask,
				   oui_ext->data_mask_length);

	if (oui_ext->mac_addr_length) {
		action_oui_trace("mac");
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   oui_ext->mac_addr,
				   oui_ext->mac_addr_length);
	}

	if (oui_ext->mac_mask_length) {
		action_oui_trace("mac mask");
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   oui_ext->mac_mask,
				   oui_ext->mac_mask_length);
	}

	if (oui_ext->mac_exclusion_length) {
		action_oui_trace("mac exclusion");
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   oui_ext->mac_exclusion.mac_addr,
				   QDF_MAC_ADDR_SIZE);
	}

	if (oui_ext->mac_exclusion_mask_length) {
		action_oui_trace("mac exclusion mask");
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_ACTION_OUI,
				   QDF_TRACE_LEVEL_TRACE,
				   &oui_ext->mac_exclusion.mac_addr_mask,
				   1);
	}
}

bool wlan_action_oui_is_dynamic(enum action_oui_id action_id)
{
	if (action_id >= ACTION_OUI_HOST_FW_EXT_START &&
	    action_id < ACTION_OUI_HOST_ONLY)
		return true;

	return false;
}

enum action_oui_id
action_oui_get_active_action_id(
			 struct wlan_objmgr_psoc *psoc,
			 enum action_oui_arbitrator_type arbitrator_type)
{
	struct action_oui_psoc_priv *psoc_priv;
	struct action_oui_priv *oui_priv;

	if (!psoc) {
		action_oui_err("psoc is NULL");
		return ACTION_OUI_MAXIMUM_ID;
	}

	if (arbitrator_type >= ACTION_OUI_ARBITRATOR_TYPE_MAX) {
		action_oui_err("Invalid arbitrator type: %d", arbitrator_type);
		return ACTION_OUI_MAXIMUM_ID;
	}

	psoc_priv = action_oui_psoc_get_priv(psoc);
	if (!psoc_priv) {
		action_oui_err("psoc_priv is NULL");
		return ACTION_OUI_MAXIMUM_ID;
	}

	switch (arbitrator_type) {
	case ACTION_OUI_ARBITRATOR_TYPE_NSS:

		/* Check if allow list is non-empty */
		oui_priv = psoc_priv->oui_priv[ACTION_OUI_ALLOW_NSS_GREATER_THAN_2];
		qdf_mutex_acquire(&oui_priv->extension_lock);
		if (!qdf_list_empty(&oui_priv->extension_list)) {
			qdf_mutex_release(&oui_priv->extension_lock);
			action_oui_debug("NSS allow list is active");
			return ACTION_OUI_ALLOW_NSS_GREATER_THAN_2;
		}
		qdf_mutex_release(&oui_priv->extension_lock);

		/* Check if disallow list is non-empty */
		oui_priv = psoc_priv->oui_priv[ACTION_OUI_DISALLOW_NSS_GREATER_THAN_2];
		qdf_mutex_acquire(&oui_priv->extension_lock);
		if (!qdf_list_empty(&oui_priv->extension_list)) {
			qdf_mutex_release(&oui_priv->extension_lock);
			action_oui_debug("NSS disallow list is active");
			return ACTION_OUI_DISALLOW_NSS_GREATER_THAN_2;
		}
		qdf_mutex_release(&oui_priv->extension_lock);
		break;

	default:
		action_oui_debug("Unknown arbitrator type: %d",
				 arbitrator_type);
		break;
	}

	action_oui_debug("No active list found for arbitrator type: %d",
			 arbitrator_type);
	return ACTION_OUI_MAXIMUM_ID;
}

void
action_oui_get_nss_policy(struct wlan_objmgr_psoc *psoc,
			  struct action_oui_search_attr *attr,
			  bool *found_in_list,
			  uint32_t *list_type)
{
	enum action_oui_id active_action_id;

	ACTION_OUI_ENTER();

	if (!psoc) {
		action_oui_err("psoc is NULL");
		return;
	}

	if (!attr) {
		action_oui_err("attr is NULL");
		return;
	}

	if (!found_in_list || !list_type) {
		action_oui_err("found_in_list or list_type pointer is NULL");
		return;
	}

	*found_in_list = false;
	*list_type = ACTION_OUI_MAXIMUM_ID;

	active_action_id = action_oui_get_active_action_id(
					psoc,
					ACTION_OUI_ARBITRATOR_TYPE_NSS);
	if (active_action_id == ACTION_OUI_MAXIMUM_ID) {
		action_oui_debug("No active NSS arbitrator list");
		return;
	}

	*list_type = active_action_id;

	*found_in_list = wlan_action_oui_search(psoc, attr, active_action_id);

	action_oui_debug("NSS arbitrator result - list_type: %u found_in_list: %u",
			 *list_type, *found_in_list);

	ACTION_OUI_EXIT();
}

uint32_t
wlan_action_oui_max_ext_num(enum action_oui_id action_id)
{
	if (wlan_action_oui_is_dynamic(action_id))
		return ACTION_OUI_MAX_HOST_FW_EXT;
	return  action_id < ACTION_OUI_HOST_ONLY ?
		ACTION_OUI_MAX_EXT_TO_FW : ACTION_OUI_MAX_EXT_HOST_ONLY;
}

bool wlan_action_oui_id_valid(enum action_oui_id action_id)
{
	if ((action_id >= ACTION_OUI_CONNECT_1X1 &&
	     action_id < ACTION_OUI_MAXIMUM_STATIC_ID) ||
	    (action_id >= ACTION_OUI_HOST_FW_EXT_START &&
	     action_id < ACTION_OUI_MAXIMUM_ID))
		return true;

	return false;
}
