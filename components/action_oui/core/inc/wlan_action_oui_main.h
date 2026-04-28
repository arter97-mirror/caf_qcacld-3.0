/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
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
 * DOC: Declare private API which shall be used internally only
 * in action_oui component. This file shall include prototypes of
 * various notification handlers and logging functions.
 *
 * Note: This API should be never accessed out of action_oui component.
 */

#ifndef _WLAN_ACTION_OUI_MAIN_H_
#define _WLAN_ACTION_OUI_MAIN_H_

#include <qdf_types.h>
#include "wlan_action_oui_public_struct.h"
#include "wlan_action_oui_priv.h"
#include "wlan_action_oui_objmgr.h"

#define action_oui_log(level, args...) \
	QDF_TRACE(QDF_MODULE_ID_ACTION_OUI, level, ## args)

#define action_oui_logfl(level, format, args...) \
	action_oui_log(level, FL(format), ## args)

#define action_oui_fatal(format, args...) \
		action_oui_logfl(QDF_TRACE_LEVEL_FATAL, format, ## args)
#define action_oui_err(format, args...) \
		action_oui_logfl(QDF_TRACE_LEVEL_ERROR, format, ## args)
#define action_oui_warn(format, args...) \
		action_oui_logfl(QDF_TRACE_LEVEL_WARN, format, ## args)
#define action_oui_info(format, args...) \
		action_oui_logfl(QDF_TRACE_LEVEL_INFO, format, ## args)
#define action_oui_debug(format, args...) \
		action_oui_logfl(QDF_TRACE_LEVEL_DEBUG, format, ## args)
#define action_oui_trace(format, args...) \
		action_oui_logfl(QDF_TRACE_LEVEL_TRACE, format, ## args)

#define ACTION_OUI_ENTER() action_oui_debug("enter")
#define ACTION_OUI_EXIT() action_oui_debug("exit")

/**
 * action_oui_psoc_create_notification(): Handler for psoc create notify.
 * @psoc: psoc which is going to be created by objmgr
 * @arg: argument for notification handler.
 *
 * Allocate and attach psoc private object.
 *
 * Return: QDF_STATUS status in case of success else return error.
 */
QDF_STATUS
action_oui_psoc_create_notification(struct wlan_objmgr_psoc *psoc, void *arg);

/**
 * action_oui_psoc_destroy_notification(): Handler for psoc destroy notify.
 * @psoc: psoc which is going to be destroyed by objmgr
 * @arg: argument for notification handler.
 *
 * Deallocate and detach psoc private object.
 *
 * Return QDF_STATUS status in case of success else return error
 */
QDF_STATUS
action_oui_psoc_destroy_notification(struct wlan_objmgr_psoc *psoc, void *arg);

/**
 * wlan_action_oui_search() - Check for OUIs and related info in IE data.
 * @psoc: objmgr psoc object
 * @attr: pointer to structure containing type of action, beacon IE data etc.,
 * @action_id: type of action to be checked
 *
 * This is a wrapper function which invokes internal function to search
 * for OUIs and related info (specified from ini file) in vendor specific
 * data of beacon IE for given action.
 *
 * Return: If search is successful return true else false.
 */
#ifdef WLAN_FEATURE_ACTION_OUI
bool wlan_action_oui_search(struct wlan_objmgr_psoc *psoc,
			    struct action_oui_search_attr *attr,
			    enum action_oui_id action_id);

/**
 * wlan_action_oui_is_empty() - Check action oui present or not
 * @psoc: psoc object
 * @action_id: action oui id
 *
 * This function will check action oui present or not for specific action type.
 *
 * Return: True if no action oui for the action type.
 */
bool wlan_action_oui_is_empty(struct wlan_objmgr_psoc *psoc,
			      enum action_oui_id action_id);

/**
 * wlan_action_oui_cleanup() - Cleanup action OUI with special handling
 * @psoc_priv: action oui objmgr private context
 * @action_id: type of action to be cleaned up
 *
 * This function handles cleanup of action OUI entries. For Mutual exclusive
 * list, it cleans up all the mutually exclusive lists.
 * For other action IDs, it performs standard cleanup.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_action_oui_cleanup(struct action_oui_psoc_priv *psoc_priv,
			enum action_oui_id action_id);

/**
 * wlan_action_oui_restore_default_and_send() - Restore default and send
 * action OUI to firmware
 * @psoc_priv: action oui objmgr private context
 * @action_id: type of action to be restored and sent
 *
 * This function cleans up the action OUI configuration, restores default list,
 * and sends the configuration to firmware.
 *
 * Return: QDF_STATUS_SUCCESS if restore and send is successful.
 */
QDF_STATUS
wlan_action_oui_restore_default_and_send(
			struct action_oui_psoc_priv *psoc_priv,
			enum action_oui_id action_id);

/**
 * action_oui_psoc_enable() - Notify action OUI psoc enable
 * @psoc: objmgr psoc object
 * @load_default_config: flag to indicate if we need to load
 * default config or not.
 *
 * Return: void
 */
void action_oui_psoc_enable(struct wlan_objmgr_psoc *psoc,
			    bool load_default_config);

/**
 * action_oui_psoc_disable() - Notify action OUI psoc disable
 * @psoc: objmgr psoc object
 *
 * Return: void
 */
void action_oui_psoc_disable(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_action_oui_v2_enabled() - Check whether action oui v2 is enabled
 * @psoc: psoc object
 *
 * This function will check whether action oui v2 is enabled by both target cap
 * and ini config.
 *
 * Return: True if action oui v2 is enabled by both target cap and ini config.
 */
bool wlan_action_oui_v2_enabled(struct wlan_objmgr_psoc *psoc);

/**
 * wlan_action_oui_add_token() - Add action oui token to oui extension
 * @action_token: oui token type
 * @value: oui token value
 * @value_len: oui token value
 * @oui_ext: pointer to action oui extension to save token
 *
 * Return: QDF_STATUS.
 */
QDF_STATUS
wlan_action_oui_add_token(enum action_oui_token_type action_token,
			  uint8_t *value,
			  uint32_t value_len,
			  struct action_oui_extension *oui_ext);

#ifdef ACTION_OUI_OP_ATTR
/**
 * wlan_action_oui_add_cap() - Add action oui capability config
 * @nss_bitmap: oui token type
 * @nss_bitmap: nss bitmap
 *  bit 0 : NSS 1
 *  bit 1 : NSS 2
 *  bit 2 : NSS 3
 *  bit 3 : NSS 4
 * @ht: is ht supported
 * @vht: is vht supported
 * @band_bitmap: band bitmap: 2G and 5G
 * @oui_ext: action oui extension to save cap
 *
 * Return: QDF_STATUS.
 */
QDF_STATUS
wlan_action_oui_add_cap(uint8_t nss_bitmap,
			bool ht,
			bool vht,
			uint8_t band_bitmap,
			struct action_oui_extension *oui_ext);
#endif

/**
 * wlan_action_oui_extension_store() - Store action oui
 * @psoc: psoc
 * @action_id: oui action id
 * @oui_ext: action oui extension to be stored
 * @oui_ext_num: number of action oui extension to be stored
 *
 * Return: QDF_STATUS.
 */
QDF_STATUS
wlan_action_oui_extension_store(struct wlan_objmgr_psoc *psoc,
				enum action_oui_id action_id,
				struct action_oui_extension *oui_ext,
				uint8_t oui_ext_num);

/**
 * wlan_action_oui_max_ext_num() - Max number of oui extensions supported in
 * each action oui
 * @action_id: oui action id
 *
 * Return: QDF_STATUS.
 */
uint32_t
wlan_action_oui_max_ext_num(enum action_oui_id action_id);

/**
 * wlan_action_oui_extension_dump() - Dump oui extension of action oui
 * @oui_ext: action oui extension
 *
 * Return: void
 */
void wlan_action_oui_extension_dump(struct action_oui_extension *oui_ext);

/**
 * wlan_action_oui_is_dynamic() - Check if action OUI ID is dynamic
 * @action_id: action OUI ID to check
 *
 * This function checks if the given action OUI ID belongs to dynamic OUI
 * category. Dynamic OUI IDs have variable data length and are identified
 * by specific enum values (128 and 129).
 *
 * Return: true if action_id is 128 or 129, false otherwise
 */
bool wlan_action_oui_is_dynamic(enum action_oui_id action_id);
#else
static inline
bool wlan_action_oui_search(struct wlan_objmgr_psoc *psoc,
			    struct action_oui_search_attr *attr,
			    enum action_oui_id action_id)
{
	return false;
}

static inline
bool wlan_action_oui_is_empty(struct wlan_objmgr_psoc *psoc,
			      enum action_oui_id action_id)
{
	return true;
}

static inline QDF_STATUS
wlan_action_oui_restore_default_and_send(
				struct action_oui_psoc_priv *psoc_priv,
				enum action_oui_id action_id)
{
	return QDF_STATUS_SUCCESS;
}

static inline
void action_oui_psoc_enable(struct wlan_objmgr_psoc *psoc,
			    bool load_default_config)
{
}

static inline
void action_oui_psoc_disable(struct wlan_objmgr_psoc *psoc)
{
}

static inline
bool wlan_action_oui_v2_enabled(struct wlan_objmgr_psoc *psoc)
{
	return false;
}

static inline void
wlan_action_oui_extension_dump(struct action_oui_extension *oui_ext)
{
}

static inline
bool wlan_action_oui_is_dynamic(enum action_oui_id action_id)
{
	return false;
}
#endif
#endif /* end  of _WLAN_ACTION_OUI_MAIN_H_ */
