#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>    /* system */
#include <stdio.h>     /* fopen, fgetc, ... */
#include <unistd.h>    /* unlink */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/errno.h"
#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/assert.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/ad.h"

/**
   @addtogroup stob
   @{
 */

enum {
	NR    = 2,
	COUNT = 4096*1024
};

static struct c2_stob_domain *dom_back;
static struct c2_stob_domain *dom_fore;

static const struct c2_stob_id id_back = {
	.si_bits = {
		.u_hi = 1,
		.u_lo = 2
	}
};

static const struct c2_stob_id id_fore = {
	.si_bits = {
		.u_hi = 11,
		.u_lo = 22
	}
};

static const char db_name[] = "ut-ad";

static struct c2_stob *obj_back;
static struct c2_stob *obj_fore;
static const char path[] = "./__s/o/0000000000000001.0000000000000002";
static struct c2_stob_io io;
static c2_bcount_t user_vec[NR];
static char user_buf[NR][COUNT];
static char read_buf[NR][COUNT];
static char *user_bufs[NR];
static char *read_bufs[NR];
static c2_bindex_t stob_vec[NR];
static struct c2_clink clink;
static struct c2_dtx tx;
static struct c2_dbenv db;

static int test_ad_init(void)
{
	int i;
	int result;

	result = system("rm -fr ./__s");
	C2_ASSERT(result == 0);

	result = mkdir("./__s", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  "./__s", &dom_back);
	C2_ASSERT(result == 0);

	result = dom_back->sd_ops->sdo_stob_find(dom_back, &id_back, &obj_back);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_back->so_state == CSS_UNKNOWN);

	result = c2_stob_create(obj_back, NULL);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_back->so_state == CSS_EXISTS);

	result = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type, "",
						       &dom_fore);
	C2_ASSERT(result == 0);

	result = ad_setup(dom_fore, &db, obj_back, NULL);
	C2_ASSERT(result == 0);

	c2_stob_put(obj_back);

	result = dom_fore->sd_ops->sdo_stob_find(dom_fore, &id_fore, &obj_fore);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_fore->so_state == CSS_UNKNOWN);

	result = dom_fore->sd_ops->sdo_tx_make(dom_fore, &tx);
	C2_ASSERT(result == 0);

	result = c2_stob_create(obj_fore, &tx);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_fore->so_state == CSS_EXISTS);

	for (i = 0; i < NR; ++i) {
		user_bufs[i] = user_buf[i];
		read_bufs[i] = read_buf[i];
		user_vec[i] = COUNT;
		stob_vec[i] = COUNT * (2 * i + 1);
		memset(user_buf[i], ('a' + i)|1, sizeof user_buf[i]);
	}
	return result;
}

static int test_ad_fini(void)
{
	int result;

	result = c2_db_tx_commit(&tx.tx_dbtx);
	C2_ASSERT(result == 0);

	c2_stob_put(obj_fore);
	dom_fore->sd_ops->sdo_fini(dom_fore);
	dom_back->sd_ops->sdo_fini(dom_back);
	return 0;
}

static void test_write(int i)
{
	int result;
	c2_stob_io_init(&io);

	io.si_opcode = SIO_WRITE;
	io.si_flags  = 0;
	io.si_user.div_vec.ov_vec.v_nr = i;
	io.si_user.div_vec.ov_vec.v_count = user_vec;
	io.si_user.div_vec.ov_buf = (void **)user_bufs;

	io.si_stob.ov_vec.v_nr = i;
	io.si_stob.ov_vec.v_count = user_vec;
	io.si_stob.ov_index = stob_vec;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj_fore, &tx, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_ASSERT(io.si_rc == 0);
	C2_ASSERT(io.si_count == COUNT * i);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

static void test_read(int i)
{
	int result;
	c2_stob_io_init(&io);

	io.si_opcode = SIO_READ;
	io.si_flags  = 0;
	io.si_user.div_vec.ov_vec.v_nr = i;
	io.si_user.div_vec.ov_vec.v_count = user_vec;
	io.si_user.div_vec.ov_buf = (void **)read_bufs;

	io.si_stob.ov_vec.v_nr = i;
	io.si_stob.ov_vec.v_count = user_vec;
	io.si_stob.ov_index = stob_vec;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj_fore, &tx, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_ASSERT(io.si_rc == 0);
	C2_ASSERT(io.si_count == COUNT * i);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

/**
   AD unit-test. 
 */
static void test_ad(void)
{
	int i;

	for (i = 1; i < NR; ++i)
		test_write(i);
	for (i = 1; i < NR; ++i) {
		test_read(i);
		C2_ASSERT(memcmp(user_buf, read_buf, COUNT * i) == 0);
	}
}

const struct c2_test_suite ad_ut = {
	.ts_name = "ad-ut",
	.ts_init = test_ad_init,
	.ts_fini = test_ad_fini,
	.ts_tests = {
		{ "ad", test_ad },
		{ NULL, NULL }
	}
};

enum {
	UB_ITER = 10
};

static void ub_write(int i)
{
	test_write(NR - 1);
}

static void ub_read(int i)
{
	test_read(NR - 1);
}

struct c2_ub_set c2_ad_ub = {
	.us_name = "ad-ub",
	.us_init = (void *)test_ad_init,
	.us_fini = (void *)test_ad_fini,
	.us_run  = { 
		{ .ut_name = "write-prime",
		  .ut_iter = 1,
		  .ut_round = ub_write },

		{ .ut_name = "write",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_write },

		{ .ut_name = "read",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_read },

		{ .ut_name = NULL }
	}
};

/** @} end group stob */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
