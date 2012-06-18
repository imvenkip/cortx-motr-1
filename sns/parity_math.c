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
 * Revision       : Anup Barve <Anup_Barve@xyratex.com>
 * Revision date  : 06/14/2011
 */

#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h" /* SET0() */
#include "lib/types.h"

#include "sns/parity_ops.h"
#include "sns/parity_math.h"

enum {
	C2_SNS_PARITY_MATH_DATA_BLOCKS_MAX = 1 << (C2_PARITY_GALOIS_W - 1)
};

/* c2_parity_* are to much eclectic. just more simple names. */
static int gadd(int x, int y)
{
	return c2_parity_add(x, y);
}

static int gsub(int x, int y)
{
	return c2_parity_sub(x, y);
}

static int gmul(int x, int y)
{
	return c2_parity_mul(x, y);
}

static int gdiv(int x, int y)
{
	return c2_parity_div(x, y);
}

static int gpow(int x, int p)
{
	int ret = x;
	int i = 1;

	if (p == 0)
		return 1;

	for (i = 1; i < p; ++i)
		ret = gmul(ret, x);

	return ret;
}

/* Fills vandermonde matrix with initial values. */
static int vandmat_init(struct c2_matrix *m, uint32_t data_count,
			uint32_t parity_count)
{
	int ret;
	uint32_t y;
	uint32_t x;
	uint32_t mat_height = data_count + parity_count;
	uint32_t mat_width = data_count;

	ret = c2_matrix_init(m, mat_width, mat_height);
	if (ret < 0)
		return ret;

	for (y = 0; y < mat_height; ++y)
		for (x = 0; x < mat_width; ++x) {
			*c2_matrix_elem_get(m, x, y) = gpow(y, x);
		}

	return ret;
}

static void vandmat_fini(struct c2_matrix *mat)
{
	c2_matrix_fini(mat);
}

/* Checks if row has only one element equals 1, and 0 others */
static bool check_row_is_id(struct c2_matrix *m, uint32_t row)
{
	bool ret = true;
	uint32_t x;

	for (x = 0; x < m->m_width && ret; ++x) {
		ret &= (row == x) == *c2_matrix_elem_get(m, x, row);
	}

	return ret;
}

/* Normalizes vandermonde matrix, upper part of which becomes identity matrix
 * in case of success. */
static int vandmat_norm(struct c2_matrix *m)
{
	uint32_t y;

	for (y = 0; y < m->m_width; ++y) {
		uint32_t x = 0;
		c2_matrix_col_operate(m, y, *c2_matrix_elem_get(m, y, y), gdiv);

		for (x = 0; x < m->m_width; ++x)
			if (x != y)
				c2_matrix_cols_operate(m, x, y, gsub, 0, gmul,
                                            *c2_matrix_elem_get(m, x, y), gsub);

		/* Assert if units are not configured properly. */
		C2_ASSERT(check_row_is_id(m, y));
	}

	return 0;
}

void c2_parity_math_fini(struct c2_parity_math *math)
{
	if (math->pmi_parity_algo == C2_PARITY_CAL_ALGO_REED_SOLOMON) {
		vandmat_fini(&math->pmi_vandmat);
		c2_matrix_fini(&math->pmi_vandmat_parity_slice);
		c2_vector_fini(&math->pmi_data);
		c2_vector_fini(&math->pmi_parity);

		c2_linsys_fini(&math->pmi_sys);
		c2_matrix_fini(&math->pmi_sys_mat);
		c2_vector_fini(&math->pmi_sys_vec);
		c2_vector_fini(&math->pmi_sys_res);

		c2_parity_fini();
	}
}

