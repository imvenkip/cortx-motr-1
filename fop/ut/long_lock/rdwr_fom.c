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
 * Original creation date: 08/06/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "fop/fop_format.h"
#include "fop/ut/long_lock/rdwr_fom.h"
#include "fop/ut/long_lock/rdwr_fop.h"
#include "fop/ut/long_lock/rdwr_tb.h"

#ifdef __KERNEL__
#include "rdwr_fop_k.h"
#else
#include "rdwr_fop_u.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "rpc/rpc2.h"

static int rdwr_fop_fom_create(struct c2_fop *fop, struct c2_fom **m);

/** Generic ops object for rdwr */
struct c2_fom_ops c2_fom_rdwr_ops = {
	.fo_fini = c2_fop_rdwr_fom_fini,
	.fo_state = c2_fom_rdwr_state,
	.fo_home_locality = c2_fom_rdwr_home_locality
};

/** FOM type specific functions for rdwr FOP. */
static const struct c2_fom_type_ops c2_fom_rdwr_type_ops = {
	.fto_create = rdwr_fop_fom_create
};

/** Rdwr specific FOM type operations vector. */
struct c2_fom_type c2_fom_rdwr_mopt = {
        .ft_ops = &c2_fom_rdwr_type_ops,
};

size_t c2_fom_rdwr_home_locality(const struct c2_fom *fom)
{
	static int cnt = 0;
	C2_PRE(fom != NULL);

	//return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
	return cnt++;
}

/* Init for rdwr */
static int rdwr_fop_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_fom                   *fom;
        struct c2_fom_rdwr		*fom_obj;
        struct c2_fom_type              *fom_type;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);

        fom_obj= c2_alloc(sizeof(struct c2_fom_rdwr));
        if (fom_obj == NULL)
                return -ENOMEM;
        fom_type = &c2_fom_rdwr_mopt;
        C2_ASSERT(fom_type != NULL);
        fop->f_type->ft_fom_type = *fom_type;
	fom = &fom_obj->fp_gen;
	c2_fom_init(fom, fom_type, &c2_fom_rdwr_ops, fop, NULL);
	fom_obj->fp_fop = fop;
	*m = fom;
	return 0;
}

void c2_fop_rdwr_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_rdwr *fom_obj;

	fom_obj = container_of(fom, struct c2_fom_rdwr, fp_gen);
	c2_fom_fini(fom);
	c2_free(fom_obj);

	return;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
