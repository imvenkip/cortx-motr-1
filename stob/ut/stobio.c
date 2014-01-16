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

#include "lib/misc.h"    /* M0_SET0 */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "ut/ut.h"
#include "lib/mutex.h"
#include "lib/arith.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/linux_internal.h"
#include "stob/ad.h"
#include "balloc/balloc.h"
#include "mero/init.h"
#include "fol/fol.h"

#define WITH_LOCK(lock, action, args...) ({		\
			struct m0_mutex *lk = lock;	\
			m0_mutex_lock(lk);		\
			action(args);			\
			m0_mutex_unlock(lk);		\
		})

enum {
	RW_BUFF_NR    = 10,
	MIN_BUFF_SIZE = 4096,
	MIN_BUFF_SIZE_IN_BLOCKS = 4,
	TEST_NR = 10
};

struct stobio_test {
	/* ctrl part */
	struct m0_stob	        *st_obj;
	const struct m0_stob_id  st_id;
	/* Real block device, if any */
	char			*st_dev_path;

	struct m0_stob_domain   *st_dom;
	struct m0_stob_io	 st_io;

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
	m0_bcount_t st_rdvec[RW_BUFF_NR];
	m0_bcount_t st_wrvec[RW_BUFF_NR];
};

/* Test block device */
static const char test_blkdev[] = "/dev/loop0";

/* sync object for init/fini */
static struct m0_mutex lock;
static struct m0_thread thread[TEST_NR];

#define ST_ID(hi, lo) .st_id = { .si_bits = { .u_hi = (hi), .u_lo = (lo) } }
struct stobio_test tests[TEST_NR] = {
	/* buffered IO tests */
	[0] = { ST_ID(1, 2), .st_directio = false },
	[1] = { ST_ID(3, 4), .st_directio = false },
	[2] = { ST_ID(5, 6), .st_directio = false },
	[3] = { ST_ID(7, 8), .st_directio = false },
	[4] = { ST_ID(9, 0), .st_directio = false },

	/* direct IO tests */
	[5] = { ST_ID(1, 2), .st_directio = true },
	[6] = { ST_ID(3, 4), .st_directio = true },
	[7] = { ST_ID(5, 6), .st_directio = true },
	[8] = { ST_ID(7, 8), .st_directio = true },
	[9] = { ST_ID(10, 0), .st_directio = true, .st_dev_path="/dev/loop0" },
};

#undef ST_ID

/*
 * Assumes that we are dealing with loop-back device /dev/loop0
 * We don't deal with real device in UT.
 */
static void stob_dev_init(const struct stobio_test *test)
{
	struct stat statbuf;
	int	    result;
	m0_bcount_t dev_sz;
	char	    sysbuf[PATH_MAX];
	char	    backingfile[PATH_MAX];

	result = stat(test->st_dev_path, &statbuf);
	M0_UT_ASSERT(result == 0);

	if (strcmp(test->st_dev_path, test_blkdev))
		return;

	/* Device size in KB */
	dev_sz = MIN_BUFF_SIZE/1024 * MIN_BUFF_SIZE_IN_BLOCKS * RW_BUFF_NR * \
		 TEST_NR * TEST_NR;

	/* Device size in MB */
	dev_sz = dev_sz/1024 + 1;

	sprintf(backingfile, "%s/%lu", test->st_dom->sd_name,
				       test->st_id.si_bits.u_hi);
	sprintf(sysbuf, "dd if=/dev/zero of=%s bs=1M count=%lu &>>/dev/null",
			backingfile, (unsigned long)dev_sz);
	result = system(sysbuf);
	M0_UT_ASSERT(result == 0);

	sprintf(sysbuf, "losetup %s %s", test->st_dev_path, backingfile);
	result = system(sysbuf);
	M0_UT_ASSERT(result == 0);
}

static void stob_dev_fini(const struct stobio_test *test)
{
	int	    result;
	char	    sysbuf[PATH_MAX];

	if(test->st_dev_path == NULL)
		return;

	if(strcmp(test->st_dev_path, test_blkdev))
		return;

	result = system("sleep 1");
	M0_UT_ASSERT(result == 0);
	sprintf(sysbuf, "losetup -d %s", test->st_dev_path);
	result = system(sysbuf);
	M0_UT_ASSERT(result == 0);
}

