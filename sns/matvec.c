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
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/types.h"

#ifdef __KERNEL__
#define DBG(fmt, args...) printk("%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#else
# include <stdio.h>
# include <stdlib.h>
#define DBG(fmt, args...) printf("%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#endif

#include "sns/matvec.h"

int c2_vector_init(struct c2_vector *v, uint32_t sz)
{
	v->v_size = sz;
	C2_ALLOC_ARR(v->v_vector, sz);
	return v->v_vector == NULL ? -ENOMEM : 0;
}

void c2_vector_fini(struct c2_vector *v)
{
	c2_free(v->v_vector);
	v->v_vector = NULL;
	v->v_size = 0;
}

int c2_matrix_init(struct c2_matrix *m, uint32_t w, uint32_t h)
{
	uint32_t i;
	m->m_height = h;
	m->m_width = w;

	C2_ALLOC_ARR(m->m_matrix, h);
	if (m->m_matrix == NULL)
		return -ENOMEM;

	for (i = 0; i < h; ++i) {
		C2_ALLOC_ARR(m->m_matrix[i], w);
		if (m->m_matrix[i] == NULL) {
			c2_matrix_fini(m);
			return -ENOMEM;
		}
	}

	return 0;
}

void c2_matrix_fini(struct c2_matrix *m)
{
	uint32_t i;

	for (i = 0; i < m->m_height; ++i) {
		c2_free(m->m_matrix[i]);
		m->m_matrix[i] = NULL;
	}

	c2_free(m->m_matrix);

	m->m_matrix = NULL;
	m->m_height = 0;
	m->m_width = 0;
}

c2_parity_elem_t* c2_vector_elem_get(const struct c2_vector *v, uint32_t x)
{
	C2_PRE(x < v->v_size);
	return &v->v_vector[x];
}

c2_parity_elem_t* c2_matrix_elem_get(const struct c2_matrix *m, uint32_t x, uint32_t y)
{
	C2_PRE(x < m->m_width);
	C2_PRE(y < m->m_height);
	return &m->m_matrix[y][x];
}

void c2_matrix_print(const struct c2_matrix *mat)
{
	uint32_t x, y;
	C2_PRE(mat);

	DBG("-----> mat %p\n", mat);
        
	for (y = 0; y < mat->m_height; ++y) {
                for (x = 0; x < mat->m_width; ++x)
			DBG("%6d ", *c2_matrix_elem_get(mat, x, y));
		DBG("\n");
	}
        
	DBG("\n");
}

void c2_vector_print(const struct c2_vector *vec)
{
	uint32_t x;
	C2_PRE(vec);

	DBG("-----> vec %p\n", vec);
	for (x = 0; x < vec->v_size; ++x)
		DBG("%6d\n", *c2_vector_elem_get(vec, x));
	DBG("\n");
}

void c2_matrix_swap_row(struct c2_matrix *m, uint32_t r0, uint32_t r1)
{
	c2_parity_elem_t *temp;
	C2_PRE(r0 < m->m_height && r1 < m->m_height);

	temp = m->m_matrix[r0];
	m->m_matrix[r0] = m->m_matrix[r1];
	m->m_matrix[r1] = temp;
}

void c2_vector_swap_row(struct c2_vector *v, uint32_t r0, uint32_t r1)
{
	c2_parity_elem_t temp;
	C2_PRE(r0 < v->v_size && r1 < v->v_size);

	temp = v->v_vector[r0];
	v->v_vector[r0] = v->v_vector[r1];
	v->v_vector[r1] = temp;
}

void c2_matrix_row_operate(struct c2_matrix *m, uint32_t row, c2_parity_elem_t c,
			   c2_vector_matrix_binary_operator_t f)
{
	uint32_t x;
	C2_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
                c2_parity_elem_t *e = c2_matrix_elem_get(m, x, row);
                *e = f(*e, c);
	}
}

void c2_vector_row_operate(struct c2_vector *v, uint32_t row, c2_parity_elem_t c,
			   c2_vector_matrix_binary_operator_t f)
{
	c2_parity_elem_t *e = c2_vector_elem_get(v, row);
	C2_PRE(v);

	*e = f(*e, c);
}

void c2_matrix_rows_operate(struct c2_matrix *m, uint32_t row0, uint32_t row1,
			    c2_vector_matrix_binary_operator_t f0, c2_parity_elem_t c0,
			    c2_vector_matrix_binary_operator_t f1, c2_parity_elem_t c1,
			    c2_vector_matrix_binary_operator_t f)
{
	uint32_t x;	
	C2_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
		c2_parity_elem_t *e0 = c2_matrix_elem_get(m, x, row0);
		c2_parity_elem_t *e1 = c2_matrix_elem_get(m, x, row1);
		*e0 = f(f0(*e0, c0), f1(*e1, c1));
	}	
}

void c2_matrix_rows_operate2(struct c2_matrix *m, uint32_t row0, uint32_t row1,
			     c2_vector_matrix_binary_operator_t f0, c2_parity_elem_t c0,
			     c2_vector_matrix_binary_operator_t f)
{
	uint32_t x;	
	C2_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
		c2_parity_elem_t *e0 = c2_matrix_elem_get(m, x, row0);
		c2_parity_elem_t *e1 = c2_matrix_elem_get(m, x, row1);
		*e0 = f(f0(*e0, c0), *e1);
	}	
}

