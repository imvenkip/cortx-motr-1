/* -*- C -*- */

#ifndef __COLIBRI_SNS_LS_SOLVE_H__
#define __COLIBRI_SNS_LS_SOLVE_H__

#include "matvec.h"

/**
 * Represents linear system of equations [l_mat]*[l_res]=[l_vec]
 */
struct c2_linsys {
	struct c2_matrix *l_mat;
	struct c2_vector *l_vec;
	struct c2_vector *l_res;
};

/**
 * @pre c2_mat_init(mat) && c2_vec_init(vec) && c2_vec_init(res)
 */
void c2_linsys_init(struct c2_linsys *linsys,
                    struct c2_matrix *mat,
                    struct c2_vector *vec,
                    struct c2_vector *res);

void c2_linsys_fini(struct c2_linsys *linsys);

/**
 * Solves given system of linear equatons, writes result into 'linsys->l_res'.
 * @pre c2_mat_init(mat) && c2_vec_init(vec) && c2_vec_init(res)
 */
void c2_linsys_solve(struct c2_linsys *linsys);

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
