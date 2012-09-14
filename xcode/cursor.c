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
 * Original creation date: 28-Dec-2011
 */

#include "lib/assert.h"

#include "xcode/xcode.h"

/**
   @addtogroup xcode

   @{
 */

void c2_xcode_cursor_init(struct c2_xcode_cursor *it,
			  const struct c2_xcode_obj *obj)
{
	C2_SET0(it);
	c2_xcode_cursor_top(it)->s_obj = *obj;
}

struct c2_xcode_cursor_frame *c2_xcode_cursor_top(struct c2_xcode_cursor *it)
{
	C2_PRE(IS_IN_ARRAY(it->xcu_depth, it->xcu_stack));
	return &it->xcu_stack[it->xcu_depth];
}

int c2_xcode_next(struct c2_xcode_cursor *it)
{
	struct c2_xcode_cursor_frame *top;
	struct c2_xcode_cursor_frame *next;
	const struct c2_xcode_type   *xt;
	int                           nr;

	C2_PRE(it->xcu_depth >= 0);

	top = c2_xcode_cursor_top(it);
	xt  = top->s_obj.xo_type;
	nr  = xt->xct_nr;

	C2_ASSERT(c2_xcode_type_invariant(xt));

	switch (top->s_flag) {
	case C2_XCODE_CURSOR_NONE:
		top->s_flag = C2_XCODE_CURSOR_PRE;
		break;
	case C2_XCODE_CURSOR_IN:
		switch (xt->xct_aggr) {
		case C2_XA_RECORD:
		case C2_XA_TYPEDEF:
			++top->s_fieldno;
			break;
		case C2_XA_SEQUENCE:
			if (top->s_fieldno == 0) {
				top->s_elno = 0;
				top->s_fieldno = 1;
			} else
				++top->s_elno;
			if (top->s_elno >= c2_xcode_tag(&top->s_obj)) {
				top->s_elno = 0;
				top->s_fieldno = 2;
			}
			break;
		case C2_XA_UNION:
			if (top->s_fieldno != 0) {
				top->s_fieldno = nr;
				break;
			}
			for (; top->s_fieldno < nr; ++top->s_fieldno) {
				if (c2_xcode_tag(&top->s_obj) ==
				    xt->xct_child[top->s_fieldno].xf_tag)
					break;
			}
			break;
		case C2_XA_OPAQUE:
		default:
			C2_IMPOSSIBLE("wrong aggregation type");
		}
		/* fall through */
	case C2_XCODE_CURSOR_PRE:
		if (top->s_fieldno < nr) {
			top->s_flag = C2_XCODE_CURSOR_IN;
			if (xt->xct_aggr != C2_XA_ATOM) {
				int result;

				++it->xcu_depth;
				next = c2_xcode_cursor_top(it);
				result = c2_xcode_subobj(&next->s_obj,
							 &top->s_obj,
							 top->s_fieldno,
							 top->s_elno);
				if (result != 0)
					return result;
				next->s_fieldno = 0;
				next->s_elno    = 0;
				next->s_flag    = C2_XCODE_CURSOR_PRE;
				next->s_datum   = 0;
			}
		} else
			top->s_flag = C2_XCODE_CURSOR_POST;
		break;
	case C2_XCODE_CURSOR_POST:
		if (--it->xcu_depth < 0)
			return 0;
		top = c2_xcode_cursor_top(it);
		C2_ASSERT(top->s_flag < C2_XCODE_CURSOR_POST);
		top->s_flag = C2_XCODE_CURSOR_IN;
		break;
	default:
		C2_IMPOSSIBLE("wrong order");
	}
	return +1;
}

void c2_xcode_skip(struct c2_xcode_cursor *it)
{
	c2_xcode_cursor_top(it)->s_flag = C2_XCODE_CURSOR_POST;
}

/** @} end of xcode group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
