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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/06/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom_generic.h"        /* c2_fom_type_register() */
#include "lib/ut.h"
#include "fop/fom_long_lock.h"

struct test_request;

/**
 * Object encompassing FOM for rdwr
 * operation and necessary context data
 */
struct fom_rdwr {
	/** Generic c2_fom object. */
        struct c2_fom                    fr_gen;
	struct c2_long_lock_link	 fr_link;

	/* Long lock UT test data */
	struct test_request		*fr_req;
	size_t				 fr_seqn;
};

static size_t fom_rdwr_home_locality(const struct c2_fom *fom);
static void fop_rdwr_fom_fini(struct c2_fom *fom);
static int fom_rdwr_state(struct c2_fom *fom);

/** Generic ops object for rdwr */
static const struct c2_fom_ops fom_rdwr_ops = {
	.fo_fini = fop_rdwr_fom_fini,
	.fo_state = fom_rdwr_state,
	.fo_home_locality = fom_rdwr_home_locality
};

/** FOM type specific functions for rdwr FOP. */
static const struct c2_fom_type_ops fom_rdwr_type_ops = {
	.fto_create = NULL
};

/** Rdwr specific FOM type operations vector. */
static struct c2_fom_type rdwr_fom_type = {
        .ft_ops = &fom_rdwr_type_ops,
};

static size_t fom_rdwr_home_locality(const struct c2_fom *fom)
{
	static size_t locality = 0;

	C2_PRE(fom != NULL);
	return locality++;
}

static int rdwr_fom_create(struct c2_fom **m)
{
        struct c2_fom                   *fom;
        struct fom_rdwr			*fom_obj;

        C2_PRE(m != NULL);

        C2_ALLOC_PTR(fom_obj);
        C2_UT_ASSERT(fom_obj != NULL);

	fom = &fom_obj->fr_gen;
	c2_fom_type_register(&rdwr_fom_type);
	c2_fom_init(fom, &rdwr_fom_type, &fom_rdwr_ops,
		    (struct c2_fop *) 1, NULL);

	c2_long_lock_link_init(&fom_obj->fr_link, fom);

	*m = fom;
	return 0;
}

static void fop_rdwr_fom_fini(struct c2_fom *fom)
{
	struct fom_rdwr *fom_obj;

	fom_obj = container_of(fom, struct fom_rdwr, fr_gen);
	c2_fom_fini(fom);
	c2_long_lock_link_fini(&fom_obj->fr_link);
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