static void stobio_io_prepare(struct stobio_test *test,
			      struct m0_stob_io *io)
{
	io->si_flags  = 0;
	io->si_user.ov_vec.v_nr = RW_BUFF_NR;
	io->si_user.ov_vec.v_count = test->st_wrvec;

	io->si_stob.iv_vec.v_nr = RW_BUFF_NR;
	io->si_stob.iv_vec.v_count = test->st_wrvec;
	io->si_stob.iv_index = test->st_rdvec;
}

static void stobio_write_prepare(struct stobio_test *test,
				 struct m0_stob_io *io)
{
	io->si_opcode = SIO_WRITE;
	io->si_fol_rec_part = (void *)1;
	io->si_user.ov_buf = (void **) test->st_wrbuf_packed;
	stobio_io_prepare(test, io);
}

static void stobio_read_prepare(struct stobio_test *test,
				struct m0_stob_io *io)
{
	io->si_opcode = SIO_READ;
	io->si_user.ov_buf = (void **) test->st_rdbuf_packed;
	stobio_io_prepare(test, io);
}

static void stobio_write(struct stobio_test *test)
{
	int result;
	struct m0_stob_io  io;
	struct m0_clink    clink;

	m0_stob_io_init(&io);

	stobio_write_prepare(test, &io);

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);

	result = m0_stob_io_launch(&io, test->st_obj, NULL, NULL);
	M0_UT_ASSERT(result == 0);

	m0_chan_wait(&clink);

	M0_UT_ASSERT(io.si_rc == 0);
	M0_UT_ASSERT(io.si_count == test->st_rw_buf_size_in_blocks * RW_BUFF_NR);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);

	m0_stob_io_fini(&io);
}

static void stobio_read(struct stobio_test *test)
{
	int result;
	struct m0_stob_io io;
	struct m0_clink   clink;

	m0_stob_io_init(&io);

	stobio_read_prepare(test, &io);

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);

	result = m0_stob_io_launch(&io, test->st_obj, NULL, NULL);
	M0_UT_ASSERT(result == 0);

	m0_chan_wait(&clink);

	M0_UT_ASSERT(io.si_rc == 0);
	M0_UT_ASSERT(io.si_count == test->st_rw_buf_size_in_blocks * RW_BUFF_NR);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);

	m0_stob_io_fini(&io);
}

static void stobio_rw_buffs_init(struct stobio_test *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test->st_rdbuf); ++i) {
		test->st_rdbuf[i] = m0_alloc_aligned(test->st_rw_buf_size,
					test->st_block_shift);
		test->st_rdbuf_packed[i] = m0_stob_addr_pack(test->st_rdbuf[i]
					, test->st_block_shift);
		M0_UT_ASSERT(test->st_rdbuf[i] != NULL);
	}

	for (i = 0; i < ARRAY_SIZE(test->st_wrbuf); ++i) {
		test->st_wrbuf[i] = m0_alloc_aligned(test->st_rw_buf_size,
					test->st_block_shift);
		test->st_wrbuf_packed[i] = m0_stob_addr_pack(test->st_wrbuf[i],
					test->st_block_shift);
		M0_UT_ASSERT(test->st_wrbuf[i] != NULL);
	}
}

static void stobio_rw_buffs_fini(struct stobio_test *test)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test->st_rdbuf); ++i) {
		m0_free(test->st_rdbuf[i]);
		test->st_rdbuf_packed[i] = 0;
	}

	for (i = 0; i < ARRAY_SIZE(test->st_wrbuf); ++i) {
		m0_free(test->st_wrbuf[i]);
		test->st_wrbuf_packed[i] = 0;
	}
}

static int stobio_storage_init(void)
{
	int result;

	result = system("rm -fr ./__s");
	M0_UT_ASSERT(result == 0);

	result = mkdir("./__s", 0700);
	M0_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	M0_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	return result;
}

