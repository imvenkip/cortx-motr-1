/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Madhavrao Vemuri<madhav_vemuri@xyratex.com>
 * Original creation date: 27/02/2013
 */

#pragma once

#ifndef __MERO_STOB_AD_PRIVATE_H__
#define __MERO_STOB_AD_PRIVATE_H__

#include "stob/stob_id.h"
#include "stob/stob_id_xc.h"
#include "db/extmap_xc.h"

struct ad_rec_part_seg {
	uint32_t            ps_segments;
	struct m0_emap_seg *ps_old_data;
} M0_XCA_SEQUENCE;

struct ad_rec_part {
	uint32_t	       arp_dom_id;
	struct m0_stob_id      arp_stob_id;
	struct ad_rec_part_seg arp_seg;
} M0_XCA_RECORD;

/* __MERO_STOB_AD_PRIVATE_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
