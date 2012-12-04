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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNS
#include "lib/trace.h"        /* M0_LOG and M0_ENTRY */

#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/types.h"
#include "sns/matvec.h"

M0_INTERNAL int m0_vector_init(struct m0_vector *v, uint32_t sz)
{
	v->v_size = sz;
	M0_ALLOC_ARR(v->v_vector, sz);
	return v->v_vector == NULL ? -ENOMEM : 0;
}

M0_INTERNAL void m0_vector_fini(struct m0_vector *v)
{
	m0_free(v->v_vector);
	v->v_vector = NULL;
	v->v_size = 0;
}

M0_INTERNAL int m0_matrix_init(struct m0_matrix *m, uint32_t w, uint32_t h)
{
	uint32_t i;
	m->m_height = h;
	m->m_width = w;

	M0_ALLOC_ARR(m->m_matrix, h);
	if (m->m_matrix == NULL)
		return -ENOMEM;

	for (i = 0; i < h; ++i) {
		M0_ALLOC_ARR(m->m_matrix[i], w);
		if (m->m_matrix[i] == NULL) {
			m0_matrix_fini(m);
			return -ENOMEM;
		}
	}

	return 0;
}

M0_INTERNAL void m0_matrix_fini(struct m0_matrix *m)
{
	uint32_t i;

	for (i = 0; i < m->m_height; ++i) {
		m0_free(m->m_matrix[i]);
		m->m_matrix[i] = NULL;
	}

	m0_free(m->m_matrix);

	m->m_matrix = NULL;
	m->m_height = 0;
	m->m_width = 0;
}

m0_parity_elem_t* m0_vector_elem_get(const struct m0_vector *v, uint32_t x)
{
	M0_PRE(x < v->v_size);
	return &v->v_vector[x];
}

m0_parity_elem_t* m0_matrix_elem_get(const struct m0_matrix *m, uint32_t x, uint32_t y)
{
	M0_PRE(x < m->m_width);
	M0_PRE(y < m->m_height);
	return &m->m_matrix[y][x];
}

M0_INTERNAL void m0_matrix_print(const struct m0_matrix *mat)
{
	uint32_t x, y;
	M0_PRE(mat);

	M0_LOG(M0_DEBUG, "-----> mat %p\n", mat);

	for (y = 0; y < mat->m_height; ++y) {
                for (x = 0; x < mat->m_width; ++x)
			M0_LOG(M0_DEBUG, "%6d ", *m0_matrix_elem_get(mat, x, y));
		M0_LOG(M0_DEBUG, "\n");
	}

	M0_LOG(M0_DEBUG, "\n");
}

M0_INTERNAL void m0_vector_print(const struct m0_vector *vec)
{
	uint32_t x;
	M0_PRE(vec);

	M0_LOG(M0_DEBUG, "-----> vec %p\n", vec);
	for (x = 0; x < vec->v_size; ++x)
		M0_LOG(M0_DEBUG, "%6d\n", *m0_vector_elem_get(vec, x));
	M0_LOG(M0_DEBUG, "\n");
}

M0_INTERNAL void m0_matrix_swap_row(struct m0_matrix *m, uint32_t r0,
				    uint32_t r1)
{
	m0_parity_elem_t *temp;
	M0_PRE(r0 < m->m_height && r1 < m->m_height);

	temp = m->m_matrix[r0];
	m->m_matrix[r0] = m->m_matrix[r1];
	m->m_matrix[r1] = temp;
}

M0_INTERNAL void m0_vector_swap_row(struct m0_vector *v, uint32_t r0,
				    uint32_t r1)
{
	m0_parity_elem_t temp;
	M0_PRE(r0 < v->v_size && r1 < v->v_size);

	temp = v->v_vector[r0];
	v->v_vector[r0] = v->v_vector[r1];
	v->v_vector[r1] = temp;
}

M0_INTERNAL void m0_matrix_row_operate(struct m0_matrix *m, uint32_t row,
				       m0_parity_elem_t c,
				       m0_vector_matrix_binary_operator_t f)
{
	uint32_t x;
	M0_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
                m0_parity_elem_t *e = m0_matrix_elem_get(m, x, row);
                *e = f(*e, c);
	}
}

M0_INTERNAL void m0_vector_row_operate(struct m0_vector *v, uint32_t row,
				       m0_parity_elem_t c,
				       m0_vector_matrix_binary_operator_t f)
{
	m0_parity_elem_t *e = m0_vector_elem_get(v, row);
	M0_PRE(v);

	*e = f(*e, c);
}

M0_INTERNAL void m0_matrix_rows_operate(struct m0_matrix *m, uint32_t row0,
					uint32_t row1,
					m0_vector_matrix_binary_operator_t f0,
					m0_parity_elem_t c0,
					m0_vector_matrix_binary_operator_t f1,
					m0_parity_elem_t c1,
					m0_vector_matrix_binary_operator_t f)
{
	uint32_t x;
	M0_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
		m0_parity_elem_t *e0 = m0_matrix_elem_get(m, x, row0);
		m0_parity_elem_t *e1 = m0_matrix_elem_get(m, x, row1);
		*e0 = f(f0(*e0, c0), f1(*e1, c1));
	}
}

