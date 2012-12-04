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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 */

#pragma once

#ifndef __MERO_SNS_LS_SOLVE_H__
#define __MERO_SNS_LS_SOLVE_H__

#include "matvec.h"

/**
   @defgroup m0_linsys Systems of Linear Equations Solving Algorithm:

   A systems of linear equations solving algorithm is a part of M0 core.
   It is based on Gauss method and performs the following:
   @li Solves a linear system of equations represented by (NxN) matrix and (1xN) vector, result is (1xN) vector.
   @{
 */

/**
 * Represents linear system of equations [l_mat]*[l_res]=[l_vec]
 */
struct m0_linsys {
	struct m0_matrix *l_mat;
	struct m0_vector *l_vec;
	struct m0_vector *l_res;
};

/**
 * @pre m0_matrix_init(mat) && m0_vector_init(vec) && m0_vec_init(res)
 * @pre mat->m_height > 0 && mat->width > 0
 * @pre mat->m_width == mat->m_height && res->v_size == vec->v_size && vec->v_size == mat->m_width
 */
M0_INTERNAL void m0_linsys_init(struct m0_linsys *linsys,
				struct m0_matrix *mat,
				struct m0_vector *vec, struct m0_vector *res);

M0_INTERNAL void m0_linsys_fini(struct m0_linsys *linsys);

/**
 * Solves given system of linear equatons, writes result into 'linsys->l_res'.
 */
M0_INTERNAL void m0_linsys_solve(struct m0_linsys *linsys);

/** @} end group m0_linsys */

/* __MERO_SNS_LS_SOLVE_H__*/
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
