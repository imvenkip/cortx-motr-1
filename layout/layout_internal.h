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

struct c2_layout_domain;
struct c2_layout;
struct c2_layout_ops;
struct c2_layout_type;
struct c2_layout_enum;
struct c2_layout_enum_ops;
struct c2_layout_enum_type;
struct c2_layout_striped;
struct c2_layout_schema;
enum c2_addb_event_id;
struct c2_addb_ev;
struct c2_addb_ctx;
struct c2_addb_loc;

enum {
	/** Invalid layout id. */
	LID_NONE                   = 0,

	DEFAULT_DB_FLAG            = 0,

	/**
	 * Reference coount assigned to layout, during its initialisation and
	 * assigned to layout type and enum type, during their registration.
	 */
	DEFAULT_REF_COUNT          = 1,

	PRINT_ADDB_MSG             = 1,
	PRINT_TRACE_MSG            = 1,

	/** If lid is applicable to ADDB or TRACE message. */
	LID_APPLICABLE             = 1,

	/** Invalid number of elements, for enumeration objects. */
	NR_NONE                    = 0,

	/**
	 * Maximum limit on the number of COB entries those can be stored
	 * inline into the layouts table, while rest of those are stored into
	 * the cob_lists table.
	 */
	LDB_MAX_INLINE_COB_ENTRIES = 20
};

bool domain_invariant(const struct c2_layout_domain *dom);
bool layout_invariant(const struct c2_layout *l);
bool enum_invariant(const struct c2_layout_enum *le, uint64_t lid);
bool striped_layout_invariant(const struct c2_layout_striped *stl,
			      uint64_t lid);
int schema_invariant(const struct c2_layout_schema *schema);

bool is_layout_type_valid(uint32_t lt_id, const struct c2_layout_domain *dom);
bool is_enum_type_valid(uint32_t let_id, const struct c2_layout_domain *dom);

int layout_init(struct c2_layout_domain *dom,
		struct c2_layout *l,
		uint64_t lid, uint64_t pool_id,
		const struct c2_layout_type *type,
		const struct c2_layout_ops *ops);
void layout_fini(struct c2_layout_domain *dom, struct c2_layout *l);

int striped_init(struct c2_layout_domain *dom,
		 struct c2_layout_striped *str_l,
		 struct c2_layout_enum *e,
		 uint64_t lid, uint64_t pool_id,
		 const struct c2_layout_type *type,
		 const struct c2_layout_ops *ops);
void striped_fini(struct c2_layout_domain *dom,
		  struct c2_layout_striped *str_l);

int enum_init(struct c2_layout_domain *dom,
	      struct c2_layout_enum *le, uint64_t lid,
	      const struct c2_layout_enum_type *et,
	      const struct c2_layout_enum_ops *ops);
void enum_fini(struct c2_layout_domain *dom,
	       struct c2_layout_enum *le);

void layout_log(const char *fn_name,
		const char *err_msg,
		bool if_addb_msg, /* If ADDB message is to be printed. */
		bool if_trace_msg, /* If C2_LOG message is to be printed. */
		enum c2_addb_event_id ev_id,
		struct c2_addb_ctx *ctx,
		bool if_lid, /* If LID is applicable for the log message. */
		uint64_t lid,
		int rc);

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
