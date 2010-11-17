/* -*- C -*- */

#include "lib/errno.h"              /* EALREADY, EAGAIN */
#include "lib/arith.h"              /* C2_3WAY */
#include "dtm/verno.h"
#include "fol/fol.h"

/**
   @addtogroup dtm
   @{
 */

int c2_verno_cmp(const struct c2_verno *vn0, const struct c2_verno *vn1)
{
	C2_ASSERT(c2_verno_cmp_invariant(vn0, vn1));

	return c2_lsn_cmp(vn0->vn_lsn, vn1->vn_lsn);
}

int c2_verno_is_redoable(const struct c2_verno *unit, 
			 const struct c2_verno *before_update, bool total)
{
	int result;
	int cmp;

	C2_ASSERT(c2_verno_cmp_invariant(unit, before_update));

	cmp = c2_verno_cmp(unit, before_update);
	if (cmp < 0)
		result = total ? 0 : -EAGAIN;
	else if (cmp > 0)
		result = -EALREADY;
	else
		result = 0;
	return result;
}

int c2_verno_is_undoable(const struct c2_verno *unit, 
			 const struct c2_verno *before_update, bool total)
{
	int result;
	int cmp;

	C2_ASSERT(c2_verno_cmp_invariant(unit, before_update));

	cmp = c2_verno_cmp(unit, before_update);
	if (cmp <= 0)
		result = -EALREADY;
	else if (total)
		result = 0;
	else
		result = before_update->vn_vc + 1 == unit->vn_vc ? 0 : -EAGAIN;
	return result;
}

int c2_verno_cmp_invariant(const struct c2_verno *vn0, 
			   const struct c2_verno *vn1)
{
	return c2_lsn_cmp(vn0->vn_lsn, vn1->vn_lsn) ==
		C2_3WAY(vn0->vn_vc, vn1->vn_vc);
}

void c2_verno_inc(struct c2_verno *unit, struct c2_fol_rec *rec, uint32_t index)
{
	C2_PRE(index < rec->fr_desc.rd_header.rh_obj_nr);
	C2_PRE(c2_lsn_is_valid(rec->fr_desc.rd_lsn));

	rec->fr_desc.rd_ref[index].or_before_ver = *unit;
	unit->vn_vc++;
	unit->vn_lsn = rec->fr_desc.rd_lsn;

	C2_POST(unit->vn_vc != 0); /* overflow */
	C2_POST(c2_verno_cmp(&rec->fr_desc.rd_ref[index].or_before_ver,
			     unit) == -1);
}

/** @} end of dtm group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
