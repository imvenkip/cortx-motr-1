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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 */

#ifndef __COLIBRI_SNS_LS_SOLVE_H__
#define __COLIBRI_SNS_LS_SOLVE_H__

#include "matvec.h"

/**
   @defgroup c2_linsys Systems of Linear Equations Solving Algorithm:

   A systems of linear equations solving algorithm is a part of C2 core.
   It is based on Gauss method and performs the following:
   @li Solves a linear system of equations represented by (NxN) matrix and (1xN) vector, result is (1xN) vector.
   @{
 */

/**
 * Represents linear system of equations [l_mat]*[l_res]=[l_vec]
 */
struct c2_linsys {
	struct c2_matrix *l_mat;
	struct c2_vector *l_vec;
	struct c2_vector *l_res;
};

/**
 * @pre c2_matrix_init(mat) && c2_vector_init(vec) && c2_vec_init(res)
 * @pre mat->m_height > 0 && mat->width > 0
 * @pre mat->m_width == mat->m_height && res->v_size == vec->v_size && vec->v_size == mat->m_width
 */
void c2_linsys_init(struct c2_linsys *linsys,
                    struct c2_matrix *mat,
                    struct c2_vector *vec,
                    struct c2_vector *res);

void c2_linsys_fini(struct c2_linsys *linsys);

/**
 * Solves given system of linear equatons, writes result into 'linsys->l_res'.
 */
void c2_linsys_solve(struct c2_linsys *linsys);

/** @} end group c2_linsys */

/* __COLIBRI_SNS_LS_SOLVE_H__*/
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
