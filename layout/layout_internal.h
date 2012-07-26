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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 01/11/2011
 */

#ifndef __COLIBRI_LAYOUT_LAYOUT_INTERNAL_H__
#define __COLIBRI_LAYOUT_LAYOUT_INTERNAL_H__

/**
 * @addtogroup layout
 * @{
 */

/* import */
struct c2_layout_domain;
struct c2_layout;
struct c2_layout_ops;
struct c2_layout_type;
struct c2_layout_enum;
struct c2_layout_enum_ops;
struct c2_layout_enum_type;
struct c2_striped_layout;
struct c2_layout_instance;
struct c2_layout_instance_ops;
enum c2_addb_event_id;
struct c2_addb_ev;
struct c2_addb_ctx;
struct c2_addb_loc;
struct c2_fid;

enum {
	/** Invalid layout id. */
	LID_NONE                   = 0,

	/** Flag used during table creation, using c2_table_init() */
	DEFAULT_DB_FLAG            = 0,

	/**
	 * Maximum limit on the number of COB entries those can be stored
	 * inline into the layouts table, while rest of those are stored into
	 * the cob_lists table.
	 */
	LDB_MAX_INLINE_COB_ENTRIES = 20
};

bool c2_layout__domain_invariant(const struct c2_layout_domain *dom);
bool c2_layout__allocated_invariant(const struct c2_layout *l);
bool c2_layout__invariant(const struct c2_layout *l);
bool c2_layout__enum_invariant(const struct c2_layout_enum *le);
bool c2_layout__striped_allocated_invariant(const struct c2_striped_layout *s);
bool c2_layout__striped_invariant(const struct c2_striped_layout *stl);

struct c2_layout *c2_layout__list_lookup(const struct c2_layout_domain *dom,
					 uint64_t lid,
					 bool ref_increment);

void c2_layout__init(struct c2_layout *l,
		     struct c2_layout_domain *dom,
		     uint64_t lid,
		     struct c2_layout_type *type,
		     const struct c2_layout_ops *ops);
void c2_layout__fini(struct c2_layout *l);
void c2_layout__populate(struct c2_layout *l,
			 uint32_t ref_count);
void c2_layout__delete(struct c2_layout *l);

void c2_layout__striped_init(struct c2_striped_layout *stl,
			     struct c2_layout_domain *dom,
			     uint64_t lid,
			     struct c2_layout_type *type,
			     const struct c2_layout_ops *ops);
void c2_layout__striped_fini(struct c2_striped_layout *stl);
void c2_layout__striped_populate(struct c2_striped_layout *str_l,
				 struct c2_layout_enum *e,
				 uint32_t ref_count);
void c2_layout__striped_delete(struct c2_striped_layout *stl);

void c2_layout__enum_init(struct c2_layout_domain *dom,
			  struct c2_layout_enum *le,
			  struct c2_layout_enum_type *et,
			  const struct c2_layout_enum_ops *ops);
void c2_layout__enum_fini(struct c2_layout_enum *le);

void c2_layout__log(const char *fn_name,
		    const char *err_msg,
		    const struct c2_addb_ev *ev,
		    struct c2_addb_ctx *ctx,
		    uint64_t lid,
		    int rc);

c2_bcount_t c2_layout__enum_max_recsize(struct c2_layout_domain *dom);

void c2_layout__instance_init(struct c2_layout_instance *li,
			      const struct c2_fid *gfid,
			      const struct c2_layout_instance_ops *ops);
void c2_layout__instance_fini(struct c2_layout_instance *li);

#define IF_FI_ENABLED_SET_ERROR_AND_JUMP(fault_point, return_code, label)     \
{                                                                             \
	if (C2_FI_ENABLED(fault_point)) {                                     \
		rc = return_code;                                             \
		goto label;                                                   \
	}                                                                     \
}

#define IF_FI_ENABLED_SET_VAR_AND_JUMP(fault_point, var, val, label)          \
{                                                                             \
	if (C2_FI_ENABLED(fault_point)) {                                     \
		var = val;                                                    \
		goto label;                                                   \
	}                                                                     \
}

/** @} end group layout */

/* __COLIBRI_LAYOUT_LAYOUT_INTERNAL_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
