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
 * Original creation date: 04/19/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/misc.h"              /* C2_SET0 */
#include "lib/bitstring.h"

#include "fop/fop.h"
#include "addb/addb.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_fops_ff.h"
#include "mdservice/ut/lustre.h"

typedef int (*fop_translate_t)(struct c2_fop *fop, void *data);

static void lustre_copy_fid(struct c2_fop_fid *bf,
                            const struct c2_md_lustre_fid *cf)
{
        bf->f_oid = cf->f_oid;
        bf->f_seq = cf->f_seq;
}

static int lustre_copy_name(struct c2_fop_str *n,
                            const struct c2_md_lustre_logrec *rec)
{
        n->s_buf = c2_alloc(rec->cr_namelen);
        if (n->s_buf == NULL)
                return -ENOMEM;
        n->s_len = rec->cr_namelen;
        memcpy(n->s_buf, rec->cr_name, rec->cr_namelen);
        return 0;
}

enum lustre_la_valid {
        C2_LA_ATIME   = 1 << 0,
        C2_LA_MTIME   = 1 << 1,
        C2_LA_CTIME   = 1 << 2,
        C2_LA_SIZE    = 1 << 3,
        C2_LA_MODE    = 1 << 4,
        C2_LA_UID     = 1 << 5,
        C2_LA_GID     = 1 << 6,
        C2_LA_BLOCKS  = 1 << 7,
        C2_LA_TYPE    = 1 << 8,
        C2_LA_FLAGS   = 1 << 9,
        C2_LA_NLINK   = 1 << 10,
        C2_LA_RDEV    = 1 << 11,
        C2_LA_BLKSIZE = 1 << 12
};

static uint16_t lustre_get_valid(uint16_t valid)
{
        uint16_t result = 0;

        if (valid & C2_LA_ATIME)
                result |= C2_COB_ATIME;
        if (valid & C2_LA_MTIME)
                result |= C2_COB_MTIME;
        if (valid & C2_LA_CTIME)
                result |= C2_COB_CTIME;
        if (valid & C2_LA_SIZE)
                result |= C2_COB_SIZE;
        if (valid & C2_LA_MODE)
                result |= C2_COB_MODE;
        if (valid & C2_LA_UID)
                result |= C2_COB_UID;
        if (valid & C2_LA_GID)
                result |= C2_COB_GID;
        if (valid & C2_LA_BLOCKS)
                result |= C2_COB_BLOCKS;
        if (valid & C2_LA_TYPE)
                result |= C2_COB_TYPE;
        if (valid & C2_LA_FLAGS)
                result |= C2_COB_FLAGS;
        if (valid & C2_LA_NLINK)
                result |= C2_COB_NLINK;
        if (valid & C2_LA_RDEV)
                result |= C2_COB_RDEV;
        if (valid & C2_LA_BLKSIZE)
                result |= C2_COB_BLKSIZE;
        return result;
}

static void lustre_copy_body(struct c2_fop_cob *body,
                             const struct c2_md_lustre_logrec *rec)
{
        body->b_index = rec->cr_index;
        if (rec->cr_valid & C2_LA_SIZE)
                body->b_size = rec->cr_size;
        if (rec->cr_valid & C2_LA_BLKSIZE)
                body->b_blksize = rec->cr_blksize;
        if (rec->cr_valid & C2_LA_BLOCKS)
                body->b_blocks = rec->cr_blocks;
        if (rec->cr_valid & C2_LA_UID)
                body->b_uid = rec->cr_uid;
        if (rec->cr_valid & C2_LA_GID)
                body->b_gid = rec->cr_gid;
        if (rec->cr_valid & C2_LA_ATIME)
                body->b_atime = rec->cr_atime;
        if (rec->cr_valid & C2_LA_MTIME)
                body->b_mtime = rec->cr_mtime;
        if (rec->cr_valid & C2_LA_CTIME)
                body->b_ctime = rec->cr_ctime;
        if (rec->cr_valid & C2_LA_NLINK)
                body->b_nlink = rec->cr_nlink;
        if (rec->cr_valid & C2_LA_MODE)
                body->b_mode = rec->cr_mode;
        body->b_sid = rec->cr_sid;
        body->b_nid = rec->cr_clnid;
        body->b_version = rec->cr_version;
        body->b_flags = rec->cr_flags;
        body->b_valid = lustre_get_valid(rec->cr_valid);
        lustre_copy_fid(&body->b_tfid, &rec->cr_tfid);
        lustre_copy_fid(&body->b_pfid, &rec->cr_pfid);
}

