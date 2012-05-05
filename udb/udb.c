/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 01/04/2011
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "udb.h"

/**
   @addtogroup udb
   @{
 */


int c2_udb_ctxt_init(struct c2_udb_ctxt *ctxt)
{
	/* TODO add more here. Now it is a stub */
	return 0;
}

void c2_udb_ctxt_fini(struct c2_udb_ctxt *ctxt)
{

	/* TODO add more here. Now it is a stub */
	return;
}

int c2_udb_add(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_domain *edomain,
	       const struct c2_udb_cred *external,
	       const struct c2_udb_cred *internal)
{
	/* TODO add more here. Now it is a stub */
	return 0;
}

int c2_udb_del(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_domain *edomain,
	       const struct c2_udb_cred *external,
	       const struct c2_udb_cred *internal)
{

	/* TODO add more here. Now it is a stub */
	return 0;
}

int c2_udb_e2i(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_cred *external,
	       struct c2_udb_cred *internal)
{

	/* TODO add more here. Now it is a stub */
	return 0;
}

int c2_udb_i2e(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_cred *internal,
	       struct c2_udb_cred *external)
{

	/* TODO add more here. Now it is a stub */
	return 0;
}

/** @} end group udb */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
