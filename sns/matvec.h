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

#ifndef __COLIBRI_SNS_MAT_VEC_H__
#define __COLIBRI_SNS_MAT_VEC_H__

#include "lib/types.h"
#include "parity_ops.h"

/**
 * Represents math-vector of sz elements of type 'c2_parity_elem_t'
 */
struct c2_vector {
	uint32_t v_size;
	c2_parity_elem_t *v_vector;
};

C2_INTERNAL int c2_vector_init(struct c2_vector *v, uint32_t sz);
C2_INTERNAL void c2_vector_fini(struct c2_vector *v);
C2_INTERNAL void c2_vector_print(const struct c2_vector *vec);

/**
 * Gets element of vector 'v' in 'x' row
 *
 * @pre c2_vector_init(v) has been called
 * @pre x < v->v_size
 */
c2_parity_elem_t* c2_vector_elem_get(const struct c2_vector *v, uint32_t x);


/**
 * Represents math-matrix of m_width and m_height elements of type 'c2_parity_elem_t'
 */
struct c2_matrix {
	uint32_t m_width;
	uint32_t m_height;
	c2_parity_elem_t **m_matrix;
};

C2_INTERNAL int c2_matrix_init(struct c2_matrix *m, uint32_t w, uint32_t h);
C2_INTERNAL void c2_matrix_fini(struct c2_matrix *m);
C2_INTERNAL void c2_matrix_print(const struct c2_matrix *mat);

/**
 * Gets element of matrix 'm' in ('x','y') pos
 *
 * @pre c2_matrix_init(v) has been called
 * @pre x < m->m_width && y < m->m_height
 */
c2_parity_elem_t* c2_matrix_elem_get(const struct c2_matrix *m, uint32_t x, uint32_t y);



/**
 * Defines binary operation over matrix or vector element
 */
typedef c2_parity_elem_t (*c2_vector_matrix_binary_operator_t)(c2_parity_elem_t,
							       c2_parity_elem_t);

/**
 * Apply operation 'f' to element of vector 'v' in row 'row' with const 'c':
 * v[row] = f(v[row], c);
 *
 * @pre c2_vector_init(v) has been called
 */
C2_INTERNAL void c2_vector_row_operate(struct c2_vector *v, uint32_t row,
				       c2_parity_elem_t c,
				       c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row' with const 'c':
 * m(row,i) = f(m(row,i), c);
 *
 * @pre c2_vector_init(v) has been called
 */
C2_INTERNAL void c2_matrix_row_operate(struct c2_matrix *m, uint32_t row,
				       c2_parity_elem_t c,
				       c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every col element of matrix 'm' in col 'col' with const 'c':
 * m(i,col) = f(m(i,col), c);
 *
 * @pre c2_matrix_init(m) has been called
 */
C2_INTERNAL void c2_matrix_col_operate(struct c2_matrix *m, uint32_t col,
				       c2_parity_elem_t c,
				       c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of matrix 'm' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), f1(m(row1,i),c1));
 *
 * @pre c2_matrix_init(m) has been called
 */
C2_INTERNAL void c2_matrix_rows_operate(struct c2_matrix *m, uint32_t row0,
					uint32_t row1,
					c2_vector_matrix_binary_operator_t f0,
					c2_parity_elem_t c0,
					c2_vector_matrix_binary_operator_t f1,
					c2_parity_elem_t c1,
					c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of matrix 'm' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), m(row1,i));
 *
 * @pre c2_matrix_init(m)
 */
C2_INTERNAL void c2_matrix_rows_operate2(struct c2_matrix *m, uint32_t row0,
					 uint32_t row1,
					 c2_vector_matrix_binary_operator_t f0,
					 c2_parity_elem_t c0,
					 c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of matrix 'm' in row 'rowi' with const 'ci':
 * m(row0,i) = f(m(row0,i), f1(m(row1,i),c1));
 *
 * @pre c2_matrix_init(m) has been called
 */
C2_INTERNAL void c2_matrix_rows_operate1(struct c2_matrix *m, uint32_t row0,
					 uint32_t row1,
					 c2_vector_matrix_binary_operator_t f1,
					 c2_parity_elem_t c1,
					 c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of vector 'v' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of vector 'v' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), f1(m(row1,i),c1));
 *
 * @pre c2_vector_init(v) has been called
 */
