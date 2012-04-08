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

#ifdef __KERNEL__
#include <linux/kernel.h>  /* snprintf */
#else
#include <stdio.h>         /* snprintf */
#endif

#include "lib/errno.h"     /* ENOMEM */
#include "lib/memory.h"    /* C2_ALLOC_ARR */
#include "lib/mutex.h"     /* c2_mutex */
#include "lib/misc.h"      /* <linux/string.h> <string.h> for strcmp */
#include "lib/assert.h"    /* C2_ASSERT */
#include "lib/tlist.h"
#include "lib/finject.h"
#include "lib/finject_internal.h"


enum {
	FI_STATES_ARRAY_SIZE = 64 * 1024,
};

struct c2_fi_fpoint_state fi_states[FI_STATES_ARRAY_SIZE];
uint32_t                  fi_states_free_idx;
struct c2_mutex           fi_states_mutex;

struct fi_dynamic_id {
	struct c2_tlink  fdi_tlink;
	uint64_t         fdi_magic;
	char            *fdi_str;
};

enum {
	DYNID_LINK_MAGIC = 0x666964796e69646c,
	DYNID_HEAD_MAGIC = 0x666964796e696468,
};

C2_TL_DESCR_DEFINE(fi_dynamic_ids, "finject_dynamic_id", static,
		   struct fi_dynamic_id, fdi_tlink, fdi_magic,
		   DYNID_LINK_MAGIC, DYNID_HEAD_MAGIC);
C2_TL_DEFINE(fi_dynamic_ids, static, struct fi_dynamic_id);

/**
 * A storage for fault point ID strings, which are allocated dynamically in
 * runtime.
 *
 * Almost always ID string is a C string-literal with a
 * static storage duration. But in some rare cases ID strings need to be
 * allocated dynamically (for example when enabling FP via debugfs). To prevent
 * memleaks in such cases, all dynamically allocated ID strings are stored in
 * this linked list, using c2_fi_add_dyn_id(), which is cleaned in
 * fi_states_fini().
 */
static struct c2_tl fi_dynamic_ids;

/* keep these long strings on a single line for easier editing */
const char *c2_fi_states_headline[] = {
" Idx | Enb |TotHits|TotTrig|Hits|Trig|   Type   |   Data   | Module |              File name                 | Line |             Func name             |   Tag\n",
"-----+-----+-------+-------+----+----+----------+----------+--------+----------------------------------------+------+-----------------------------------+----------\n",
};
C2_EXPORTED(c2_fi_states_headline);

const char c2_fi_states_print_format[] =
" %-3u    %c   %-7u %-7u %-4u %-4u %-10s %-10s %-8s %-40s  %-4u  %-35s  %s\n";
C2_EXPORTED(c2_fi_states_print_format);


const struct c2_fi_fpoint_state *c2_fi_states_get(void)
{
	return fi_states;
}
C2_EXPORTED(c2_fi_states_get);

uint32_t c2_fi_states_get_free_idx(void)
{
	return fi_states_free_idx;
}
C2_EXPORTED(c2_fi_states_get_free_idx);

static inline uint32_t fi_state_idx(const struct c2_fi_fpoint_state *s)
{
	return s - fi_states;
}

static void fi_state_info_init(struct c2_fi_fpoint_state_info *si)
{
	si->si_idx               = 0;
	si->si_enb               = 'n';
	si->si_total_hit_cnt     = 0;
	si->si_total_trigger_cnt = 0;
	si->si_hit_cnt           = 0;
	si->si_trigger_cnt       = 0;
	si->si_type              = "";
	si->si_module            = "";
	si->si_file              = "";
	si->si_func              = "";
	si->si_tag               = "";
	si->si_line_num          = 0;

	C2_SET_ARR0(si->si_data);
}

/**
 * Extracts a "colibri core" file name from a full-path file name.
 *
 * For example, given the following full-path file name:
 *
 *     /data/colibri/core/build_kernel_modules/lib/ut/finject.c
 *
 * The "colibri core" file name is:
 *
 *     build_kernel_modules/lib/ut/finject.c
 */
static inline const char *core_file_name(const char *fname)
{
	static const char  core[] = "core/";
	const char        *cfn;

	cfn = strstr(fname, core);
	if (cfn == NULL)
		return fname;

	return cfn + strlen(core);
}

void c2_fi_states_get_state_info(const struct c2_fi_fpoint_state *s,
				 struct c2_fi_fpoint_state_info *si)
{
	const struct c2_fi_fault_point  *fp;

	fi_state_info_init(si);

	si->si_idx = fi_state_idx(s);
	si->si_func = s->fps_id.fpi_func;
	si->si_tag = s->fps_id.fpi_tag;
	si->si_total_hit_cnt = s->fps_total_hit_cnt;
	si->si_total_trigger_cnt = s->fps_total_trigger_cnt;
	fp = s->fps_fp;

