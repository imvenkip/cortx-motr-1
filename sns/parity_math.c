#include <stdlib.h>
#include <string.h>
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/types.h"

#define _C2_PARITY_USE_OPS_
#include "parity_ops.h"
#include "parity_math.h"
#include "matvec.h"
#include "ls_solve.h"

#define C2_SNS_PARITY_MATH_DATA_BLOCKS_MAX (128)

struct c2_parity_math_impl {
	/* private: */
	uint32_t pmi_data_count;
	uint32_t pmi_parity_count;

	/* structures used for parity calculation and recovery */
	struct c2_vector pmi_data;
	struct c2_vector pmi_parity;
	struct c2_matrix pmi_vandmat;
	struct c2_matrix pmi_vandmat_parity_slice;

	/* structures used for recovery */
	struct c2_matrix pmi_sys_mat;
	struct c2_vector pmi_sys_vec;
	struct c2_vector pmi_sys_res;
	struct c2_linsys pmi_sys;
};

#define HANDLE_IF_FAIL(ret, handle)		\
	do {					\
	if (ret < 0)				\
		goto handle;			\
	} while(0)

#define RETURN_IF_FAIL(ret)			\
	do {                                    \
		if (ret < 0)                    \
			return ret;             \
	} while(0)

// c2_parity_* are to much eclectic. just more simple names.
static int gadd(int x, int y) { return c2_parity_add(x, y); }

static int gsub(int x, int y) { return c2_parity_sub(x, y); }

static int gmul(int x, int y) { return c2_parity_mul(x, y); }

static int gdiv(int x, int y) { return c2_parity_div(x, y); }

static int gpow(int x, int p) {
	int ret = x;
	int i = 1;

	// suppressing this : parity_ops.h:30: error: ‘c2_parity_lt’ defined but not used
	// WHY SHOULD I DO THIS???
	(void)c2_parity_lt(1, 2);

	if (p == 0)
		return 1;
        
	for (; i < p; ++i)
		ret = gmul(ret, x);
        
	return ret;
}

/* fills vandermonde matrix with initial values */
static int _vandmat_init(struct c2_matrix *m, uint32_t data_count, uint32_t checksum_count) {
	int ret = 0;
	uint32_t y = 0, x = 0;
	uint32_t mat_height = data_count + checksum_count, mat_width = data_count;

	ret = c2_mat_init(m, mat_width, mat_height);
	RETURN_IF_FAIL(ret);
	
	for (y = 0; y < mat_height; ++y)
		for (x = 0; x < mat_width; ++x) {
			*c2_mat_elem_get(m, x, y) = gpow(y, x);
		}

	return ret;
}

static void vandmat_fini(struct c2_matrix *mat) {
	c2_mat_fini(mat);
}

/* checks if row has only one element equals 1, and 0 others */
static int check_row_is_id(struct c2_matrix *m, uint32_t row) {
	int ret = 1;
	uint32_t x = 0;
	for (; x < m->m_width; ++x) {
		if (row == x)
			ret &= 1 == *c2_mat_elem_get(m, x, row);
		else
			ret &= 0 == *c2_mat_elem_get(m, x, row);
	}
	return ret ? 0 : -1;
}

/* normalize vandermonde matrix. upper part of which become identity matrix in case of success. */
static int vandmat_norm(struct c2_matrix *m) {
	uint32_t y = 0;
	
	for (; y < m->m_width; ++y) {
		uint32_t x = 0;
		c2_mat_col_operate(m, y, *c2_mat_elem_get(m, y, y), gdiv);
		
		for (; x < m->m_width; ++x)
			if (x != y)
				c2_mat_cols_operate(m, x, y,
                                                    gsub,
                                                    0, gmul,
                                                    *c2_mat_elem_get(m, x, y), gsub);
		
		/* Assert if units configured unproperly */
		C2_ASSERT(check_row_is_id(m, y) >= 0);
	}

	return 0;
}

static void int_parity_math_fini(struct c2_parity_math_impl *math) {
	vandmat_fini(&math->pmi_vandmat);
	c2_mat_fini(&math->pmi_vandmat_parity_slice);
	c2_vec_fini(&math->pmi_data);
	c2_vec_fini(&math->pmi_parity);

	c2_linsys_fini(&math->pmi_sys);
	c2_mat_fini(&math->pmi_sys_mat);
	c2_vec_fini(&math->pmi_sys_vec);
	c2_vec_fini(&math->pmi_sys_res);
}

