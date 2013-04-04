/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 09/25/2012
 */
#pragma once

#ifndef __MERO_ADDB_MACROS_H__
#define __MERO_ADDB_MACROS_H__

#include "lib/assert.h"
#include "lib/cdefs.h"

/**
   @ingroup addb_pvt
   @{
*/

/*
 *****************************************************************************
 * Record type va_args expansion macro family with name fields in the union.
 *****************************************************************************
 */
#define M0__ADDB_RT_N_BEGIN(name, bt, id, nr)		\
M0_BASSERT(M0_HAS_TYPE((id), int) && (id) > 0);		\
M0_BASSERT(M0_ADDB_BRT_ ## bt == M0_ADDB_BRT_EX ||	\
	   M0_ADDB_BRT_ ## bt == M0_ADDB_BRT_DP);	\
struct m0_addb_rec_type name = {			\
	.art_magic = 0,					\
	.art_base_type = M0_ADDB_BRT_ ## bt,		\
	.art_name = #name,				\
	.art_id = (id),					\
	.art_rf_nr = nr,				\
        .art_rf = {
#define M0__ADDB_RT_N_END()				\
        }						\
}

#define M0__ADDB_RT_N0(name, bt, id)		\
M0__ADDB_RT_N_BEGIN(name, bt, id, 0)		\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N1(name, bt, id, a1)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 1)		\
	{ .arfu_name = a1 },			\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N2(name, bt, id, a1, a2)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 2)		\
	{ .arfu_name = a1 },			\
	{ .arfu_name = a2 },			\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N3(name, bt, id, a1, a2, a3)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 3)			\
	{ .arfu_name = a1 },				\
	{ .arfu_name = a2 },				\
	{ .arfu_name = a3 },				\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N4(name, bt, id, a1, a2, a3, a4)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 4)			\
	{ .arfu_name = a1 },				\
	{ .arfu_name = a2 },				\
	{ .arfu_name = a3 },				\
	{ .arfu_name = a4 },				\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N5(name, bt, id, a1, a2, a3, a4, a5)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 5)				\
	{ .arfu_name = a1 },					\
	{ .arfu_name = a2 },					\
	{ .arfu_name = a3 },					\
	{ .arfu_name = a4 },					\
	{ .arfu_name = a5 },					\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N6(name, bt, id, a1, a2, a3, a4, a5, a6)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 6)				\
	{ .arfu_name = a1 },					\
	{ .arfu_name = a2 },					\
	{ .arfu_name = a3 },					\
	{ .arfu_name = a4 },					\
	{ .arfu_name = a5 },					\
	{ .arfu_name = a6 },					\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N7(name, bt, id, a1, a2, a3, a4, a5, a6, a7)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 7)					\
	{ .arfu_name = a1 },						\
	{ .arfu_name = a2 },						\
	{ .arfu_name = a3 },						\
	{ .arfu_name = a4 },						\
	{ .arfu_name = a5 },						\
	{ .arfu_name = a6 },						\
	{ .arfu_name = a7 },						\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N8(name, bt, id, a1, a2, a3, a4, a5, a6, a7, a8)	\
M0__ADDB_RT_N_BEGIN(name, bt, id, 8)					\
	{ .arfu_name = a1 },						\
	{ .arfu_name = a2 },						\
	{ .arfu_name = a3 },						\
	{ .arfu_name = a4 },						\
	{ .arfu_name = a5 },						\
	{ .arfu_name = a6 },						\
	{ .arfu_name = a7 },						\
	{ .arfu_name = a8 },						\
M0__ADDB_RT_N_END()

#define M0__ADDB_RT_N9(name, bt, id, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
M0__ADDB_RT_N_BEGIN(name, bt, id, 9)					\
	{ .arfu_name = a1 },						\
	{ .arfu_name = a2 },						\
	{ .arfu_name = a3 },						\
	{ .arfu_name = a4 },						\
	{ .arfu_name = a5 },						\
	{ .arfu_name = a6 },						\
	{ .arfu_name = a7 },						\
	{ .arfu_name = a8 },						\
	{ .arfu_name = a9 },						\
