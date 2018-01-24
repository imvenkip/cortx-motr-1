/* -*- c -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 07-Jan-2015
 */
#pragma once
#ifndef __MERO_CONF_UT_CONFC_H__
#define __MERO_CONF_UT_CONFC_H__

#include "fid/fid.h"       /* M0_FID_TINIT */

enum {
	M0_UT_CONF_ROOT,
	M0_UT_CONF_SITE,
	M0_UT_CONF_PROF,
	M0_UT_CONF_NODE,
	M0_UT_CONF_PROCESS0,
	M0_UT_CONF_PROCESS1,
	M0_UT_CONF_SERVICE0,
	M0_UT_CONF_SERVICE1,
	M0_UT_CONF_SDEV0,
	M0_UT_CONF_SDEV1,
	M0_UT_CONF_SDEV2,
	M0_UT_CONF_RACK,
	M0_UT_CONF_ENCLOSURE,
	M0_UT_CONF_CONTROLLER,
	M0_UT_CONF_DISK,
	M0_UT_CONF_POOL,
	M0_UT_CONF_PVER,
	M0_UT_CONF_RACKV,
	M0_UT_CONF_ENCLOSUREV,
	M0_UT_CONF_CONTROLLERV,
	M0_UT_CONF_DISKV,
	M0_UT_CONF_UNKNOWN_NODE
};

/* See ut/conf.xc */
static const struct m0_fid m0_ut_conf_fids[] = {
	[M0_UT_CONF_ROOT]         = M0_FID_TINIT('t', 1, 0),
	[M0_UT_CONF_SITE]         = M0_FID_TINIT('S', 1, 1),
	[M0_UT_CONF_PROF]         = M0_FID_TINIT('p', 1, 0),
	[M0_UT_CONF_NODE]         = M0_FID_TINIT('n', 1, 2),
	[M0_UT_CONF_PROCESS0]     = M0_FID_TINIT('r', 1, 5),
	[M0_UT_CONF_PROCESS1]     = M0_FID_TINIT('r', 1, 6),
	[M0_UT_CONF_SERVICE0]     = M0_FID_TINIT('s', 1, 9),
	[M0_UT_CONF_SERVICE1]     = M0_FID_TINIT('s', 1, 10),
	[M0_UT_CONF_SDEV0]        = M0_FID_TINIT('d', 1, 13),
	[M0_UT_CONF_SDEV1]        = M0_FID_TINIT('d', 1, 14),
	[M0_UT_CONF_SDEV2]        = M0_FID_TINIT('d', 1, 15),
	[M0_UT_CONF_RACK]         = M0_FID_TINIT('a', 1, 3),
	[M0_UT_CONF_ENCLOSURE]    = M0_FID_TINIT('e', 1, 7),
	[M0_UT_CONF_CONTROLLER]   = M0_FID_TINIT('c', 1, 11),
	[M0_UT_CONF_DISK]         = M0_FID_TINIT('k', 1, 16),
	[M0_UT_CONF_POOL]         = M0_FID_TINIT('o', 1, 4),
	[M0_UT_CONF_PVER]         = M0_FID_TINIT('v', 1, 8),
	[M0_UT_CONF_RACKV]        = M0_FID_TINIT('j', 1, 12),
	[M0_UT_CONF_ENCLOSUREV]   = M0_FID_TINIT('j', 1, 17),
	[M0_UT_CONF_CONTROLLERV]  = M0_FID_TINIT('j', 1, 18),
	[M0_UT_CONF_DISKV]        = M0_FID_TINIT('j', 1, 19),
	[M0_UT_CONF_UNKNOWN_NODE] = M0_FID_TINIT('n', 5, 5)
};

#endif /* __MERO_CONF_UT_CONFC_H__ */
