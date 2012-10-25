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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 07-Sep-2012
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/arith.h"                          /* min64 */
#include "lib/string.h"                         /* sscanf */

#include "xcode/xcode.h"

/**
 * @addtogroup xcode
 *
 * @{
 */

/* xcode.c */
extern ssize_t xcode_alloc(struct c2_xcode_cursor *it,
			   void *(*alloc)(struct c2_xcode_cursor *, size_t));

C2_INTERNAL const const char *space_skip(const char *str)
{
	static const char space[] = " \t\v\n\r";

	while (*str != 0 && strchr(space, *str) != NULL)
		str++;
	return str;
}

static int string_literal(struct c2_xcode_obj *obj, const char *str)
{
	uint64_t                    len;
	const char                 *eol;
	char                       *mem;
	const struct c2_xcode_type *count_type;

	count_type = obj->xo_type->xct_child[0].xf_type;
	if (count_type == &C2_XT_VOID) {
		/* fixed length string */
		len = c2_xcode_tag(obj);
	} else {
		eol = strchr(str, '"');
		if (eol == NULL)
			return -EPROTO;
		len = eol - str;
		switch (count_type->xct_atype) {
		case C2_XAT_U8:
			*C2_XCODE_VAL(obj, 0, 0, uint8_t) = (uint8_t)len;
			break;
		case C2_XAT_U32:
			*C2_XCODE_VAL(obj, 0, 0, uint32_t) = (uint32_t)len;
			break;
		case C2_XAT_U64:
			*C2_XCODE_VAL(obj, 0, 0, uint64_t) = (uint64_t)len;
			break;
		default:
			C2_IMPOSSIBLE("Invalid counter type.");
		}
	}
	*(void **)c2_xcode_addr(obj, 1, ~0ULL) = mem = c2_alloc(len);
	if (mem != NULL) {
		memcpy(mem, str, len);
		return len;
	} else
		return -ENOMEM;
}

static int char_check(const char **str, char ch)
{
	int result = 0;

	if (ch != 0) {
		if (**str == ch) {
			(*str)++;
			*str = space_skip(*str);
		} else
			result = -EPROTO;
	}
	return result;
}

C2_INTERNAL int c2_xcode_read(struct c2_xcode_obj *obj, const char *str)
{
	struct c2_xcode_cursor it;
	int                    result;

	static const char structure[C2_XA_NR][C2_XCODE_CURSOR_NR] = {
		               /* NONE  PRE   IN POST */
		[C2_XA_RECORD]   = { 0, '(',   0, ')' },
		[C2_XA_UNION]    = { 0, '{',   0, '}' },
		[C2_XA_SEQUENCE] = { 0, '[',   0, ']' },
		[C2_XA_TYPEDEF]  = { 0,   0,   0,   0 },
		[C2_XA_OPAQUE]   = { 0,   0,   0,   0 },
		[C2_XA_ATOM]     = { 0,   0,   0,   0 }
	};
	static const char punctuation[C2_XA_NR][3] = {
		                /* 1st  2nd later */
		[C2_XA_RECORD]   = { 0, ',', ',' },
		[C2_XA_UNION]    = { 0, '|',  0  },
		[C2_XA_SEQUENCE] = { 0, ':', ',' },
		[C2_XA_TYPEDEF]  = { 0,  0,   0  },
		[C2_XA_OPAQUE]   = { 0,  0,   0  },
		[C2_XA_ATOM]     = { 0,  0,   0  }
	};
	static const char *fmt[C2_XAT_NR] = {
		[C2_XAT_VOID] = " %0c %n",
		[C2_XAT_U8]   = " %u %n",
		[C2_XAT_U32]  = " %u %n",
		[C2_XAT_U64]  = " %li %n"
	};

	/* check that formats above are valid. */
	C2_CASSERT(sizeof(uint64_t) == sizeof(unsigned long));
	C2_CASSERT(sizeof(uint32_t) == sizeof(unsigned));

	c2_xcode_cursor_init(&it, obj);

	while ((result = c2_xcode_next(&it)) > 0) {
		struct c2_xcode_cursor_frame *top  = c2_xcode_cursor_top(&it);
		struct c2_xcode_obj          *cur  = &top->s_obj;
		enum c2_xcode_cursor_flag     flag = top->s_flag;
		const struct c2_xcode_type   *xt   = cur->xo_type;
		enum c2_xcode_aggr            aggr = xt->xct_aggr;
		char                          ch;

		str = space_skip(str);
		if (flag == C2_XCODE_CURSOR_PRE) {
			result = xcode_alloc(&it, c2_xcode_alloc);
			if (result != 0)
				return result;
			if (it.xcu_depth > 0) {
				int                           order;
				enum c2_xcode_aggr            par;
				struct c2_xcode_cursor_frame *pre = top - 1;

				order = min64(pre->s_datum++,
					      ARRAY_SIZE(punctuation[0]) - 1);
				par = pre->s_obj.xo_type->xct_aggr;
				result = char_check(&str,
						    punctuation[par][order]);
				if (result != 0)
					return result;
			}
			if (xt->xct_aggr == C2_XA_SEQUENCE &&
			    xt->xct_child[1].xf_type == &C2_XT_U8 &&
			    *str == '"') {
				/* string literal */
				result = string_literal(cur, ++str);
				if (result < 0)
					return result;
				str += result + 1;
				c2_xcode_skip(&it);
				continue;
			}
		}
		ch = structure[aggr][flag];
		result = char_check(&str, structure[aggr][flag]);
		if (result != 0)
			return result;
		if (flag == C2_XCODE_CURSOR_PRE && aggr == C2_XA_ATOM) {
			int nob;
			int nr;

			nr = sscanf(str, fmt[xt->xct_atype],
				    cur->xo_ptr, &nob);
			/*
			 * WARNING
			 *
			 *     The C standard says: "Execution of a %n directive
			 *     does not increment the assignment count returned
			 *     at the completion of execution" but the
			 *     Corrigendum seems to contradict this.  Probably
			 *     it is wise not to make any assumptions on the
			 *     effect of %n conversions on the return value.
			 *
			 *                          -- man 3 sscanf
			 *
			 * glibc-2.11.2 and Linux kernel do not increment. See
			 * NO_BUG_IN_ISO_C_CORRIGENDUM_1 in
			 * glibc/stdio-common/vfscanf.c.
			 */
			if (nr != 1)
				return -EPROTO;
			str += nob;
		}
	}
	return *str == 0 ? 0 : -EINVAL;
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
