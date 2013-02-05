/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>,
 *                  Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 10-Jan-2012
 */

#include <ctype.h>         /* isprint */
#include "conf/preload.h"  /* m0_confstr_parse */
#include "conf/ut/file_helpers.h"
#include "lib/string.h"    /* strlen */
#include "lib/memory.h"    /* m0_free */
#include "lib/ut.h"

extern void test_confdb(void);

/** Statistics accumulator for m0_confstr_parse() call. */
struct fuzz_acc {
	uint32_t nr_successes;
	uint32_t nr_total;
};

static size_t chunk_tail(size_t len, size_t chunk_idx, size_t nr_chunks)
{
	M0_PRE(nr_chunks > 0);

	if (chunk_idx == nr_chunks - 1) /* last chunk? */
		return len % nr_chunks;
	return 0;
}

/**
 * Returns copy of a string with chunk distorted.
 *
 * str        Input string.
 * chunk_idx  0-based index of the chunk to be distorted.
 * nr_chunks  Total number of chunks in the string.
 * what       Character being replaced.
 * by         Replacement character.
 * eq         Which characters of the chunk to replace: true -- those that are
 *            equal to `what'; false -- non-equal.
 * nr_subs    Maximum number of replacements (substitutions).
 */
static char *strdup_mangled(const char *str, size_t chunk_idx, size_t nr_chunks,
			    char what, char by, bool eq, size_t nr_subs)
{
	char        *chunk;
	char        *result;
	size_t       chunk_len;
	size_t       i;
	const size_t len = strlen(str);
	bool         replaced = false;

	M0_PRE(len >= nr_chunks);
	M0_PRE(chunk_idx < nr_chunks);
	M0_PRE(nr_subs > 0);

	result = strdup(str);
	M0_ASSERT(result != NULL);

	chunk_len = len / nr_chunks;
	chunk = &result[chunk_idx * chunk_len];
	chunk_len += chunk_tail(len, chunk_idx, nr_chunks);

	for (i = 0; i < chunk_len; ++i) {
		if (eq ? chunk[i] == what : chunk[i] != what) {
			chunk[i] = by;
			replaced = true;

			--nr_subs;
			if (nr_subs == 0)
				break;
		}
	}

	if (replaced)
		return result;

	m0_free(result);
	return NULL;
}

/** Returns copy of a string with chunk withdrawn. */
static char *
strdup_without_chunk(const char *str, size_t chunk_idx, size_t nr_chunks,
		     char what M0_UNUSED, char by M0_UNUSED, bool eq M0_UNUSED,
		     size_t nr_subs M0_UNUSED)
{
	char        *result;
	char        *pos; /* writing position */
	size_t       cap; /* remaining capacity of `result' */
	const size_t len = strlen(str);
	const size_t chunk_len = len / nr_chunks;

	M0_PRE(len >= nr_chunks);
	M0_PRE(chunk_idx < nr_chunks);
	M0_PRE(nr_chunks > 1); /* otherwise result would be empty */

	cap = len - chunk_len - chunk_tail(len, chunk_idx, nr_chunks);
	M0_ASSERT(cap > 0);

	result = m0_alloc(cap + 1);
	M0_ASSERT(result != NULL);
	result[cap] = '\0';

	pos = result;
	if (chunk_idx != 0) {
		size_t n = chunk_idx * chunk_len;

		memcpy(pos, str, n);
		pos += n;
		cap -= n;
	}
	if (chunk_idx != nr_chunks - 1) {
		memcpy(pos, &str[(chunk_idx + 1) * chunk_len], cap);
		cap = 0;
	}

	M0_ASSERT(cap == 0);
	return result;
}

static void
_fuzz_test(const char *conf_str, char what, char by, bool eq, size_t nr_subs,
	   char *(*gen)(const char *str, size_t chunk_idx, size_t nr_chunks,
			char what, char by, bool eq, size_t nr_subs),
	   struct fuzz_acc *acc)
{
	struct m0_confx *enc;
	char            *sample;
	size_t           nr_chunks;
	size_t           i;
	int              rc;

	if (gen == NULL)
		gen = strdup_mangled;

	if (gen == strdup_mangled && what == by && eq)
		return; /* nothing to do */

	for (nr_chunks = 4; nr_chunks <= 16; nr_chunks *= 2) {
		for (i = 0; i < nr_chunks; ++i) {
			sample = gen(conf_str, i, nr_chunks, what, by, eq,
				     nr_subs);
			if (sample == NULL)
				continue;

			rc = m0_confstr_parse(sample, &enc);
			if (unlikely(rc == 0)) {
				m0_confx_free(enc);
				++acc->nr_successes;
			}
			++acc->nr_total;

			m0_free(sample);
		}
	}
}

static void
fuzz_missing_brace_test(const char *str, size_t nr_subs, struct fuzz_acc *acc)
{
	size_t     i;
	const char a[] = "(){}[]\"";

	for (i = 0; i < ARRAY_SIZE(a); ++i)
		_fuzz_test(str, a[i], ' ', true, nr_subs, NULL, acc);
}

static void
fuzz_odd_brace_test(const char *str, size_t nr_subs, struct fuzz_acc *acc)
{
	int         i;
	int         j;
	const char a[] = "(){}[]";
	const char b[] = " \t\n(){}[]";

	for (i = 0; i < ARRAY_SIZE(a); ++i) {
		for (j = 0; j < ARRAY_SIZE(b); ++j)
			_fuzz_test(str, a[i], b[j], true, nr_subs, NULL, acc);
	}
}

static void
fuzz_nonprint_test(const char *str, size_t nr_subs, struct fuzz_acc *acc)
{
	int        i;
	const char a[] = "(){}[]\" \n\t";

	M0_ASSERT(!isprint('\3'));
	for (i = 0; i < ARRAY_SIZE(a); ++i)
		_fuzz_test(str, a[i], '\3', false, nr_subs, NULL, acc);
}

void test_confstr_fuzz(void)
{
	int             rc;
	struct fuzz_acc acc = {0};
	char            buf[1024] = {0};

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf_xc.txt"), buf, sizeof buf);
	M0_UT_ASSERT(rc == 0);

	fuzz_missing_brace_test(buf, 1, &acc);
	fuzz_missing_brace_test(buf, 2, &acc);
	fuzz_odd_brace_test(buf, 1, &acc);
	fuzz_odd_brace_test(buf, 2, &acc);
	fuzz_nonprint_test(buf, 5, &acc);
	_fuzz_test(buf, '\0', '\0', false, 0, strdup_without_chunk, &acc);

	M0_ASSERT(100 * acc.nr_successes / acc.nr_total < 1); /* < 1% */
}

const struct m0_test_suite confstr_ut = {
	.ts_name  = "confstr-ut",
	.ts_init  = NULL,
	.ts_fini  = NULL,
	.ts_tests = {
		{ "db",   test_confdb       },
		{ "fuzz", test_confstr_fuzz },
		{ NULL, NULL }
	}
};
