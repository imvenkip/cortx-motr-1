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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 23/05/2013
 */

#pragma once

#ifndef __MERO_LIB_CHECKSUM_H__
#define __MERO_LIB_CHECKSUM_H__

#include "lib/types.h"
#include "lib/buf.h"
#include "lib/vec.h"
#include "fid/fid.h"

/**
   @defgroup checksum Data integrity using checksum
   @{
	Checksum for data blocks is computed based on checksum algorithm
	selected from configuration.
	Also checksum length is chosen based on this algorithm.

	A Tag for each block is computed based on global file identifier,
	target cob identifier and offset on sender side and is send along with
	checksum.

	struct m0_di_checksum_data contains checksum type id and checksum
	payload.

	struct m0_di object is embedded in fop(struct m0_fop) or can be
	placed in struct m0_cob.
	struct m0_fop {
		...
		struct m0_di f_di;
		...
	};
	It is initialized in m0_fop_init().
	void m0_fop_init(struct m0_fop *fop, struct m0_fop_type *fopt,
			 void *data, ...) {
		...
		rc = m0_di_init(fop->f_di, enum m0_di_type di_type,
				const struct m0_di_checksum_type *type,
				struct m0_di_checksum_data *csum_data // NULL,
				struct m0_di_tag *tag // NULL);
		...
	}

	struct m0_di_checksum_data is embedded in fop data(fd_data).
	It is initialized and allocated during fop data initialization like,
	int m0_io_fop_init() {
		...
		// struct m0_fop_cob_rw *rw;
		// csum_data = rw->crw_csum_data;
		// tag = rw->crw_tag;
		...
	}

	Checksum values are computed during additiion of data buffers to fops.
	Also Tag values are computed using file id, cob id and offset.

	void m0_data_integrity_init(void)
	{
	 // Get checksum algorithm type from configuration.
	 // Default checksum algorithm is set to M0_CHECKSUM_NONE.

	 // Checksum algorithm type is set in file attributes during
	 // file creation.
	}
*/

/* Forward declarations. */
struct m0_di_checksum_data;
struct m0_di_checksum_type_ops;

/** Data integrity types. */
enum m0_di_type {
	/** Data integrity is disabled. */
	M0_DI_NONE,
	/** Data integrity is performed in software only. */
	M0_DI_SW,
	/** Data integrity uses T10 disk to store and verify checksums. */
	M0_DI_T10
};

/** Checksum algorithm to be used. */
enum m0_di_checksum_alg_type {
	/** No checksum algorithm is used. */
	M0_DI_CHECKSUM_NONE,
	/** CRC algorithm is used to compute 32-bit checksum. */
	M0_DI_CHECKSUM_CRC32,
	/** SHA-2 checksum algorithm is used to compute 256-bit checksum. */
	M0_DI_CHECKSUM_SHA,
	/** Fletcher checksum algorithm is used to compute 64-bit checksum. */
	M0_DI_CHECKSUM_FF,
	M0_DI_CHECKSUM_NR,
};

/** Returns block size of the data for which checksum is computed. */
M0_INTERNAL m0_bcount_t m0_di_data_blocksize(enum m0_di_type di_type);

struct m0_di_checksum_type {
	const char                           *cht_name;
	/** checksum identifier passed as m0_di_checksum_data::chd_id. */
	uint64_t			      cht_id;
	/** Checksum type operations. */
	const struct m0_di_checksum_type_ops *cht_ops;
	uint64_t                              cht_magic;
};

M0_INTERNAL int m0_di_checksum_type_register(struct m0_di_checksum_type *ct);
M0_INTERNAL void m0_di_checksum_type_deregister(struct m0_di_checksum_type *ct);
M0_INTERNAL const struct m0_di_checksum_type *m0_di_checksum_type_lookup(
						uint64_t cid);

struct m0_di {
	/** Data integrity type to be used. */
	enum m0_di_type			  ch_di_type;
	/** Pointer to the type object. */
	const struct m0_di_checksum_type *ch_type;
	/** Checksum payload. */
	struct m0_di_checksum_data       *ch_data;
	/** Tag for each block. */
	struct m0_di_tag		 *ch_tag;
};

/**
 * Checksum is passed around as an array of 64-bit integers.
 * For 32-bit checksum chp_nr is 1 and lower 32 bits of ->chp_data[0] are used.
 */
struct m0_di_checksum_payload {
	uint64_t  chp_nr;
	uint64_t *chp_data;
} M0_XCA_SEQUENCE;

/** Checksum for one or more blocks of data are stored. */
struct m0_di_checksum_vec {
	uint64_t                       chv_nr;
	struct m0_di_checksum_payload *chv_csum;
} M0_XCA_SEQUENCE;

struct m0_di_checksum_data {
	/** Checksum identifier. */
	uint64_t		  chd_id;
	/** Checksum values. */
	struct m0_di_checksum_vec chd_payload;
} M0_XCA_RECORD;

M0_INTERNAL int m0_di_init(struct m0_di *di, enum m0_di_type di_type,
			   const struct m0_di_type_ops *di_type_ops;
			   const struct m0_di_checksum_type *type,
			   struct m0_di_checksum_data *csum_data,
			   struct m0_di_tag *tag);
M0_INTERNAL void m0_di_fini(struct m0_di *di);

struct m0_di_checksum_type_ops {
	/** Returns the length of the checksum. */
	m0_bcount_t (*checksum_data_len)(void);
	/** Computes the checksum on data and stores in the payload. */
	int (*checksum_compute)(const struct m0_buf *data, uint32_t blk_size,
				struct m0_di_checksum_data *csum);
	/** Returns true iff two checksums are equal. */
	bool (*checksum_verify)(struct m0_di_checksum_data *cs1,
				struct m0_di_checksum_data *cs2);
};

#define M0_DI_CSUM_TYPE_DECLARE(type, name, id, csum_type_ops) \
struct m0_di_checksum_type type =			       \
(struct m0_di_checksum_type) {		                       \
	.cht_name = name,		                       \
	.cht_id   = (id),				       \
	.cht_ops  = (csum_type_ops)	                       \
};							       \

/**
 * Tag is passed around as an array of 64-bit integers.
 * It is computed using global file identifier, file identifier of
 * read/write request and offset.
 */
struct m0_di_tag {
	uint64_t  dt_nr;
	uint64_t *dt_data;
} M0_XCA_SEQUENCE;

M0_INTERNAL uint64_t m0_di_tag_compute(const struct m0_fid *gfid,
				       const struct m0_fid *fid,
				       uint64_t offset);

M0_INTERNAL bool m0_di_tag_verify(const uint64_t tag, const struct m0_fid *gfid,
				  const struct m0_fid *fid, uint64_t offset);

/**
   Computes the checksum for the region excluding checksum field and
   sets this value in the checksum field.

   @param cksum_field Address of the checksum field
 */
M0_INTERNAL void m0_md_di_set(const void *addr, m0_bcount_t nob,
			       uint64_t *cksum_field);

/**
   Compares the checksum value in cksum_field with the
   computed checksum for this region.

    @param cksum_field Address of the checksum field
 */
M0_INTERNAL bool m0_md_di_chk(const void *addr, m0_bcount_t nob,
			      uint64_t *cksum_fileld);

#define M0_MD_DI_SET(obj, field) \
m0_md_di_set((obj), sizeof *(obj), &(obj)->field)

#define M0_MD_DI_CHK(obj, field) \
m0_md_di_chk((obj), sizeof *(obj), &(obj)->field)

/** @} end of checksum */
#endif /*  __MERO_LIB_CHECKSUM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
