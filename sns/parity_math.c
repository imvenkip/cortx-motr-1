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
 * Revision date  : 06/14/2012
 */

#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h" /* SET0() */
#include "lib/types.h"

#include "sns/parity_ops.h"
#include "sns/parity_math.h"

/* Forward declarations */
static void xor_calculate(struct m0_parity_math *math,
                          const struct m0_buf *data,
                          struct m0_buf *parity);

static void reed_solomon_encode(struct m0_parity_math *math,
                                const struct m0_buf *data,
                                struct m0_buf *parity);

static void xor_diff(struct m0_parity_math *math,
		     struct m0_buf         *old,
		     struct m0_buf         *new,
		     struct m0_buf         *parity,
		     uint32_t               index);

static void reed_solomon_diff(struct m0_parity_math *math,
		              struct m0_buf         *old,
		              struct m0_buf         *new,
		              struct m0_buf         *parity,
		              uint32_t               index);

static void xor_recover(struct m0_parity_math *math,
                        struct m0_buf *data,
                        struct m0_buf *parity,
                        struct m0_buf *fails);

static void reed_solomon_recover(struct m0_parity_math *math,
                                 struct m0_buf *data,
                                 struct m0_buf *parity,
                                 struct m0_buf *fails);

static void fail_idx_xor_recover(struct m0_parity_math *math,
				 struct m0_buf *data,
				 struct m0_buf *parity,
				 const uint32_t failure_index);

static void fail_idx_reed_solomon_recover(struct m0_parity_math *math,
					  struct m0_buf *data,
					  struct m0_buf *parity,
					  const uint32_t failure_index);

static void (*calculate[M0_PARITY_CAL_ALGO_NR])(struct m0_parity_math *math,
						const struct m0_buf *data,
						struct m0_buf *parity) = {
	[M0_PARITY_CAL_ALGO_XOR] = xor_calculate,
	[M0_PARITY_CAL_ALGO_REED_SOLOMON] = reed_solomon_encode,
};

static void (*diff[M0_PARITY_CAL_ALGO_NR])(struct m0_parity_math *math,
					   struct m0_buf         *old,
					   struct m0_buf         *new,
					   struct m0_buf         *parity,
					   uint32_t               index) = {
	[M0_PARITY_CAL_ALGO_XOR]          = xor_diff,
	[M0_PARITY_CAL_ALGO_REED_SOLOMON] = reed_solomon_diff,
};

static void (*recover[M0_PARITY_CAL_ALGO_NR])(struct m0_parity_math *math,
					      struct m0_buf *data,
					      struct m0_buf *parity,
					      struct m0_buf *fails) = {
	[M0_PARITY_CAL_ALGO_XOR] = xor_recover,
	[M0_PARITY_CAL_ALGO_REED_SOLOMON] = reed_solomon_recover,
};

static void (*fidx_recover[M0_PARITY_CAL_ALGO_NR])(struct m0_parity_math *math,
						   struct m0_buf *data,
						   struct m0_buf *parity,
						   const uint32_t fidx) = {
	[M0_PARITY_CAL_ALGO_XOR] = fail_idx_xor_recover,
	[M0_PARITY_CAL_ALGO_REED_SOLOMON] = fail_idx_reed_solomon_recover,
};

enum {
	SNS_PARITY_MATH_DATA_BLOCKS_MAX = 1 << (M0_PARITY_GALOIS_W - 1),
	BAD_FAIL_INDEX = -1
};

/* m0_parity_* are to much eclectic. just more simple names. */
static int gadd(int x, int y)
{
	return m0_parity_add(x, y);
}

static int gsub(int x, int y)
{
	return m0_parity_sub(x, y);
}

static int gmul(int x, int y)
{
	return m0_parity_mul(x, y);
}

