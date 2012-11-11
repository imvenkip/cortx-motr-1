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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06-Nov-2012
 */

#pragma once

#ifndef __COLIBRI_STOB_MIRROR_H__
#define __COLIBRI_STOB_MIRROR_H__

/**
 * @defgroup stobmirror Mirroring stob implementation.
 *
 * Mirror stob type (c2_mirror_stob_type) implements a simple multi-way
 * mirroring of data.
 *
 * At the configuration time, mirror stob domain is supplied with an array of
 * "target domains" (c2_mirror_stob_setup()), which can be any stob domains. A
 * stob in the mirror domain uses "target stobs", one in each of the target
 * domains to store and retrieve mirror stob's data. All target stobs of a
 * mirror stob have the same stob identifier as the mirror stob. Operations on
 * target stobs (creation, lookup, read and write) are induced by the
 * corresponding operationson the mirror stob. The implementation assumes that
 * target domains are not accessed except through the mirror stob interface.
 *
 *
 * "I will pack my comb and mirror to praxis oval owes and artless awes."
 *
 * @{
 */

#include "stob/stob.h"

extern struct c2_stob_type c2_mirror_stob_type;

/**
 * Setup a mirror storage domain.
 *
 * @param mdom - mirror type stob domain;
 * @param nr - number of targets;
 * @param targets - array of targets;
 */
int  c2_mirror_stob_setup(struct c2_stob_domain *dom, uint32_t nr,
			  struct c2_stob_domain **targets);

void c2_mirror_failed(struct c2_stob_domain *dom, uint64_t mask);
void c2_mirror_repair(struct c2_stob_domain *dom, uint64_t mask);


/** @} end group stobmirror */

/* __COLIBRI_STOB_MIRROR_H__ */
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