M0__ADDB_RT_N_END()

/*
 *****************************************************************************
 * Counter record type macro with uint64_t fields in the union.
 *****************************************************************************
 */

#define M0__ADDB_IS_INT64(num) \
(M0_HAS_TYPE((num), int) || M0_HAS_TYPE((num), long int))

#define M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, nr) \
M0_BASSERT(M0_HAS_TYPE((id), int) && (id) > 0);	\
struct m0_addb_rec_type name = {		\
	.art_magic = 0,				\
	.art_base_type = M0_ADDB_BRT_##bt,	\
	.art_name = #name,			\
	.art_id = (id),				\
	.art_sm_conf = (smconf),		\
	.art_rf_nr = nr,			\
        .art_rf = {
#define M0__ADDB_RT_L_END()			\
        }					\
}

#define M0__ADDB_RT_L0(name, bt, id, smconf)	\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 0)	\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L1(name, bt, id, smconf, b1) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);	\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 1)	\
	{ .arfu_lower = (b1) },			\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L2(name, bt, id, smconf, b1, b2)	\
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);		\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));	\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 2)		\
	{ .arfu_lower = (b1) },				\
	{ .arfu_lower = (b2) },				\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L3(name, bt, id, smconf, b1, b2, b3) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);		\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));	\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));	\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 3)		\
	{ .arfu_lower = (b1) },				\
	{ .arfu_lower = (b2) },				\
	{ .arfu_lower = (b3) },				\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L4(name, bt, id, smconf, b1, b2, b3, b4) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);		\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));	\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));	\
M0_BASSERT(M0__ADDB_IS_INT64(b4) && (b4) > (b3));	\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 4)		\
	{ .arfu_lower = (b1) },				\
	{ .arfu_lower = (b2) },				\
	{ .arfu_lower = (b3) },				\
	{ .arfu_lower = (b4) },				\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L5(name, bt, id, smconf, b1, b2, b3, b4, b5) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);		\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));	\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));	\
M0_BASSERT(M0__ADDB_IS_INT64(b4) && (b4) > (b3));	\
M0_BASSERT(M0__ADDB_IS_INT64(b5) && (b5) > (b4));	\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 5)		\
	{ .arfu_lower = (b1) },				\
	{ .arfu_lower = (b2) },				\
	{ .arfu_lower = (b3) },				\
	{ .arfu_lower = (b4) },				\
	{ .arfu_lower = (b5) },				\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L6(name, bt, id, smconf, b1, b2, b3, b4, b5, b6) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);			\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));		\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));		\
M0_BASSERT(M0__ADDB_IS_INT64(b4) && (b4) > (b3));		\
M0_BASSERT(M0__ADDB_IS_INT64(b5) && (b5) > (b4));		\
M0_BASSERT(M0__ADDB_IS_INT64(b6) && (b6) > (b5));		\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 6)			\
	{ .arfu_lower = (b1) },					\
	{ .arfu_lower = (b2) },					\
	{ .arfu_lower = (b3) },					\
	{ .arfu_lower = (b4) },					\
	{ .arfu_lower = (b5) },					\
	{ .arfu_lower = (b6) },					\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L7(name, bt, id, smconf, b1, b2, b3, b4, b5, b6, b7) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);			\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));		\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));		\
