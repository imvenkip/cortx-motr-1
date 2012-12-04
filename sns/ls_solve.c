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


#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/types.h"

#include "sns/parity_ops.h"
#include "sns/ls_solve.h"

M0_INTERNAL void m0_linsys_init(struct m0_linsys *lynsys,
				struct m0_matrix *m,
				struct m0_vector *v, struct m0_vector *r)
{
	M0_PRE(m != NULL && v != NULL && r != NULL);
	M0_PRE(m->m_height > 0 && m->m_width > 0);
	M0_PRE(m->m_width == m->m_height && r->v_size == v->v_size && v->v_size == m->m_width);

	lynsys->l_mat = m;
	lynsys->l_res = r;
	lynsys->l_vec = v;
}

M0_INTERNAL void m0_linsys_fini(struct m0_linsys *lynsys)
{
	lynsys->l_mat = NULL;
	lynsys->l_res = NULL;
	lynsys->l_vec = NULL;
}

/* Works with partially processed matrix */
static uint32_t find_max_row_index_for_col(struct m0_matrix *m,
					   uint32_t column)
{
	uint32_t i = 0;
	uint32_t ret = column;
	m0_parity_elem_t max_el = *m0_matrix_elem_get(m, column, column);

	for (i = column + 1; i < m->m_height; ++i) {
		m0_parity_elem_t cur_el = *m0_matrix_elem_get(m, column, i);

		if (m0_parity_lt(max_el, cur_el)) {
			max_el = cur_el;
			ret = i;
		}
	}

	return ret;
}

static void triangularize(struct m0_matrix *m, struct m0_vector *v)
{
	uint32_t col = 0;
	uint32_t current_row = 0;

	for (; col < m->m_width; ++col, ++current_row) {
		uint32_t max_row;
		m0_parity_elem_t divisor;
		uint32_t row;

		/* move row with max first elem to the top of matrix */
		max_row = find_max_row_index_for_col(m, col);
		m0_matrix_swap_row(m, current_row, max_row);
		m0_vector_swap_row(v, current_row, max_row);

		/* divide row to eliminate first element of the row */
		divisor = *m0_matrix_elem_get(m, col, current_row);
		m0_matrix_row_operate(m, col, divisor, m0_parity_div);
		m0_vector_row_operate(v, col, divisor, m0_parity_div);

		/* eliminate first elements in other rows */
		row = current_row + 1;
		for (; row < m->m_height; ++row) {
			m0_parity_elem_t mult = *m0_matrix_elem_get(m, col, row);
			m0_matrix_rows_operate1(m, row, col, m0_parity_mul, mult, m0_parity_sub);
			m0_vector_rows_operate1(v, row, col, m0_parity_mul, mult, m0_parity_sub);
		}
	}
}

static void substitute(struct m0_matrix *m, struct m0_vector *v, struct m0_vector *r)
{
	uint32_t col = m->m_width  - 1;
	uint32_t row = m->m_height - 1;

	for (; (int32_t)row >= 0; --row, --col) {
		uint32_t pos;
		m0_parity_elem_t *ev = m0_vector_elem_get(v, row);
		m0_parity_elem_t *em = m0_matrix_elem_get(m, col, row);
		m0_parity_elem_t *er = m0_vector_elem_get(r, row);
		m0_parity_elem_t rhs = *ev;

		for (pos = 1; pos < m->m_height - row; ++pos) {
			m0_parity_elem_t *er_prev = m0_vector_elem_get(r, row + pos);
			m0_parity_elem_t *em_prev = m0_matrix_elem_get(m, col + pos, row);
			rhs = m0_parity_sub(rhs, m0_parity_mul(*er_prev, *em_prev));
		}

		*er = m0_parity_div(rhs, *em);
	}
}

M0_INTERNAL void m0_linsys_solve(struct m0_linsys *lynsys)
{
	struct m0_matrix *m = lynsys->l_mat;
	struct m0_vector *v = lynsys->l_vec;
	struct m0_vector *r = lynsys->l_res;

	triangularize(m, v);
	substitute(m, v, r);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
