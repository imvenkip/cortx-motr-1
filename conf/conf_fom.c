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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */

#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "fop/fop_format.h"
#include "conf_fom.h"
#include "conf_fop.h"
#include "onwire.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "rpc/rpc.h"

/**
 * Generic FOP/FOM functions
 */
static int fop_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
}

static void fop_fom_fini(struct c2_fom *fom)
{
}

C2_INTERNAL size_t fom_locality(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);
	return c2_fop_opcode(fom->fo_fop);
}

/* -------------------------------------------------------------------
 * c2_conf_fetch_resp FOM
 */

/**
 * State function for c2_conf_fetch_resp
 */
C2_INTERNAL int c2_fom_fetch_state(struct c2_fom *fom)
{
}

/** Generic ops object for c2_conf_fetch */
struct c2_fom_ops c2_fom_fetch_ops = {
	.fo_fini = fop_fom_fini,
	.fo_state = c2_fom_fetch_state,
	.fo_home_locality = fom_locality
};

/** FOM type specific functions for c2_conf_fetch FOP. */
static const struct c2_fom_type_ops c2_fom_fetch_type_ops = {
	.fto_create = fop_fom_create
};

/** c2_conf_fetch specific FOM type operations vector. */
struct c2_fom_type c2_fom_fetch_mopt = {
        .ft_ops = &c2_fom_fetch_type_ops,
};

/* -------------------------------------------------------------------
 * c2_conf_update FOM
 */

/**
 * State function for c2_conf_update request
 */
C2_INTERNAL int c2_fom_update_state(struct c2_fom *fom)
{
}

/** Generic ops object for c2_conf_update */
struct c2_fom_ops c2_fom_update_ops = {
	.fo_fini = fop_fom_fini,
	.fo_state = c2_fom_update_state,
	.fo_home_locality = fom_locality
};

/** FOM type specific functions for c2_conf_update FOP. */
static const struct c2_fom_type_ops c2_fom_update_type_ops = {
	.fto_create = fop_fom_create
};

/** c2_conf_update specific FOM type operations vector. */
struct c2_fom_type c2_fom_update_mopt = {
        .ft_ops = &c2_fom_update_type_ops,
};




/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

