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
#ifndef __COLIBRI_CONF_FOP_H__
#define __COLIBRI_CONF_FOP_H__

#include "fop/fop.h"

/**
 * @defgroup conf_fop Configuration FOPs
 *
 * @{
 */

extern struct c2_fop_type c2_conf_fetch_fopt;
extern struct c2_fop_type c2_conf_fetch_resp_fopt;

extern struct c2_fop_type c2_conf_update_fopt;
extern struct c2_fop_type c2_conf_update_resp_fopt;

C2_INTERNAL int c2_conf_fops_init(void);
C2_INTERNAL void c2_conf_fops_fini(void);

/** @} conf_fop */
#endif /* __COLIBRI_CONF_FOP_H__ */