void c2_matrix_rows_operate1(struct c2_matrix *m, uint32_t row0, uint32_t row1,
			     c2_vector_matrix_binary_operator_t f1, c2_parity_elem_t c1,
			     c2_vector_matrix_binary_operator_t f)
{
	uint32_t x;	
	C2_PRE(m);

	for (x = 0; x < m->m_width; ++x) {
		c2_parity_elem_t *e0 = c2_matrix_elem_get(m, x, row0);
		c2_parity_elem_t *e1 = c2_matrix_elem_get(m, x, row1);
		*e0 = f(*e0, f1(*e1, c1));
	}	
}

void c2_matrix_cols_operate(struct c2_matrix *m, uint32_t col0, uint32_t col1,
			    c2_vector_matrix_binary_operator_t f0, c2_parity_elem_t c0,
			    c2_vector_matrix_binary_operator_t f1, c2_parity_elem_t c1,
			    c2_vector_matrix_binary_operator_t f)
{
	uint32_t y;

	C2_PRE(m);

	for (y = 0; y < m->m_height; ++y) {
		c2_parity_elem_t *e0 = c2_matrix_elem_get(m, col0, y);
		c2_parity_elem_t *e1 = c2_matrix_elem_get(m, col1, y);
		*e0 = f(f0(*e0, c0), f1(*e1, c1));
	}
}

void c2_matrix_col_operate(struct c2_matrix *m, uint32_t col, c2_parity_elem_t c,
			   c2_vector_matrix_binary_operator_t f)
{
	uint32_t y;
	C2_PRE(m);

	for (y = 0; y < m->m_height; ++y) {
		c2_parity_elem_t *e = c2_matrix_elem_get(m, col, y);
		*e = f(*e, c);
	}
}

void c2_vector_rows_operate(struct c2_vector *v, uint32_t row0, uint32_t row1,
			    c2_vector_matrix_binary_operator_t f0, c2_parity_elem_t c0,
			    c2_vector_matrix_binary_operator_t f1, c2_parity_elem_t c1,
			    c2_vector_matrix_binary_operator_t f)
{
	c2_parity_elem_t *e0;
	c2_parity_elem_t *e1;

	C2_PRE(v);

	e0 = c2_vector_elem_get(v, row0);
	e1 = c2_vector_elem_get(v, row1);
	*e0 = f(f0(*e0, c0), f1(*e1, c1));
}

void c2_vector_rows_operate1(struct c2_vector *v, uint32_t row0, uint32_t row1,
			     c2_vector_matrix_binary_operator_t f1, c2_parity_elem_t c1,
			     c2_vector_matrix_binary_operator_t f)
{
	c2_parity_elem_t *e0;
	c2_parity_elem_t *e1;

	C2_PRE(v);

	e0 = c2_vector_elem_get(v, row0);
	e1 = c2_vector_elem_get(v, row1);
	*e0 = f(*e0, f1(*e1, c1));
}

void c2_vector_rows_operate2(struct c2_vector *v, uint32_t row0, uint32_t row1,
			     c2_vector_matrix_binary_operator_t f0, c2_parity_elem_t c0,
			     c2_vector_matrix_binary_operator_t f)
{
	c2_parity_elem_t *e0;
	c2_parity_elem_t *e1;

	C2_PRE(v);

	e0 = c2_vector_elem_get(v, row0);
	e1 = c2_vector_elem_get(v, row1);
	*e0 = f(f0(*e0, c0), *e1);
}


void c2_matrix_vec_multiply(struct c2_matrix *m, struct c2_vector *v, struct c2_vector *r,
			    c2_vector_matrix_binary_operator_t mul,
			    c2_vector_matrix_binary_operator_t add)
{
	uint32_t y;
	uint32_t x;

        C2_PRE(v != NULL && m != NULL && r != NULL);
	C2_PRE(m->m_width == v->v_size && m->m_height == r->v_size);

	for (y = 0; y < m->m_height; ++y) {
		c2_parity_elem_t *er = c2_vector_elem_get(r, y);
		*er = C2_PARITY_ZERO;

                for (x = 0; x < m->m_width; ++x) {
			c2_parity_elem_t ev = *c2_vector_elem_get(v, x);
			c2_parity_elem_t em = *c2_matrix_elem_get(m, x, y);
			*er = add(*er, mul(ev, em));
		}
	}
}

void c2_matrix_get_submatrix(struct c2_matrix *mat, struct c2_matrix *submat,
			     uint32_t x_off, uint32_t y_off)
{
	uint32_t x;
	uint32_t y;
        
	C2_PRE(mat->m_width >= (submat->m_width + x_off)
               && mat->m_height >= (submat->m_height + y_off));
	
	for (y = 0; y < submat->m_height; ++y) {
		for (x = 0; x < submat->m_width; ++x) {
			*c2_matrix_elem_get(submat, x, y) =
                                *c2_matrix_elem_get(mat, x + x_off, y + y_off);
		}
        }
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
