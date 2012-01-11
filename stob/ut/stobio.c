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

 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact

 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/21/2010
 */

#include <stdlib.h>    /* system */
#include <stdio.h>     /* fopen, fgetc, ... */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <linux/limits.h>

#include "lib/misc.h"    /* C2_SET0 */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/ut.h"
#include "lib/mutex.h"
#include "lib/arith.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/linux_internal.h"
#include "stob/ad.h"
#include "balloc/balloc.h"
#include "colibri/init.h"

#define WITH_LOCK(lock, action, args...) ({		\
			struct c2_mutex *lk = lock;	\
			c2_mutex_lock(lk);		\
			action(args);			\
			c2_mutex_unlock(lk);		\
		})

enum {
	RW_BUFF_NR    = 10,
	MIN_BUFF_SIZE = 4096,
	MIN_BUFF_SIZE_IN_BLOCKS = 4,
	TEST_NR = 10
};

struct stobio_test {
	/* ctrl part */
	struct c2_stob	        *st_obj;
	const struct c2_stob_id  st_id;
	/* Real block device, if any */
	char			*st_dev_path;

	struct c2_stob_domain   *st_dom;
	struct c2_stob_io	 st_io;

	/* this flag controls whether to use direct IO */
	bool st_directio;

	size_t   st_rw_buf_size;
	size_t   st_rw_buf_size_in_blocks;
	uint32_t st_block_shift;
	size_t   st_block_size;

	/* read/write buffers */
	char *st_rdbuf[RW_BUFF_NR];
	char *st_rdbuf_packed[RW_BUFF_NR];
	char *st_wrbuf[RW_BUFF_NR];
	char *st_wrbuf_packed[RW_BUFF_NR];

	/* read/write vectors */
	c2_bcount_t st_rdvec[RW_BUFF_NR];
	c2_bcount_t st_wrvec[RW_BUFF_NR];
};

/* sync object for init/fini */
static struct c2_mutex lock;
static struct c2_thread thread[TEST_NR];
struct stobio_test test[TEST_NR] = {
	/* buffered IO tests */
	[0] = { .st_id = { .si_bits = { .u_hi = 1, .u_lo = 2 } },
		.st_directio = false },
	[1] = { .st_id = { .si_bits = { .u_hi = 3, .u_lo = 4 } },
		.st_directio = false },
	[2] = { .st_id = { .si_bits = { .u_hi = 5, .u_lo = 6 } },
		.st_directio = false },
	[3] = { .st_id = { .si_bits = { .u_hi = 7, .u_lo = 8 } },
		.st_directio = false },
	[4] = { .st_id = { .si_bits = { .u_hi = 9, .u_lo = 0 } },
		.st_directio = false },

	/* direct IO tests */
	[5] = { .st_id = { .si_bits = { .u_hi = 1, .u_lo = 2 } },
		.st_directio = true },
	[6] = { .st_id = { .si_bits = { .u_hi = 3, .u_lo = 4 } },
		.st_directio = true },
	[7] = { .st_id = { .si_bits = { .u_hi = 5, .u_lo = 6 } },
		.st_directio = true },
	[8] = { .st_id = { .si_bits = { .u_hi = 7, .u_lo = 8 } },
		.st_directio = true },
	[9] = { .st_id = { .si_bits = { .u_hi = 10, .u_lo = 0 } },
		.st_directio = true, .st_dev_path="/dev/loop0"  },
};

/*
 * Assumes that we are dealing with loop-back devices and not real devices.
 */
static void stob_dev_init(const struct stobio_test *test)
{
	struct stat statbuf;
	int	    result;
	int	    dev_sz;
	char	    sysbuf[PATH_MAX];

	result = stat(test->st_dev_path, &statbuf);
	C2_ASSERT(result == 0);

	if(strncmp(test->st_dev_path, "/dev/loop", strlen("/dev/loop")))
		return;

	/* Device size in KB */
	dev_sz = (MIN_BUFF_SIZE/1024) * MIN_BUFF_SIZE_IN_BLOCKS * RW_BUFF_NR * \
		 TEST_NR * TEST_NR;

	/* Device size in MB */
	dev_sz = (dev_sz > 1024) ? ((dev_sz/1024) + 1) : 1;

	sprintf(sysbuf, "dd if=/dev/zero of=./__s/%lu bs=1M count=%d",
			test->st_id.si_bits.u_hi, dev_sz);
	result = system(sysbuf);
	C2_ASSERT(result == 0);

	sprintf(sysbuf, "losetup %s ./__s/%lu", test->st_dev_path,
						test->st_id.si_bits.u_hi);
	result = system(sysbuf);
	C2_ASSERT(result == 0);
}

