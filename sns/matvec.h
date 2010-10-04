/* -*- C -*- */

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

int c2_vec_init(struct c2_vector *v, uint32_t sz);
void c2_vec_fini(struct c2_vector *v);
void c2_vec_print(struct c2_vector *vec);

/**
 * Gets element of vector 'v' in 'x' row
 *
 * @pre c2_vec_init(v)
 */
c2_parity_elem_t* c2_vec_elem_get(struct c2_vector *v, uint32_t x);


/**
 * Represents math-matrix of m_width and m_height elements of type 'c2_parity_elem_t'
 */
struct c2_matrix {
	uint32_t m_width, m_height;
	c2_parity_elem_t **m_matrix;
};

int c2_mat_init(struct c2_matrix *m, uint32_t w, uint32_t h);
void c2_mat_fini(struct c2_matrix *m);
void c2_mat_print(struct c2_matrix *mat);

/**
 * Gets element of vector 'v' in 'x' row
 *
 * @pre c2_vec_init(v)
 */
c2_parity_elem_t* c2_mat_elem_get(struct c2_matrix *m, uint32_t x, uint32_t y);



/**
 * Defines binary operation over matrix or vector element
 */
typedef c2_parity_elem_t (*c2_vec_mat_binary_operator_t)(c2_parity_elem_t,
                                                          c2_parity_elem_t);

/**
 * Apply operation 'f' to element of vector 'v' in row 'row' with const 'c':
 * v[row] = f(v[row], c);
 *
 * @pre c2_vec_init(v)
 */
void c2_vec_row_operate(struct c2_vector *v, uint32_t row, c2_parity_elem_t c,
                        c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row' with const 'c':
 * m(row,i) = f(m(row,i), c);
 *
 * @pre c2_vec_init(v)
 */
void c2_mat_row_operate(struct c2_matrix *m, uint32_t row, c2_parity_elem_t c,
                        c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every col element of matrix 'm' in col 'col' with const 'c':
 * m(i,col) = f(m(i,col), c);
 *
 * @pre c2_mat_init(m)
 */
void c2_mat_col_operate(struct c2_matrix *m, uint32_t col, c2_parity_elem_t c,
                        c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of matrix 'm' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), f1(m(row1,i),c1));
 *
 * @pre c2_mat_init(m)
 */
void c2_mat_rows_operate(struct c2_matrix *m, uint32_t row0, uint32_t row1,
                         c2_vec_mat_binary_operator_t f0, c2_parity_elem_t c0,
                         c2_vec_mat_binary_operator_t f1, c2_parity_elem_t c1,
                         c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of matrix 'm' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), m(row1,i));
 *
 * @pre c2_mat_init(m)
 */
void c2_mat_rows_operate2(struct c2_matrix *m, uint32_t row0, uint32_t row1,
			  c2_vec_mat_binary_operator_t f0, c2_parity_elem_t c0,
			  c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of matrix 'm' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of matrix 'm' in row 'rowi' with const 'ci':
 * m(row0,i) = f(m(row0,i), f1(m(row1,i),c1));
 *
 * @pre c2_mat_init(m)
 */
void c2_mat_rows_operate1(struct c2_matrix *m, uint32_t row0, uint32_t row1,
			  c2_vec_mat_binary_operator_t f1, c2_parity_elem_t c1,
			  c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of vector 'v' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of vector 'v' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), f1(m(row1,i),c1));
 *
 * @pre c2_vec_init(v)
 */
void c2_vec_rows_operate(struct c2_vector *v, uint32_t row0, uint32_t row1,
                         c2_vec_mat_binary_operator_t f0, c2_parity_elem_t c0,
                         c2_vec_mat_binary_operator_t f1, c2_parity_elem_t c1,
                         c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of vector 'v' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of vector 'v' in row 'rowi' with const 'ci':
 * m(row0,i) = f(m(row0,i), f1(m(row1,i),c1));
 *
 * @pre c2_vec_init(v)
 */
void c2_vec_rows_operate1(struct c2_vector *v, uint32_t row0, uint32_t row1,
			  c2_vec_mat_binary_operator_t f1, c2_parity_elem_t c1,
			  c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every row element of vector 'v' in row 'row0' with row 'row1' and
 * apply operation 'fi' to every row element of vector 'v' in row 'rowi' with const 'ci':
 * m(row0,i) = f(f0(m(row0,i),c0), m(row1,i));
 *
 * @pre c2_vec_init(v)
 */
void c2_vec_rows_operate2(struct c2_vector *v, uint32_t row0, uint32_t row1,
			  c2_vec_mat_binary_operator_t f0, c2_parity_elem_t c0,
			  c2_vec_mat_binary_operator_t f);

/**
 * Apply operation 'f' to every col element of matrix 'm' in col 'col0' with col 'col1' and
 * apply operation 'fi' to every col element of matrix 'm' in col 'coli' with const 'ci':
 * m(i,col0) = f(f0(m(i,col0),c0), f1(m(i,col1),c1));
 *
 * @pre c2_mat_init(m)
 */
void c2_mat_cols_operate(struct c2_matrix *m, uint32_t col0, uint32_t col1,
                         c2_vec_mat_binary_operator_t f0, c2_parity_elem_t c0,
                         c2_vec_mat_binary_operator_t f1, c2_parity_elem_t c1,
                         c2_vec_mat_binary_operator_t f);

/**
 * Swaps row 'r0' and row 'r1' each with other
 * @pre c2_mat_init(m)
 */
void c2_mat_swap_row(struct c2_matrix *m, uint32_t r0, uint32_t r1);

/**
 * Swaps row 'r0' and row 'r1' each with other
 * @pre c2_vec_init(v)
 */
void c2_vec_swap_row(struct c2_vector *v, uint32_t r0, uint32_t r1);

/**
 * Multiplies matrix 'm' on vector 'v'
 * Returns vector 'r'
 * @param mul - multiplicaton function
 * @param add - addition function
 *
 * @pre c2_vec_init(v) && c2_vec_init(r) && c2_mat_init(m)
 */
void c2_mat_vec_multiply(struct c2_matrix *m, struct c2_vector *v, struct c2_vector *r,
                         c2_vec_mat_binary_operator_t mul,
                         c2_vec_mat_binary_operator_t add);

/**
 * Returns submatrix of matrix 'mat' into 'submat', where 'x', 'y' - offsets
 * Uses 'submat' dimensions
 * @pre c2_mat_init(mat) && c2_mat_init(submat)
 */
void c2_mat_get_submatrix(struct c2_matrix *mat,
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