static int gdiv(int x, int y)
{
	return m0_parity_div(x, y);
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
static int vandmat_init(struct m0_matrix *m, uint32_t data_count,
			uint32_t parity_count)
{
	int ret;
	uint32_t y;
	uint32_t x;
	uint32_t mat_height = data_count + parity_count;
	uint32_t mat_width = data_count;

	ret = m0_matrix_init(m, mat_width, mat_height);
	if (ret < 0)
		return ret;

	for (y = 0; y < mat_height; ++y)
		for (x = 0; x < mat_width; ++x)
			*m0_matrix_elem_get(m, x, y) = gpow(y, x);

	return ret;
}

static void vandmat_fini(struct m0_matrix *mat)
{
	m0_matrix_fini(mat);
}

/* Checks if row has only one element equals 1, and 0 others */
static bool check_row_is_id(struct m0_matrix *m, uint32_t row)
{
	bool ret = true;
	uint32_t x;

	for (x = 0; x < m->m_width && ret; ++x)
		ret &= (row == x) == *m0_matrix_elem_get(m, x, row);

	return ret;
}

/* Normalises vandermonde matrix, upper part of which becomes identity matrix
 * in case of success. */
static int vandmat_norm(struct m0_matrix *m)
{
	uint32_t y;

	for (y = 0; y < m->m_width; ++y) {
		uint32_t x = 0;
		m0_matrix_col_operate(m, y, *m0_matrix_elem_get(m, y, y), gdiv);

		for (x = 0; x < m->m_width; ++x)
			if (x != y)
				m0_matrix_cols_operate(m, x, y, gsub, 0, gmul,
                                            *m0_matrix_elem_get(m, x, y), gsub);

		/* Assert if units are not configured properly. */
		M0_ASSERT(check_row_is_id(m, y));
	}

	return 0;
}

M0_INTERNAL void m0_parity_math_fini(struct m0_parity_math *math)
{
	if (math->pmi_parity_algo == M0_PARITY_CAL_ALGO_REED_SOLOMON) {
		vandmat_fini(&math->pmi_vandmat);
		m0_matrix_fini(&math->pmi_vandmat_parity_slice);
		m0_vector_fini(&math->pmi_data);
		m0_vector_fini(&math->pmi_parity);

		m0_linsys_fini(&math->pmi_sys);
		m0_matrix_fini(&math->pmi_sys_mat);
		m0_vector_fini(&math->pmi_sys_vec);
		m0_vector_fini(&math->pmi_sys_res);

		m0_parity_fini();
	}
}

M0_INTERNAL int m0_parity_math_init(struct m0_parity_math *math,
				    uint32_t data_count, uint32_t parity_count)
{
	int ret;

	M0_PRE(data_count >= 1);
	M0_PRE(parity_count >= 1);
	M0_PRE(data_count >= parity_count);
	M0_PRE(data_count <= SNS_PARITY_MATH_DATA_BLOCKS_MAX);

	M0_SET0(math);

	math->pmi_data_count   = data_count;
	math->pmi_parity_count = parity_count;

        if (parity_count == 1) {
		math->pmi_parity_algo = M0_PARITY_CAL_ALGO_XOR;
		return 0;
	} else {

		math->pmi_parity_algo = M0_PARITY_CAL_ALGO_REED_SOLOMON;
		/*
                 * init galois, only first call makes initialisation,
		 * no de-initialisation needed.
		 */
		m0_parity_init();

		ret = vandmat_init(&math->pmi_vandmat, data_count,
				   parity_count);
		if (ret < 0)
			goto handle_error;

		ret = vandmat_norm(&math->pmi_vandmat);
		if (ret < 0)
			goto handle_error;

		ret = m0_matrix_init(&math->pmi_vandmat_parity_slice,
				     data_count, parity_count);
		if (ret < 0)
			goto handle_error;

		m0_matrix_get_submatrix(&math->pmi_vandmat,
				        &math->pmi_vandmat_parity_slice, 0,
					data_count);

		ret = m0_vector_init(&math->pmi_data, data_count);
		if (ret < 0)
			goto handle_error;

		ret = m0_vector_init(&math->pmi_parity, parity_count);
		if (ret < 0)
			goto handle_error;

		ret = m0_vector_init(&math->pmi_sys_vec, math->pmi_data.v_size);
		if (ret < 0)
			goto handle_error;

		ret = m0_vector_init(&math->pmi_sys_res, math->pmi_data.v_size);
		if (ret < 0)
			goto handle_error;

		ret = m0_matrix_init(&math->pmi_sys_mat, math->pmi_data.v_size,
				     math->pmi_data.v_size);
		if (ret < 0)
			goto handle_error;
	}
	return ret;
 handle_error:
	m0_parity_math_fini(math);
	return ret;
}

static void xor_calculate(struct m0_parity_math *math,
			  const struct m0_buf *data,
			  struct m0_buf *parity)
{
        uint32_t          ei; /* block element index. */
        uint32_t          ui; /* unit index. */
        uint32_t          block_size = data[0].b_nob;
	m0_parity_elem_t  pe;

	M0_PRE(block_size == parity[0].b_nob);
	for (ui = 1; ui < math->pmi_data_count; ++ui)
		M0_PRE(block_size == data[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		pe = 0;
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			pe ^= (m0_parity_elem_t)((uint8_t*)data[ui].b_addr)[ei];

		((uint8_t*)parity[0].b_addr)[ei] = pe;
	}

}

static void reed_solomon_diff(struct m0_parity_math *math,
			      struct m0_buf         *old,
			      struct m0_buf         *new,
			      struct m0_buf         *parity,
			      uint32_t               index)
{
	struct m0_matrix *mat;
	uint32_t          ei;
	uint32_t          ui;
	uint8_t		  diff_data;
	m0_parity_elem_t  mat_elem;

	M0_PRE(math   != NULL);
	M0_PRE(old    != NULL);
	M0_PRE(new    != NULL);
	M0_PRE(parity != NULL);
	M0_PRE(index  <  math->pmi_data_count);
	M0_PRE(old[index].b_nob == new[index].b_nob);
	M0_PRE(m0_forall(i, math->pmi_parity_count,
		         new[index].b_nob == parity[i].b_nob));

	mat = &math->pmi_vandmat_parity_slice;
	for (ui = 0; ui < math->pmi_parity_count; ++ui) {
		for (ei = 0; ei < new[index].b_nob; ++ei) {
			mat_elem = *m0_matrix_elem_get(mat, index, ui);
			diff_data = ((uint8_t *)old[index].b_addr)[ei] ^
				    ((uint8_t *)new[index].b_addr)[ei];
			((uint8_t*)parity[ui].b_addr)[ei] ^=
				gmul(diff_data, mat_elem);
		}
	}
}

static void xor_diff(struct m0_parity_math *math,
		     struct m0_buf         *old,
		     struct m0_buf         *new,
		     struct m0_buf         *parity,
		     uint32_t               index)
{
	uint32_t ei;

	M0_PRE(math   != NULL);
	M0_PRE(old    != NULL);
	M0_PRE(new    != NULL);
	M0_PRE(parity != NULL);
	M0_PRE(index  <  math->pmi_data_count);
	M0_PRE(old[index].b_nob == new[index].b_nob);
	M0_PRE(new[index].b_nob == parity[0].b_nob);

	for (ei = 0; ei < new[index].b_nob; ++ei) {
		((uint8_t*)parity[0].b_addr)[ei] ^=
			((uint8_t *)old[index].b_addr)[ei] ^
			((uint8_t *)new[index].b_addr)[ei];
	}
}

static void reed_solomon_encode(struct m0_parity_math *math,
				const struct m0_buf *data,
				struct m0_buf *parity)
{
	uint32_t ei; /* block element index. */
	uint32_t ui; /* unit index. */
	uint32_t block_size = data[0].b_nob;

	for (ui = 1; ui < math->pmi_data_count; ++ui)
		M0_ASSERT(block_size == data[ui].b_nob);

	for (ui = 0; ui < math->pmi_parity_count; ++ui)
		M0_ASSERT(block_size == parity[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			*m0_vector_elem_get(&math->pmi_data, ui) =
				((uint8_t*)data[ui].b_addr)[ei];

		m0_matrix_vec_multiply(&math->pmi_vandmat_parity_slice,
				&math->pmi_data, &math->pmi_parity, gmul, gadd);

		for (ui = 0; ui < math->pmi_parity_count; ++ui)
			((uint8_t*)parity[ui].b_addr)[ei] =
				*m0_vector_elem_get(&math->pmi_parity, ui);
	}
}

M0_INTERNAL void m0_parity_math_calculate(struct m0_parity_math *math,
					  struct m0_buf *data,
					  struct m0_buf *parity)
{
	(*calculate[math->pmi_parity_algo])(math, data, parity);
}

M0_INTERNAL void m0_parity_math_diff(struct m0_parity_math *math,
				     struct m0_buf *old,
				     struct m0_buf *new,
				     struct m0_buf *parity, uint32_t index)
{
	(*diff[math->pmi_parity_algo])(math, old, new, parity, index);
}

M0_INTERNAL void m0_parity_math_refine(struct m0_parity_math *math,
				       struct m0_buf *data,
				       struct m0_buf *parity,
				       uint32_t data_ind_changed)
{
	/* for simplicity: */
	m0_parity_math_calculate(math, data, parity);
}

/* Counts number of failed blocks. */
static uint32_t fails_count(uint8_t *fail, uint32_t unit_count)
{
	uint32_t x;
	uint32_t count = 0;

	for (x = 0; x < unit_count; ++x)
		count += !!fail[x];

	return count;
}

/* Fills 'mat' and 'vec' with data passed to recovery algorithm. */
static void recovery_data_fill(struct m0_parity_math *math,
			       uint8_t *fail, uint32_t unit_count, /* in. */
			       struct m0_matrix *mat,
			       struct m0_vector *vec) /* out. */
{
	uint32_t f;
	uint32_t y = 0;
	uint32_t x;

	for (f = 0; f < unit_count; ++f) {
		/*
		 * if (block is ok) and
		 * (not enough equations to solve system).
		 */
		if (!fail[f] && y < vec->v_size) {
			/* copy vec. */
			*m0_vector_elem_get(vec, y) = f < math->pmi_data_count
				? *m0_vector_elem_get(&math->pmi_data, f)
				: *m0_vector_elem_get(&math->pmi_parity,
						f - math->pmi_data_count);
			/* copy mat. */
			for (x = 0; x < mat->m_width; ++x)
				*m0_matrix_elem_get(mat, x, y) =
					*m0_matrix_elem_get(&math->pmi_vandmat,
							    x, f);

			++y;
		}
	}
}

/* Updates internal structures of 'math' with recovered data. */
static void parity_math_recover(struct m0_parity_math *math,
				uint8_t *fail, uint32_t unit_count)
{
	struct m0_matrix *mat = &math->pmi_sys_mat;
	struct m0_vector *vec = &math->pmi_sys_vec;
	struct m0_vector *res = &math->pmi_sys_res;
	struct m0_linsys *sys = &math->pmi_sys;

	recovery_data_fill(math, fail, unit_count, mat, vec);

	m0_linsys_init(sys, mat, vec, res);
	m0_linsys_solve(sys);
}

static void xor_recover(struct m0_parity_math *math,
			struct m0_buf *data,
			struct m0_buf *parity,
			struct m0_buf *fails)
{
	uint32_t          ei; /* block element index. */
	uint32_t          ui; /* unit index. */
	uint8_t          *fail;
	uint32_t          fail_count;
	uint32_t          unit_count;
	uint32_t          block_size = data[0].b_nob;
	m0_parity_elem_t  pe;
	int		  fail_index = BAD_FAIL_INDEX;

	unit_count = math->pmi_data_count + math->pmi_parity_count;
	fail = (uint8_t*) fails->b_addr;
	fail_count = fails_count(fail, unit_count);

	M0_PRE(fail_count == 1);
	M0_PRE(fail_count <= math->pmi_parity_count);
	M0_PRE(block_size == parity[0].b_nob);

	for (ui = 1; ui < math->pmi_data_count; ++ui)
		M0_PRE(block_size == data[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		pe = 0;
                for (ui = 0; ui < math->pmi_data_count; ++ui) {
			if (fail[ui] != 1)
				pe ^= (m0_parity_elem_t)((uint8_t*)
				       data[ui].b_addr)[ei];
			else
				fail_index = ui;
                }
		/* Now ui points to parity block, test if it was failed. */
		if (fail[ui] != 1) {
			M0_ASSERT(fail_index != BAD_FAIL_INDEX);
			((uint8_t*)data[fail_index].b_addr)[ei] = pe ^
				((uint8_t*)parity[0].b_addr)[ei];
		} else /* Parity was lost, so recover it. */
			((uint8_t*)parity[0].b_addr)[ei] = pe;
        }
}

static void reed_solomon_recover(struct m0_parity_math *math,
				 struct m0_buf *data,
				 struct m0_buf *parity,
				 struct m0_buf *fails)
{
	uint32_t ei; /* block element index. */
	uint32_t ui; /* unit index. */
	uint8_t *fail;
	uint32_t fail_count;
	uint32_t unit_count = math->pmi_data_count + math->pmi_parity_count;
	uint32_t block_size = data[0].b_nob;

	fail = (uint8_t*) fails->b_addr;
	fail_count = fails_count(fail, unit_count);

	M0_ASSERT(fail_count > 0);
	M0_ASSERT(fail_count <= math->pmi_parity_count);

	for (ui = 1; ui < math->pmi_data_count; ++ui)
		M0_ASSERT(block_size == data[ui].b_nob);

	for (ui = 0; ui < math->pmi_parity_count; ++ui)
		M0_ASSERT(block_size == parity[ui].b_nob);

	for (ei = 0; ei < block_size; ++ei) {
		struct m0_vector *recovered = &math->pmi_sys_res;

		/* load data and parity. */
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			*m0_vector_elem_get(&math->pmi_data, ui) =
				((uint8_t*)data[ui].b_addr)[ei];

		for (ui = 0; ui < math->pmi_parity_count; ++ui)
			*m0_vector_elem_get(&math->pmi_parity, ui) =
				((uint8_t*)parity[ui].b_addr)[ei];

		/* recover data. */
		parity_math_recover(math, fail, unit_count);
		/* store data. */
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			((uint8_t*)data[ui].b_addr)[ei] =
				*m0_vector_elem_get(recovered, ui);
	}

	/* recalculate parity. */
	m0_parity_math_calculate(math, data, parity);
}

M0_INTERNAL void m0_parity_math_recover(struct m0_parity_math *math,
					struct m0_buf *data,
					struct m0_buf *parity,
					struct m0_buf *fails)
{
	(*recover[math->pmi_parity_algo])(math, data, parity, fails);
}

static void fail_idx_xor_recover(struct m0_parity_math *math,
				 struct m0_buf *data,
				 struct m0_buf *parity,
				 const uint32_t failure_index)
{
        uint32_t          ei; /* block element index. */
        uint32_t          ui; /* unit index. */
        uint32_t          unit_count;
        uint32_t          block_size = data[0].b_nob;
        m0_parity_elem_t  pe;

	M0_PRE(block_size == parity[0].b_nob);

        unit_count = math->pmi_data_count + math->pmi_parity_count;
        M0_ASSERT(failure_index < unit_count);

	for (ui = 1; ui < math->pmi_data_count; ++ui)
		M0_ASSERT(block_size == data[ui].b_nob);

        for (ei = 0; ei < block_size; ++ei) {
                pe = 0;
                for (ui = 0; ui < math->pmi_data_count; ++ui)
			if (ui != failure_index)
				pe ^= (m0_parity_elem_t)((uint8_t*)
				       data[ui].b_addr)[ei];

                if (ui != failure_index)
			((uint8_t*)data[failure_index].b_addr)[ei] = pe ^
				((uint8_t*)parity[0].b_addr)[ei];
                else /* Parity was lost, so recover it. */
                        ((uint8_t*)parity[0].b_addr)[ei] = pe;
        }

}

/** @todo Iterative reed-solomon decode to be implemented. */
static void fail_idx_reed_solomon_recover(struct m0_parity_math *math,
					  struct m0_buf *data,
					  struct m0_buf *parity,
					  const uint32_t failure_index)
{
}

M0_INTERNAL void m0_parity_math_fail_index_recover(struct m0_parity_math *math,
						   struct m0_buf *data,
						   struct m0_buf *parity,
						   const uint32_t fidx)
{
	(*fidx_recover[math->pmi_parity_algo])(math, data, parity, fidx);
}

M0_INTERNAL void m0_parity_math_buffer_xor(struct m0_buf *dest,
					   const struct m0_buf *src)
{
        uint32_t  ei; /* block element index. */

        for (ei = 0; ei < src[0].b_nob; ++ei)
		((uint8_t*)dest[0].b_addr)[ei] ^=
			(m0_parity_elem_t)((uint8_t*)src[0].b_addr)[ei];
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