static void stob_dev_fini(const struct stobio_test *test)
{
	int	    result;
	char	    sysbuf[PATH_MAX];

	if(strncmp(test->st_dev_path, "/dev/loop", strlen("/dev/loop")))
		return;

	sprintf(sysbuf, "losetup -d %s", test->st_dev_path);
	result = system(sysbuf);
	C2_ASSERT(result == 0);

	sprintf(sysbuf, "cp ./__s/%lu /root/testfile", test->st_id.si_bits.u_hi);
	result = system(sysbuf);
	C2_ASSERT(result == 0);
}

static void stobio_io_prepare(struct stobio_test *test,
			      struct c2_stob_io *io)
{
	io->si_flags  = 0;
	io->si_user.ov_vec.v_nr = RW_BUFF_NR;
	io->si_user.ov_vec.v_count = test->st_wrvec;

	io->si_stob.iv_vec.v_nr = RW_BUFF_NR;
	io->si_stob.iv_vec.v_count = test->st_wrvec;
	io->si_stob.iv_index = test->st_rdvec;
}

static void stobio_write_prepare(struct stobio_test *test,
				 struct c2_stob_io *io)
{
	io->si_opcode = SIO_WRITE;
	io->si_user.ov_buf = (void **) test->st_wrbuf_packed;
	stobio_io_prepare(test, io);
}

static void stobio_read_prepare(struct stobio_test *test,
				struct c2_stob_io *io)
{
	io->si_opcode = SIO_READ;
	io->si_user.ov_buf = (void **) test->st_rdbuf_packed;
	stobio_io_prepare(test, io);
}

static void stobio_write(struct stobio_test *test)
{
	int result;
	struct c2_stob_io  io;
	struct c2_clink    clink;

	c2_stob_io_init(&io);
	
	stobio_write_prepare(test, &io);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, test->st_obj, NULL, NULL);
	C2_UT_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_UT_ASSERT(io.si_rc == 0);
	C2_UT_ASSERT(io.si_count == test->st_rw_buf_size_in_blocks * RW_BUFF_NR);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

static void stobio_read(struct stobio_test *test)
{
	int result;
	struct c2_stob_io io;
	struct c2_clink   clink;
	c2_stob_io_init(&io);

	stobio_read_prepare(test, &io);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, test->st_obj, NULL, NULL);
	C2_UT_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_UT_ASSERT(io.si_rc == 0);
	C2_UT_ASSERT(io.si_count == test->st_rw_buf_size_in_blocks * RW_BUFF_NR);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

static void stobio_rw_buffs_init(struct stobio_test *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test->st_rdbuf); ++i) {
		test->st_rdbuf[i] = c2_alloc_aligned(test->st_rw_buf_size,
					test->st_block_shift);
		test->st_rdbuf_packed[i] = c2_stob_addr_pack(test->st_rdbuf[i]
					, test->st_block_shift);
		C2_UT_ASSERT(test->st_rdbuf[i] != NULL);
	}

	for (i = 0; i < ARRAY_SIZE(test->st_wrbuf); ++i) {
		test->st_wrbuf[i] = c2_alloc_aligned(test->st_rw_buf_size,
					test->st_block_shift);
		test->st_wrbuf_packed[i] = c2_stob_addr_pack(test->st_wrbuf[i],
					test->st_block_shift);
		C2_UT_ASSERT(test->st_wrbuf[i] != NULL);
	}
}

static void stobio_rw_buffs_fini(struct stobio_test *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test->st_rdbuf); ++i) {
		c2_free(test->st_rdbuf[i]);
		test->st_rdbuf_packed[i] = 0;
	}

	for (i = 0; i < ARRAY_SIZE(test->st_wrbuf); ++i) {
		c2_free(test->st_wrbuf[i]);
		test->st_wrbuf_packed[i] = 0;
	}
}

static int stobio_storage_init(void)
{
	int result;

	result = system("rm -fr ./__s");
	C2_UT_ASSERT(result == 0);

	result = mkdir("./__s", 0700);
	C2_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	C2_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));	

	return result;
}

static void stobio_storage_fini(void)
{
	int result;
	result = system("rm -fr ./__s");
	C2_UT_ASSERT(result == 0);
}

