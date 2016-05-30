/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 25-Mar-2015
 */


/**
 * @addtogroup XXX
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_XCODE

#include <stdio.h>
#include <string.h>                   /* strcmp */
#include <stdlib.h>                   /* qsort */
#include <err.h>
#include <sys/stat.h>
#include <sysexits.h>

#include "lib/uuid.h"                 /* m0_node_uuid_string_set */
#include "lib/misc.h"                 /* ARRAY_SIZE */
#include "mero/init.h"
#include "module/instance.h"
#include "sm/sm.h"                    /* m0_sm_conf_print */
#include "lib/user_space/trace.h"     /* m0_trace_set_mmapped_buffer */
#include "xcode/xcode.h"

#undef __MERO_XCODE_XLIST_H__

#define _TI(x)
#define _FI(x)
#define _FF(x)

#define _XT(x) M0_EXTERN struct m0_xcode_type *x;
#include "xcode/xlist.h"
#undef _XT

/* Let it be included second time. */
#undef __MERO_XCODE_XLIST_H__

static struct m0_xcode_type **xt[] = {
#define _XT(x) &x,
#include "xcode/xlist.h"
#undef _XT
};

#undef _TI
#undef _FI
#undef _FF

static void field_print(const struct m0_xcode_field *f, int i);
static void type_print(const struct m0_xcode_type *xt);
static void protocol_print(void);
static int  cmp(const void *p0, const void *p1);

static void type_print(const struct m0_xcode_type *xt)
{
	int i;

	printf("\n%-30s %8.8s %4zi",
	       xt->xct_name,
	       xt->xct_aggr == M0_XA_ATOM ?
	       m0_xcode_atom_type_name[xt->xct_atype] :
	       m0_xcode_aggr_name[xt->xct_aggr],
	       xt->xct_sizeof);
	for (i = 0; i < xt->xct_nr; ++i)
		field_print(&xt->xct_child[i], i);
	printf("\nend %s", xt->xct_name);
}

static void protocol_print(void)
{
	int i;

	qsort(xt, ARRAY_SIZE(xt), sizeof xt[0], &cmp);
	printf("\nMero binary protocol.\n");
	for (i = 0; i < ARRAY_SIZE(xt); ++i)
		type_print(*(xt[i]));
	printf("\n");
}

static int cmp(const void *p0, const void *p1)
{
	const struct m0_xcode_type *xt0 = **(void ***)p0;
	const struct m0_xcode_type *xt1 = **(void ***)p1;

	return strcmp(xt0->xct_name, xt1->xct_name);
}

static void field_print(const struct m0_xcode_field *f, int i)
{
	printf("\n    %4i %-20s %-20s %4"PRIi32" [%"PRIx64"]",
	       i, f->xf_name, f->xf_type->xct_name, f->xf_offset, f->xf_tag);
}

void (*m0_sm__conf_init)(const struct m0_sm_conf *conf);

int main(int argc, char **argv)
{
	struct m0 instance = {};
	int       result;

	/* prevent creation of trace file for ourselves */
	m0_trace_set_mmapped_buffer(false);

	/*
	 * break dependency on m0mero.ko module and make ADDB happy,
	 * as we don't need a real node uuid for normal operation of this utility
	 */
	m0_node_uuid_string_set(NULL);

	m0_sm__conf_init = &m0_sm_conf_print;
	result = m0_init(&instance);
	if (result != 0)
		err(EX_CONFIG, "Cannot initialise mero: %d", result);
	protocol_print();
	m0_fini();
	return EX_OK;
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of XXX group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
