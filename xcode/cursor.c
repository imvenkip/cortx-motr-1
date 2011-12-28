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

static struct c2_xcode_cursor_frame *frame_get(struct c2_xcode_cursor *it,
					       int depth)
{
	C2_PRE(IS_IN_ARRAY(depth, it->xcu_stack));
	return &it->xcu_stack[depth];
}

static struct c2_xcode_cursor_frame *top_get(struct c2_xcode_cursor *it)
{
	return frame_get(it, it->xcu_depth);
}

int c2_xcode_next(struct c2_xcode_cursor *it)
{
	struct c2_xcode_cursor_frame *top;
	struct c2_xcode_cursor_frame *next;
	const struct c2_xcode_type   *xt;
	int                           nr;

	C2_PRE(it->xcu_depth >= 0);

	top = top_get(it);
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
			if (top->s_fieldno == 1 &&
			    ++top->s_elno < c2_xcode_tag(&top->s_obj))
				; /* nothing */
			else {
				top->s_elno = 0;
				++top->s_fieldno;
			}
			break;
		case C2_XA_UNION:
			if (top->s_fieldno != 0) {
				top->s_fieldno = nr;
				break;
			}
			for (; top->s_fieldno < nr; ++top->s_fieldno) {
				if (c2_xcode_tag(&top->s_obj) ==
				    xt->xct_child[top->s_fieldno].xf_u.u_tag)
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
				next = top_get(it);
				result = c2_xcode_subobj(&next->s_obj,
							 &top->s_obj,
							 top->s_fieldno,
							 top->s_elno);
				if (result != 0)
					return result;
				next->s_fieldno = 0;
				next->s_elno    = 0;
				next->s_flag    = C2_XCODE_CURSOR_PRE;
			}
		} else
			top->s_flag = C2_XCODE_CURSOR_POST;
		break;
	case C2_XCODE_CURSOR_POST:
		if (--it->xcu_depth < 0)
			return 0;
		top = top_get(it);
		C2_ASSERT(top->s_flag < C2_XCODE_CURSOR_POST);
		top->s_flag = C2_XCODE_CURSOR_IN;
		break;
	default:
		C2_IMPOSSIBLE("wrong order");
	}
	return +1;
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