int  c2_parity_math_init(struct c2_parity_math *math,
			 uint32_t data_count, uint32_t parity_count)
{
	int ret;

	C2_PRE(data_count >= 1);
	C2_PRE(parity_count >= 1);
	C2_PRE(data_count >= parity_count);
	C2_PRE(data_count <= C2_SNS_PARITY_MATH_DATA_BLOCKS_MAX);

	C2_SET0(math);

	math->pmi_data_count   = data_count;
	math->pmi_parity_count = parity_count;

        if (parity_count == 1) {
		math->pmi_parity_algo = C2_PARITY_CAL_ALGO_XOR;
		return 0;
	} else {

		math->pmi_parity_algo = C2_PARITY_CAL_ALGO_REED_SOLOMON;
		/* init galois, only first call makes initialization,
		 * no deinitialization needed. */
		c2_parity_init();

		ret = vandmat_init(&math->pmi_vandmat, data_count,
				   parity_count);
		if (ret < 0)
			goto handle_error;

		ret = vandmat_norm(&math->pmi_vandmat);
		if (ret < 0)
			goto handle_error;

		ret = c2_matrix_init(&math->pmi_vandmat_parity_slice,
				     data_count, parity_count);
		if (ret < 0)
			goto handle_error;

		c2_matrix_get_submatrix(&math->pmi_vandmat,
				        &math->pmi_vandmat_parity_slice, 0,
					data_count);

		ret = c2_vector_init(&math->pmi_data, data_count);
		if (ret < 0)
			goto handle_error;

		ret = c2_vector_init(&math->pmi_parity, parity_count);
		if (ret < 0)
			goto handle_error;

		ret = c2_vector_init(&math->pmi_sys_vec, math->pmi_data.v_size);
		if (ret < 0)
			goto handle_error;

		ret = c2_vector_init(&math->pmi_sys_res, math->pmi_data.v_size);
		if (ret < 0)
			goto handle_error;

		ret = c2_matrix_init(&math->pmi_sys_mat, math->pmi_data.v_size,
				     math->pmi_data.v_size);
		if (ret < 0)
			goto handle_error;
	}
	return ret;
 handle_error:
	c2_parity_math_fini(math);
	return ret;
}

static void parity_math_xor_calculate(struct c2_parity_math *math,
				      struct c2_buf *data,
				      struct c2_buf *parity)
{
        uint32_t          ei; /* block element index. */
        uint32_t          ui; /* unit index. */
        uint32_t          block_size = data[0].b_nob;
	c2_parity_elem_t  pe;

        for (ui = 0; ui < math->pmi_data_count; ++ui)
                C2_ASSERT(block_size == data[ui].b_nob);

        for (ui = 0; ui < math->pmi_parity_count; ++ui)
                C2_ASSERT(block_size == parity[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		pe = 0;
		for (ui = 0; ui < math->pmi_data_count; ++ui) {
			pe = pe ^
			     (c2_parity_elem_t)((uint8_t*)data[ui].b_addr)[ei];
		}
		((uint8_t*)parity[0].b_addr)[ei] = pe;
	}

}

static void parity_math_reed_solomon_encode(struct c2_parity_math *math,
					    struct c2_buf *data,
					    struct c2_buf *parity)
{
	uint32_t ei; /* block element index. */
	uint32_t ui; /* unit index. */
	uint32_t block_size = data[0].b_nob;

	for (ui = 0; ui < math->pmi_data_count; ++ui)
		C2_ASSERT(block_size == data[ui].b_nob);

	for (ui = 0; ui < math->pmi_parity_count; ++ui)
		C2_ASSERT(block_size == parity[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			*c2_vector_elem_get(&math->pmi_data, ui) =
				((uint8_t*)data[ui].b_addr)[ei];

		c2_matrix_vec_multiply(&math->pmi_vandmat_parity_slice,
				&math->pmi_data, &math->pmi_parity, gmul, gadd);

		for (ui = 0; ui < math->pmi_parity_count; ++ui)
			((uint8_t*)parity[ui].b_addr)[ei] =
				*c2_vector_elem_get(&math->pmi_parity, ui);
	}
}

void c2_parity_math_calculate(struct c2_parity_math *math,
			      struct c2_buf *data,
			      struct c2_buf *parity)
{
	if (math->pmi_parity_algo == C2_PARITY_CAL_ALGO_XOR)
		parity_math_xor_calculate(math, data, parity);
	else if (math->pmi_parity_algo == C2_PARITY_CAL_ALGO_REED_SOLOMON)
		parity_math_reed_solomon_encode(math, data, parity);
}

void c2_parity_math_refine(struct c2_parity_math *math,
			   struct c2_buf *data,
			   struct c2_buf *parity,
			   uint32_t data_ind_changed)
{
	/* for simplicity: */
	c2_parity_math_calculate(math, data, parity);
}

/* Counts number of fialed blocks. */
static uint32_t fails_count(uint8_t *fail, uint32_t unit_count)
{
	uint32_t x;
	uint32_t count = 0;

	for (x = 0; x < unit_count; ++x)
		count += !!fail[x];

	return count;
}

/* Fills 'mat' and 'vec' with data passed to recovery algorithm. */
static void recovery_data_fill(struct c2_parity_math *math,
			       uint8_t *fail, uint32_t unit_count, /* in. */
			       struct c2_matrix *mat,
			       struct c2_vector *vec) /* out. */
{
	uint32_t f;
	uint32_t y = 0;
	uint32_t x;

