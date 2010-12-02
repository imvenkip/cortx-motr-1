#include "stob_id.h"

bool c2_stob_id_eq(const struct c2_stob_id *id0, const struct c2_stob_id *id1)
{
	return c2_uint128_eq(&id0->si_bits, &id1->si_bits);
}
C2_EXPORTED(c2_stob_id_eq);

int c2_stob_id_cmp(const struct c2_stob_id *id0, const struct c2_stob_id *id1)
{
	return c2_uint128_cmp(&id0->si_bits, &id1->si_bits);
}
C2_EXPORTED(c2_stob_id_cmp);

bool c2_stob_id_is_set(const struct c2_stob_id *id)
{
	static const struct c2_stob_id zero = {
		.si_bits = {
			.u_hi = 0,
			.u_lo = 0
		}
	};
	return !c2_stob_id_eq(id, &zero);
}
C2_EXPORTED(c2_stob_id_is_set);