static void stobio_storage_fini(void)
{
	int result;

	result = system("rm -fr ./__s");
	M0_UT_ASSERT(result == 0);
}

static int stobio_init(struct stobio_test *test)
{
	int		   result;
	struct linux_stob *lstob;

	result = m0_linux_stob_domain_locate("./__s", &test->st_dom);
	M0_UT_ASSERT(result == 0);

	result = m0_linux_stob_setup(test->st_dom, test->st_directio);
	M0_UT_ASSERT(result == 0);

	result = m0_stob_find(test->st_dom, &test->st_id, &test->st_obj);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(test->st_obj->so_state == CSS_UNKNOWN);

	if(test->st_dev_path != NULL) {
		stob_dev_init(test);
		result = m0_linux_stob_link(test->st_dom, test->st_obj,
					    test->st_dev_path, NULL);
		M0_UT_ASSERT(result == 0);

	}

	result = m0_stob_create(test->st_obj, NULL);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(test->st_obj->so_state == CSS_EXISTS);
	lstob = stob2linux(test->st_obj);
	M0_UT_ASSERT(S_ISREG(lstob->sl_mode) || S_ISBLK(lstob->sl_mode));

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
	m0_stob_put(test->st_obj);
	test->st_dom->sd_ops->sdo_fini(test->st_dom);
	stobio_rw_buffs_fini(test);
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
		for (j = 0; j < test->st_rw_buf_size; ++j)
			M0_UT_ASSERT(test->st_rdbuf[i][j] == (('A' + 2 * i) | 1));
	}

	for (; i < RW_BUFF_NR; ++i) {
		for (j = 0; j < test->st_rw_buf_size; ++j)
			M0_UT_ASSERT(test->st_rdbuf[i][j] == (('a' + i) | 1));
	}

	WITH_LOCK(&lock, stobio_fini, test);
}

void test_stobio(void)
{
	int i;
	int result;

	m0_mutex_init(&lock);

	result = stobio_storage_init();
	M0_UT_ASSERT(result == 0);

	for (i = 0; i < TEST_NR; ++i) {
		result = M0_THREAD_INIT
			(&thread[i], int, NULL,
			 LAMBDA(void, (int x)
				{ overlapped_rw_test(&tests[x], x*100); } ), i,
			 "overlap_test%d", i);
		M0_UT_ASSERT(result == 0);
	}

	for (i = 0; i < TEST_NR; ++i) {
		m0_thread_join(&thread[i]);
		m0_thread_fini(&thread[i]);
	}

	stobio_storage_fini();

	m0_mutex_fini(&lock);
}

void test_short_read(void)
{
	int                 i;
	int                 j;
	int                 result;
	struct stobio_test *test = &tests[0];

	m0_mutex_init(&lock);

	result = stobio_storage_init();
	M0_UT_ASSERT(result == 0);

	WITH_LOCK(&lock, stobio_init, test);

	stobio_rwsegs_prepare(test, 0);
	stobio_write(test);

	stobio_rwsegs_prepare(test, 0);
	for (i = 0; i < RW_BUFF_NR; ++i)
		test->st_rdvec[i]++;
	stobio_read(test);

	for (i = 0; i < RW_BUFF_NR; ++i) {
		char expected = ('a' + i) | 1;
		for (j = 0; j < test->st_rw_buf_size - test->st_block_size; ++j)
			M0_UT_ASSERT(test->st_rdbuf[i][j] == expected);
		for (; j < test->st_rw_buf_size; ++j)
			M0_UT_ASSERT(test->st_rdbuf[i][j] == 0);
	}

	WITH_LOCK(&lock, stobio_fini, test);
	stobio_storage_fini();
	m0_mutex_fini(&lock);
}

/*
 * In stob_io, indexvec can have different number of elements
 * than bufvec. Stob implementation will do all the splitting and merging
 * internally, as necessary.
 * This test tries to write 1M at offset 1M with a single indexvec. Data
 * is available in 256 4K buffers.
 */
