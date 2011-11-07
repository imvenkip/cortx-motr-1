/* -*- C -*- */
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
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 11/05/2011
 */

#include "lib/aptr.h"
#include "lib/assert.h"
#include "lib/misc.h"	/* C2_SET0 */

struct aptr {
	volatile void *ap_ptr;
	volatile long ap_count;
};

static inline struct aptr *aptr_align(struct c2_aptr *ap)
{
	const long align = 0x10;
	long addr = (long) ap;

	C2_CASSERT(sizeof addr == sizeof ap);
	addr = (addr + align) & ~(align - 1);
	return (struct aptr *) addr;
}

static inline bool CAS16(void *dst, void *old_value, void *new_value)
{
	char r = 0;

	__asm__ __volatile__("lock; cmpxchg16b (%6);"
			     "setz %7; "
			   : "=a" (*((int64_t *) old_value)),
			     "=d" (*((int64_t *) old_value + 1))
			   : "0" (*((int64_t *) old_value)),
			     "1" (*((int64_t *) old_value + 1)),
			     "b" (*((int64_t *) new_value)),
			     "c" (*((int64_t *) new_value + 1)),
			     "r" ((int64_t *) dst),
			     "m" (r)
			   : "cc", "memory");
	return !!r;
}

static inline void READ16(void *dst, void *src)
{
	__asm__ __volatile__(
			"xor %%rax, %%rax; "
			"xor %%rdx, %%rdx; "
			"xor %%rcx, %%rcx; "
			"xor %%rbx, %%rbx; "
			"lock; cmpxchg16b (%%rsi); "
			"mov %%rax, (%%rdi); "
			"mov %%rdx, 0x8(%%rdi); "
		      :   
		      : "D" (dst),
			"S" (src)
		      : "cc","memory", "%rax", "%rbx", "%rcx", "%rdx");
}

static inline volatile void **aptr_ptr(struct c2_aptr *ap)
{
	return &aptr_align(ap)->ap_ptr;
}

static inline volatile long *aptr_count(struct c2_aptr *ap)
{
	return &aptr_align(ap)->ap_count;
}

void c2_aptr_init(struct c2_aptr *ap)
{
	C2_CASSERT(sizeof(struct c2_aptr) == 32);
	C2_SET0(ap);
	c2_aptr_set(ap, NULL, 0);
}

void c2_aptr_fini(struct c2_aptr *ap)
{
}

void *c2_aptr_ptr(struct c2_aptr *ap)
{
	volatile void *ptr = *aptr_ptr(ap);
	return (void *) ptr;
}

long c2_aptr_count(struct c2_aptr *ap)
{
	volatile long count = *aptr_count(ap);
	return (long) count;
}

void c2_aptr_set(struct c2_aptr *ap, void *ptr, long count)
{
	struct c2_aptr new_ap;

	*aptr_ptr(&new_ap) = ptr;
	*aptr_count(&new_ap) = count;
	c2_aptr_copy(ap, &new_ap);
}

void c2_aptr_copy(struct c2_aptr *dst, struct c2_aptr *src)
{
	READ16(aptr_align(dst), aptr_align(src));
}

bool c2_aptr_eq(struct c2_aptr *ap1, struct c2_aptr *ap2)
{
	struct c2_aptr t1;
	struct c2_aptr t2;

	c2_aptr_copy(&t1, ap1);
	c2_aptr_copy(&t2, ap2);

	return c2_aptr_ptr(&t1) == c2_aptr_ptr(&t2) &&
		c2_aptr_count(&t1) == c2_aptr_count(&t2);
}

bool c2_aptr_cas(struct c2_aptr *dst, struct c2_aptr *old_value,
		void *new_ptr, long new_count)
{
	struct c2_aptr new_value;
	bool rc;

	C2_ASSERT(dst != NULL);
	C2_ASSERT(old_value != NULL);
	C2_ASSERT(new_ptr != NULL);
	C2_ASSERT(dst != old_value);

	c2_aptr_init(&new_value);
	c2_aptr_set(&new_value, new_ptr, new_count);
	rc = CAS16(aptr_align(dst), aptr_align(old_value),
			aptr_align(&new_value));
	c2_aptr_fini(&new_value);

	return rc;
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
