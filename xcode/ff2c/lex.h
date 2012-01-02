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
 * Original creation date: 30-Dec-2011
 */

#ifndef __COLIBRI_XCODE_FF2C_LEX_H__
#define __COLIBRI_XCODE_FF2C_LEX_H__

/**
   @addtogroup xcode
 */
/** @{ */

#include <sys/types.h>                  /* size_t */
/* export */
struct ff2c_context;
struct ff2c_token;

enum ff2c_token_type {
	FTT_IDENTIFIER = 1,
	FTT_REQUIRE,
	FTT_STRING,
	FTT_VOID,
	FTT_U8,
	FTT_U32,
	FTT_U64,
	FTT_OPAQUE,
	FTT_RECORD,
	FTT_UNION,
	FTT_SEQUENCE,
	FTT_OPEN,
	FTT_CLOSE,
	FTT_SEMICOLON,
	FTT_TAG,
	FTT_ESCAPE
};

extern const char *ff2c_token_type_name[];

struct ff2c_token {
	enum ff2c_token_type  ft_type;
	const char           *ft_val;
	size_t                ft_len;
};

int  ff2c_token_get(struct ff2c_context *ctx, struct ff2c_token *tok);
void ff2c_token_put(struct ff2c_context *ctx, struct ff2c_token *tok);

enum { FF2C_CTX_STACK_MAX = 32 };

struct ff2c_context {
	const char        *fc_origin;
	size_t             fc_size;
	const char        *fc_pt;
	size_t             fc_remain;
	struct ff2c_token  fc_stack[FF2C_CTX_STACK_MAX];
	int                fc_depth;
	int                fc_line;
	int                fc_col;
};

void ff2c_context_init(struct ff2c_context *ctx, const char *buf, size_t size);
void ff2c_context_fini(struct ff2c_context *ctx);
int  ff2c_context_loc(struct ff2c_context *ctx, int nr, char *buf);

/** @} end of xcode group */

/* __COLIBRI_XCODE_FF2C_LEX_H__ */
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
