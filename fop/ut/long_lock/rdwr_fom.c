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

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/ut.h"
#include "fop/fom_long_lock.h"

/**
 * Object encompassing FOM for rdwr
 * operation and necessary context data
 */
struct fom_rdwr {
	/** Generic c2_fom object. */
        struct c2_fom                    fp_gen;
	/** FOP associated with this FOM. */
        struct c2_fop			*fp_fop;
	struct c2_long_lock_link	 fp_link;

	/* Long lock UT test data */
        uint32_t             fr_type;
        uint32_t             fr_owners_min;
        uint32_t             fr_owners_max;
        uint32_t             fr_waiters;
        uint32_t             fr_seqn;
};

static int rdwr_fop_fom_create(struct c2_fop *fop, struct c2_fom **m);
static size_t fom_rdwr_home_locality(const struct c2_fom *fom);
static void fop_rdwr_fom_fini(struct c2_fom *fom);
static int fom_rdwr_state(struct c2_fom *fom);

/** Generic ops object for rdwr */
struct c2_fom_ops fom_rdwr_ops = {
	.fo_fini = fop_rdwr_fom_fini,
	.fo_state = fom_rdwr_state,
	.fo_home_locality = fom_rdwr_home_locality
};

/** FOM type specific functions for rdwr FOP. */
static const struct c2_fom_type_ops fom_rdwr_type_ops = {
	.fto_create = rdwr_fop_fom_create
};

/** Rdwr specific FOM type operations vector. */
struct c2_fom_type fom_rdwr_mopt = {
        .ft_ops = &fom_rdwr_type_ops,
};

static size_t fom_rdwr_home_locality(const struct c2_fom *fom)
{
	static size_t locality = 0;

	C2_PRE(fom != NULL);
	return locality++;
}

static int rdwr_fop_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_fom                   *fom;
        struct fom_rdwr			*fom_obj;
        struct c2_fom_type              *fom_type;

        C2_PRE(m != NULL);

        fom_obj= c2_alloc(sizeof(struct fom_rdwr));
        C2_UT_ASSERT(fom_obj != NULL);

        fom_type = &fom_rdwr_mopt;
        C2_UT_ASSERT(fom_type != NULL);

	fom = &fom_obj->fp_gen;
	c2_fom_init(fom, fom_type, &fom_rdwr_ops, fop, NULL);
	fom_obj->fp_fop = fop;

	c2_long_lock_link_init(&fom_obj->fp_link, fom);

	*m = fom;
	return 0;
}

static void fop_rdwr_fom_fini(struct c2_fom *fom)
{
	struct fom_rdwr *fom_obj;

	fom_obj = container_of(fom, struct fom_rdwr, fp_gen);
	c2_fom_fini(fom);
	c2_long_lock_link_fini(&fom_obj->fp_link);
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
