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
 * Original creation date: 12/07/2012
 */
#include<stdlib.h>
#include<string.h>
#include<sys/time.h>
#include<errno.h>
#include"cookie.h"
#include"lib/assert.h"

struct c2_time_stamp cookie_generation;

/*This function checks if address is aligned to 8-byte-divisible memory location */
static bool addr_is_sane(const void *obj)
{
	return obj != NULL || (uintptr_t)(const void*)obj%8 == 0;
}

void c2_cookie_init(void)
{
	cookie_generation.ts_time=c2_time_now();
}

int c2_cookie_remote_build(void *obj_ptr, struct c2_cookie *out)
{

       	C2_PRE(out != NULL);

	if (cookie_generation.ts_time > 0) {
		out->co_addr=(uint64_t)obj_ptr;
		out->co_generation=cookie_generation.ts_time;
		return 0;
	} else
		return -1;
}

int c2_cookie_dereference(const struct c2_cookie *cookie, void **out)
{
	void *obj;
	int flag;

	C2_PRE(cookie != NULL);
	C2_PRE(out != NULL);

	obj=(void*)cookie->co_addr;
	flag=addr_is_sane(obj);
	if (flag && cookie->co_generation == cookie_generation.ts_time) {
		*out=obj;
		return 0;

	} else {
		return EPROTO;
	}
}

void c2_cookie_copy(struct c2_cookie *des, const struct c2_cookie *src)
{
		C2_PRE(des != NULL);
		C2_PRE(src != NULL);

		des=memcpy(des,src,sizeof(*src));
}
