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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 03/17/2011
 */
#pragma once

#ifndef __MERO_ADDB_ADDBFF_ADDB_H__
#define __MERO_ADDB_ADDBFF_ADDB_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"

/* buf in memory */
struct m0_mem_buf {
	uint32_t  cmb_count;
	uint8_t  *cmb_value;
} M0_XCA_SEQUENCE;

/* on-wire and on-disk addb record header */
struct m0_addb_record_header {
	uint64_t arh_magic1;
	uint32_t arh_version;
	uint32_t arh_len;
	uint64_t arh_event_id;
	uint64_t arh_timestamp;
	uint64_t arh_magic2;
} M0_XCA_RECORD;

/* on wire addb record */
struct m0_addb_record {
	struct m0_addb_record_header ar_header;
	struct m0_mem_buf            ar_data;
} M0_XCA_RECORD;

/* on wire addb record reply */
struct m0_addb_reply {
	uint32_t ar_rc;
} M0_XCA_RECORD;

#endif /* __MERO_ADDB_ADDBFF_ADDB_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
