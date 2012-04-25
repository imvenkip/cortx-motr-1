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

#ifndef __COLIBRI_MDSERVICE_MD_FOMS_H__
#define __COLIBRI_MDSERVICE_MD_FOMS_H__

struct c2_fom;
struct c2_fop;
struct c2_fid;

struct c2_cob;
struct c2_fop_fid;
struct c2_cob_nskey;
struct c2_cob_oikey;

struct c2_fom_md {
	/** Generic c2_fom object. */
        struct c2_fom        fm_fom;
};

enum c2_md_fom_phases {
        C2_FOPH_MD_GENERIC = C2_FOPH_NR + 1
};

/**
   Init request fom for all types of requests.
*/
int c2_md_req_fom_create(struct c2_fop *fop, 
                         struct c2_fop_ctx *ctx, 
                         struct c2_fom **m);
int c2_md_rep_fom_create(struct c2_fop *fop, 
                         struct c2_fop_ctx *ctx, 
                         struct c2_fom **m);

/**
   Init reply fom.
*/
int c2_md_rep_fom_init(struct c2_fop *fop, 
                       struct c2_fop_ctx *ctx, 
                       struct c2_fom **m);

/**
   Make in-memory fid from wire fid (wid).
*/
void c2_md_make_fid(struct c2_fid *fid, 
                    const struct c2_fop_fid *wid);

/**
   Make nskey from passed parent fid and child name.
*/
void c2_md_make_nskey(struct c2_cob_nskey **keyh, 
                      const struct c2_fop_fid *fid, 
                      struct c2_fop_str *name);

/**
   Make oikey from passed child fid and link number.
*/
void c2_md_make_oikey(struct c2_cob_oikey *oikey, 
                      const struct c2_fop_fid *fid,
                      int linkno);

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