M0_BASSERT(M0__ADDB_IS_INT64(b4) && (b4) > (b3));		\
M0_BASSERT(M0__ADDB_IS_INT64(b5) && (b5) > (b4));		\
M0_BASSERT(M0__ADDB_IS_INT64(b6) && (b6) > (b5));		\
M0_BASSERT(M0__ADDB_IS_INT64(b7) && (b7) > (b6));		\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 7)			\
	{ .arfu_lower = (b1) },					\
	{ .arfu_lower = (b2) },					\
	{ .arfu_lower = (b3) },					\
	{ .arfu_lower = (b4) },					\
	{ .arfu_lower = (b5) },					\
	{ .arfu_lower = (b6) },					\
	{ .arfu_lower = (b7) },					\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L8(name, bt, id, smconf, b1, b2, b3, b4, b5, b6, b7, b8) \
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);				\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));			\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));			\
M0_BASSERT(M0__ADDB_IS_INT64(b4) && (b4) > (b3));			\
M0_BASSERT(M0__ADDB_IS_INT64(b5) && (b5) > (b4));			\
M0_BASSERT(M0__ADDB_IS_INT64(b6) && (b6) > (b5));			\
M0_BASSERT(M0__ADDB_IS_INT64(b7) && (b7) > (b6));			\
M0_BASSERT(M0__ADDB_IS_INT64(b8) && (b8) > (b7));			\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 8)				\
	{ .arfu_lower = (b1) },						\
	{ .arfu_lower = (b2) },						\
	{ .arfu_lower = (b3) },						\
	{ .arfu_lower = (b4) },						\
	{ .arfu_lower = (b5) },						\
	{ .arfu_lower = (b6) },						\
	{ .arfu_lower = (b7) },						\
	{ .arfu_lower = (b8) },						\
M0__ADDB_RT_L_END()

#define M0__ADDB_RT_L9(name, bt, id, smconf,				\
                       b1, b2, b3, b4, b5, b6, b7, b8, b9)		\
M0_BASSERT(M0__ADDB_IS_INT64(b1) && (b1) > 0);				\
M0_BASSERT(M0__ADDB_IS_INT64(b2) && (b2) > (b1));			\
M0_BASSERT(M0__ADDB_IS_INT64(b3) && (b3) > (b2));			\
M0_BASSERT(M0__ADDB_IS_INT64(b4) && (b4) > (b3));			\
M0_BASSERT(M0__ADDB_IS_INT64(b5) && (b5) > (b4));			\
M0_BASSERT(M0__ADDB_IS_INT64(b6) && (b6) > (b5));			\
M0_BASSERT(M0__ADDB_IS_INT64(b7) && (b7) > (b6));			\
M0_BASSERT(M0__ADDB_IS_INT64(b8) && (b8) > (b7));			\
M0_BASSERT(M0__ADDB_IS_INT64(b9) && (b9) > (b8));			\
M0__ADDB_RT_L_BEGIN(name, bt, id, smconf, 9)				\
	{ .arfu_lower = (b1) },						\
	{ .arfu_lower = (b2) },						\
	{ .arfu_lower = (b3) },						\
	{ .arfu_lower = (b4) },						\
	{ .arfu_lower = (b5) },						\
	{ .arfu_lower = (b6) },						\
	{ .arfu_lower = (b7) },						\
	{ .arfu_lower = (b8) },						\
	{ .arfu_lower = (b9) },						\
M0__ADDB_RT_L_END()

/*
 *****************************************************************************
 * Context init va_args expansion macros
 *****************************************************************************
 */
#define M0__ADDB_CTX_FVEC(...) (uint64_t []){__VA_ARGS__}

#define M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, num, f)	\
M0_PRE(!m0_addb_ctx_is_imported(pctx));				\
M0_PRE((ct)->act_cf_nr == num);					\
m0__addb_ctx_init(mc, ctx, ct, pctx, f)

#define M0__ADDB_CTX_INIT0(mc, ctx, ct, pctx)			\
do {								\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 0, NULL);	\
} while (0)

#define M0__ADDB_CTX_INIT1(mc, ctx, ct, pctx, f1)			\
do {									\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 1, M0__ADDB_CTX_FVEC(f1)); \
} while (0)

#define M0__ADDB_CTX_INIT2(mc, ctx, ct, pctx, f1, f2)	\
do {							\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 2,	\
			 M0__ADDB_CTX_FVEC(f1, f2));	\
} while (0)

#define M0__ADDB_CTX_INIT3(mc, ctx, ct, pctx, f1, f2, f3)	\
do {								\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 3,		\
			 M0__ADDB_CTX_FVEC(f1, f2, f3));	\
} while (0)

#define M0__ADDB_CTX_INIT4(mc, ctx, ct, pctx, f1, f2, f3, f4)	\
do {								\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 4,		\
			 M0__ADDB_CTX_FVEC(f1, f2, f3, f4));	\
} while (0)

