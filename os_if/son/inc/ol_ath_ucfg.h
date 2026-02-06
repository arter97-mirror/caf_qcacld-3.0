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

#ifndef OL_ATH_UCFG_H_
#define OL_ATH_UCFG_H_

enum {
	HE_SR_PSR_ENABLE                        = 1,
	HE_SR_NON_SRG_OBSSPD_ENABLE             = 2,
	HE_SR_SR15_ENABLE                       = 3,
	HE_SR_SRG_OBSSPD_ENABLE                 = 4,
	HE_SR_ENABLE_PER_AC                     = 5,
};

enum {
	HE_SRP_IE_SRG_BSS_COLOR_BITMAP                 = 1,
	HE_SRP_IE_SRG_PARTIAL_BSSID_BITMAP             = 2,
};

#ifdef QCA_SUPPORT_WDS_EXTENDED
/**
 * wlan_hdd_enable_wds_ext() - Enable WDS extension for a vdev
 * @psoc: Pointer to PSOC object
 * @vdev: Pointer to vdev object
 *
 * This function enables WDS (Wireless Distribution System) extension feature
 * for the specified virtual device. It configures the data path layer to
 * support WDS extension functionality by:
 *
 * 1. Getting the data path handle from the PSOC
 * 2. Reading the WDS extension configuration from the configuration
 *    manager
 * 3. Setting the WDS extension parameter in the data path layer
 *
 * Context: Can be called from process context. The function accesses
 * configuration and data path components.
 *
 * Return: None
 */
void wlan_hdd_enable_wds_ext(struct wlan_objmgr_psoc *psoc,
			     struct wlan_objmgr_vdev *vdev);
#endif

#endif /* OL_ATH_UCFG_H_ */
