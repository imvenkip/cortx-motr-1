/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 27-Jan-2013
 */


#pragma once

#ifndef __MERO_DTM_DOMAIN_H__
#define __MERO_DTM_DOMAIN_H__


/**
 * @defgroup dtm
 *
 * @{
 */

#include "lib/tlist.h"
#include "sm/sm.h"
#include "dtm/nucleus.h"

/* export */
struct m0_dtm_domain;
struct m0_dtm_domain_dest;

struct m0_dtm_domain {
	struct m0_dtm_hi dom_hi;
	struct m0_tlist  dom_dest;
};

struct m0_dtm_domain_dest {
	struct m0_rpc_service *ddd_ser;
};

M0_INTERNAL void m0_dtm_domain_init(struct m0_dtm_domain *dom);
M0_INTERNAL void m0_dtm_domain_fini(struct m0_dtm_domain *dom);

M0_INTERNAL void m0_dtm_domain_add(struct m0_dtm_domain *dom,
				   struct m0_dtm_domain_dest *dest);
M0_INTERNAL void m0_dtm_domain_open(struct m0_dtm_domain *dom);
M0_INTERNAL void m0_dtm_domain_close(struct m0_dtm_domain *dom);
M0_INTERNAL void m0_dtm_domain_connect(struct m0_dtm_domain *dom);
M0_INTERNAL void m0_dtm_domain_disconnect(struct m0_dtm_domain *dom);


/** @} end of dtm group */

#endif /* __MERO_DTM_DOMAIN_H__ */


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
