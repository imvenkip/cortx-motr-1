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

#include <sys/stat.h>    /* S_ISDIR */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/rwlock.h"

#include "net/net.h"
#include "fid/fid.h"
#include "fop/fop.h"
#include "cob/cob.h"
#include "reqh/reqh.h"
#include "rpc/rpc_opcodes.h"
#include "fop/fom_generic.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_fops_ff.h"
#include "mdservice/md_foms.h"

#include "mdstore/mdstore.h"

/**
   Make in-memory fid from wire fid (wid).
*/
void c2_md_fid_make(struct c2_fid *fid, const struct c2_fop_fid *wid)
{
        fid->f_container = wid->f_seq;
        fid->f_key = wid->f_oid;
}

/**
   Make nskey from passed parent fid and child name.
*/
void c2_md_nskey_make(struct c2_cob_nskey **keyh,
                      const struct c2_fop_fid *fid,
                      struct c2_fop_str *name)
{
        struct c2_fid cfid;

        c2_md_fid_make(&cfid, fid);
        c2_cob_nskey_make(keyh, &cfid, (char *)name->s_buf, name->s_len);
}

/**
   Make oikey from passed child fid and liunk number.
*/
void c2_md_oikey_make(struct c2_cob_oikey *oikey,
                      const struct c2_fop_fid *fid,
                      int linkno)
{
        c2_md_fid_make(&oikey->cok_fid, fid);
        oikey->cok_linkno = linkno;
}

static void c2_md_fop_cob2attr(struct c2_cob_attr *attr,
                               struct c2_fop_cob *body)
{
        C2_SET0(attr);
        c2_md_fid_make(&attr->ca_pfid, &body->b_pfid);
        c2_md_fid_make(&attr->ca_tfid, &body->b_tfid);
        attr->ca_flags = body->b_valid;
        if (body->b_valid & C2_COB_MODE)
                attr->ca_mode = body->b_mode;
        if (body->b_valid & C2_COB_UID)
                attr->ca_uid = body->b_uid;
        if (body->b_valid & C2_COB_GID)
                attr->ca_gid = body->b_gid;
        if (body->b_valid & C2_COB_ATIME)
                attr->ca_atime = body->b_atime;
        if (body->b_valid & C2_COB_MTIME)
                attr->ca_mtime = body->b_mtime;
        if (body->b_valid & C2_COB_CTIME)
                attr->ca_ctime = body->b_ctime;
        if (body->b_valid & C2_COB_NLINK)
                attr->ca_nlink = body->b_nlink;
        if (body->b_valid & C2_COB_RDEV)
                attr->ca_rdev = body->b_rdev;
        if (body->b_valid & C2_COB_SIZE)
                attr->ca_size = body->b_size;
        if (body->b_valid & C2_COB_BLKSIZE)
                attr->ca_blksize = body->b_blksize;
        if (body->b_valid & C2_COB_BLOCKS)
                attr->ca_blocks = body->b_blocks;
        attr->ca_version = body->b_version;
}

static void c2_md_fop_attr2cob(struct c2_fop_cob *body,
                               struct c2_cob_attr *attr)
{
        body->b_valid = attr->ca_flags;
        if (body->b_valid & C2_COB_UID)
                body->b_mode = attr->ca_mode;
        if (body->b_valid & C2_COB_UID)
                body->b_uid = attr->ca_uid;
        if (body->b_valid & C2_COB_UID)
                body->b_gid = attr->ca_gid;
        if (body->b_valid & C2_COB_ATIME)
                body->b_atime = attr->ca_atime;
        if (body->b_valid & C2_COB_MTIME)
                body->b_mtime = attr->ca_mtime;
        if (body->b_valid & C2_COB_CTIME)
                body->b_ctime = attr->ca_ctime;
        if (body->b_valid & C2_COB_NLINK)
                body->b_nlink = attr->ca_nlink;
        if (body->b_valid & C2_COB_RDEV)
                body->b_rdev = attr->ca_rdev;
        if (body->b_valid & C2_COB_SIZE)
                body->b_size = attr->ca_size;
        if (body->b_valid & C2_COB_BLKSIZE)
                body->b_blksize = attr->ca_blksize;
        if (body->b_valid & C2_COB_BLOCKS)
                body->b_blocks = attr->ca_blocks;
        body->b_version = attr->ca_version;
}

