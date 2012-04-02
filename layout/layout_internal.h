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

struct c2_layout;
struct c2_layout_enum;
struct c2_layout_striped;
struct c2_ldb_schema;
enum c2_addb_event_id;
struct c2_addb_ev;
struct c2_addb_ctx;
struct c2_addb_loc;

enum {
	LID_NONE        = 0, /* Invalid layout id. */
	DEFAULT_DB_FLAG = 0,
	PRINT_ADDB_MSG  = 1,
	PRINT_TRACE_MSG = 1,
	LID_APPLICABLE  = 1
};

bool layout_invariant(const struct c2_layout *l);
bool enum_invariant(const struct c2_layout_enum *le, uint64_t lid);
bool striped_layout_invariant(const struct c2_layout_striped *stl,
			      uint64_t lid);

int layout_type_verify(uint32_t lt_id, const struct c2_ldb_schema *schema);
int enum_type_verify(uint32_t let_id, const struct c2_ldb_schema *schema);

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
