/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 03-Feb-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT

#include "lib/trace.h"
#include "lib/misc.h"              /* ARRAY_SIZE */
#include "lib/semaphore.h"
#include "ut/ut.h"
#include "stob/stob.h"
#include "stob/domain.h"
#include "addb2/addb2.h"
#include "addb2/storage.h"
#include "addb2/internal.h"
#include "addb2/consumer.h"
#include "addb2/ut/common.h"

static struct m0_stob_domain   *dom;
static struct m0_stob          *stob;
static struct m0_addb2_sit     *sit;
static struct m0_addb2_storage *stor;
static struct m0_addb2_mach    *mach;

enum {
	DOMAIN_KEY = 76,
	STOB_KEY = 80,
	SIZE = 4 * 1024 * 1024 * 1024ull
};

static void stob_get(bool create)
{
	int result;

	result = m0_stob_domain_create_or_init("linuxstob:./__s",
					       "directio=true",
					       DOMAIN_KEY, NULL, &dom);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(dom != NULL);
	result = m0_stob_find_by_key(dom, STOB_KEY, &stob);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(stob != NULL);
	result = m0_stob_locate(stob);
	M0_UT_ASSERT(result == 0);
	if (create) {
		result = m0_stob_create(stob, NULL, NULL);
		M0_UT_ASSERT(result == 0);
	}
	M0_UT_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
}

static void stob_put(bool destroy)
{
	int result;

	if (destroy) {
		result = m0_stob_destroy(stob, NULL);
		M0_UT_ASSERT(result == 0);
		result = m0_stob_domain_destroy(dom);
		M0_ASSERT(result == 0);
	} else {
		m0_stob_put(stob);
		m0_stob_domain_fini(dom);
	}
	stob = NULL;
	dom = NULL;
}

static bool idled;

static void once_idle(struct m0_addb2_storage *_stor)
{
	M0_UT_ASSERT(_stor == stor);
	M0_UT_ASSERT(!idled);
	idled = true;
}

static struct m0_semaphore idlewait;
static unsigned done;
static unsigned committed;
static       struct m0_addb2_frame_header  last;
static const struct m0_addb2_frame_header *anchor;

static void test_idle(struct m0_addb2_storage *stor)
{
	once_idle(stor);
	m0_semaphore_up(&idlewait);
}

/**
 * "write-init-fini" test: create storage machine, stop it and finalise.
 */
static void write_init_fini(void)
{
	const struct m0_addb2_storage_ops ops = {
		.sto_idle = &test_idle
	};

	stob_get(true);
	idled = false;
	m0_semaphore_init(&idlewait, 0);
	stor = m0_addb2_storage_init(stob, SIZE,
				     &M0_ADDB2_HEADER_INIT, &ops, NULL);
	M0_UT_ASSERT(stor != NULL);
	m0_addb2_storage_stop(stor);
	/*
	 * Storage machine may not be idle at this point, because whenever the
	 * machine is initialised a special marker record is pushed onto
	 * stob. But the corresponding IO can be already completed by the time
	 * m0_addb2_storage_stop() returns, so neither "idled" nor "!idled" can
	 * be asserted.
	 */
	m0_semaphore_down(&idlewait);
	M0_UT_ASSERT(idled);
	m0_addb2_storage_fini(stor);
	stor = NULL;
	stob_put(true);
}

static void test_done(struct m0_addb2_storage *_stor,
	       struct m0_addb2_trace_obj *obj)
{
	M0_UT_ASSERT(_stor == stor);
	++done;
}

static void test_commit(struct m0_addb2_storage *_stor,
			const struct m0_addb2_frame_header *_last)
{
	M0_UT_ASSERT(_stor == stor);
	++committed;
	last = *_last;
}

static const struct m0_addb2_storage_ops test_ops = {
	.sto_idle   = &test_idle,
	.sto_done   = &test_done,
	.sto_commit = &test_commit
};

static struct m0_semaphore machwait;

static void mach_idle(const struct m0_addb2_mach *mach)
{
	m0_semaphore_up(&machwait);
}

static unsigned traces_submitted;

static int test_submit(const struct m0_addb2_mach *m, struct m0_addb2_trace *t)
{
	struct m0_addb2_trace_obj *obj = M0_AMB(obj, t, o_tr);

	M0_UT_ASSERT(m == mach);
	++traces_submitted;
	return m0_addb2_storage_submit(stor, obj);
}