/**
   Create object in @pfid with @tfid and attributes from @attr.
   Handle possible variants for existing/missing objects and
   links.
 */
static int c2_md_create(struct c2_md_store  *md,
                        struct c2_fid       *pfid,
                        struct c2_fid       *tfid,
                        struct c2_cob_attr  *attr,
                        struct c2_db_tx     *tx)
{
        struct c2_cob *scob = NULL;
        int            rc;

        rc = c2_md_store_locate(md, tfid, &scob,
                                C2_MD_LOCATE_STORED, tx);
        if (rc == -ENOENT) {
                /*
                 * No file at all, let's create it no matter
                 * where we are called from, scan or changelog.
                 */
                rc = c2_md_store_create(md, pfid, attr, &scob, tx);
        } else if (rc == 0) {
                /*
                 * There is statdata name, this must be hardlink.
                 */
                rc = c2_md_store_link(md, pfid, scob, attr->ca_name,
                                      attr->ca_namelen, tx);
                /*
                 * Each operation changes target attributes (times).
                 * We want to keep them up-to-date.
                 */
                if (rc == 0)
                        rc = c2_md_store_setattr(md, scob, attr, tx);
        }
        if (scob)
                c2_cob_put(scob);
        return rc;
}

static int c2_md_create_tick(struct c2_fom *fom)
{
        struct c2_cob_attr        attr;
        struct c2_fop_cob        *body;
        struct c2_fop_create     *req;
        struct c2_fop_create_rep *rep;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             pfid;
        struct c2_fid             tfid;
        int                       rc;
        struct c2_local_service  *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->c_body;
        c2_md_fop_cob2attr(&attr, body);

        attr.ca_name = (char *)req->c_name.s_buf;
        attr.ca_namelen = req->c_name.s_len;

        if (S_ISLNK(attr.ca_mode))
                attr.ca_link = (char *)req->c_target.s_buf;

        c2_md_fid_make(&pfid, &body->b_pfid);
        c2_md_fid_make(&tfid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_create(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &pfid, &tfid,
                          &attr, &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_link_tick(struct c2_fom *fom)
{
        struct c2_fop_cob        *body;
        struct c2_fop_link       *req;
        struct c2_fop_link_rep   *rep;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             tfid;
        struct c2_fid             pfid;
        struct c2_cob_attr        attr;
        int                       rc;
        struct c2_local_service  *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->l_body;
        c2_md_fop_cob2attr(&attr, body);
        attr.ca_name = (char *)req->l_name.s_buf;
        attr.ca_namelen = req->l_name.s_len;

        c2_md_fid_make(&pfid, &body->b_pfid);
        c2_md_fid_make(&tfid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_create(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &pfid, &tfid,
                          &attr, &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_unlink_tick(struct c2_fom *fom)
{
        struct c2_cob_attr        attr;
        struct c2_fop_cob        *body;
        struct c2_cob            *scob = NULL;
        struct c2_fop_unlink     *req;
        struct c2_fop_unlink_rep *rep;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             tfid;
        struct c2_fid             pfid;
        struct c2_db_tx          *tx;
        struct c2_md_store       *md;
        int                       rc;
        struct c2_local_service  *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        md = fom->fo_loc->fl_dom->fd_reqh->rh_mdstore;
        tx = &fom->fo_tx.tx_dbtx;

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->u_body;
        c2_md_fop_cob2attr(&attr, body);
        attr.ca_name = (char *)req->u_name.s_buf;
        attr.ca_namelen = req->u_name.s_len;

        c2_md_fid_make(&pfid, &body->b_pfid);
        c2_md_fid_make(&tfid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_store_locate(md, &tfid, &scob, 
                                C2_MD_LOCATE_STORED,
                                tx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_md_store_unlink(md, &pfid, scob, attr.ca_name,
                                attr.ca_namelen, tx);
        if (rc == 0 && scob->co_nsrec.cnr_nlink > 0)
                rc = c2_md_store_setattr(md, scob, &attr, tx);
        c2_cob_put(scob);
        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_rename(struct c2_md_store  *md,
                        struct c2_fid       *pfid_tgt,
                        struct c2_fid       *pfid_src,
                        struct c2_fid       *tfid_tgt,
                        struct c2_fid       *tfid_src,
                        struct c2_cob_attr  *tattr,
                        struct c2_cob_attr  *sattr,
                        struct c2_cob       *tcob,
                        struct c2_cob       *scob,
                        struct c2_db_tx     *tx)
{
        int rc;

        C2_ASSERT(scob != NULL);
        C2_ASSERT(tcob != NULL);

        /*
         * Do normal rename as all objects are fine.
         */
        rc = c2_md_store_rename(md, pfid_tgt, pfid_src, tcob, scob,
                                tattr->ca_name, tattr->ca_namelen,
                                sattr->ca_name, sattr->ca_namelen, tx);
        if (rc != 0)
                return rc;
        /*
         * Update attributes of source and target.
         */
        if (c2_fid_eq(scob->co_fid, tcob->co_fid)) {
                if (tcob->co_nsrec.cnr_nlink > 0)
                        rc = c2_md_store_setattr(md, tcob, tattr, tx);
        } else {
                rc = c2_md_store_setattr(md, scob, sattr, tx);
                if (rc == 0 && tcob->co_nsrec.cnr_nlink > 0)
                        rc = c2_md_store_setattr(md, tcob, tattr, tx);
        }
        return rc;
}

static int c2_md_rename_tick(struct c2_fom *fom)
{
        struct c2_fop_cob        *sbody;
        struct c2_fop_cob        *tbody;
        struct c2_fop_rename     *req;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_cob            *tcob = NULL;
        struct c2_cob            *scob = NULL;
        struct c2_fid             tfid_src;
        struct c2_fid             tfid_tgt;
        struct c2_fid             pfid_tgt;
        struct c2_fid             pfid_src;
        struct c2_cob_attr        sattr;
        struct c2_cob_attr        tattr;
        struct c2_db_tx          *tx;
        struct c2_md_store       *md;
        int                       rc;
        struct c2_local_service  *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        tx = &fom->fo_tx.tx_dbtx;
        md = fom->fo_loc->fl_dom->fd_reqh->rh_mdstore;

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        req = c2_fop_data(fop);
        sbody = &req->r_sbody;
        tbody = &req->r_tbody;

        c2_md_fid_make(&pfid_src, &sbody->b_pfid);
        c2_md_fid_make(&pfid_tgt, &tbody->b_pfid);

        c2_md_fid_make(&tfid_src, &sbody->b_tfid);
        c2_md_fid_make(&tfid_tgt, &tbody->b_tfid);

        c2_md_fop_cob2attr(&tattr, tbody);
        tattr.ca_name = (char *)req->r_tname.s_buf;
        tattr.ca_namelen = req->r_tname.s_len;

        c2_md_fop_cob2attr(&sattr, sbody);
        sattr.ca_name = (char *)req->r_sname.s_buf;
        sattr.ca_namelen = req->r_sname.s_len;

        c2_fom_block_enter(fom);
        rc = c2_md_store_locate(md, &tfid_src, &scob,
                                C2_MD_LOCATE_STORED, tx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }
        if (c2_fid_eq(&tfid_tgt, &tfid_src)) {
                rc = c2_md_rename(md, &pfid_tgt, &pfid_src, &tfid_tgt,
                                  &tfid_src, &tattr, &sattr, scob, scob, tx);
        } else {
                rc = c2_md_store_locate(md, &tfid_tgt, &tcob,
                                        C2_MD_LOCATE_STORED, tx);
                if (rc != 0) {
                        c2_fom_block_leave(fom);
                        goto out;
                }
                rc = c2_md_rename(md, &pfid_tgt, &pfid_src, &tfid_tgt,
                                  &tfid_src, &tattr, &sattr, tcob, scob, tx);
                c2_cob_put(tcob);
        }
        c2_cob_put(scob);
        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_open_tick(struct c2_fom *fom)
{
        struct c2_fop_cob        *body;
        struct c2_cob            *cob;
        struct c2_fop_open       *req;
        struct c2_fop_open_rep   *rep;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             fid;
        struct c2_cob_attr        attr;
        int                       rc;
        struct c2_local_service  *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->o_body;
        c2_md_fop_cob2attr(&attr, body);

        c2_md_fid_make(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_store_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid, &cob,
                                C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc == 0) {
                rc = c2_md_store_open(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob,
                                      body->b_flags, &fom->fo_tx.tx_dbtx);
                if (rc == 0 &&
                    (!(attr.ca_flags & C2_COB_NLINK) || attr.ca_nlink > 0)) {
                        /*
                         * Mode contains open flags that we don't need
                         * to store to db.
                         */
                        attr.ca_flags &= ~C2_COB_MODE;
                        rc = c2_md_store_setattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                                 cob, &attr, &fom->fo_tx.tx_dbtx);
                }
                c2_cob_put(cob);
        } else if (rc == -ENOENT) {
                /*
                 * Lustre has create before open in case of OPEN_CREATE.
                 * We don't have to create anything here as file already
                 * should exist, let's just check this.
                 */
                //C2_ASSERT(!(body->b_flags & C2_MD_OPEN_CREAT));
        } else if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_close_tick(struct c2_fom *fom)
{
        struct c2_fop_cob        *body;
        struct c2_cob            *cob;
        struct c2_fop_close      *req;
        struct c2_fop_close_rep  *rep;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             fid;
        struct c2_cob_attr        attr;
        int                       rc;
        struct c2_local_service  *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->c_body;
        c2_md_fop_cob2attr(&attr, body);

        c2_md_fid_make(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        /*
         * @todo: This should lookup for cobs in special opened
         * cobs table. But so far orphans and open/close are not
         * quite implemented and we lookup on main store to make
         * ut happy.
         */
        rc = c2_md_store_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid, &cob,
                                C2_MD_LOCATE_STORED/*OPENED*/, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_md_store_close(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob, 
                               &fom->fo_tx.tx_dbtx);
        if (rc == 0 && 
            (!(attr.ca_flags & C2_COB_NLINK) || attr.ca_nlink > 0)) {
                /*
                 * Mode contains open flags that we don't need
                 * to store to db.
                 */
                attr.ca_flags &= ~C2_COB_MODE;
                rc = c2_md_store_setattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob,
                                         &attr, &fom->fo_tx.tx_dbtx);
        }
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_setattr_tick(struct c2_fom *fom)
{
        struct c2_cob_attr             attr;
        struct c2_fop_cob             *body;
        struct c2_cob                 *cob;
        struct c2_fop_setattr         *req;
        struct c2_fop_setattr_rep     *rep;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        int                            rc;
        struct c2_local_service       *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->s_body;
        c2_md_fop_cob2attr(&attr, body);

        c2_md_fid_make(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);

        /*
         * Setattr fop does not carry enough information to create
         * an object in case there is no target yet. This is why
         * we return quickly if no object is found.
         */
        rc = c2_md_store_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_md_store_setattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob, &attr,
                                 &fom->fo_tx.tx_dbtx);
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_getattr_tick(struct c2_fom *fom)
{
        struct c2_cob_attr             attr;
        struct c2_fop_cob             *body;
        struct c2_cob                 *cob;
        struct c2_fop_getattr         *req;
        struct c2_fop_getattr_rep     *rep;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        int                            rc;
        struct c2_local_service       *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);
        body = &req->g_body;

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        c2_md_fid_make(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_store_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_md_store_getattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob, &attr,
                                 &fom->fo_tx.tx_dbtx);
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
        if (rc == 0)
                c2_md_fop_attr2cob(&rep->g_body, &attr);
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

#define C2_MD_READDIR_BUF_ALLOC 4096

static int c2_md_readdir_tick(struct c2_fom *fom)
{
        struct c2_fop_cob             *body;
        struct c2_cob                 *cob;
        struct c2_fop_readdir         *req;
        struct c2_fop_readdir_rep     *rep;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        struct c2_rdpg                 rdpg;
        void                          *addr;
        int                            rc;
        struct c2_local_service       *svc;

        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                   Don't send reply in case there is local reply consumer defined.
                 */
                if (svc && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        /**
           Init some fop fields (full path) that require mdstore and other
           initialialized structres.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->r_body;
        c2_md_fid_make(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_store_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        if (!S_ISDIR(cob->co_omgrec.cor_mode)) {
                c2_fom_block_leave(fom);
                rc = -ENOTDIR;
                c2_cob_put(cob);
                goto out;
        }

        rdpg.r_pos = c2_bitstring_alloc((char *)req->r_pos.s_buf, 
                                        req->r_pos.s_len);
        if (rdpg.r_pos == NULL) {
                c2_fom_block_leave(fom);
                c2_cob_put(cob);
                rc = -ENOMEM;
                goto out;
        }

        addr = c2_alloc(C2_MD_READDIR_BUF_ALLOC);
        if (addr == NULL) {
                c2_fom_block_leave(fom);
                c2_bitstring_free(rdpg.r_pos);
                c2_cob_put(cob);
                rc = -ENOMEM;
                goto out;
        }

        c2_buf_init(&rdpg.r_buf, addr, C2_MD_READDIR_BUF_ALLOC);

        rc = c2_md_store_readdir(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob, &rdpg,
                                 &fom->fo_tx.tx_dbtx);
        c2_bitstring_free(rdpg.r_pos);
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
        if (rc < 0) {
                c2_free(addr);
                goto out;
        }

        /*
         * Prepare end position.
         */
        rep->r_end.s_len = c2_bitstring_len_get(rdpg.r_end);
        rep->r_end.s_buf = c2_alloc(rep->r_end.s_len);
        if (rep->r_end.s_buf == NULL) {
                c2_free(addr);
                rc = -ENOMEM;
                goto out;
        }
        strncpy((char *)rep->r_end.s_buf, c2_bitstring_buf_get(rdpg.r_end),
                rep->r_end.s_len);

        /*
         * Prepare buf with data.
         */
        rep->r_buf.b_count = rdpg.r_buf.b_nob;
        rep->r_buf.b_addr = rdpg.r_buf.b_addr;
out:
        fom->fo_rc = rc;
        c2_fom_phase_set(fom, rc < 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_set(fom, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_req_path_get(struct c2_md_store *mdstore,
                              struct c2_fid *fid,
                              struct c2_fop_str *str)
{
        int rc;

        rc = c2_md_store_path(mdstore, fid, (char **)&str->s_buf);
        if (rc != 0)
                return rc;
        str->s_len = strlen((char *)str->s_buf);
        return 0;
}

static inline struct c2_fid *c2_md_fid_get(struct c2_fop_fid *fid)
{
        static struct c2_fid fid_fop2mem;
        c2_md_fid_make(&fid_fop2mem, fid);
        return &fid_fop2mem;
}

int c2_md_fop_init(struct c2_fop *fop, struct c2_fom *fom)
{
        struct c2_fop_create    *create;
        struct c2_fop_unlink    *unlink;
        struct c2_fop_rename    *rename;
        struct c2_fop_link      *link;
        struct c2_fop_setattr   *setattr;
        struct c2_fop_getattr   *getattr;
        struct c2_fop_open      *open;
        struct c2_fop_close     *close;
        struct c2_fop_readdir   *readdir;
        int                      rc;

        C2_PRE(fop != NULL);

        switch (fop->f_type->ft_rpc_item_type.rit_opcode) {
        case C2_MDSERVICE_CREATE_OPCODE:
                create = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&create->c_body.b_pfid),
                                        &create->c_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                link = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&link->l_body.b_pfid),
                                        &link->l_tpath);
                if (rc != 0)
                        return rc;
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&link->l_body.b_tfid),
                                        &link->l_spath);
                if (rc != 0) {
                        c2_free(link->l_tpath.s_buf);
                        link->l_tpath.s_buf = NULL;
                        link->l_tpath.s_len = 0;
                        return rc;
                }
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                unlink = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&unlink->u_body.b_pfid),
                                        &unlink->u_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                rename = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&rename->r_sbody.b_pfid),
                                        &rename->r_spath);
                if (rc != 0)
                        return rc;
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&rename->r_tbody.b_pfid),
                                        &rename->r_tpath);
                if (rc != 0) {
                        c2_free(rename->r_spath.s_buf);
                        rename->r_spath.s_buf = NULL;
                        rename->r_spath.s_len = 0;
                        return rc;
                }
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                open = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&open->o_body.b_tfid),
                                        &open->o_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                close = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&close->c_body.b_tfid),
                                        &close->c_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                setattr = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&setattr->s_body.b_tfid),
                                        &setattr->s_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                getattr = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&getattr->g_body.b_tfid),
                                        &getattr->g_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                readdir = c2_fop_data(fop);
                rc = c2_md_req_path_get(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                        c2_md_fid_get(&readdir->r_body.b_tfid),
                                        &readdir->r_path);
                if (rc != 0)
                        return rc;
                break;
        default:
                return -EOPNOTSUPP;
        }

        return rc;
}

void c2_md_fop_free(struct c2_fop *fop)
{
        struct c2_fop_create    *create;
        struct c2_fop_unlink    *unlink;
        struct c2_fop_rename    *rename;
        struct c2_fop_link      *link;
        struct c2_fop_setattr   *setattr;
        struct c2_fop_getattr   *getattr;
        struct c2_fop_open      *open;
        struct c2_fop_close     *close;
        struct c2_fop_readdir   *readdir;

        switch (fop->f_type->ft_rpc_item_type.rit_opcode) {
        case C2_MDSERVICE_CREATE_OPCODE:
                create = c2_fop_data(fop);
                if (create->c_name.s_len != 0)
                        c2_free(create->c_name.s_buf);
                if (create->c_target.s_len != 0)
                        c2_free(create->c_target.s_buf);
                if (create->c_path.s_len != 0)
                        c2_free(create->c_path.s_buf);
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                link = c2_fop_data(fop);
                if (link->l_name.s_len != 0)
                        c2_free(link->l_name.s_buf);
                if (link->l_spath.s_len != 0)
                        c2_free(link->l_spath.s_buf);
                if (link->l_tpath.s_len != 0)
                        c2_free(link->l_tpath.s_buf);
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                unlink = c2_fop_data(fop);
                if (unlink->u_name.s_len != 0)
                        c2_free(unlink->u_name.s_buf);
                if (unlink->u_path.s_len != 0)
                        c2_free(unlink->u_path.s_buf);
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                rename = c2_fop_data(fop);
                if (rename->r_sname.s_len != 0)
                        c2_free(rename->r_sname.s_buf);
                if (rename->r_tname.s_len != 0)
                        c2_free(rename->r_tname.s_buf);
                if (rename->r_spath.s_len != 0)
                        c2_free(rename->r_spath.s_buf);
                if (rename->r_tpath.s_len != 0)
                        c2_free(rename->r_tpath.s_buf);
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                setattr = c2_fop_data(fop);
                if (setattr->s_path.s_len != 0)
                        c2_free(setattr->s_path.s_buf);
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                getattr = c2_fop_data(fop);
                if (getattr->g_path.s_len != 0)
                        c2_free(getattr->g_path.s_buf);
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                open = c2_fop_data(fop);
                if (open->o_path.s_len != 0)
                        c2_free(open->o_path.s_buf);
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                close = c2_fop_data(fop);
                if (close->c_path.s_len != 0)
                        c2_free(close->c_path.s_buf);
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                readdir = c2_fop_data(fop);
                if (readdir->r_path.s_len != 0)
                        c2_free(readdir->r_path.s_buf);
                break;
        default:
                break;
        }
}

static void c2_md_req_fom_fini(struct c2_fom *fom)
{
        struct c2_fom_md         *fom_obj;
        struct c2_local_service  *svc;

        /* Let local sevice know that we have finished. */
        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;
        if (svc && svc->s_ops->lso_fini)
                svc->s_ops->lso_fini(svc, fom);

        /* Free all fop fields and fop itself. */
        c2_md_fop_free(fom->fo_fop);
        c2_fop_free(fom->fo_fop);

        /* XXX: Free all rep fop field. */

        /* Free fop_rep as we don't need it anymore. */
        c2_fop_free(fom->fo_rep_fop);

        /* Fini fom itself. */
        c2_fom_fini(fom);

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);
        c2_free(fom_obj);
}