static int stobio_init(struct stobio_test *test)
{
	int result;
	struct linux_stob *lstob;

	result = linux_stob_type.st_op->
		sto_domain_locate(&linux_stob_type, "./__s", &test->st_dom);
	C2_UT_ASSERT(result == 0);

	result = c2_linux_stob_setup(test->st_dom, test->st_directio);
	C2_UT_ASSERT(result == 0);

	result = test->st_dom->sd_ops->
		sdo_stob_find(test->st_dom, &test->st_id, &test->st_obj);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(test->st_obj->so_state == CSS_UNKNOWN);

	if(test->st_dev_path != NULL) {
		stob_dev_init(test);
		result = c2_linux_stob_link(test->st_dom, test->st_obj,
					    test->st_dev_path, NULL);
		C2_ASSERT(result == 0);

	}

	result = c2_stob_create(test->st_obj, NULL);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(test->st_obj->so_state == CSS_EXISTS);
	lstob = stob2linux(test->st_obj);
	C2_ASSERT(S_ISREG(lstob->sl_mode) || S_ISBLK(lstob->sl_mode));

	test->st_block_shift = test->st_obj->so_op->sop_block_shift(test->st_obj);
	test->st_block_size = 1 << test->st_block_shift;
	/* buf_size is chosen so it would be at least MIN_BUFF_SIZE in bytes
	 * or it would consist of at least MIN_BUFF_SIZE_IN_BLOCKS blocks */
	test->st_rw_buf_size = max_check(MIN_BUFF_SIZE,
			(1 << test->st_block_shift) * MIN_BUFF_SIZE_IN_BLOCKS);
	test->st_rw_buf_size_in_blocks = test->st_rw_buf_size / test->st_block_size;

	stobio_rw_buffs_init(test);

	return 0;
}

static void stobio_fini(struct stobio_test *test)
{
	c2_stob_put(test->st_obj);
	test->st_dom->sd_ops->sdo_fini(test->st_dom);
	stobio_rw_buffs_fini(test);

	if(test->st_dev_path != NULL)
		stob_dev_fini(test);

}

static void stobio_rwsegs_prepare(struct stobio_test *test, int starts_from)
{
	int i;
	for (i = 0; i < RW_BUFF_NR; ++i) {
		test->st_wrvec[i] = test->st_rw_buf_size_in_blocks;
		test->st_rdvec[i] = test->st_rw_buf_size_in_blocks
					* (2 * i + 1 + starts_from);
		memset(test->st_wrbuf[i], ('a' + i) | 1, test->st_rw_buf_size);
	}
}

static void stobio_rwsegs_overlapped_prepare(struct stobio_test *test, int starts_from)
{
	int i;
	for (i = 0; i < RW_BUFF_NR; ++i) {
		test->st_wrvec[i] = test->st_rw_buf_size_in_blocks;
		test->st_rdvec[i] = test->st_rw_buf_size_in_blocks
					* (i + 1 + starts_from);
		memset(test->st_wrbuf[i], ('A' + i) | 1, test->st_rw_buf_size);
	}
}

void overlapped_rw_test(struct stobio_test *test, int starts_from)
{
	int i;
	int j;

	WITH_LOCK(&lock, stobio_init, test);

	/* Write overlapped segments */
	stobio_rwsegs_prepare(test, starts_from);
	stobio_write(test);

	stobio_rwsegs_overlapped_prepare(test, starts_from);
	stobio_write(test);

	/* read WR data */
	stobio_rwsegs_prepare(test, starts_from);
	stobio_read(test);

	/* check WR data */
	for (i = 0; i < RW_BUFF_NR/2; ++i) {
		for (j = 0; j < RW_BUFF_NR; ++j)
			C2_UT_ASSERT(test->st_rdbuf[i][j] == (('A' + 2 * i) | 1));
	}

	for (; i < RW_BUFF_NR; ++i) {
		for (j = 0; j < RW_BUFF_NR; ++j)
			C2_UT_ASSERT(test->st_rdbuf[i][j] == (('a' + i) | 1));
	}

	WITH_LOCK(&lock, stobio_fini, test);
}

void test_stobio(void)
{
	int i;
	int result;

	c2_mutex_init(&lock);

	result = stobio_storage_init();
	C2_UT_ASSERT(result == 0);

	for (i = 0; i < TEST_NR; ++i) {
		result = C2_THREAD_INIT
			(&thread[i], int, NULL,
			 LAMBDA(void, (int x) 
				{ overlapped_rw_test(&test[x], x*100); } ), i,
			 "overlap_test%d", i);
		C2_UT_ASSERT(result == 0);
	}

	for (i = 0; i < TEST_NR; ++i) {
		c2_thread_join(&thread[i]);
		c2_thread_fini(&thread[i]);
	}

	stobio_storage_fini();

	c2_mutex_fini(&lock);
}

const struct c2_test_suite stobio_ut = {
	.ts_name = "stobio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "stobio", test_stobio },
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
