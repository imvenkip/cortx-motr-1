/* -*- C -*- */
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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 08/29/2012
 */

#include <linux/bio.h>
#include <linux/loop.h>

#include "lib/ut.h"

int c2loop_accum_bios(struct loop_device *lo, struct bio_list *bios,
		      struct iovec *iovecs, loff_t *ppos, unsigned *psize);

static void loop_dev_init(struct loop_device *lo)
{
	spin_lock_init(&lo->lo_lock);
	bio_list_init(&lo->lo_bio_list);
	lo->lo_offset = 0;
}

static struct iovec iovecs[BIO_MAX_PAGES];

/*
 * Basic functionality tests:
 *
 *   - One bio request (in the queue) with one segment. One element in
 *     iovecs array is returned with correct file pos and I/O size.
 */
static void accumulate_basic_test1(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	bio = bio_alloc(GFP_KERNEL, 1);
	C2_UT_ASSERT(bio != NULL);

	bio->bi_bdev = (void*)1;
	bio->bi_io_vec[0].bv_page = NULL;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_vcnt = 1;
	bio->bi_size = PAGE_SIZE;

	bio_list_add(&lo.lo_bio_list, bio);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 1);

	bio_put(bio);
}

/*
 *   - One bio request with two segments. Two elements in iovecs array
 *     are returned with correct file pos and summary I/O size.
 */
static void accumulate_basic_test2(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	bio = bio_alloc(GFP_KERNEL, 2);
	C2_UT_ASSERT(bio != NULL);

	bio->bi_bdev = (void*)1;
	bio->bi_io_vec[0].bv_page = NULL;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_io_vec[1].bv_page = NULL;
	bio->bi_io_vec[1].bv_len = PAGE_SIZE;
	bio->bi_io_vec[1].bv_offset = 0;
	bio->bi_vcnt = 2;
	bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

	bio_list_add(&lo.lo_bio_list, bio);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 2);

	bio_put(bio);
}

/*
 *   - Two bio requests (for the same read/write operation), one segment
 *     each, for contiguous file region. Two elements in iovecs array
 *     are returned with correct file pos and summary I/O size.
 */
static void accumulate_basic_test3(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;
	int i;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	for (i = 0; i < 2; i++) {
		bio = bio_alloc(GFP_KERNEL, 1);
		C2_UT_ASSERT(bio != NULL);

		bio->bi_bdev = (void*)1;
		bio->bi_sector = PAGE_SIZE / 512 * i;
		bio->bi_io_vec[0].bv_page = NULL;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

		bio_list_add(&lo.lo_bio_list, bio);
	}

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 2);
	C2_UT_ASSERT(size == 2*PAGE_SIZE);

	C2_UT_ASSERT(bio_list_size(&bios) == 2);
	while (!bio_list_empty(&bios))
		bio_put(bio_list_pop(&bios));
}

/*
 * Exception cases tests:
 *
 *   - Two bio requests (one segment each) but for non-contiguous file
 *     regions. Two calls are expected with one element in iovecs array
 *     returned each time.
 */
static void accumulate_except_test1(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;
	int i;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	for (i = 0; i < 2; i++) {
		bio = bio_alloc(GFP_KERNEL, 1);
		C2_UT_ASSERT(bio != NULL);

		bio->bi_bdev = (void*)1;
		bio->bi_sector = 0; /* XXX */
		bio->bi_io_vec[0].bv_page = NULL;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

		bio_list_add(&lo.lo_bio_list, bio);
	}

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 1);
	C2_UT_ASSERT(size == PAGE_SIZE);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 1);
	C2_UT_ASSERT(size == PAGE_SIZE);

	C2_UT_ASSERT(bio_list_size(&bios) == 2);
	while (!bio_list_empty(&bios))
		bio_put(bio_list_pop(&bios));
}

/*
 *   - Two bio requests for contiguous file region, but for different
 *     operations: one for read, another for write. Two calls are
 *     expected with one element in iovecs array returned each time.
 */
static void accumulate_except_test2(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;
	int i;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	for (i = 0; i < 2; i++) {
		bio = bio_alloc(GFP_KERNEL, 1);
		C2_UT_ASSERT(bio != NULL);

		bio->bi_bdev = (void*)1;
		bio->bi_sector = PAGE_SIZE / 512 * i;
		bio->bi_rw = i; /* XXX */
		bio->bi_io_vec[0].bv_page = NULL;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

		bio_list_add(&lo.lo_bio_list, bio);
	}

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 1);
	C2_UT_ASSERT(size == PAGE_SIZE);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 1);
	C2_UT_ASSERT(size == PAGE_SIZE);

	C2_UT_ASSERT(bio_list_size(&bios) == 2);
	while (!bio_list_empty(&bios))
		bio_put(bio_list_pop(&bios));
}

/*
 * Iovecs array boundary (BIO_MAX_PAGES) tests (contiguous file region):
 *
 *   - BIO_MAX_PAGES bio requests in the list (one segment each).
 *     BIO_MAX_PAGES elements in iovecs array are returned in one call.
 */
