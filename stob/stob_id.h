#ifndef __COLIBRI_STOB_STOB_ID_H__
#define __COLIBRI_STOB_STOB_ID_H__

#include "lib/arith.h"

/**
   Unique storage object identifier.

   A storage object in a cluster is identified by identifier of this type.
 */
struct c2_stob_id {
	struct c2_uint128 si_bits;
};

bool c2_stob_id_eq (const struct c2_stob_id *id0, const struct c2_stob_id *id1);
int  c2_stob_id_cmp(const struct c2_stob_id *id0, const struct c2_stob_id *id1);
bool c2_stob_id_is_set(const struct c2_stob_id *id);

/* __COLIBRI_STOB_STOB_ID_H__ */
#endif