C2_INTERNAL void c2_vector_rows_operate(struct c2_vector *v, uint32_t row0,
					uint32_t row1,
					c2_vector_matrix_binary_operator_t f0,
					c2_parity_elem_t c0,
					c2_vector_matrix_binary_operator_t f1,
					c2_parity_elem_t c1,
					c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of vector 'v' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of vector 'v' in row 'rowi' with const 'ci':
 * m(row0,i) = f(m(row0,i), f1(m(row1,i),c1));
 *
 * @pre c2_vector_init(v) has been called
 */
C2_INTERNAL void c2_vector_rows_operate1(struct c2_vector *v, uint32_t row0,
					 uint32_t row1,
					 c2_vector_matrix_binary_operator_t f1,
					 c2_parity_elem_t c1,
					 c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of vector 'v' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of vector 'v' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), m(row1,i));
 *
 * @pre c2_vector_init(v) has been called
 */
C2_INTERNAL void c2_vector_rows_operate2(struct c2_vector *v, uint32_t row0,
					 uint32_t row1,
					 c2_vector_matrix_binary_operator_t f0,
					 c2_parity_elem_t c0,
					 c2_vector_matrix_binary_operator_t f);

/**
 * Apply operation 'f' to every col element of matrix 'm' in col 'col0' with col 'col1' and
 * apply operation 'fi' to every col element of matrix 'm' in col 'coli' with const 'ci':
 * m(i,col0) = f(f0(m(i,col0),c0), f1(m(i,col1),c1));
 *
 * @pre c2_matrix_init(m) has been called
 */
C2_INTERNAL void c2_matrix_cols_operate(struct c2_matrix *m, uint32_t col0,
					uint32_t col1,
					c2_vector_matrix_binary_operator_t f0,
					c2_parity_elem_t c0,
					c2_vector_matrix_binary_operator_t f1,
					c2_parity_elem_t c1,
					c2_vector_matrix_binary_operator_t f);

/**
 * Swaps row 'r0' and row 'r1' each with other
 * @pre c2_matrix_init(m) has been called
 */
C2_INTERNAL void c2_matrix_swap_row(struct c2_matrix *m, uint32_t r0,
				    uint32_t r1);

/**
 * Swaps row 'r0' and row 'r1' each with other
 * @pre c2_vector_init(v) has been called
 */
C2_INTERNAL void c2_vector_swap_row(struct c2_vector *v, uint32_t r0,
				    uint32_t r1);

/**
 * Multiplies matrix 'm' on vector 'v'
 * Returns vector 'r'
 * @param mul - multiplicaton function
 * @param add - addition function
 *
 * @pre c2_vector_init(v) has been called
 * @pre c2_matrix_init(m) has been called
 * @pre c2_vec_init(r) has been called
 */
C2_INTERNAL void c2_matrix_vec_multiply(struct c2_matrix *m,
					struct c2_vector *v,
					struct c2_vector *r,
					c2_vector_matrix_binary_operator_t mul,
					c2_vector_matrix_binary_operator_t add);

/**
 * Returns submatrix of matrix 'mat' into 'submat', where 'x', 'y' - offsets
 * Uses 'submat' dimensions
 * @pre c2_matrix_init(mat) has been called
 * @pre c2_matrix_init(submat) has been called
 */
C2_INTERNAL void c2_matrix_get_submatrix(struct c2_matrix *mat,
					 struct c2_matrix *submat,
					 uint32_t x, uint32_t y);

/* __COLIBRI_SNS_MAT_VEC_H__ */
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
