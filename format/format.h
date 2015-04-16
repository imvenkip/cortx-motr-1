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
#ifndef __MERO_FORMAT_H__
#define __MERO_FORMAT_H__

#include "lib/types.h"  /* uint64_t */
#include "lib/misc.h"   /* M0_FIELD_VALUE */
#include "lib/mutex.h"
#include "lib/rwlock.h"

/**
 * @defgroup format Persistent objects format
 *
 * @{
 */

/** Standard header of a persistent object. */
struct m0_format_header {
	/**
	 * Encoding of m0_format_tag data.
	 *
	 * - 16 most significant bits  -- version number;
	 * - 16 bits in the middle     -- object type;
	 * - 32 least significant bits -- size in bytes.
	 *
	 * @see  m0_format_header_pack(), m0_format_header_unpack()
	 */
	uint64_t hd_bits;
} M0_XCA_RECORD;

/** Standard footer of a persistent object. */
struct m0_format_footer {
	uint64_t ft_magic;
	uint64_t ft_checksum;
} M0_XCA_RECORD;

struct m0_format_tag {
	uint16_t ot_version;
	uint16_t ot_type;
	uint32_t ot_size; /* NOTE: the size is measured in bytes */
};

M0_INTERNAL void m0_format_header_pack(struct m0_format_header *dest,
				       const struct m0_format_tag *src);
M0_INTERNAL void m0_format_header_unpack(struct m0_format_tag *dest,
					 const struct m0_format_header *src);

M0_INTERNAL void m0_format_footer_generate(struct m0_format_footer *footer,
					   void                    *buffer,
					   uint32_t                 size);
M0_INTERNAL int m0_format_footer_verify(const struct m0_format_footer *footer,
					void                          *buffer,
					uint32_t                       size);

struct m0_be_mutex {
	union {
		struct m0_mutex mutex;
		char            pad[168];
	} bm_u;
};
M0_BASSERT(sizeof(struct m0_mutex) <=
	   sizeof(M0_FIELD_VALUE(struct m0_be_mutex, bm_u.pad)));

struct m0_be_rwlock {
	union {
		struct m0_rwlock rwlock;
		char             pad[144];
	} bl_u;
};
M0_BASSERT(sizeof(struct m0_rwlock) <=
	   sizeof(M0_FIELD_VALUE(struct m0_be_rwlock, bl_u.pad)));

/** @} format */
#endif /* __MERO_FORMAT_H__ */
