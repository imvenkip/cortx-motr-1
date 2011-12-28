/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 25-Dec-2011
 */

#ifndef __COLIBRI_XCODE_XCODE_H__
#define __COLIBRI_XCODE_XCODE_H__

#include "lib/vec.h"                /* c2_bufvec_cursor */
#include "lib/types.h"              /* c2_bcount_t */

/* import */
struct c2_bufvec_cursor;

/* export */
struct c2_xcode;
struct c2_xcode_type;
struct c2_xcode_type_ops;
struct c2_xcode_ctx;
struct c2_xcode_obj;
struct c2_xcode_field;
struct c2_xcode_cursor;

enum c2_xcode_aggr {
	C2_XA_RECORD,
	C2_XA_UNION,
	C2_XA_SEQUENCE,
	C2_XA_TYPEDEF,
	C2_XA_OPAQUE,
	C2_XA_ATOM,
	C2_XA_NR
};

extern const char *c2_xcode_aggr_name[C2_XA_NR];

enum c2_xode_atom_type {
	C2_XAT_VOID,
	C2_XAT_BYTE,
	C2_XAT_U32,
	C2_XAT_U64,

	C2_XAT_NR
};

extern const char *c2_xcode_atom_type_name[C2_XAT_NR];

enum { C2_XCODE_DECOR_MAX = 10 };

struct c2_xcode_field {
	const char                 *xf_name;
	const struct c2_xcode_type *xf_type;
	union {
		uint32_t   u_tag;
		int      (*u_type)(const struct c2_xcode_obj   *par,
				   const struct c2_xcode_type **out);
	}                           xf_u;
	uint32_t                    xf_offset;
	void                       *xf_decor[C2_XCODE_DECOR_MAX];
};

struct c2_xcode_type {
	enum c2_xcode_aggr              xct_aggr;
	const char                     *xct_name;
	const struct c2_xcode_type_ops *xct_ops;
	enum c2_xode_atom_type          xct_atype;
	void                           *xct_decor[C2_XCODE_DECOR_MAX];
	size_t                          xct_sizeof;
	size_t                          xct_nr;
	struct c2_xcode_field           xct_child[0];
};

struct c2_xcode_obj {
	const struct c2_xcode_type *xo_type;
	void                       *xo_ptr;
};

struct c2_xcode_type_ops {
	int (*xto_length)(struct c2_xcode_ctx *ctx, const void *obj);
	int (*xto_encode)(struct c2_xcode_ctx *ctx, const void *obj);
	int (*xto_decode)(struct c2_xcode_ctx *ctx, void *obj);
};

enum c2_xcode_endianness {
	C2_XEND_LE,
	C2_XEND_BE,
	C2_XEND_NR
};

extern const char *c2_xcode_endianness_name[C2_XEND_NR];

struct c2_xcode_ctx {
	enum c2_xcode_endianness xcx_end;
	struct c2_bufvec_cursor  xcx_it;
	c2_bcount_t              xcx_share_threshold;
};

int c2_xcode_decode(struct c2_xcode_ctx *ctx, struct c2_xcode_obj *obj);
int c2_xcode_encode(struct c2_xcode_ctx *ctx, const struct c2_xcode_obj *obj);
int c2_xcode_length(struct c2_xcode_ctx *ctx, const struct c2_xcode_obj *obj);

void *c2_xcode_addr(const struct c2_xcode_obj *obj, int fieldno, uint32_t elno);

#define C2_XCODE_VAL(obj, fieldno, elno, __type) \
        ((__type *)c2_xcode_addr(obj, fieldno, elno))

int c2_xcode_subobj(struct c2_xcode_obj *subobj, const struct c2_xcode_obj *obj,
		    int fieldno, uint32_t elno);

uint32_t c2_xcode_tag(struct c2_xcode_obj *obj);

enum { C2_XCODE_DEPTH_MAX = 10 };

enum c2_xcode_cursor_flag {
	C2_XCODE_CURSOR_NONE,
	C2_XCODE_CURSOR_PRE,
	C2_XCODE_CURSOR_IN,
	C2_XCODE_CURSOR_POST,
	C2_XCODE_CURSOR_NR
};

extern const char *c2_xcode_cursor_flag_name[C2_XCODE_CURSOR_NR];

struct c2_xcode_cursor {
	int xcu_depth;
	struct c2_xcode_cursor_frame {
		struct c2_xcode_obj       s_obj;
		int                       s_fieldno;
		uint32_t                  s_elno;
		enum c2_xcode_cursor_flag s_flag;
	} xcu_stack[C2_XCODE_DEPTH_MAX];
};

int c2_xcode_next(struct c2_xcode_cursor *it);

bool c2_xcode_type_invariant(const struct c2_xcode_type *xt);

extern const struct c2_xcode_type C2_XT_VOID;
extern const struct c2_xcode_type C2_XT_BYTE;
extern const struct c2_xcode_type C2_XT_U32;
extern const struct c2_xcode_type C2_XT_U64;

/** @} end of xcode group */

/* __COLIBRI_XCODE_XCODE_H__ */
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
