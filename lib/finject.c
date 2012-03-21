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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 02/22/2012
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef ENABLE_FAULT_INJECTION

#include "lib/errno.h"     /* ENOMEM */
#include "lib/memory.h"    /* C2_ALLOC_ARR */
#include "lib/mutex.h"     /* c2_mutex */
#include "lib/misc.h"      /* <linux/string.h> <string.h> for strcmp */
#include "lib/assert.h"    /* C2_ASSERT */
#include "lib/finject.h"


/**
 * Set of attributes, which uniquely identifies each fault point.
 * @see c2_fi_fault_point
 */
struct c2_fi_fpoint_id {
	/** Name of a function, where FP is declared */
	const char  *fpi_func;
	/** Tag - short descriptive name of fault point */
	const char  *fpi_tag;
};

struct c2_fi_fpoint_state;
typedef bool (*fp_state_func_t)(struct c2_fi_fpoint_state *fps);

/**
 * Holds information about state of a fault point.
 */
struct c2_fi_fpoint_state {
	/** FP identifier */
	struct c2_fi_fpoint_id    fps_id;
	/**
	 * State function, which implements a particular "triggering algorithm"
	 * for each FP type
	 */
	fp_state_func_t           fps_trigger_func;
	/**
	 * Input parameters for "triggering algorithm", which control it's
	 * behavior
	 */
	struct c2_fi_fpoint_data  fps_data;
	/** Back reference to the corresponding c2_fi_fault_point structure */
	struct c2_fi_fault_point  *fps_fp;
	/* Mutex, used to keep "state" structure in consistent state */
	struct c2_mutex           fps_mutex;
};

enum {
	FI_STATES_ARRAY_SIZE = 64 * 1024,
};

static struct c2_fi_fpoint_state fi_states[FI_STATES_ARRAY_SIZE];
static uint32_t                  fi_states_free_idx;
struct c2_mutex                  fi_states_mutex;


void fi_states_fini(void)
{
	int i;

	for (i = 0; i < fi_states_free_idx; ++i)
		c2_mutex_fini(&fi_states[i].fps_mutex);
}

static inline bool fi_state_enabled(const struct c2_fi_fpoint_state *state)
{
	/*
	 * If fps_trigger_func is not set, then FP state is considered to be
	 * "disabled"
	 */
	return state->fps_trigger_func != NULL;
}

/**
 * Checks equality of two fault point ids.
 *
 * @param id1 Pointer to first ID
 * @param id2 Pointer to second ID
 *
 * @return    true, if provided IDs are equal
 * @return    false otherwise
 */
static inline bool fi_fpoint_id_eq(const struct c2_fi_fpoint_id *id1,
				   const struct c2_fi_fpoint_id *id2)
{
	return strcmp(id1->fpi_func, id2->fpi_func) == 0 &&
			strcmp(id1->fpi_tag, id2->fpi_tag) == 0;
}

/**
 * Searches for c2_fi_fpoint_state structure in global fi_states array by fault
 * point ID.
 *
 * @param fp_id Pointer to fault point ID
 *
 * @return      Pointer to c2_fi_fpoint_state object, which has fps_id equal
 *              to the provided fp_id, if any
 * @return      NULL, if no such state object exists
 */
static
struct c2_fi_fpoint_state *__fi_state_find(const struct c2_fi_fpoint_id *fp_id)
{
	int i;

	for (i = 0; i < fi_states_free_idx; ++i)
		if (fi_fpoint_id_eq(&fi_states[i].fps_id, fp_id))
			return &fi_states[i];

	return NULL;
}

/**
 * A wrapper around __fi_state_find(), which uses fi_states_mutex mutex to
 * prevent potential changes of fi_states array from other threads.
 *
 * @see __fi_state_find()
 */
static inline
struct c2_fi_fpoint_state *fi_state_find(struct c2_fi_fpoint_id *fp_id)
{
	struct c2_fi_fpoint_state *state;

	c2_mutex_lock(&fi_states_mutex);
	state = __fi_state_find(fp_id);
	c2_mutex_unlock(&fi_states_mutex);

	return state;
}

/**
 * "Allocates" and initializes state structure in fi_states array.
 */
static struct c2_fi_fpoint_state *fi_state_alloc(struct c2_fi_fpoint_id *id)
{
	struct c2_fi_fpoint_state *state;

	state = &fi_states[fi_states_free_idx];
	fi_states_free_idx++;
	C2_ASSERT(fi_states_free_idx < ARRAY_SIZE(fi_states));
	c2_mutex_init(&state->fps_mutex);
	state->fps_id = *id;

	return state;
}

/**
 * Triggering algorithm for C2_FI_ALWAYS type
 */
static bool fi_state_always(struct c2_fi_fpoint_state *fps)
{
	return true;
}

static void fi_disable_state(struct c2_fi_fpoint_state *fps);

/**
 * Triggering algorithm for C2_FI_ONESHOT type
 */
