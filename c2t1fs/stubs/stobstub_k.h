#ifndef _COLIBRI_STOB_STUB_K_
#define _COLIBRI_STOB_STUB_K_

#include "lib/arith.h"

struct c2_stob_id {
	struct c2_uint128 si_bits;
};

static inline bool c2_stob_id_eq(const struct c2_stob_id *id0, const struct c2_stob_id *id1)
{
	return c2_uint128_eq(&id0->si_bits, &id1->si_bits);
}

static inline bool c2_stob_id_is_set(const struct c2_stob_id *id)
{
	static const struct c2_stob_id zero = {
		.si_bits = {
			.u_hi = 0,
			.u_lo = 0
		}
	};
	return !c2_stob_id_eq(id, &zero);
}


/* _COLIBRI_STOB_STUB_K_ */
#endif
