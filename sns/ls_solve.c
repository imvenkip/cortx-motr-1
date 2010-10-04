#include "lib/assert.h"
#include "lib/types.h"

#define _C2_PARITY_USE_OPS_
#include "parity_ops.h"
#include "ls_solve.h"

void c2_linsys_init(struct c2_linsys* lynsys,
                    struct c2_matrix* mat,
                    struct c2_vector* vec,
                    struct c2_vector* res) {
	lynsys->l_mat = mat;
	lynsys->l_res = res;
	lynsys->l_vec = vec;
}

void c2_linsys_fini(struct c2_linsys* lynsys) {
	lynsys->l_mat = NULL;
	lynsys->l_res = NULL;
	lynsys->l_vec = NULL;
}

extern void __compiler_warnings_remove_in_ls_solve() {
	c2_parity_init();
	c2_parity_add(0,0);
}

static uint32_t find_max_row_ind_for_col(struct c2_matrix *m,
                                       uint32_t column) {
	uint32_t i = 0, ret = column;
	c2_parity_elem_t max_el = *c2_mat_elem_get(m, column, column);

	for (i = column + 1; i < m->m_height; ++i) {
		c2_parity_elem_t cur_el = *c2_mat_elem_get(m, column, i);

		if (c2_parity_lt(max_el, cur_el)) {
			max_el = cur_el;
			ret = i;
		}
	}

	return ret;
}

static void triangularize(struct c2_matrix *m, struct c2_vector *v) {
	uint32_t col = 0, current_row = 0;

	for (; col < m->m_width; ++col, ++current_row) {		
		// move row with max first elem to the top of matrix
		{
			uint32_t max_row = find_max_row_ind_for_col(m, col);
			c2_mat_swap_row(m, current_row, max_row);
			c2_vec_swap_row(v, current_row, max_row);		
		}

		// divide row to eliminate first element of the row
		{	
			c2_parity_elem_t divisor = *c2_mat_elem_get(m, col, current_row);
			c2_mat_row_operate(m, col, divisor, c2_parity_div);
			c2_vec_row_operate(v, col, divisor, c2_parity_div);
		}

		// eliminate first elements in other rows
		{
			uint32_t row = current_row + 1;
			for (; row < m->m_height; ++row) {
				c2_parity_elem_t mult = *c2_mat_elem_get(m, col, row);
				c2_mat_rows_operate1(m, row, col, c2_parity_mul, mult, c2_parity_sub);
				c2_vec_rows_operate1(v, row, col, c2_parity_mul, mult, c2_parity_sub);
			}
		}
	}
}

static void substitute(struct c2_matrix *m, struct c2_vector *v, struct c2_vector *r) {
	uint32_t col = m->m_width  - 1;
	uint32_t row = m->m_height - 1;

	for (; (int32_t)row >= 0; --row, --col) {
		c2_parity_elem_t *ev = c2_vec_elem_get(v, row);
		c2_parity_elem_t *em = c2_mat_elem_get(m, col, row);
		c2_parity_elem_t *er = c2_vec_elem_get(r, row);
		c2_parity_elem_t rhs = *ev;

		uint32_t pos = 1;
		for (; pos < m->m_height - row; ++pos) {
			c2_parity_elem_t *er_prev = c2_vec_elem_get(r, row + pos);
			c2_parity_elem_t *em_prev = c2_mat_elem_get(m, col + pos, row);
			rhs = c2_parity_sub(rhs, c2_parity_mul(*er_prev, *em_prev));
		}
		
		*er = c2_parity_div(rhs, *em);
	}
}

void c2_linsys_solve(struct c2_linsys *lynsys) {
	struct c2_matrix *m = lynsys->l_mat;
	struct c2_vector *v = lynsys->l_vec;
	struct c2_vector *r = lynsys->l_res;

	C2_PRE(m != NULL && v != NULL && r != NULL);
	C2_PRE(m->m_height > 0 && m->m_width > 0);
	C2_PRE(m->m_width == m->m_height && r->v_size == v->v_size && v->v_size == m->m_width);

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
