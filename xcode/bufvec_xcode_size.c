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
 * Original creation date: 08/21/2011
 */

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "xcode/bufvec_xcode.h"
#include "lib/errno.h"
#include "lib/memory.h"

/**
   @addtogroup xcode

   <b>Fop xcode</b>

   See head comments in fop_format.h for overview of fop formats.

   This file defines generic functions to calculate the onwire fop size.
   Main entry point is c2_xcode_fop_size_get() which calculates the onwire
   size (in bytes) of the fop data that would be encode/decoded into/from
   a bufvec. The size is calculated by c2_xcode_length()

   @{
 */

/**
  Calculates the onwire size of fop data. This function internally calls
  the fop field type specific functions to calculate the size

  @param fop The data for this fop is to be encoded/decoded.

  @retval Onwire size of the fop in bytes.
*/

size_t c2_xcode_fop_size_get(struct c2_fop *fop)
{
	size_t              size;
	size_t              padding;
	struct c2_xcode_ctx ctx;

	C2_PRE(fop != NULL);

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj) {
			  *fop->f_type->ft_xc_type,
			  c2_fop_data(fop) });
	size    = c2_xcode_length(&ctx);
	padding = c2_xcode_pad_bytes_get(size);

	return size + padding;
}
C2_EXPORTED(c2_xcode_fop_size_get);

/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
