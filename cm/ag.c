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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 30/11/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cm/ag.h"

/**
   @addtogroup CMAG
   @{
 */

enum {
	/** Hex value of "ag_link". */
	AGGR_GROUP_LINK_MAGIC = 0x61675f6c696e6b,
	/** Hex value of "ag_head". */
	AGGR_GROUP_LINK_HEAD = 0x61675f68656164,
};

C2_TL_DESCR_DEFINE(aggr_grps, "aggr_grp_list_descr", ,
		   struct c2_cm_aggr_group, cag_sw_linkage, cag_magic,
		   AGGR_GROUP_LINK_MAGIC, AGGR_GROUP_LINK_HEAD);

C2_TL_DEFINE(aggr_grps, , struct c2_cm_aggr_group);

struct c2_bob_type aggr_grps_bob;
C2_BOB_DEFINE( , &aggr_grps_bob, c2_cm_aggr_group);

/** @} CMAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