static m0_bcount_t stob_size = SIZE;

static void stor_init(bool create)
{
	idled = false;
	idle = &mach_idle;
	done = 0;
	traces_submitted = 0;
	committed = 0;
	m0_semaphore_init(&idlewait, 0);
	m0_semaphore_init(&machwait, 0);
	stob_get(create);
	stor = m0_addb2_storage_init(stob, stob_size, anchor, &test_ops, NULL);
	M0_UT_ASSERT(stor != NULL);
	mach = mach_set(&test_submit);
	M0_UT_ASSERT(mach != NULL);
}

static void stor_fini(bool destroy)
{
	m0_addb2_mach_stop(mach);
	m0_addb2_storage_stop(stor);
	m0_semaphore_down(&idlewait);
	M0_UT_ASSERT(idled);
	m0_semaphore_down(&machwait);
	mach_fini(mach);
	mach = NULL;
	m0_addb2_storage_fini(stor);
	stor = NULL;
	stob_put(destroy);
	m0_semaphore_fini(&idlewait);
	m0_semaphore_fini(&machwait);
}

static void submit_one(void)
{
	anchor = &M0_ADDB2_HEADER_INIT;
	stor_init(true);
	M0_ADDB2_ADD(18, 127, 0, 0, 1);
	stor_fini(true);
}

/**
 * "read-one" test: create storage machine, add one record; check that storage
 * iterator returns the record.
 */
static void read_one(void)
{
	int result;
	struct m0_addb2_record *rec = NULL;
	uint64_t orig_trace_nr;

	last = *(anchor = &M0_ADDB2_HEADER_INIT);
	stor_init(true);
	orig_trace_nr = last.he_trace_nr;
	M0_ADDB2_PUSH(1, 2);
	M0_ADDB2_ADD(0x0011111111111111,
		     0x2222222222222222, 0x3333333333333333,
		     0x4444444444444444, 0x5555555555555555);
	m0_addb2_pop(1);
	stor_fini(false);
	M0_UT_ASSERT(memcmp(&last, &M0_ADDB2_HEADER_INIT, sizeof last));
	M0_UT_ASSERT(last.he_trace_nr    == 1 + orig_trace_nr);
	M0_UT_ASSERT(last.he_offset      == 0);
	M0_UT_ASSERT(last.he_prev_offset == 0);

	stob_get(false);
	result = m0_addb2_sit_init(&sit, stob, SIZE, &last);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(sit != NULL);
	result = m0_addb2_sit_next(sit, &rec);
	M0_UT_ASSERT(result > 0);
	M0_UT_ASSERT(rec != NULL);
	M0_UT_ASSERT(receq(rec, &(struct small_record) {
			.ar_val = VAL(0x0011111111111111,
				      0x2222222222222222, 0x3333333333333333,
				      0x4444444444444444, 0x5555555555555555),
			.ar_label_nr = 1,
			.ar_label = {
				[0] = VAL(1, 2)
			}
	}));
	result = m0_addb2_sit_next(sit, &rec);
	M0_UT_ASSERT(result == 0);
	m0_addb2_sit_fini(sit);
	stob_put(true);
}

static unsigned issued;

#define PAYLOAD(seq) {				\
		seq ^ 0xdead,		\
		seq << 6,			\
		74,				\
		8 - seq,			\
		0x473824622,			\
		1 + seq,			\
		2 - seq,			\
		3 + seq,			\
		4 - seq,			\
		5 + seq,			\
		6 - seq,			\
		7 + seq,			\
		8 - seq			\
}

enum { DEPTH_MAX = M0_ADDB2_LABEL_MAX };

static void add_one(void)
{
	uint64_t payload[] = PAYLOAD(issued);

	if ((issued % DEPTH_MAX) == 0 && issued > 0) {
		int i;

		for (i = DEPTH_MAX - 1; i >= 0; --i)
			m0_addb2_pop(2 * i);
	}
	m0_addb2_add(issued, issued % ARRAY_SIZE(payload), payload);
	m0_addb2_push(2 * (issued % DEPTH_MAX),
		      issued % ARRAY_SIZE(payload), payload);
	++issued;
}

