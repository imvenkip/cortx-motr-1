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
 * Original creation date: 22-Dec-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_LIB
#include "lib/trace.h"

#include "format/format.h"
#include "lib/assert.h"  /* M0_PRE */
#include "mero/magic.h"
#include "lib/misc.h"
#include "lib/errno.h"

/**
 * @addtogroup format
 *
 * @{
 */

M0_INTERNAL void m0_format_header_pack(struct m0_format_header *dest,
				       const struct m0_format_tag *src)
{
	dest->hd_bits = (uint64_t)src->ot_version << 48 |
			(uint64_t)src->ot_type    << 32 |
			(uint64_t)src->ot_size;
}

M0_INTERNAL void m0_format_header_unpack(struct m0_format_tag *dest,
					 const struct m0_format_header *src)
{
	*dest = (struct m0_format_tag){
		.ot_version = src->hd_bits >> 48,
		.ot_type    = src->hd_bits >> 32 & 0x0000ffff,
		.ot_size    = src->hd_bits & 0xffffffff
	};
}

M0_INTERNAL void m0_format_footer_generate(struct m0_format_footer *footer,
					   void                    *buffer,
					   uint32_t                 size)
{
	footer->ft_magic = M0_FORMAT_FOOTER_MAGIC;
	/** @todo generate checksum from cursor/buffer. */
}

M0_INTERNAL int m0_format_footer_verify(const struct m0_format_footer *footer,
					void                          *buffer,
					uint32_t                       size)
{
	if (footer->ft_magic != M0_FORMAT_FOOTER_MAGIC)
		return M0_ERR(-EPROTO);
	/** @todo generate and verify checksum from cursor/buffer */

	return M0_RC(0);
}

#undef M0_TRACE_SUBSYSTEM

/** @} format */