static int lustre_create_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_create *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;

        lustre_copy_body(&d->c_body, rec);
        return lustre_copy_name(&d->c_name, rec);
}

static int lustre_link_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_link *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;

        lustre_copy_body(&d->l_body, rec);
        return lustre_copy_name(&d->l_name, rec);
}

static int lustre_unlink_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_unlink *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;

        lustre_copy_body(&d->u_body, rec);
        return lustre_copy_name(&d->u_name, rec);
}

static int lustre_open_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_open *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;
        lustre_copy_body(&d->o_body, rec);
        return 0;
}

static int lustre_close_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_close *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;
        lustre_copy_body(&d->c_body, rec);
        return 0;
}

static int lustre_setattr_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_setattr *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;
        lustre_copy_body(&d->s_body, rec);
        return 0;
}

static int lustre_rename_fop(struct c2_fop *fop, void *data)
{
        struct c2_fop_rename *d = c2_fop_data(fop);
        struct c2_md_lustre_logrec *rec = data;

        if (rec->cr_type == RT_RENAME) {
                lustre_copy_body(&d->r_sbody, rec);
                return lustre_copy_name(&d->r_sname, rec);
        } else {
                lustre_copy_body(&d->r_tbody, rec);
                return lustre_copy_name(&d->r_tname, rec);
        }
}

int c2_md_lustre_fop_alloc(struct c2_fop **fop, void *data)
{
        struct c2_md_lustre_logrec *rec = data;
        fop_translate_t translate = NULL;
        struct c2_fop_type *fopt = NULL;
        int rc1, rc = 0;

        switch (rec->cr_type) {
        case RT_MARK:
        case RT_IOCTL:
        case RT_TRUNC:
        case RT_HSM:
        case RT_XATTR:
                return -EOPNOTSUPP;
        case RT_CREATE:
        case RT_MKDIR:
        case RT_MKNOD:
        case RT_SOFTLINK:
                fopt = &c2_fop_create_fopt;
                translate = lustre_create_fop;
                break;
        case RT_HARDLINK:
                fopt = &c2_fop_link_fopt;
                translate = lustre_link_fop;
                break;
        case RT_UNLINK:
        case RT_RMDIR:
                fopt = &c2_fop_unlink_fopt;
                translate = lustre_unlink_fop;
                break;
        case RT_RENAME:
                fopt = &c2_fop_rename_fopt;
                translate = lustre_rename_fop;
                rc = -EAGAIN;
                break;
        case RT_EXT:
                C2_ASSERT(*fop != NULL);
                translate = lustre_rename_fop;
                break;
        case RT_OPEN:
                fopt = &c2_fop_open_fopt;
                translate = lustre_open_fop;
                break;
        case RT_CLOSE:
                fopt = &c2_fop_close_fopt;
                translate = lustre_close_fop;
                break;
        case RT_SETATTR:
        case RT_MTIME:
        case RT_CTIME:
        case RT_ATIME:
                fopt = &c2_fop_setattr_fopt;
                translate = lustre_setattr_fop;
                break;
        default:
                return -EINVAL;
        }

        if (*fop == NULL) {
                *fop = c2_fop_alloc(fopt, NULL);
                if (*fop == NULL)
                        return -ENOMEM;
        }

        C2_ASSERT(translate != NULL);
        rc1 = translate(*fop, rec);
        if (rc1 != 0) {
                c2_fop_free(*fop);
                *fop = NULL;
                rc = rc1;
        }

        return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