static unsigned checked;

static void check_one(const struct m0_addb2_record *rec)
{
	uint64_t payload[] = PAYLOAD(checked);
	const struct m0_addb2_value ideal = {
		.va_id   = checked,
		.va_nr   = checked % ARRAY_SIZE(payload),
		.va_data = payload
	};
	M0_UT_ASSERT(valeq(&rec->ar_val, &ideal));
	M0_UT_ASSERT(rec->ar_label_nr == checked % DEPTH_MAX);
	M0_UT_ASSERT(m0_forall(i, checked % DEPTH_MAX,
			       rec->ar_label[i].va_id == 2 * (i % DEPTH_MAX)));
	++ checked;
}

enum { NR = 500 * DEPTH_MAX };

/**
 * "read-one" test: create storage machine, add a number of records; check that
 * storage iterator returns all of them.
 */
static void read_many(void)
{
	int result;
	int i;
	struct m0_addb2_record *rec = NULL;

	issued = 0;
	checked = 0;
	last = *(anchor = &M0_ADDB2_HEADER_INIT);
	stor_init(true);
	for (i = 0; i <= NR; ++i)
		add_one();
	m0_addb2_pop(0);
	stor_fini(false);

	stob_get(false);
	result = m0_addb2_sit_init(&sit, stob, SIZE, &last);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(sit != NULL);
	for (i = 0; i <= NR; ++i) {
		result = m0_addb2_sit_next(sit, &rec);
		M0_UT_ASSERT(result > 0);
		M0_UT_ASSERT(rec != NULL);
		check_one(rec);
	}
	result = m0_addb2_sit_next(sit, &rec);
	M0_UT_ASSERT(result == 0);
	m0_addb2_sit_fini(sit);
	stob_put(true);
}

static void frame_fill(void)
{
	uint64_t seqno = last.he_seqno;

	while (last.he_seqno == seqno) {
		add_one();
		while (traces_submitted - done >= 100) {
			nanosleep(&(struct timespec) { .tv_sec = 0,
						.tv_nsec = 100000000 }, NULL);
		}
	}
}

/**
 * Force storage stob wrap-around "n" times.
 */
static void wrap(int n)
{
	int result;
	struct m0_addb2_record *rec = NULL;

	issued = 0;
	checked = 0;
	stob_size = n * FRAME_SIZE_MAX;
	last = *(anchor = &M0_ADDB2_HEADER_INIT);
	stor_init(true);
	frame_fill();
	M0_UT_ASSERT(ergo(n > 1, last.he_offset != 0));
	while (last.he_offset != 0)
		frame_fill();
	frame_fill();
	frame_fill();
	while ((issued % DEPTH_MAX) != 1)
		add_one();
	m0_addb2_pop(0);
	stor_fini(false);
	stob_get(false);
	result = m0_addb2_sit_init(&sit, stob, stob_size, &last);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(sit != NULL);
	result = m0_addb2_sit_next(sit, &rec);
	M0_UT_ASSERT(result > 0);
	M0_UT_ASSERT(rec != NULL);
	checked = rec->ar_val.va_id;
	check_one(rec);
	while ((result = m0_addb2_sit_next(sit, &rec)) > 0)
		check_one(rec);
	M0_UT_ASSERT(result == 0);
	m0_addb2_sit_fini(sit);
	stob_put(true);
}

/**
 * "wrap-1" test: wrap stob once.
 */
static void wrap1(void)
{
	wrap(1);
}

/**
 * "wrap-2" test: wrap stob twice.
 */
static void wrap2(void)
{
	wrap(2);
}

/**
 * "wrap-3" test: wrap stob thrice.
 */
static void wrap3(void)
{
	wrap(3);
}

/**
 * "wrap-7" test: wrap stob 7 times.
 */
static void wrap7(void)
{
	wrap(7);
}

struct m0_ut_suite addb2_storage_ut = {
	.ts_name = "addb2-storage",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "write-init-fini",               &write_init_fini },
		{ "submit-one",                    &submit_one },
		{ "read-one",                      &read_one },
		{ "read-many",                     &read_many },
		{ "wrap-1",                        &wrap1 },
		{ "wrap-2",                        &wrap2 },
		{ "wrap-3",                        &wrap3 },
		{ "wrap-7",                        &wrap7 },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