static size_t c2_md_req_fom_locality_get(const struct c2_fom *fom)
{
        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_fop != NULL);

        return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static const struct c2_fom_ops c2_md_fom_create_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_create_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_link_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_link_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_unlink_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_unlink_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_rename_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_rename_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_open_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_open_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_close_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick  = c2_md_close_tick,
        .fo_fini  = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_setattr_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_setattr_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_getattr_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_getattr_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_readdir_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_readdir_tick,
        .fo_fini   = c2_md_req_fom_fini
};

int c2_md_rep_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        return 0;
}

int c2_md_req_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_fom           *fom;
        struct c2_fom_md        *fom_obj;
        struct c2_fop_type      *rep_fopt;
        const struct c2_fom_ops *ops;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);

        fom_obj = c2_alloc(sizeof(struct c2_fom_md));
        if (fom_obj == NULL)
                return -ENOMEM;

        switch (fop->f_type->ft_rpc_item_type.rit_opcode) {
        case C2_MDSERVICE_CREATE_OPCODE:
                ops = &c2_md_fom_create_ops;
                rep_fopt = &c2_fop_create_rep_fopt;
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                ops = &c2_md_fom_link_ops;
                rep_fopt = &c2_fop_link_rep_fopt;
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                ops = &c2_md_fom_unlink_ops;
                rep_fopt = &c2_fop_unlink_rep_fopt;
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                ops = &c2_md_fom_rename_ops;
                rep_fopt = &c2_fop_rename_rep_fopt;
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                ops = &c2_md_fom_open_ops;
                rep_fopt = &c2_fop_open_rep_fopt;
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                ops = &c2_md_fom_close_ops;
                rep_fopt = &c2_fop_close_rep_fopt;
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                ops = &c2_md_fom_setattr_ops;
                rep_fopt = &c2_fop_setattr_rep_fopt;
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                ops = &c2_md_fom_getattr_ops;
                rep_fopt = &c2_fop_getattr_rep_fopt;
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                ops = &c2_md_fom_readdir_ops;
                rep_fopt = &c2_fop_readdir_rep_fopt;
                break;
        default:
                c2_free(fom_obj);
                return -EOPNOTSUPP;
        }

        fom = &fom_obj->fm_fom;
        *m = fom;
        c2_fom_init(fom, &fop->f_type->ft_fom_type,
                    ops, fop, c2_fop_alloc(rep_fopt, NULL));

        if (fom->fo_rep_fop == NULL) {
                c2_fom_fini(fom);
                c2_free(fom_obj);
                return -ENOMEM;
        }

        return 0;
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
