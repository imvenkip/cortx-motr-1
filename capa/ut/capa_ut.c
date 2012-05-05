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
 * Original creation date: 01/24/2011
 */

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/ut.h"
#include "lib/misc.h" /* C2_SET0() */

#include "capa/capa.h"

/* stub on stub :-) */
struct capa_object_guard {
};

static int cog_init(struct capa_object_guard *cog)
{
	return 0;
}

static void cog_fini(struct capa_object_guard *cog)
{
}

/**
   @todo struct c2_capa_issuer is empty, put proper values.
 */

static void capa_test(void)
{
	int                      ret;
	struct c2_capa_ctxt      ctx;
	struct c2_object_capa    read_capa;
	struct c2_object_capa    write_capa;
	struct capa_object_guard guard;
	struct c2_capa_issuer    issuer;

	ret = cog_init(&guard);
	C2_UT_ASSERT(ret == 0);

	ret = c2_capa_ctxt_init(&ctx);
	C2_UT_ASSERT(ret == 0);

	ret = c2_capa_new(&read_capa,
			  C2_CAPA_ENTITY_OBJECT,
			  C2_CAPA_OP_DATA_READ,
			  &guard);
	C2_UT_ASSERT(ret == 0);

	ret = c2_capa_new(&write_capa,
			  C2_CAPA_ENTITY_OBJECT,
			  C2_CAPA_OP_DATA_WRITE,
			  &guard);
	C2_UT_ASSERT(ret == 0);

	ret = c2_capa_get(&ctx, &issuer, &read_capa);
	C2_UT_ASSERT(ret == 0);

	ret = c2_capa_get(&ctx, &issuer, &write_capa);
	C2_UT_ASSERT(ret == 0);

	/* have capability so auth should succeed */
	ret = c2_capa_auth(&ctx, &write_capa, C2_CAPA_OP_DATA_WRITE);
	C2_UT_ASSERT(ret == 0);

	ret = c2_capa_auth(&ctx, &write_capa, C2_CAPA_OP_DATA_READ);
	C2_UT_ASSERT(ret == 0);

	c2_capa_put(&ctx, &read_capa);
	c2_capa_put(&ctx, &write_capa);

/* uncomment when realization ready */
#if 0
	/* have NO capability so auth should fail */
	ret = c2_capa_auth(&ctx, &write_capa, C2_CAPA_OP_DATA_WRITE);
	C2_UT_ASSERT(ret != 0);

	ret = c2_capa_auth(&ctx, &write_capa, C2_CAPA_OP_DATA_READ);
	C2_UT_ASSERT(ret != 0);
#endif

	cog_fini(&guard);
	c2_capa_ctxt_fini(&ctx);
}

const struct c2_test_suite capa_ut = {
        .ts_name = "capa-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "capa", capa_test },
                { NULL, NULL }
        }
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