static void accumulate_bound_test1(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;
	int i;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	for (i = 0; i < BIO_MAX_PAGES; i++) {
		bio = bio_alloc(GFP_KERNEL, 1);
		C2_UT_ASSERT(bio != NULL);

		bio->bi_bdev = (void*)1;
		bio->bi_sector = PAGE_SIZE / 512 * i;
		bio->bi_io_vec[0].bv_page = NULL;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

		bio_list_add(&lo.lo_bio_list, bio);
	}

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == BIO_MAX_PAGES);
	C2_UT_ASSERT(size == BIO_MAX_PAGES * PAGE_SIZE);

	C2_UT_ASSERT(bio_list_size(&bios) == BIO_MAX_PAGES);
	while (!bio_list_empty(&bios))
		bio_put(bio_list_pop(&bios));
}

/*
 *   - (BIO_MAX_PAGES + 1) bio requests in the list. Two calls are
 *     expected: one with BIO_MAX_PAGES elements in iovecs array
 *     returned, another with one element returned.
 */
static void accumulate_bound_test2(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;
	int i;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	for (i = 0; i < BIO_MAX_PAGES+1; i++) {
		bio = bio_alloc(GFP_KERNEL, 1);
		C2_UT_ASSERT(bio != NULL);

		bio->bi_bdev = (void*)1;
		bio->bi_sector = PAGE_SIZE / 512 * i;
		bio->bi_io_vec[0].bv_page = NULL;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

		bio_list_add(&lo.lo_bio_list, bio);
	}

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == BIO_MAX_PAGES);
	C2_UT_ASSERT(size == BIO_MAX_PAGES * PAGE_SIZE);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == 1);
	C2_UT_ASSERT(size == PAGE_SIZE);

	C2_UT_ASSERT(bio_list_size(&bios) == BIO_MAX_PAGES+1);
	while (!bio_list_empty(&bios))
		bio_put(bio_list_pop(&bios));
}

/*
 *   - (BIO_MAX_PAGES - 1) bio requests one segment each and one bio
 *     request with two segments. Two calls are expected: one with
 *     (BIO_MAX_PAGES - 1) elements in iovecs array returned, another
 *     with two elements returned.
 */
static void accumulate_bound_test3(void)
{
	struct loop_device lo;
	struct bio *bio;
	struct bio_list bios;
	loff_t pos;
	unsigned size;
	unsigned n;
	int i;

	loop_dev_init(&lo);
	bio_list_init(&bios);

	for (i = 0; i < BIO_MAX_PAGES-1; i++) {
		bio = bio_alloc(GFP_KERNEL, 1);
		C2_UT_ASSERT(bio != NULL);

		bio->bi_bdev = (void*)1;
		bio->bi_sector = PAGE_SIZE / 512 * i;
		bio->bi_io_vec[0].bv_page = NULL;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

		bio_list_add(&lo.lo_bio_list, bio);
	}

	bio = bio_alloc(GFP_KERNEL, 2);
	C2_UT_ASSERT(bio != NULL);

	bio->bi_bdev = (void*)1;
	bio->bi_sector = PAGE_SIZE / 512 * i;
	bio->bi_io_vec[0].bv_page = NULL;
	bio->bi_io_vec[0].bv_len = PAGE_SIZE;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_io_vec[1].bv_page = NULL;
	bio->bi_io_vec[1].bv_len = PAGE_SIZE;
	bio->bi_io_vec[1].bv_offset = 0;
	bio->bi_vcnt = 2;
	bio->bi_size = bio->bi_vcnt * PAGE_SIZE;

	bio_list_add(&lo.lo_bio_list, bio);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(n == BIO_MAX_PAGES-1);
	C2_UT_ASSERT(size == (BIO_MAX_PAGES-1) * PAGE_SIZE);

	n = c2loop_accum_bios(&lo, &bios, iovecs, &pos, &size);
	C2_UT_ASSERT(pos == (BIO_MAX_PAGES-1) * PAGE_SIZE);
	C2_UT_ASSERT(n == 2);
	C2_UT_ASSERT(size == 2*PAGE_SIZE);

	C2_UT_ASSERT(bio_list_size(&bios) == BIO_MAX_PAGES);
	while (!bio_list_empty(&bios))
		bio_put(bio_list_pop(&bios));
}

const struct c2_test_suite c2loop_ut = {
	.ts_name = "c2loop-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "accumulate_basic_test1", accumulate_basic_test1},
		{ "accumulate_basic_test2", accumulate_basic_test2},
		{ "accumulate_basic_test3", accumulate_basic_test3},
		{ "accumulate_except_test1", accumulate_except_test1},
		{ "accumulate_except_test2", accumulate_except_test2},
		{ "accumulate_bound_test1", accumulate_bound_test1},
		{ "accumulate_bound_test2", accumulate_bound_test2},
		{ "accumulate_bound_test3", accumulate_bound_test3},
		{ NULL, NULL }
	}
};
C2_EXPORTED(c2loop_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
