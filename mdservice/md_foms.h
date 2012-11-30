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
 * Original creation date: 03/29/2011
 */

#pragma once

#ifndef __COLIBRI_MDSERVICE_MD_FOMS_H__
#define __COLIBRI_MDSERVICE_MD_FOMS_H__

#include "mdservice/md_fops_ff.h"

struct c2_fom;
struct c2_fop;
struct c2_fid;

struct c2_cob;
struct c2_cob_nskey;
struct c2_cob_oikey;

struct c2_fom_md {
        /** Generic c2_fom object. */
        struct c2_fom        fm_fom;
};

enum c2_md_fom_phases {
        C2_FOPH_MD_GENERIC = C2_FOPH_NR + 1
};

C2_INTERNAL int c2_md_fop_init(struct c2_fop *fop, struct c2_fom *fom);

/**
   Init request fom for all types of requests.
*/
C2_INTERNAL int c2_md_req_fom_create(struct c2_fop *fop, struct c2_fom **m);
C2_INTERNAL int c2_md_rep_fom_create(struct c2_fop *fop, struct c2_fom **m);

#endif /* __COLIBRI_MDSERVICE_MD_FOMS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