static int  int_parity_math_init(struct c2_parity_math_impl *math,
				 uint32_t data_count, uint32_t parity_count) {
	int ret = 0;
	if (data_count < 1 || parity_count < 1 || data_count < parity_count
	    || data_count > C2_SNS_PARITY_MATH_DATA_BLOCKS_MAX) {
		C2_ASSERT(0 && "Unproper unit configuration!");
	}

	memset((void*)math, 0, sizeof(struct c2_parity_math_impl));

	ret = _vandmat_init(&math->pmi_vandmat, data_count, parity_count);
	HANDLE_IF_FAIL(ret, handle_error);

	ret = vandmat_norm(&math->pmi_vandmat);	
	HANDLE_IF_FAIL(ret, handle_error);

	ret = c2_mat_init(&math->pmi_vandmat_parity_slice, data_count, parity_count);
	HANDLE_IF_FAIL(ret, handle_error);
	c2_mat_get_submatrix(&math->pmi_vandmat, &math->pmi_vandmat_parity_slice, 0, data_count);

	math->pmi_data_count   = data_count;
	math->pmi_parity_count = parity_count;

	ret = c2_vec_init(&math->pmi_data, data_count);
	HANDLE_IF_FAIL(ret, handle_error);

	ret = c2_vec_init(&math->pmi_parity, parity_count);
	HANDLE_IF_FAIL(ret, handle_error);

	ret = c2_vec_init(&math->pmi_sys_vec, math->pmi_data.v_size);
	HANDLE_IF_FAIL(ret, handle_error);

	ret = c2_vec_init(&math->pmi_sys_res, math->pmi_data.v_size);
	HANDLE_IF_FAIL(ret, handle_error);

	ret = c2_mat_init(&math->pmi_sys_mat, math->pmi_data.v_size, math->pmi_data.v_size);
	HANDLE_IF_FAIL(ret, handle_error);

	return ret;
 handle_error:
	int_parity_math_fini(math);
	return ret;
}

static void int_parity_math_calculate(struct c2_parity_math_impl *math,
				      struct c2_buf data[],
				      struct c2_buf parity[]) {
	uint32_t ei = 0, ui = 0; /* block element index, unit index */
	uint32_t block_size = data[0].b_nob;

	for (ui = 0; ui < math->pmi_data_count; ++ui) {
		C2_ASSERT(block_size == parity[ui].b_nob);
		C2_ASSERT(parity[ui].b_nob == data[ui].b_nob);		
	}

	for (ei = 0; ei < block_size; ++ei) {
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			*c2_vec_elem_get(&math->pmi_data, ui) = ((uint8_t*)data[ui].b_addr)[ei];

		c2_mat_vec_multiply(&math->pmi_vandmat_parity_slice, &math->pmi_data, &math->pmi_parity, gmul, gadd);

		for (ui = 0; ui < math->pmi_parity_count; ++ui)
			((uint8_t*)parity[ui].b_addr)[ei] = *c2_vec_elem_get(&math->pmi_parity, ui);
	}
}

static void int_parity_math_refine(struct c2_parity_math_impl *math,
				   struct c2_buf data[],
				   struct c2_buf parity[],
				   uint32_t data_ind_changed) {
	/* for simplicity: */
	int_parity_math_calculate(math, data, parity);
}

/* counts number of blocks failed */
static uint32_t fails_count(uint8_t *fail, uint32_t unit_count) {
	uint32_t x = 0, count = 0;
	
	for (; x < unit_count; ++x)
		count += fail[x] ? 1 : 0;

	return count;
}

/* fills 'mat' and 'vec' with data passed to recovery algorithm */
static void recovery_data_fill(struct c2_parity_math_impl *math,
			       uint8_t *fail, uint32_t unit_count, /* in */
			       struct c2_matrix *mat, struct c2_vector *vec) /* out */
{
	uint32_t f = 0, y = 0, x = 0;
	for (f = 0; f < unit_count; ++f) {
		/* if (block is ok) and (not enough equations to solve system) */
		if (!fail[f] && y < vec->v_size) {
			/* copy vec */
			*c2_vec_elem_get(vec, y) = f < math->pmi_data_count 
				? *c2_vec_elem_get(&math->pmi_data, f)
				: *c2_vec_elem_get(&math->pmi_parity, f - math->pmi_data_count);
			/* copy mat */
			for (x = 0; x < mat->m_width; ++x)
				*c2_mat_elem_get(mat, x, y) = *c2_mat_elem_get(&math->pmi_vandmat, x, f);

			++y;
		}/*
		else { / * just fill broken blocks with 0xFF * /
			if (f < math->pmi_data_count)
				*c2_vec_elem_get(&math->pmi_data, f) = 0xFF;
			else
				*c2_vec_elem_get(&math->pmi_parity, f - math->pmi_data_count) = 0xFF;
		}*/
	}
}

