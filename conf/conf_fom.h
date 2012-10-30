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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */
#pragma once
#ifndef __COLIBRI_CONF_FOM_H__
#define __COLIBRI_CONF_FOM_H__

#include "conf_fop.h"

/**
 * State transition function for "c2_conf_fetch" FOP.
 */
int c2_fom_fetch_state(struct c2_fom *fom);
size_t c2_fom_fetch_home_locality(const struct c2_fom *fom);
void c2_fop_fetch_fom_fini(struct c2_fom *fom);

/**
 * State transition function for "c2_conf_update" FOP.
 */
int c2_fom_update_state(struct c2_fom *fom);
size_t c2_fom_update_home_locality(const struct c2_fom *fom);
void c2_fop_update_fom_fini(struct c2_fom *fom);

#endif /* __COLIBRI_CONF_FOM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
