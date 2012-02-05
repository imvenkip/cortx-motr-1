/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 19-Sep-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_BUF_EXT_H__
#define __COLIBRI_CONF_BUF_EXT_H__

#include "lib/types.h" /* bool */

/**
 * @defgroup buf_ext c2_buf extensions
 *
 * @see @ref buf
 *
 * @{
 */

struct c2_buf;

/** Does the buffer point at anything? */
bool c2_buf_is_aimed(const struct c2_buf *buf);

/**
 * Do `buf' and `str' contain equal sequences of non-'\0' characters?
 *
 * @pre  c2_buf_is_aimed(buf) && str != NULL
 */
bool c2_buf_streq(const struct c2_buf *buf, const char *str);

/**
 * Duplicates a string pointed to by buf->b_addr.
 *
 * Maximum length of the resulting string, including null character,
 * is buf->b_nob.
 *
 * @pre  c2_buf_is_aimed(buf)
 */
char *c2_buf_strdup(const struct c2_buf *buf);

/** @} buf_ext */
#endif /* __COLIBRI_CONF_BUF_EXT_H__ */
