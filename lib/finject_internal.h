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
 * Original creation date: 03/28/2012
 */

#pragma once

#ifndef __COLIBRI_LIB_FINJECT_INTERNAL_H__
#define __COLIBRI_LIB_FINJECT_INTERNAL_H__

#include "lib/mutex.h"     /* c2_mutex */

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
	struct c2_fi_fault_point *fps_fp;
	/* Mutex, used to keep "state" structure in consistent state */
	struct c2_mutex           fps_mutex;
	/** Counter of how many times (in total) fault point was checked/hit */
	uint32_t                  fps_total_hit_cnt;
	/** Counter of how many times (in total) fault point was triggered */
	uint32_t                  fps_total_trigger_cnt;
};

struct c2_fi_fpoint_state_info {
	uint32_t    si_idx;
	char        si_enb;
	uint32_t    si_total_hit_cnt;
	uint32_t    si_total_trigger_cnt;
	uint32_t    si_hit_cnt;
	uint32_t    si_trigger_cnt;
	const char *si_type;
	char        si_data[64];
	const char *si_module;
	const char *si_file;
	const char *si_func;
	const char *si_tag;
	uint32_t    si_line_num;
};

#ifdef ENABLE_FAULT_INJECTION

extern struct c2_mutex  fi_states_mutex;

static inline bool fi_state_enabled(const struct c2_fi_fpoint_state *state)
{
	/*
	 * If fps_trigger_func is not set, then FP state is considered to be
	 * "disabled"
	 */
	return state->fps_trigger_func != NULL;
}

/**
 * A read-only "getter" of global fi_states array, which stores all FP states.
 *
 * The fi_states array is a private data of lib/finject.c and it should not be
 * modified by external code. This function deliberately returns a const pointer
 * to emphasize this. The main purpose of this function is to provide the
 * FP states information to kc2ctl driver, which displays it via debugfs.
 *
 * @return A constant pointer to global fi_states array.
 */
C2_INTERNAL const struct c2_fi_fpoint_state *c2_fi_states_get(void);

/**
 * A read-only "getter" of global fi_states_free_idx index of fi_states array.
 *
 * @return Current value of fi_states_free_idx variable.
 */
C2_INTERNAL uint32_t c2_fi_states_get_free_idx(void);

/**
 * Fills c2_fi_fpoint_state_info structure.
 */
C2_INTERNAL void c2_fi_states_get_state_info(const struct c2_fi_fpoint_state *s,
					     struct c2_fi_fpoint_state_info
					     *si);

extern const char  *c2_fi_states_headline[];
extern const char   c2_fi_states_print_format[];

/**
 * Add a dynamically allocated fault point ID string to persistent storage,
 * which will be cleaned during c2_fi_fini() execution.
 *
 * This function aimed to be used together with c2_fi_enable_xxx() functions.
 */
C2_INTERNAL int c2_fi_add_dyn_id(char *str);

/**
 * Returns the name of fault point type
 */
C2_INTERNAL const char *c2_fi_fpoint_type_name(enum c2_fi_fpoint_type type);

/**
 * Converts a string into fault point type
 */
C2_INTERNAL enum c2_fi_fpoint_type c2_fi_fpoint_type_from_str(const char
							      *type_name);

C2_INTERNAL void fi_states_init(void);
C2_INTERNAL void fi_states_fini(void);

#endif /* ENABLE_FAULT_INJECTION */

#endif /* __COLIBRI_LIB_FINJECT_INTERNAL_H__ */

