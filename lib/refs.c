#include "refs.h"

void c2_ref_init(struct c2_ref *ref, int init_num,
		void (*release) (struct c2_ref *ref))
{
	c2_atomic64_set(&ref->ref_cnt, init_num);
	ref->release = release;
}

void c2_ref_get(struct c2_ref *ref)
{
	c2_atomic64_inc(&ref->ref_cnt);
}

void c2_ref_put(struct c2_ref *ref)
{
	if (c2_atomic64_dec_and_test(&ref->ref_cnt)) {
		ref->release(ref);
	}
}
