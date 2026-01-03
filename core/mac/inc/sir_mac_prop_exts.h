/*
 * Copyright (c) 2011-2015, 2017-2019, 2021 The Linux Foundation.
 * All rights reserved.
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
 * This file sir_mac_prop_exts.h contains the MAC protocol
 * extensions to support ANI feature set.
 * Author:        Chandra Modumudi
 * Date:          11/27/02
 */
#ifndef __MAC_PROP_EXTS_H
#define __MAC_PROP_EXTS_H

#include "sir_types.h"
#include "sir_api.h"
#include "ani_system_defs.h"

/* / EID (Element ID) definitions */

#define PROP_CAPABILITY_GET(bitname, value) \
	(((value) >> SIR_MAC_PROP_CAPABILITY_ ## bitname) & 1)

#define IS_DOT11_MODE_HT(dot11Mode) \
	(((dot11Mode == MLME_DOT11_MODE_11N) || \
	  (dot11Mode == MLME_DOT11_MODE_11N_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11AC) || \
	  (dot11Mode == MLME_DOT11_MODE_11AC_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11AX) || \
	  (dot11Mode == MLME_DOT11_MODE_11AX_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_ALL)) ? true : false)

#define IS_DOT11_MODE_VHT(dot11Mode) \
	(((dot11Mode == MLME_DOT11_MODE_11AC) || \
	  (dot11Mode == MLME_DOT11_MODE_11AC_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11AX) || \
	  (dot11Mode == MLME_DOT11_MODE_11AX_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_ALL)) ? true : false)

#define IS_DOT11_MODE_HE(dot11Mode) \
	(((dot11Mode == MLME_DOT11_MODE_11AX) || \
	  (dot11Mode == MLME_DOT11_MODE_11AX_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_ALL)) ? true : false)

#define IS_DOT11_MODE_EHT(dot11Mode) \
	(((dot11Mode == MLME_DOT11_MODE_11BE) || \
	  (dot11Mode == MLME_DOT11_MODE_11BE_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_ALL)) ? true : false)

/* UHR (11bn) specific */
#define IS_DOT11_MODE_UHR(dot11Mode) \
	(((dot11Mode == MLME_DOT11_MODE_11BN) || \
	  (dot11Mode == MLME_DOT11_MODE_11BN_ONLY) || \
	  (dot11Mode == MLME_DOT11_MODE_ALL)) ? true : false)

#define IS_DOT11_MODE_11A(dot11mode) \
	((dot11mode == MLME_DOT11_MODE_11A) ? true : false)

#define IS_DOT11_MODE_11B(dot11Mode)  \
	((dot11Mode == MLME_DOT11_MODE_11B) ? true : false)

#define IS_DOT11_MODE_11G(dot11mode) \
	((dot11mode == MLME_DOT11_MODE_11G || \
	  dot11mode == MLME_DOT11_MODE_11G_ONLY) ? true : false)

#define IS_DOT11_MODE_LEGACY(dot11mode) \
	((dot11mode == MLME_DOT11_MODE_ABG || IS_DOT11_MODE_11A(dot11mode) || \
	  IS_DOT11_MODE_11B(dot11mode) || \
	  IS_DOT11_MODE_11G(dot11mode)) ? true : false)

#define IS_DOT11_MODE_HT_ONLY(dot11mode) \
	(!(IS_DOT11_MODE_LEGACY(dot11mode) || IS_DOT11_MODE_VHT(dot11mode) || \
	   IS_DOT11_MODE_HE(dot11mode)))

#define IS_BSS_VHT_CAPABLE(vhtCaps) \
	((vhtCaps).present && \
	 ((vhtCaps).rxMCSMap != 0xFFFF) && \
	 ((vhtCaps).txMCSMap != 0xFFFF))

#define WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ		0
#define WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ		1
#define WNI_CFG_VHT_CHANNEL_WIDTH_160MHZ		2
#define WNI_CFG_VHT_CHANNEL_WIDTH_80_PLUS_80MHZ	3

#ifdef WLAN_FEATURE_11BE
#define WNI_CFG_EHT_CHANNEL_WIDTH_320MHZ 4
#endif

/* OMI element channel width definitions */
#define WNI_OMI_CH_WIDTH_20MHZ	0
#define WNI_OMI_CH_WIDTH_40MHZ	1
#define WNI_OMI_CH_WIDTH_80MHZ	2

#endif /* __MAC_PROP_EXTS_H */