void test_single_ivec()
{
	int                i;
	int                result;
	struct m0_stob_io  io;
	struct m0_clink    clink;
	enum {
		SHIFT          = 0,
		VEC_NR         = 256,
		BLOCK_SIZE     = 4096,
		OFFSET_OR_SIZE = 1048576,
	};
	/* read/write buffers */
	char              *st_rdbuf[VEC_NR];
	char              *st_rdbuf_packed[VEC_NR];
	char              *st_wrbuf[VEC_NR];
	char              *st_wrbuf_packed[VEC_NR];
	/* read/write vectors */
	m0_bcount_t        index[1]     = {OFFSET_OR_SIZE >> SHIFT};
	m0_bcount_t        stob_size[1] = {OFFSET_OR_SIZE >> SHIFT};
	m0_bcount_t        size[VEC_NR];

	m0_mutex_init(&lock);
	result = stobio_storage_init();
	M0_UT_ASSERT(result == 0);
	WITH_LOCK(&lock, stobio_init, tests);

	for (i = 0; i < VEC_NR; ++i) {
		st_rdbuf[i] = m0_alloc_aligned(BLOCK_SIZE, SHIFT);
		st_rdbuf_packed[i] = m0_stob_addr_pack(st_rdbuf[i], SHIFT);
		M0_UT_ASSERT(st_rdbuf[i] != NULL);
		size[i] = BLOCK_SIZE >> SHIFT;

		st_wrbuf[i] = m0_alloc_aligned(BLOCK_SIZE, SHIFT);
		st_wrbuf_packed[i] = m0_stob_addr_pack(st_wrbuf[i], SHIFT);
		M0_UT_ASSERT(st_wrbuf[i] != NULL);
		memset(st_wrbuf[i], ('a' + i) | 1, SHIFT);
	}

	/* Write */
	m0_stob_io_init(&io);
	io.si_opcode              = SIO_WRITE;
	io.si_fol_rec_part        = (void *)1;
	io.si_flags               = 0;
	io.si_user.ov_vec.v_nr    = VEC_NR;
	io.si_user.ov_vec.v_count = size;
	io.si_user.ov_buf         = (void **) st_wrbuf_packed;
	io.si_stob.iv_vec.v_nr    = 1;
	io.si_stob.iv_vec.v_count = stob_size;
	io.si_stob.iv_index       = index;

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);
	result = m0_stob_io_launch(&io, tests->st_obj, NULL, NULL);
	M0_UT_ASSERT(result == 0);

	m0_chan_wait(&clink);
	M0_ASSERT(io.si_rc == 0);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_stob_io_fini(&io);

	/* Read */
	m0_stob_io_init(&io);
	io.si_opcode              = SIO_READ;
	io.si_fol_rec_part        = (void *)1;
	io.si_flags               = 0;
	io.si_user.ov_vec.v_nr    = VEC_NR;
	io.si_user.ov_vec.v_count = size;
	io.si_user.ov_buf         = (void **) st_rdbuf_packed;
	io.si_stob.iv_vec.v_nr    = 1;
	io.si_stob.iv_vec.v_count = stob_size;
	io.si_stob.iv_index       = index;

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);
	result = m0_stob_io_launch(&io, tests->st_obj, NULL, NULL);
	M0_UT_ASSERT(result == 0);

	m0_chan_wait(&clink);
	M0_ASSERT(io.si_rc == 0);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_stob_io_fini(&io);

	for (i = 0; i < VEC_NR; ++i)
		M0_ASSERT(memcmp(st_wrbuf[i], st_rdbuf[i], BLOCK_SIZE) == 0);

	WITH_LOCK(&lock, stobio_fini, tests);
	stobio_storage_fini();
	m0_mutex_fini(&lock);

	for (i = 0; i < VEC_NR; ++i) {
		m0_free(st_wrbuf[i]);
		m0_free(st_rdbuf[i]);
	}
}

const struct m0_test_suite stobio_ut = {
	.ts_name = "stobio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "stobio",             test_stobio },
		{ "short-read",         test_short_read },
		{ "stobio-single-ivec", test_single_ivec },
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
