/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 15/01/2013
 */

#include "fop/fom_long_lock.h"
#include "lib/memory.h"
#include "lib/mutex.h"
#include "lib/ub.h"

/**
 * Type of run unit benchmark test.
 */
enum ub_fom_type {
	/** Test accessing N bytes of memory on each state transition. */
	UB_FOM_MEM_B,
	/** Test accessing N kilobytes of memory on each state transition. */
	UB_FOM_MEM_KB,
	/** Test accessing N megabytes of memory on each state transition. */
	UB_FOM_MEM_MB,
	/** Test taking and releasing a mutex. */
	UB_FOM_MUTEX,
	/** Test taking and releasing a fom long lock. */
	UB_FOM_LONG_LOCK,
	/** Test calling m0_fom_block_{enter,leave}(). */
	UB_FOM_BLOCK,
	UB_FOM_NR
};

/**
 * Object encompassing FOM used as a unit benchmarkcontext data.
 */
struct ub_fom_ctx {
	/** Generic m0_fom object. */
	struct m0_fom             fc_gen;
	/** A sequence number of this structre. */
	size_t                    fc_seqn;
	/** Type of benchmark. */
	enum ub_fom_type          fc_type;
	/** Long lock used non-blocking synchronization test. */
	struct m0_long_lock      *fc_long_lock;
	/** Mutex used in synchronization overhead test. */
	struct m0_mutex          *fc_lock;
	/** Long lock link used non-blocking synchronization test. */
	struct m0_long_lock_link  fc_link;
	/** Pointer to an array with size ~2*cache size, shared by tests. */
	char                     *fc_mem;
	/** Size of ub_fom_ctx::fc_mem. */
	size_t                    fc_mem_sz;
};

static size_t ub_fom_home_locality(const struct m0_fom *fom);
static void ub_fom_fini(struct m0_fom *fom);
static int ub_fom_tick(struct m0_fom *fom);
static void ub_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);

/* Generic operations object for FOP associated with ub_fom_ctx. */
static const struct m0_fom_ops ub_fom_ops = {
	.fo_fini          = ub_fom_fini,
	.fo_tick          = ub_fom_tick,
	.fo_home_locality = ub_fom_home_locality,
	.fo_addb_init     = ub_fom_addb_init
};

/* FOM type specific functions for FOP associated with ub_fom_ctx. */
const struct m0_fom_type_ops ub_fom_type_ops = {
	.fto_create = NULL
};

/* Unit benchmark FOM type. */
struct m0_fom_type ub_fom_type;

static size_t ub_fom_home_locality(const struct m0_fom *fom)
{
	static size_t locality = 0;

	M0_PRE(fom != NULL);
	return locality++;
}

static void ub_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static int ub_fom_create(struct m0_fom **m, struct m0_reqh *reqh)
{
	extern struct m0_reqh_service_type ub_fom_service_type;
	struct m0_fom                      *fom;
	struct ub_fom_ctx                  *ctx;

	M0_PRE(m != NULL);

	M0_ALLOC_PTR(ctx);
	M0_UB_ASSERT(ctx != NULL);

	fom = &ctx->fc_gen;
	m0_fom_init(fom, &ub_fom_type, &ub_fom_ops, NULL, NULL, reqh,
		    &ub_fom_service_type);

	m0_long_lock_link_init(&ctx->fc_link, fom);

	ctx->fc_long_lock = &g_fom_long_lock;
	ctx->fc_lock = &g_fom_mutex;
	ctx->fc_mem = g_test_mem;
	ctx->fc_mem_sz = ARRAY_SIZE(g_test_mem);

	*m = fom;

	return 0;
}

static void ub_fom_fini(struct m0_fom *fom)
{
	struct ub_fom_ctx *ctx = container_of(fom, struct ub_fom_ctx, fc_gen);

	m0_long_lock_link_fini(&ctx->fc_link);
	m0_fom_fini(fom);
	m0_free(ctx);
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