/* update internal structures of 'math' with recovered data */
static void parity_math_recover(struct c2_parity_math_impl *math,
				uint8_t *fail, uint32_t unit_count) { /* in fail states of blocks */
	struct c2_matrix *mat = &math->pmi_sys_mat;
	struct c2_vector *vec = &math->pmi_sys_vec;
	struct c2_vector *res = &math->pmi_sys_res;
	struct c2_linsys *sys = &math->pmi_sys;

	recovery_data_fill(math, fail, unit_count, mat, vec);

	c2_linsys_init(sys, mat, vec, res);
	c2_linsys_solve(sys);
}

static int  int_parity_math_recover(struct c2_parity_math_impl *math,
				    struct c2_buf data[],
				    struct c2_buf parity[],
				    struct c2_buf *fails) {
	int ret = 0;
	uint32_t ei = 0, ui = 0; /* block element index, unit index */
	uint8_t *fail = NULL;
	uint32_t fail_count = 0;
	uint32_t unit_count = math->pmi_data_count + math->pmi_parity_count;
	uint32_t block_size = data[0].b_nob;

	fail = (uint8_t*) fails->b_addr;
	fail_count = fails_count(fail, unit_count);

	if (fail_count == 0)
		return -C2_SNS_PARITY_MATH_RECOVERY_IS_NOT_NEDDED;
	/* printf("int_parity_math_recover: %d, %d, %d\n", fail_count, math->pmi_parity_count, unit_count); */
	C2_ASSERT(fail_count <= math->pmi_parity_count);
	for (ui = 0; ui < math->pmi_data_count; ++ui) {
		C2_ASSERT(block_size == parity[ui].b_nob);
		C2_ASSERT(parity[ui].b_nob == data[ui].b_nob);		
	}

	for (ei = 0; ei < block_size; ++ei) {
		struct c2_vector *recovered = &math->pmi_sys_res;

		/* load data and parity */
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			*c2_vec_elem_get(&math->pmi_data,   ui) = ((uint8_t*)data  [ui].b_addr)[ei];

		for (ui = 0; ui < math->pmi_parity_count; ++ui)
			*c2_vec_elem_get(&math->pmi_parity, ui) = ((uint8_t*)parity[ui].b_addr)[ei];

		/* recover data */
		parity_math_recover(math, fail, unit_count);
		/* store data */
		for (ui = 0; ui < math->pmi_data_count; ++ui)
			((uint8_t*)data[ui].b_addr)[ei] = *c2_vec_elem_get(recovered, ui);
	}
	
	/* recalculate parity */
	int_parity_math_calculate(math, data, parity);

	return ret;
}

int  c2_parity_math_init(struct c2_parity_math *math,
			 uint32_t data_count, uint32_t parity_count) {
	math->pm_impl = (struct c2_parity_math_impl*)
		c2_alloc(sizeof(struct c2_parity_math_impl));
	
	if (math->pm_impl == NULL)
		return -ENOMEM;

	c2_parity_init();

	return int_parity_math_init(math->pm_impl, data_count, parity_count);
}

void c2_parity_math_fini(struct c2_parity_math *math) {
	C2_PRE(math->pm_impl);
	int_parity_math_fini(math->pm_impl);
	c2_free(math->pm_impl);
}

void c2_parity_math_calculate(struct c2_parity_math *math,
			      struct c2_buf data[],
			      struct c2_buf parity[]) {
	C2_PRE(math->pm_impl);
	return int_parity_math_calculate(math->pm_impl, data, parity);
}

void c2_parity_math_refine(struct c2_parity_math *math,
			   struct c2_buf data[],
			   struct c2_buf parity[],
			   uint32_t data_ind_changed) {
	C2_PRE(math->pm_impl);
	return int_parity_math_refine(math->pm_impl, data, parity, data_ind_changed);
}

int  c2_parity_math_recover(struct c2_parity_math *math,
			    struct c2_buf data[],
			    struct c2_buf parity[],
			    struct c2_buf *fail) {
	C2_PRE(math->pm_impl);
	return int_parity_math_recover(math->pm_impl, data, parity, fail);
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
