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

#ifndef __COLIBRI_MDSERVICE_MD_FOPS_H__
#define __COLIBRI_MDSERVICE_MD_FOPS_H__

extern struct c2_fop_type c2_fop_create_fopt;
extern struct c2_fop_type c2_fop_link_fopt;  
extern struct c2_fop_type c2_fop_unlink_fopt;
extern struct c2_fop_type c2_fop_open_fopt;
extern struct c2_fop_type c2_fop_close_fopt;
extern struct c2_fop_type c2_fop_setattr_fopt;
extern struct c2_fop_type c2_fop_getattr_fopt;
extern struct c2_fop_type c2_fop_rename_fopt;
extern struct c2_fop_type c2_fop_readdir_fopt;

extern struct c2_fop_type c2_fop_create_rep_fopt;
extern struct c2_fop_type c2_fop_link_rep_fopt;  
extern struct c2_fop_type c2_fop_unlink_rep_fopt;
extern struct c2_fop_type c2_fop_open_rep_fopt;
extern struct c2_fop_type c2_fop_close_rep_fopt;
extern struct c2_fop_type c2_fop_setattr_rep_fopt;
extern struct c2_fop_type c2_fop_getattr_rep_fopt;
extern struct c2_fop_type c2_fop_rename_rep_fopt;
extern struct c2_fop_type c2_fop_readdir_rep_fopt;

enum c2_fop_metadata_code {
        C2_FOP_FIRST = 64,
        C2_FOP_CREATE = C2_FOP_FIRST,
        C2_FOP_LINK,
        C2_FOP_UNLINK,
        C2_FOP_RENAME,
        C2_FOP_OPEN,
        C2_FOP_CLOSE,
        C2_FOP_SETATTR,
        C2_FOP_GETATTR,
        C2_FOP_READDIR,
        C2_FOP_CREATE_REP,
        C2_FOP_LINK_REP,
        C2_FOP_UNLINK_REP,
        C2_FOP_RENAME_REP,
        C2_FOP_OPEN_REP,
        C2_FOP_CLOSE_REP,
        C2_FOP_SETATTR_REP,
        C2_FOP_GETATTR_REP,
        C2_FOP_READDIR_REP,
        C2_FOP_LAST
};

void c2_md_fop_fini(void);
int c2_md_fop_init(void);

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