M0_INTERNAL void m0_matrix_rows_operate2(struct m0_matrix *m, uint32_t row0,
					 uint32_t row1,
					 m0_vector_matrix_binary_operator_t f0,
					 m0_parity_elem_t c0,
					 m0_vector_matrix_binary_operator_t f)
{
	uint32_t x;
	M0_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
		m0_parity_elem_t *e0 = m0_matrix_elem_get(m, x, row0);
		m0_parity_elem_t *e1 = m0_matrix_elem_get(m, x, row1);
		*e0 = f(f0(*e0, c0), *e1);
	}
}

M0_INTERNAL void m0_matrix_rows_operate1(struct m0_matrix *m, uint32_t row0,
					 uint32_t row1,
					 m0_vector_matrix_binary_operator_t f1,
					 m0_parity_elem_t c1,
					 m0_vector_matrix_binary_operator_t f)
{
	uint32_t x;
	M0_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
		m0_parity_elem_t *e0 = m0_matrix_elem_get(m, x, row0);
		m0_parity_elem_t *e1 = m0_matrix_elem_get(m, x, row1);
		*e0 = f(*e0, f1(*e1, c1));
	}
}

M0_INTERNAL void m0_matrix_cols_operate(struct m0_matrix *m, uint32_t col0,
					uint32_t col1,
					m0_vector_matrix_binary_operator_t f0,
					m0_parity_elem_t c0,
					m0_vector_matrix_binary_operator_t f1,
					m0_parity_elem_t c1,
					m0_vector_matrix_binary_operator_t f)
{
	uint32_t y;

	M0_PRE(m);

	for (y = 0; y < m->m_height; ++y) {
		m0_parity_elem_t *e0 = m0_matrix_elem_get(m, col0, y);
		m0_parity_elem_t *e1 = m0_matrix_elem_get(m, col1, y);
		*e0 = f(f0(*e0, c0), f1(*e1, c1));
	}
}

M0_INTERNAL void m0_matrix_col_operate(struct m0_matrix *m, uint32_t col,
				       m0_parity_elem_t c,
				       m0_vector_matrix_binary_operator_t f)
{
	uint32_t y;
	M0_PRE(m);

	for (y = 0; y < m->m_height; ++y) {
		m0_parity_elem_t *e = m0_matrix_elem_get(m, col, y);
		*e = f(*e, c);
	}
}

M0_INTERNAL void m0_vector_rows_operate(struct m0_vector *v, uint32_t row0,
					uint32_t row1,
					m0_vector_matrix_binary_operator_t f0,
					m0_parity_elem_t c0,
					m0_vector_matrix_binary_operator_t f1,
					m0_parity_elem_t c1,
					m0_vector_matrix_binary_operator_t f)
{
	m0_parity_elem_t *e0;
	m0_parity_elem_t *e1;

	M0_PRE(v);

	e0 = m0_vector_elem_get(v, row0);
	e1 = m0_vector_elem_get(v, row1);
	*e0 = f(f0(*e0, c0), f1(*e1, c1));
}

M0_INTERNAL void m0_vector_rows_operate1(struct m0_vector *v, uint32_t row0,
					 uint32_t row1,
					 m0_vector_matrix_binary_operator_t f1,
					 m0_parity_elem_t c1,
					 m0_vector_matrix_binary_operator_t f)
{
	m0_parity_elem_t *e0;
	m0_parity_elem_t *e1;

	M0_PRE(v);

	e0 = m0_vector_elem_get(v, row0);
	e1 = m0_vector_elem_get(v, row1);
	*e0 = f(*e0, f1(*e1, c1));
}

M0_INTERNAL void m0_vector_rows_operate2(struct m0_vector *v, uint32_t row0,
					 uint32_t row1,
					 m0_vector_matrix_binary_operator_t f0,
					 m0_parity_elem_t c0,
					 m0_vector_matrix_binary_operator_t f)
{
	m0_parity_elem_t *e0;
	m0_parity_elem_t *e1;

	M0_PRE(v);

	e0 = m0_vector_elem_get(v, row0);
	e1 = m0_vector_elem_get(v, row1);
	*e0 = f(f0(*e0, c0), *e1);
}


M0_INTERNAL void m0_matrix_vec_multiply(struct m0_matrix *m,
					struct m0_vector *v,
					struct m0_vector *r,
					m0_vector_matrix_binary_operator_t mul,
					m0_vector_matrix_binary_operator_t add)
{
	uint32_t y;
	uint32_t x;

        M0_PRE(v != NULL && m != NULL && r != NULL);
	M0_PRE(m->m_width == v->v_size && m->m_height == r->v_size);

	for (y = 0; y < m->m_height; ++y) {
		m0_parity_elem_t *er = m0_vector_elem_get(r, y);
		*er = M0_PARITY_ZERO;

                for (x = 0; x < m->m_width; ++x) {
			m0_parity_elem_t ev = *m0_vector_elem_get(v, x);
			m0_parity_elem_t em = *m0_matrix_elem_get(m, x, y);
			*er = add(*er, mul(ev, em));
		}
	}
}

M0_INTERNAL void m0_matrix_get_submatrix(struct m0_matrix *mat,
					 struct m0_matrix *submat,
					 uint32_t x_off, uint32_t y_off)
{
	uint32_t x;
	uint32_t y;

	M0_PRE(mat->m_width >= (submat->m_width + x_off)
               && mat->m_height >= (submat->m_height + y_off));

	for (y = 0; y < submat->m_height; ++y) {
		for (x = 0; x < submat->m_width; ++x) {
			*m0_matrix_elem_get(submat, x, y) =
                                *m0_matrix_elem_get(mat, x + x_off, y + y_off);
		}
        }
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
