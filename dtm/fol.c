/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 19-Apr-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/types.h"         /* m0_uint128 */
#include "lib/assert.h"        /* M0_IMPOSSIBLE */

#include "dtm/fol.h"

static const struct m0_dtm_history_ops fol_ops;
static const struct m0_dtm_history_ops fol_remote_ops;

M0_INTERNAL int m0_dtm_fol_init(struct m0_dtm_fol *fol, struct m0_dtm *dtm)
{
	struct m0_dtm_history *history = &fol->fo_ch.ch_history;

	m0_dtm_controlh_init(&fol->fo_ch, dtm);
	history->h_hi.hi_flags |= M0_DHF_OWNED;
	history->h_ops = &fol_ops;
	history->h_dtm = NULL;
}

M0_INTERNAL void m0_dtm_fol_fini(struct m0_dtm_fol *fol)
{
	m0_dtm_controlh_fini(&fol->fo_ch);
}

M0_INTERNAL void m0_dtm_fol_add(struct m0_dtm_fol *fol,
				struct m0_dtm_oper *oper)
{
	m0_dtm_controlh_add(&fol->fo_ch, oper);
}

static const struct m0_uint128 FOL_ID = {
	.u_hi = 0xaf01f01f01f01f01,
	.u_lo = 0xf01f01f01f01f01b
};

static const struct m0_uint128 *fol_id(const struct m0_dtm_history *history)
{
	return &FOL_ID;
}

static void fol_persistent(struct m0_dtm_history *history)
{
}

static void fol_fixed(struct m0_dtm_history *history)
{
	M0_IMPOSSIBLE("Fixing fol?");
}

static int fol_find(struct m0_dtm *dtm, const struct m0_dtm_history_type *ht,
		    const struct m0_uint128 *id, struct m0_dtm_history **out)
{
	if (m0_uint128_eq(id, &FOL_ID)) {
		*out = dtm->d_fol.fo_ch.ch_history;
		return 0;
	} else
		return -EPROTO;
}

static const struct m0_dtm_history_type_ops fol_htype_ops = {
	.hito_find = &fol_find
};

enum {
	M0_DTM_HTYPE_FOL     = 5
	M0_DTM_HTYPE_FOL_REM = 6
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_fol_htype = {
	.hit_id     = M0_DTM_HTYPE_FOL,
	.hit_rem_id = M0_DTM_HTYPE_FOL_REM,
	.hit_name   = "fol",
	.hit_ops    = &fol_htype_ops
};

static const struct m0_dtm_history_ops fol_ops = {
	.hio_type       = &m0_dtm_fol_htype,
	.hio_id         = &fol_id,
	.hio_persistent = &fol_persistent,
	.hio_fixed      = &fol_fixed,
	.hio_update     = &m0_dtm_controlh_update
};

M0_INTERNAL int m0_dtm_fol_remote_init(struct m0_dtm_fol_remote *frem,
				       struct m0_dtm *dtm,
				       struct m0_dtm_remote *remote)
{
	struct m0_dtm_history *history = &frem->rfo_ch.ch_history;

	m0_dtm_controlh_init(&frem->rfo_ch, dtm);
	history->h_ops = &fol_remote_ops;
	history->h_dtm = remote;
}

M0_INTERNAL void m0_dtm_fol_remote_fini(struct m0_dtm_fol_remote *frem)
{
	m0_dtm_controlh_fini(&frem->rfo_ch, dtm);
}

M0_INTERNAL void m0_dtm_fol_remote_add(struct m0_dtm_fol_remote *frem,
				       struct m0_dtm_oper *oper)
{
	m0_dtm_controlh_add(&frem->rfo_ch, oper);
}

static void fol_remote_persistent(struct m0_dtm_history *history)
{}

static int fol_remote_find(struct m0_dtm *dtm,
			   const struct m0_dtm_history_type *ht,
			   const struct m0_uint128 *id,
			   struct m0_dtm_history **out)
{
	if (m0_uint128_eq(id, &FOL_ID)) {
		return 0;
	} else
		return -EPROTO;
}

static const struct m0_dtm_history_type_ops fol_remote_htype_ops = {
	.hito_find = &fol_remote_find
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_fol_remote_htype = {
	.hit_id     = M0_DTM_HTYPE_FOL_REM,
	.hit_rem_id = M0_DTM_HTYPE_FOL,
	.hit_name   = "remote fol",
	.hit_ops    = &fol_remote_htype_ops
};

static const struct m0_dtm_history_ops fol_remote_ops = {
	.hio_type       = &m0_dtm_fol_remote_htype,
	.hio_persistent = &fol_remote_persistent,
	.hio_update     = &m0_dtm_controlh_update
};

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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
