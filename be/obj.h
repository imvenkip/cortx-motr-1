/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery.vorotyntsev@seagate.com>
 * Original creation date: 18-Dec-2014
 */
#pragma once
#ifndef __MERO_BE_OBJ_H__
#define __MERO_BE_OBJ_H__

#include "lib/types.h"  /* uint64_t */
#include "lib/misc.h"   /* M0_FIELD_VALUE */
#include "lib/mutex.h"
#include "lib/rwlock.h"

/**
 * @defgroup be_obj Persistent objects
 *
 * @{
 */

/** Standard header of a persistent object. */
struct m0_be_obj_header {
	/**
	 * Encoding of m0_be_obj_tag data.
	 *
	 * - 16 most significant bits  -- version number;
	 * - 32 bits in the middle     -- object type;
	 * - 16 least significant bits -- size, measured in 8-byte quantities.
	 *
	 * @see  m0_be_obj_header_pack(), m0_be_obj_header_unpack()
	 */
	uint64_t hd_bits;
} M0_XCA_RECORD;

/** Standard footer of a persistent object. */
struct m0_be_obj_footer {
	uint64_t ft_magic;
	uint64_t ft_checksum;
} M0_XCA_RECORD;

struct m0_be_obj_tag {
	uint32_t ot_version;
	uint32_t ot_type;
	uint32_t ot_size; /* NOTE: the size is measured in 8-byte quantities */
};

M0_INTERNAL void m0_be_obj_header_pack(struct m0_be_obj_header *dest,
				       const struct m0_be_obj_tag *src);
M0_INTERNAL void m0_be_obj_header_unpack(struct m0_be_obj_tag *dest,
					 const struct m0_be_obj_header *src);

struct m0_be_mutex {
	union {
		struct m0_mutex mutex;
		char            pad[56];
	} bm_u;
};
M0_BASSERT(sizeof(struct m0_mutex) <=
	   sizeof(M0_FIELD_VALUE(struct m0_be_mutex, bm_u.pad)));

struct m0_be_rwlock {
	union {
		struct m0_rwlock rwlock;
		char             pad[56];
	} bl_u;
};
M0_BASSERT(sizeof(struct m0_rwlock) <=
	   sizeof(M0_FIELD_VALUE(struct m0_be_rwlock, bl_u.pad)));

/** @} be */
#endif /* __MERO_BE_OBJ_H__ */
