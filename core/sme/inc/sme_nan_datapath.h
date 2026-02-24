/*
 * Copyright (c) 2016-2019, 2021 The Linux Foundation. All rights reserved.
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
 * DOC: sme_nan_datapath.h
 *
 * SME NAN Data path API specification
 */

#ifndef __SME_NAN_DATAPATH_H
#define __SME_NAN_DATAPATH_H

#include "csr_inside_api.h"
#ifdef WLAN_FEATURE_11AX
#include "dot11f.h"
#endif

#ifdef WLAN_FEATURE_NAN
void csr_roam_update_ndp_return_params(struct mac_context *mac_ctx,
					uint32_t result,
					uint32_t *roam_status,
					uint32_t *roam_result,
					struct csr_roam_info *roam_info);

#ifdef WLAN_FEATURE_11AX
#if defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
/**
 * sme_nan_pack_he_cap() - Pack HE capability IE
 * @he_cap_cfg: dot11f HE capability structure
 * @buf: Output buffer
 * @buf_len: Size of output buffer
 * @consumed: Output number of bytes consumed
 *
 * Return: QDF_STATUS
 */
QDF_STATUS sme_nan_pack_he_cap(const tDot11fIEhe_cap * he_cap_cfg,
			       uint8_t *buf, uint32_t buf_len,
			       uint32_t *consumed);
#else
static inline QDF_STATUS
sme_nan_pack_he_cap(const tDot11fIEhe_cap *he_cap_cfg,
		    uint8_t *buf, uint32_t buf_len,
		    uint32_t *consumed)
{
	return QDF_STATUS_SUCCESS;
}
#endif
#endif
#else /* WLAN_FEATURE_NAN */

static inline void csr_roam_update_ndp_return_params(struct mac_context *mac_ctx,
					uint32_t result,
					uint32_t *roam_status,
					uint32_t *roam_result,
					struct csr_roam_info *roam_info)
{
}

#endif /* WLAN_FEATURE_NAN */

#endif /* __SME_NAN_DATAPATH_H */
