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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/08/2010
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/refs.h"

struct test_struct {
	struct c2_ref	ref;
};

static int free_done;

void test_destructor(struct c2_ref *r)
{
	struct test_struct *t;

	t = container_of(r, struct test_struct, ref);

	c2_free(t);
	free_done = 1;
}

void test_refs(void)
{
	struct test_struct *t;

	t = c2_alloc(sizeof(struct test_struct));
	C2_UT_ASSERT(t != NULL);

	free_done = 0;
	c2_ref_init(&t->ref, 1, test_destructor);

	c2_ref_get(&t->ref);
	c2_ref_put(&t->ref);
	c2_ref_put(&t->ref);

	C2_UT_ASSERT(free_done);
}