	for (f = 0; f < unit_count; ++f) {
		/* if (block is ok) and
		 * (not enough equations to solve system). */
		if (!fail[f] && y < vec->v_size) {
			/* copy vec. */
			*c2_vector_elem_get(vec, y) = f < math->pmi_data_count
				? *c2_vector_elem_get(&math->pmi_data, f)
				: *c2_vector_elem_get(&math->pmi_parity,
						f - math->pmi_data_count);
			/* copy mat. */
			for (x = 0; x < mat->m_width; ++x)
				*c2_matrix_elem_get(mat, x, y) =
					*c2_matrix_elem_get(&math->pmi_vandmat,
							x, f);

			++y;
		}
	}
}

/* Updates internal structures of 'math' with recovered data. */
static void parity_math_recover(struct c2_parity_math *math,
				uint8_t *fail, uint32_t unit_count)
{
	struct c2_matrix *mat = &math->pmi_sys_mat;
	struct c2_vector *vec = &math->pmi_sys_vec;
	struct c2_vector *res = &math->pmi_sys_res;
	struct c2_linsys *sys = &math->pmi_sys;

	recovery_data_fill(math, fail, unit_count, mat, vec);

	c2_linsys_init(sys, mat, vec, res);
	c2_linsys_solve(sys);
}

static void xor_recover(struct c2_parity_math *math,
			struct c2_buf *data,
			struct c2_buf *parity,
			struct c2_buf *fails)
{
	uint32_t          ei; /* block element index. */
	uint32_t          ui; /* unit index. */
	uint8_t          *fail;
	uint32_t          fail_count;
	uint32_t          unit_count;
	uint32_t          block_size = data[0].b_nob;
	c2_parity_elem_t  pe;
	int		  fail_index;

	unit_count = math->pmi_data_count + math->pmi_parity_count;
	fail = (uint8_t*) fails->b_addr;
	fail_count = fails_count(fail, unit_count);

	C2_ASSERT(fail_count == 1);
	C2_ASSERT(fail_count <= math->pmi_parity_count);

        for (ui = 0; ui < math->pmi_data_count; ++ui)
                C2_ASSERT(block_size == data[ui].b_nob);

        for (ui = 0; ui < math->pmi_parity_count; ++ui)
                C2_ASSERT(block_size == parity[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		pe = 0;
                for (ui = 0; ui < math->pmi_data_count; ++ui) {
		    if (fail[ui] != 1) {
		    	pe = pe ^
			     (c2_parity_elem_t)((uint8_t*)data[ui].b_addr)[ei];
		    } else
			fail_index = ui;
                }
		/* Now ui points to parity block, test if it was failed. */
		if (fail[ui] != 1)
			pe = pe ^ ((uint8_t*)parity[0].b_addr)[ei];
		else /* Parity was lost, so recover it. */
			((uint8_t*)parity[0].b_addr)[ei] = pe;

		((uint8_t*)data[fail_index].b_addr)[ei] = pe;
        }

	/* recalculate parity. */
	c2_parity_math_calculate(math, data, parity);
}

static void reed_solomon_recover(struct c2_parity_math *math,
                            	 struct c2_buf *data,
				 struct c2_buf *parity,
				 struct c2_buf *fails)
{
	uint32_t ei; /* block element index. */
	uint32_t ui; /* unit index. */
	uint8_t *fail;
	uint32_t fail_count;
	uint32_t unit_count = math->pmi_data_count + math->pmi_parity_count;
	uint32_t block_size = data[0].b_nob;