static bool fi_state_oneshot(struct c2_fi_fpoint_state *fps)
{
	fi_disable_state(fps);
	return true;
}

uint32_t fi_random(void);

/**
 * Triggering algorithm for C2_FI_RANDOM type
 */
static bool fi_state_random(struct c2_fi_fpoint_state *fps)
{
	return fps->fps_data.u.fpd_p >= fi_random();
}

/**
 * Triggering algorithm for C2_FI_OFF_N_ON_M type
 */
static bool fi_state_off_n_on_m(struct c2_fi_fpoint_state *fps)
{
	struct c2_fi_fpoint_data *data = &fps->fps_data;
	bool enabled = false;

	c2_mutex_lock(&fps->fps_mutex);

	data->u.s1.fpd___n_cnt++;
	if (data->u.s1.fpd___n_cnt > data->u.s1.fpd_n) {
		enabled = true;
		data->u.s1.fpd___m_cnt++;
		if (data->u.s1.fpd___m_cnt >= data->u.s1.fpd_m) {
			data->u.s1.fpd___n_cnt = 0;
			data->u.s1.fpd___m_cnt = 0;
		}
	}

	c2_mutex_unlock(&fps->fps_mutex);

	return enabled;
}

/**
 * Triggering algorithm for C2_FI_FUNC type
 */
static bool fi_state_user_func(struct c2_fi_fpoint_state *fps)
{
	return fps->fps_data.u.s2.fpd_trigger_func(fps->fps_data.u.s2.fpd_private);
}

static const fp_state_func_t fi_trigger_funcs[C2_FI_TYPES_NR] = {
	[C2_FI_ALWAYS]     = fi_state_always,
	[C2_FI_ONESHOT]    = fi_state_oneshot,
	[C2_FI_RANDOM]     = fi_state_random,
	[C2_FI_OFF_N_ON_M] = fi_state_off_n_on_m,
	[C2_FI_FUNC]       = fi_state_user_func,
};

/**
 * Helper function for c2_fi_enable_generic()
 */
static void fi_enable_state(struct c2_fi_fpoint_state *fp_state,
			    const struct c2_fi_fpoint_data *fp_data)
{
	C2_PRE(IS_IN_ARRAY(fp_data->fpd_type, fi_trigger_funcs));

	c2_mutex_lock(&fp_state->fps_mutex);

	if (fp_data != NULL)
		fp_state->fps_data = *fp_data;

	if (fp_state->fps_data.fpd_type == C2_FI_OFF_N_ON_M) {
		fp_state->fps_data.u.s1.fpd___n_cnt = 0;
		fp_state->fps_data.u.s1.fpd___m_cnt = 0;
	}

	fp_state->fps_trigger_func = fi_trigger_funcs[fp_data->fpd_type];

	c2_mutex_unlock(&fp_state->fps_mutex);
}

/**
 * Helper function for c2_fi_disable()
 */
static void fi_disable_state(struct c2_fi_fpoint_state *fps)
{
	static const struct c2_fi_fpoint_data zero_data;

	c2_mutex_lock(&fps->fps_mutex);

	fps->fps_trigger_func = NULL;
	fps->fps_data = zero_data;

	c2_mutex_unlock(&fps->fps_mutex);
}

void c2_fi_register(struct c2_fi_fault_point *fp)
{
	struct c2_fi_fpoint_state *state;
	struct c2_fi_fpoint_id    id = {
		.fpi_func = fp->fp_func,
		.fpi_tag  = fp->fp_tag,
	};

	c2_mutex_lock(&fi_states_mutex);

	state = __fi_state_find(&id);
	if (state == NULL)
		state = fi_state_alloc(&id);

	/* Link state and fault point structures to each other */
	state->fps_fp = fp;
	fp->fp_state = state;

	c2_mutex_unlock(&fi_states_mutex);
}

bool c2_fi_enabled(struct c2_fi_fpoint_state *fps)
{
	return fi_state_enabled(fps) ? fps->fps_trigger_func(fps) : false;
}

void c2_fi_enable_generic(const char *fp_func, const char *fp_tag,
			  const struct c2_fi_fpoint_data *fp_data)
{
	struct c2_fi_fpoint_state *state;
	struct c2_fi_fpoint_id    id = {
		.fpi_func = fp_func,
		.fpi_tag  = fp_tag,
	};

	c2_mutex_lock(&fi_states_mutex);

	state = __fi_state_find(&id);
	if (state == NULL)
		state = fi_state_alloc(&id);

	fi_enable_state(state, fp_data);

	c2_mutex_unlock(&fi_states_mutex);
}

void c2_fi_disable(const char *fp_func, const char *fp_tag)
{
	struct c2_fi_fpoint_state *state;
	struct c2_fi_fpoint_id    id = {
		.fpi_func = fp_func,
		.fpi_tag  = fp_tag
	};

	state = fi_state_find(&id);
	C2_ASSERT(state != NULL);

	fi_disable_state(state);
}

#endif /* ENABLE_FAULT_INJECTION */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
