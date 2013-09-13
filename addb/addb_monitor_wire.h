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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 07/09/2013
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_MONITOR_WIRE_H__
#define __MERO_ADDB_ADDB_MONITOR_WIRE_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"
#include "addb/addb_wire.h"

/**
    ADDB Monitor Serializable Data Types

   These data types are serializable for network and stob use.
   They are all tagged with the @ref xcode attributes that automate the
   generation of the serialization code.
   @{
 */
struct m0_addb_sum_rec_wire {
	/** This is addb_rec_type:art_id for this ADDB summary record */
	uint32_t                   asrw_id;
	/* ADDB summary record data encoded in sequence form */
	struct m0_addb_uint64_seq asrw_rec;
} M0_XCA_RECORD;

struct m0_addb_sum_rec_fop {
	uint32_t                     asrf_nr;
	struct m0_addb_sum_rec_wire *asrf_recs;
} M0_XCA_SEQUENCE;

#endif /* __MERO_ADDB_ADDB_MONITOR_WIRE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
