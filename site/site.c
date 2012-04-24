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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 02/23/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/rwlock.h"

#include "addb/addb.h"
#include "db/db.h"
#include "fop/fop.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h"

#include "site.h"

/**
   @addtogroup site
   @{
*/

static struct c2_list c2_sites;
 
int c2_site_init(struct c2_site *s, 
                 struct c2_md_store *md)
{
        s->s_mdstore = md;
        c2_list_add(&c2_sites, &s->s_linkage);
        return 0;
}

void c2_site_fini(struct c2_site *s)
{
        c2_list_del(&s->s_linkage);
}

int c2_sites_init(void)
{
	c2_list_init(&c2_sites);
	return 0;
}

void c2_sites_fini(void)
{
	c2_list_fini(&c2_sites);
}

/** @} endgroup site */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
