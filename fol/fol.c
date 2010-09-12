/* -*- C -*- */

#include "lib/arith.h"         /* c2_mod_g{t,e}, C2_3WAY */

#include "fol/fol.h"

/**
   @addtogroup fol

   @{
 */

bool c2_lsn_is_valid(c2_lsn_t lsn)
{
	return lsn > C2_LSN_RESERVED_NR;
}

int c2_lsn_cmp(c2_lsn_t lsn0, c2_lsn_t lsn1)
{
	C2_PRE(c2_lsn_is_valid(lsn0));
	C2_PRE(c2_lsn_is_valid(lsn1));

	return C2_3WAY(lsn0, lsn1);
}

c2_lsn_t c2_lsn_inc(c2_lsn_t lsn)
{
	C2_PRE(c2_lsn_is_valid(lsn));

	++lsn;
	C2_ASSERT(lsn != 0);
	return lsn;
}

#if 0
bool c2_fol_rec_invariant(const struct c2_fol_rec_desc *drec)
{
	uint32_t i;
	uint32_t j;

	if (!c2_lsn_is_valid(drec->rd_lsn))
		return false;
	for (i = 0; i < drec->rd_obj_nr; ++i) {
		struct c2_fol_obj_ref *ref;

		ref = &drec->rd_ref[i];
		if (!c2_fid_is_valid(&ref->or_fid))
			return false;
		if (!c2_lsn_is_valid(ref->or_prevlsn) && 
		    ref->or_prevlsn != C2_LSN_NONE)
			return false;
		if (drec->rd_lsn <= ref->or_prevlsn)
			return false;
		for (j = 0; j < i; ++j) {
			if (c2_fid_eq(&ref->rd_fid, &drec->rd_ref[j].rd_fid))
				return false;
		}
	}
	if (!c2_epoch_is_valid(&drec->rd_epoch))
		return false;
	if (!c2_update_is_valid(&drec->rd_self))
		return false;
	for (i = 0; i < drec->rd_sibling_nr; ++i) {
		struct c2_fol_update_ref *upd;

		upd = &drec->rd_sibling[i];
		if (!c2_update_is_valid(&upd->ui_id))
			return false;
		if (!c2_update_state_is_valid(upd->ui_state))
			return false;
		for (j = 0; j < i; ++j) {
			if (c2_update_is_eq(&upd->ui_id, 
					    &drec->rd_sibling[j].ui_id))
				return false;
		}
	}
	return true;
}
#endif

/** @} end of fol group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
