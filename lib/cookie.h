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
 * Original author: Nachiket Sahasrabudhe <Nachiket_Sahasrabudhe@xyratex.com>
 * Original creation date: 10/07/2012
 */

#ifndef __C2_LIB_COOKIE_H__
#define __C2_LIB_COOKIE_H__


#include<inttypes.h>
#include"../lib/time.h"
typedef uint64_t c2_cookie_local;

struct c2_cookie {
	uint64_t co_addr;
     c2_time_t   co_generation;
};

struct c2_time_stamp {
	c2_time_t ts_time;
};

extern struct c2_time_stamp cookie_generation;

void c2_cookie_init(void);

int c2_cookie_remote_build(void *obj_ptr, struct c2_cookie *out);

int c2_cookie_dereference(const struct c2_cookie *cookie, void **out);

void c2_cookie_copy(struct c2_cookie *des, const struct c2_cookie *src);

#endif /*__C2_LIB_COOKIE_H__ */