#define M0__ADDB_CTX_INIT5(mc, ctx, ct, pctx, f1, f2, f3, f4, f5)	\
do {									\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 5,			\
			 M0__ADDB_CTX_FVEC(f1, f2, f3, f4, f5));	\
} while (0)

#define M0__ADDB_CTX_INIT6(mc, ctx, ct, pctx, f1, f2, f3, f4, f5, f6)	\
do {									\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 6,			\
			 M0__ADDB_CTX_FVEC(f1, f2, f3, f4, f5, f6));	\
} while (0)

#define M0__ADDB_CTX_INIT7(mc, ctx, ct, pctx, f1, f2, f3, f4, f5, f6, f7) \
do {									\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 7,			\
			 M0__ADDB_CTX_FVEC(f1, f2, f3, f4, f5, f6, f7)); \
} while (0)

#define M0__ADDB_CTX_INIT8(mc, ctx, ct, pctx, f1, f2, f3, f4, f5, f6, f7, \
                           f8)						\
do {									\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 8,			\
			 M0__ADDB_CTX_FVEC(f1,f2,f3,f4,f5,f6,f7,f8));	\
} while (0)

#define M0__ADDB_CTX_INIT9(mc, ctx, ct, pctx, f1, f2, f3, f4, f5, f6, f7, \
                           f8,f9)					\
do {									\
	M0__ADDB_CTX_INIT_COMMON(mc, ctx, ct, pctx, 9,			\
			 M0__ADDB_CTX_FVEC(f1,f2,f3,f4,f5,f6,f7,f8,f9)); \
} while (0)

/*
 *****************************************************************************
 * ADDB POST varags expansion macros
 *****************************************************************************
 */
#define M0__ADDB_POST_AV(...) (uint64_t []){__VA_ARGS__}

#define M0__ADDB_POST(rt, n, av)		\
	M0_ASSERT((rt)->art_rf_nr == n);	\
	pd.u.apd_args = av

#define M0__ADDB_POST0(rt)	\
M0__ADDB_POST(rt, 0, NULL)

#define M0__ADDB_POST1(rt, a1)		\
M0__ADDB_POST(rt, 1, M0__ADDB_POST_AV(a1))

#define M0__ADDB_POST2(rt, a1, a2)		\
M0__ADDB_POST(rt, 2, M0__ADDB_POST_AV(a1, a2))

#define M0__ADDB_POST3(rt, a1, a2, a3)			\
M0__ADDB_POST(rt, 3, M0__ADDB_POST_AV(a1, a2, a3))

#define M0__ADDB_POST4(rt, a1, a2, a3, a4)		\
M0__ADDB_POST(rt, 4, M0__ADDB_POST_AV(a1, a2, a3, a4))

#define M0__ADDB_POST5(rt, a1, a2, a3, a4, a5)		\
M0__ADDB_POST(rt, 5, M0__ADDB_POST_AV(a1, a2, a3, a4, a5))

#define M0__ADDB_POST6(rt, a1, a2, a3, a4, a5, a6)		\
M0__ADDB_POST(rt, 6, M0__ADDB_POST_AV(a1, a2, a3, a4, a5, a6))

#define M0__ADDB_POST7(rt, a1, a2, a3, a4, a5, a6, a7)			\
M0__ADDB_POST(rt, 7, M0__ADDB_POST_AV(a1, a2, a3, a4, a5, a6, a7))

#define M0__ADDB_POST8(rt, a1, a2, a3, a4, a5, a6, a7, a8)		\
M0__ADDB_POST(rt, 8, M0__ADDB_POST_AV(a1, a2, a3, a4, a5, a6, a7, a8))

#define M0__ADDB_POST9(rt, a1, a2, a3, a4, a5, a6, a7, a8, a9)		\
M0__ADDB_POST(rt, 9, M0__ADDB_POST_AV(a1, a2, a3, a4, a5, a6, a7, a8, a9))

/** @} end of addb_pvt group */

#endif /* __MERO_ADDB_MACROS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

