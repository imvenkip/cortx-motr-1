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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 *                  Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 08/14/2012
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_WIRE_H__
#define __MERO_ADDB_ADDB_WIRE_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"

/**
   @defgroup addb_otw ADDB Serializable Data Types
   @ingroup addb

   These data types are serializable for network and stob use.
   They are all tagged with the @ref xcode attributes that automate the
   generation of the serialization code.
   @{
 */

/**
   A serializable sequence of 64 bit integers used by ADDB.
 */
struct m0_addb_uint64_seq {
	uint32_t  au64s_nr;   /**< Sequence length */
	uint64_t *au64s_data; /**< Sequence data */
} M0_XCA_SEQUENCE;

/**
   A serializable sequence of ADDB context identifiers.
 */
struct m0_addb_ctxid_seq {
	uint32_t                   acis_nr;   /**< Sequence length */
	struct m0_addb_uint64_seq *acis_data; /**< Sequence data */
} M0_XCA_SEQUENCE;

/**
   A serializable ADDB record.
 */
struct m0_addb_rec {
	/**
	   Record identifier is of the form
	     (::m0_addb_base_rec_type << 32) + (uint32_t) id
	   where id depends on the base record type:
	   - For EX, DP, CNTR, SEQ it is the record type identifier.
	   - For CTX_DEF it is the context type identifier.
	   @see m0_addb_rec_rid_make(), m0_addb_rec_rid_to_brt(),
                m0_addb_rec_rid_to_id()
	 */
	uint64_t                  ar_rid;
	uint64_t                  ar_ts;     /**< Time stamp */
	struct m0_addb_ctxid_seq  ar_ctxids; /**< Context identifiers */
	struct m0_addb_uint64_seq ar_data;   /**< Record payload */
} M0_XCA_RECORD;

/**
   A serializable ADDB record sequence.
 */
struct m0_addb_rec_seq {
	uint32_t            ars_nr;   /**< Sequence length */
	struct m0_addb_rec *ars_data; /**< Sequence data */
} M0_XCA_SEQUENCE;

/**
   A stob segment header.
   STOBSINK_XCODE_VER_NR must be incremented when this is changed.
 */
struct m0_addb_seg_header {
	/** Header sequence number */
	uint64_t                  sh_seq_nr;
	/** version number */
	uint32_t                  sh_ver_nr;
	/** Segment size */
	uint32_t                  sh_segsize;
} M0_XCA_RECORD;

/**
   A stob segment trailer.
   STOBSINK_XCODE_VER_NR must be incremented when this is changed.
 */
struct m0_addb_seg_trailer {
	/** Trailer sequence number */
	uint64_t                  st_seq_nr;
	/** Number of records in the segment */
	uint32_t                  st_rec_nr;
	/** Reserved for future use */
	uint32_t                  st_reserved;
} M0_XCA_RECORD;

/** @} end of addb_otw group */

#endif /* __MERO_ADDB_ADDB_WIRE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
