/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Madhavrao Vemuri <Madhavrao Vemuri@xyratex.com>
 * Original creation date: 12/12/2012
 */

#pragma once

#ifndef __MERO_DB_EXTMAP_SEG_H__
#define __MERO_DB_EXTMAP_SEG_H__
#include "lib/ext.h"       /* m0_ext */
#include "lib/types_xc.h"
#include "lib/ext_xc.h"

/** Extent map segment. */
struct m0_emap_seg {
	/** Map prefix, identifying the map in its collection. */
	struct m0_uint128 ee_pre;
	/** Name-space extent. */
	struct m0_ext     ee_ext;
	/** Value associated with the extent. */
	uint64_t          ee_val;
} M0_XCA_RECORD;

/** Extent map segment vector */
struct m0_emap_seg_vec {
	/** number of segments. */
	uint32_t	    sv_nr;
	/** array of emap segments. */
	struct m0_emap_seg *sv_es;
} M0_XCA_SEQUENCE;

#endif /* __MERO_DB_EXTMAP_SEG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