	/*
	 * fp can be NULL if fault point was enabled but had not been registered
	 * yet
	 */
	if (fp != NULL) {
		si->si_module = fp->fp_module;
		si->si_file = core_file_name(fp->fp_file);
		si->si_line_num = fp->fp_line_num;
	}

	if (fi_state_enabled(s)) {
		si->si_enb = 'y';
		si->si_type = c2_fi_fpoint_type_name(s->fps_data.fpd_type);
		switch (s->fps_data.fpd_type) {
		case C2_FI_OFF_N_ON_M:
			snprintf(si->si_data, sizeof si->si_data, "n=%u,m=%u",
					s->fps_data.u.s1.fpd_n,
					s->fps_data.u.s1.fpd_m);
			break;
		case C2_FI_RANDOM:
			snprintf(si->si_data, sizeof si->si_data, "p=%u",
					s->fps_data.u.fpd_p);
			break;
		default:
			break; /* leave data string empty */
		}
		si->si_hit_cnt = s->fps_data.fpd_hit_cnt;
		si->si_trigger_cnt = s->fps_data.fpd_trigger_cnt;
	}

	return;
}
C2_EXPORTED(c2_fi_states_get_state_info);

int c2_fi_add_dyn_id(char *str)
{
	struct fi_dynamic_id *fdi;

	C2_ALLOC_PTR(fdi);
	if (fdi == NULL)
		return -ENOMEM;

	c2_tlink_init(&fi_dynamic_ids_tl, &fdi->fdi_tlink);
	fdi->fdi_str = str;
	c2_tlist_add(&fi_dynamic_ids_tl, &fi_dynamic_ids, &fdi->fdi_tlink);

	return 0;
}
C2_EXPORTED(c2_fi_add_dyn_id);

static void fi_dynamic_ids_fini(void)
{
	struct fi_dynamic_id *entry;

	c2_tlist_for(&fi_dynamic_ids_tl, &fi_dynamic_ids, entry) {
		c2_tlist_del(&fi_dynamic_ids_tl, entry);
		c2_free(entry->fdi_str);
		c2_free(entry);
	} c2_tlist_endfor;

	c2_tlist_fini(&fi_dynamic_ids_tl, &fi_dynamic_ids);
}

void fi_states_init(void)
{
	c2_tlist_init(&fi_dynamic_ids_tl, &fi_dynamic_ids);
}

void fi_states_fini(void)
{
	int i;

	for (i = 0; i < fi_states_free_idx; ++i)
		c2_mutex_fini(&fi_states[i].fps_mutex);

	fi_dynamic_ids_fini();
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

static const char *fi_type_names[C2_FI_TYPES_NR] = {
	[C2_FI_ALWAYS]       = "always",
	[C2_FI_ONESHOT]      = "oneshot",
	[C2_FI_RANDOM]       = "random",
	[C2_FI_OFF_N_ON_M]   = "off_n_on_m",
	[C2_FI_FUNC]         = "user_func",
	[C2_FI_INVALID_TYPE] = "",
};

const char *c2_fi_fpoint_type_name(enum c2_fi_fpoint_type type)
{
	C2_PRE(IS_IN_ARRAY(type, fi_type_names));
	return fi_type_names[type];
}
C2_EXPORTED(c2_fi_fpoint_type_name);

enum c2_fi_fpoint_type c2_fi_fpoint_type_from_str(const char *type_name)
{
	int i;

	for (i = 0; i < C2_FI_TYPES_NR; i++)
		if (strcmp(fi_type_names[i], type_name) == 0)
			return i;

	return C2_FI_INVALID_TYPE;
}
C2_EXPORTED(c2_fi_fpoint_type_from_str);

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
	bool enabled;

	enabled = fi_state_enabled(fps) ? fps->fps_trigger_func(fps) : false;
	if (enabled) {
		fps->fps_total_trigger_cnt++;
		fps->fps_data.fpd_trigger_cnt++;
	}
	if (fi_state_enabled(fps))
		fps->fps_data.fpd_hit_cnt++;
	fps->fps_total_hit_cnt++;

	return enabled;
}

void c2_fi_enable_generic(const char *fp_func, const char *fp_tag,
			  const struct c2_fi_fpoint_data *fp_data)
{
	struct c2_fi_fpoint_state *state;
	struct c2_fi_fpoint_id    id = {
		.fpi_func = fp_func,
		.fpi_tag  = fp_tag,
	};

	C2_PRE(fp_func != NULL && fp_tag != NULL);

	c2_mutex_lock(&fi_states_mutex);

	state = __fi_state_find(&id);
	if (state == NULL)
		state = fi_state_alloc(&id);

	fi_enable_state(state, fp_data);

	c2_mutex_unlock(&fi_states_mutex);
}
C2_EXPORTED(c2_fi_enable_generic);

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
C2_EXPORTED(c2_fi_disable);

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