	fail = (uint8_t*) fails->b_addr;
	fail_count = fails_count(fail, unit_count);

	C2_ASSERT(fail_count > 0);
	C2_ASSERT(fail_count <= math->pmi_parity_count);

	for (ui = 0; ui < math->pmi_data_count; ++ui)
		C2_ASSERT(block_size == data[ui].b_nob);

	for (ui = 0; ui < math->pmi_parity_count; ++ui)
		C2_ASSERT(block_size == parity[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		struct c2_vector *recovered = &math->pmi_sys_res;

		/* load data and parity. */
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			*c2_vector_elem_get(&math->pmi_data, ui) =
				((uint8_t*)data  [ui].b_addr)[ei];

		for (ui = 0; ui < math->pmi_parity_count; ++ui)
			*c2_vector_elem_get(&math->pmi_parity, ui) =
				((uint8_t*)parity[ui].b_addr)[ei];

		/* recover data. */
		parity_math_recover(math, fail, unit_count);
		/* store data. */
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			((uint8_t*)data[ui].b_addr)[ei] =
				*c2_vector_elem_get(recovered, ui);
	}

	/* recalculate parity. */
	c2_parity_math_calculate(math, data, parity);
}

void c2_parity_math_recover(struct c2_parity_math *math,
			    struct c2_buf *data,
			    struct c2_buf *parity,
			    struct c2_buf *fails)
{
	if (math->pmi_parity_algo == C2_PARITY_CAL_ALGO_XOR)
		xor_recover(math, data, parity, fails);
	else
		reed_solomon_recover(math, data, parity, fails);
}

static void fail_index_xor_recover(struct c2_parity_math *math,
				   struct c2_buf *data,
				   struct c2_buf *parity,
				   const uint32_t failure_index)
{
        uint32_t          ei; /* block element index. */
        uint32_t          ui; /* unit index. */
        uint32_t          unit_count;
        uint32_t          block_size = data[0].b_nob;
        c2_parity_elem_t  pe;

        unit_count = math->pmi_data_count + math->pmi_parity_count;

        C2_ASSERT(failure_index <= unit_count);

        for (ui = 0; ui < math->pmi_data_count; ++ui)
                C2_ASSERT(block_size == data[ui].b_nob);

        for (ui = 0; ui < math->pmi_parity_count; ++ui)
                C2_ASSERT(block_size == parity[ui].b_nob);

        for (ei = 0; ei < block_size; ++ei) {
                pe = 0;
                for (ui = 0; ui < math->pmi_data_count; ++ui) {
                    if (ui != failure_index) {
                        pe = pe ^
                             (c2_parity_elem_t)((uint8_t*)data[ui].b_addr)[ei];
                    }
                }
                if (ui != failure_index)
                        pe = pe ^ ((uint8_t*)parity[0].b_addr)[ei];
                else /* Parity was lost, so recover it. */
                        ((uint8_t*)parity[0].b_addr)[ei] = pe;

                ((uint8_t*)data[failure_index].b_addr)[ei] = pe;
        }

}

/** @todo Iterative reed-solomon decode to be implemented. */
static void fail_index_reed_solomon_recover(struct c2_parity_math *math,
					    struct c2_buf *data,
					    struct c2_buf *parity,
					    const uint32_t failure_index)
{
}

void c2_parity_math_fail_index_recover(struct c2_parity_math *math,
                                       struct c2_buf *data,
                                       struct c2_buf *parity,
                                       const uint32_t failure_index)
{
        if (math->pmi_parity_algo == C2_PARITY_CAL_ALGO_XOR)
		fail_index_xor_recover(math, data, parity, failure_index);
        else
                fail_index_reed_solomon_recover(math, data, parity,
						failure_index);
}

void c2_parity_math_buffer_xor(struct c2_buf *src, struct c2_buf *dest)
{
        uint32_t  ei; /* block element index. */
        uint32_t  block_size = src[0].b_nob;

        for (ei = 0; ei < block_size; ++ei)
		((uint8_t*)dest[0].b_addr)[ei] =
			(c2_parity_elem_t)((uint8_t*)src[0].b_addr)[ei] ^
			(c2_parity_elem_t)((uint8_t*)dest[0].b_addr)[ei];
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
