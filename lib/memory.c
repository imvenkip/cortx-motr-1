/* -*- C -*- */
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
 * Original creation date: 29-Mar-2015
 */


/**
 * @addtogroup memory
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MEMORY
#include "config.h"      /* ENABLE_FREE_POISON */
#include "lib/arith.h"   /* min_type, m0_is_po2 */
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/finject.h"

#include "addb2/addb2.h"
#include "addb2/identifier.h"

enum { U_POISON_BYTE = 0x5f };

#ifdef ENABLE_DEV_MODE
#define DEV_MODE (true)
#else
#define DEV_MODE (false)
#endif

#ifdef ENABLE_FREE_POISON
static void poison_before_free(void *data, size_t size)
{
	memset(data, U_POISON_BYTE, size);
}
#else
static void poison_before_free(void *data, size_t size)
{;}
#endif

M0_INTERNAL void  *m0_arch_alloc       (size_t size);
M0_INTERNAL void   m0_arch_free        (void *data);
M0_INTERNAL void   m0_arch_allocated_zero(void *data, size_t size);
M0_INTERNAL size_t m0_arch_alloc_size(void *data);
M0_INTERNAL void  *m0_arch_alloc_wired(size_t size, unsigned shift);
M0_INTERNAL void   m0_arch_free_wired(void *data, size_t size, unsigned shift);
M0_INTERNAL void  *m0_arch_alloc_aligned(size_t alignment, size_t size);
M0_INTERNAL void   m0_arch_free_aligned(void *data, size_t size, unsigned shft);
M0_INTERNAL int    m0_arch_pagesize_get(void);
M0_INTERNAL int    m0_arch_memory_init (void);
M0_INTERNAL void   m0_arch_memory_fini (void);

static struct m0_atomic64 allocated;
static struct m0_atomic64 cumulative_alloc;
static struct m0_atomic64 cumulative_free;

void *m0_alloc(size_t size)
{
	void *area;

	M0_ENTRY("size=%zi", size);
	if (M0_FI_ENABLED("fail_allocation"))
		return NULL;
	area = m0_arch_alloc(size);
	if (area != NULL) {
		m0_arch_allocated_zero(area, size);
		if (DEV_MODE) {
			size_t asize = m0_arch_alloc_size(area);

			m0_atomic64_add(&allocated, asize);
			m0_atomic64_add(&cumulative_alloc, asize);
		}
	} else if (!M0_FI_ENABLED("keep_quiet")) {
		M0_LOG(M0_ERROR, "Failed to allocate %zi bytes.", size);
		m0_backtrace();
	}
	M0_ADDB2_ADD(M0_AVI_ALLOC, size, (uint64_t)area);
	M0_LEAVE("ptr=%p size=%zi", area, size);
	return area;
}
M0_EXPORTED(m0_alloc);

void m0_free(void *data)
{
	if (data != NULL) {
		size_t size = m0_arch_alloc_size(data);

		if (DEV_MODE) {
			m0_atomic64_sub(&allocated, size);
			m0_atomic64_add(&cumulative_free, size);
		}
		poison_before_free(data, size);
		m0_arch_free(data);
	}
}
M0_EXPORTED(m0_free);

M0_INTERNAL void *m0_alloc_aligned(size_t size, unsigned shift)
{
	void  *result;
	size_t alignment;

	/*
	 * posix_memalign(3):
	 *
	 *         The requested alignment must be a power of 2 at least as
	 *         large as sizeof(void *).
	 */

	alignment = max_type(size_t, 1 << shift, sizeof result);
	M0_ASSERT(m0_is_po2(alignment));
	result = m0_arch_alloc_aligned(alignment, size);
	if (result != NULL)
		m0_arch_allocated_zero(result, size);
	return result;
}
M0_EXPORTED(m0_alloc_aligned);

M0_INTERNAL void m0_free_aligned(void *data, size_t size, unsigned shift)
{
	if (data != NULL) {
		M0_PRE(m0_addr_is_aligned(data, shift));
		poison_before_free(data, size);
		m0_arch_free_aligned(data, size, shift);
	}
}
M0_EXPORTED(m0_free_aligned);

M0_INTERNAL void *m0_alloc_wired(size_t size, unsigned shift)
{
	return m0_arch_alloc_wired(size, shift);
}

M0_INTERNAL void m0_free_wired(void *data, size_t size, unsigned shift)
{
	if (data != NULL) {
		poison_before_free(data, size);
		m0_arch_free_wired(data, size, shift);
	}
}

M0_INTERNAL size_t m0_allocated(void)
{
	return m0_atomic64_get(&allocated);
}
M0_EXPORTED(m0_allocated);

M0_INTERNAL size_t m0_allocated_total(void)
{
	return m0_atomic64_get(&cumulative_alloc);
}
M0_EXPORTED(m0_allocated_total);

M0_INTERNAL size_t m0_freed_total(void)
{
	return m0_atomic64_get(&cumulative_free);
}
M0_EXPORTED(m0_freed_total);

M0_INTERNAL int m0_pagesize_get(void)
{
	return m0_arch_pagesize_get();
}

M0_INTERNAL int m0_memory_init(void)
{
	m0_atomic64_set(&allocated, 0);
	m0_atomic64_set(&cumulative_alloc, 0);
	m0_atomic64_set(&cumulative_free, 0);
	return m0_arch_memory_init();
}

M0_INTERNAL void m0_memory_fini(void)
{
	m0_arch_memory_fini();
}

#undef DEV_MODE
#undef M0_TRACE_SUBSYSTEM

/** @} end of memory group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
