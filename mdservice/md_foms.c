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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_COB
#include <sys/stat.h>    /* S_ISDIR */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/rwlock.h"
#include "lib/trace.h"

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
   Make in-memory fid from wire fid.
*/
C2_INTERNAL void c2_md_fid_wire2mem(struct c2_fid *fid,
				    const struct c2_fop_fid *wid)
{
        fid->f_container = wid->f_seq;
        fid->f_key = wid->f_oid;
}

static void c2_md_cob_wire2mem(struct c2_cob_attr *attr,
                               struct c2_fop_cob *body)
{
        C2_SET0(attr);
        c2_md_fid_wire2mem(&attr->ca_pfid, &body->b_pfid);
        c2_md_fid_wire2mem(&attr->ca_tfid, &body->b_tfid);
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

/**
   Make in-memory wire fid from attr fid.
*/
C2_INTERNAL void c2_md_fid_mem2wire(struct c2_fop_fid *wid, const struct c2_fid *fid)
{
        wid->f_seq = fid->f_container;
        wid->f_oid = fid->f_key;
}

static void c2_md_cob_mem2wire(struct c2_fop_cob *body,
                              struct c2_cob_attr *attr)
{
        c2_md_fid_mem2wire(&body->b_pfid, &attr->ca_pfid);
        c2_md_fid_mem2wire(&body->b_tfid, &attr->ca_tfid);
        body->b_valid = attr->ca_flags;
        if (body->b_valid & C2_COB_MODE)
                body->b_mode = attr->ca_mode;
        if (body->b_valid & C2_COB_UID)
                body->b_uid = attr->ca_uid;
        if (body->b_valid & C2_COB_GID)
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
static int c2_md_create(struct c2_mdstore  *md,
                        struct c2_fid       *pfid,
                        struct c2_fid       *tfid,
                        struct c2_cob_attr  *attr,
                        struct c2_db_tx     *tx)
{
        struct c2_cob *scob = NULL;
        int            rc;

        rc = c2_mdstore_locate(md, tfid, &scob,
                                C2_MD_LOCATE_STORED, tx);
        if (rc == -ENOENT) {
                /*
                 * Statdata cob is not found, let's create it. This
                 * must be normal create case.
                 */
                rc = c2_mdstore_create(md, pfid, attr, &scob, tx);
        } else if (rc == 0) {
                /*
                 * There is statdata name, this must be hardlink
                 * case.
                 */
                rc = c2_mdstore_link(md, pfid, scob, &attr->ca_name, tx);
                /*
                 * Each operation changes target attributes (times).
                 * We want to keep them up-to-date.
                 */
                if (rc == 0)
                        rc = c2_mdstore_setattr(md, scob, attr, tx);
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

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->c_body;
        c2_md_cob_wire2mem(&attr, body);

        c2_buf_init(&attr.ca_name, req->c_name.s_buf, req->c_name.s_len);

        if (S_ISLNK(attr.ca_mode)) {
                /** Symlink body size is stored in @attr->ca_size */
                c2_buf_init(&attr.ca_link, (char *)req->c_target.s_buf, attr.ca_size);
        }

        c2_md_fid_wire2mem(&pfid, &body->b_pfid);
        c2_md_fid_wire2mem(&tfid, &body->b_tfid);

        C2_LOG(C2_DEBUG, "Create [%lx:%lx]/[%lx:%lx] %*s",
               pfid.f_container, pfid.f_key, tfid.f_container, tfid.f_key,
               (int)attr.ca_name.b_nob, (char *)attr.ca_name.b_addr);

        c2_fom_block_enter(fom);
        rc = c2_md_create(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &pfid,
                          &tfid, &attr, &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);
out:
        C2_LOG(C2_DEBUG, "Create finished with %d", rc);
        rep->c_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
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

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->l_body;
        c2_md_cob_wire2mem(&attr, body);

        c2_buf_init(&attr.ca_name, req->l_name.s_buf, req->l_name.s_len);
        c2_md_fid_wire2mem(&pfid, &body->b_pfid);
        c2_md_fid_wire2mem(&tfid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_md_create(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                          &pfid, &tfid, &attr, &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);
out:
        rep->l_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
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
        struct c2_mdstore        *md;
        int                       rc;

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->u_body;
        c2_md_cob_wire2mem(&attr, body);
        c2_buf_init(&attr.ca_name, req->u_name.s_buf, req->u_name.s_len);
        c2_md_fid_wire2mem(&pfid, &body->b_pfid);
        c2_md_fid_wire2mem(&tfid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_mdstore_locate(md, &tfid, &scob,
                                C2_MD_LOCATE_STORED,
                                tx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_mdstore_unlink(md, &pfid, scob, &attr.ca_name, tx);
        if (rc == 0 && scob->co_nsrec.cnr_nlink > 0)
                rc = c2_mdstore_setattr(md, scob, &attr, tx);
        c2_cob_put(scob);
        c2_fom_block_leave(fom);
out:
        rep->u_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_rename(struct c2_mdstore  *md,
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
        rc = c2_mdstore_rename(md, pfid_tgt, pfid_src, tcob, scob,
                               &tattr->ca_name, &sattr->ca_name, tx);
        if (rc != 0)
                return rc;
        /*
         * Update attributes of source and target.
         */
        if (c2_fid_eq(scob->co_fid, tcob->co_fid)) {
                if (tcob->co_nsrec.cnr_nlink > 0)
                        rc = c2_mdstore_setattr(md, tcob, tattr, tx);
        } else {
                rc = c2_mdstore_setattr(md, scob, sattr, tx);
                if (rc == 0 && tcob->co_nsrec.cnr_nlink > 0)
                        rc = c2_mdstore_setattr(md, tcob, tattr, tx);
        }
        return rc;
}

static int c2_md_rename_tick(struct c2_fom *fom)
{
        struct c2_fop_cob        *sbody;
        struct c2_fop_cob        *tbody;
        struct c2_fop_rename     *req;
        struct c2_fop_rename_rep *rep;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_cob            *tcob = NULL;
        struct c2_cob            *scob = NULL;
        struct c2_fid             src_tfid;
        struct c2_fid             tgt_tfid;
        struct c2_fid             tgt_pfid;
        struct c2_fid             src_pfid;
        struct c2_cob_attr        sattr;
        struct c2_cob_attr        tattr;
        struct c2_db_tx          *tx;
        struct c2_mdstore       *md;
        int                       rc;

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        req = c2_fop_data(fop);
        sbody = &req->r_sbody;
        tbody = &req->r_tbody;

        rep = c2_fop_data(fop_rep);

        c2_md_fid_wire2mem(&src_pfid, &sbody->b_pfid);
        c2_md_fid_wire2mem(&tgt_pfid, &tbody->b_pfid);

        c2_md_fid_wire2mem(&src_tfid, &sbody->b_tfid);
        c2_md_fid_wire2mem(&tgt_tfid, &tbody->b_tfid);

        c2_md_cob_wire2mem(&tattr, tbody);
        c2_buf_init(&tattr.ca_name, req->r_tname.s_buf, req->r_tname.s_len);

        c2_md_cob_wire2mem(&sattr, sbody);
        c2_buf_init(&sattr.ca_name, req->r_sname.s_buf, req->r_sname.s_len);

        c2_fom_block_enter(fom);
        rc = c2_mdstore_locate(md, &src_tfid, &scob,
                                C2_MD_LOCATE_STORED, tx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }
        if (c2_fid_eq(&tgt_tfid, &src_tfid)) {
                rc = c2_md_rename(md, &tgt_pfid, &src_pfid, &tgt_tfid,
                                  &src_tfid, &tattr, &sattr, scob, scob, tx);
        } else {
                rc = c2_mdstore_locate(md, &tgt_tfid, &tcob,
                                        C2_MD_LOCATE_STORED, tx);
                if (rc != 0) {
                        c2_fom_block_leave(fom);
                        goto out;
                }
                rc = c2_md_rename(md, &tgt_pfid, &src_pfid, &tgt_tfid,
                                  &src_tfid, &tattr, &sattr, tcob, scob, tx);
                c2_cob_put(tcob);
        }
        c2_cob_put(scob);
        c2_fom_block_leave(fom);
out:
        rep->r_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
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
        struct c2_mdstore        *md;

        C2_PRE(c2_fom_invariant(fom));
        md = fom->fo_loc->fl_dom->fd_reqh->rh_mdstore;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->o_body;
        c2_md_cob_wire2mem(&attr, body);

        c2_md_fid_wire2mem(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_mdstore_locate(md, &fid, &cob,
                                C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc == 0) {
                rc = c2_mdstore_open(md, cob,
                                     body->b_flags, &fom->fo_tx.tx_dbtx);
                if (rc == 0 &&
                    (!(attr.ca_flags & C2_COB_NLINK) || attr.ca_nlink > 0)) {
                        /*
                         * Mode contains open flags that we don't need
                         * to store to db.
                         */
                        attr.ca_flags &= ~C2_COB_MODE;
                        rc = c2_mdstore_setattr(md, cob, &attr, &fom->fo_tx.tx_dbtx);
                }
                c2_cob_put(cob);
        } else if (rc == -ENOENT) {
                /*
                 * Lustre has create before open in case of OPEN_CREATE.
                 * We don't have to create anything here as file already
                 * should exist, let's just check this.
                 */
                /* C2_ASSERT(!(body->b_flags & C2_MD_OPEN_CREAT)); */
        } else if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        c2_fom_block_leave(fom);
out:
        rep->o_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
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
        struct c2_mdstore        *md;

        C2_PRE(c2_fom_invariant(fom));
        md = fom->fo_loc->fl_dom->fd_reqh->rh_mdstore;

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->c_body;
        c2_md_cob_wire2mem(&attr, body);

        c2_md_fid_wire2mem(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        /*
         * @todo: This should lookup for cobs in special opened
         * cobs table. But so far orphans and open/close are not
         * quite implemented and we lookup on main store to make
         * ut happy.
         */
        rc = c2_mdstore_locate(md, &fid, &cob,
                                C2_MD_LOCATE_STORED/*OPENED*/, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_mdstore_close(md, cob,
                               &fom->fo_tx.tx_dbtx);
        if (rc == 0 &&
            (!(attr.ca_flags & C2_COB_NLINK) || attr.ca_nlink > 0)) {
                /*
                 * Mode contains open flags that we don't need
                 * to store to db.
                 */
                attr.ca_flags &= ~C2_COB_MODE;
                rc = c2_mdstore_setattr(md, cob, &attr, &fom->fo_tx.tx_dbtx);
        }
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
out:
        rep->c_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
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

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->s_body;
        c2_md_cob_wire2mem(&attr, body);

        c2_md_fid_wire2mem(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);

        /*
         * Setattr fop does not carry enough information to create
         * an object in case there is no target yet. This is why
         * we return quickly if no object is found.
         */
        rc = c2_mdstore_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid,
                                &cob, C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_mdstore_setattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, cob,
                                &attr, &fom->fo_tx.tx_dbtx);
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
out:
        rep->s_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_lookup_tick(struct c2_fom *fom)
{
        struct c2_cob_attr             attr;
        struct c2_fop_cob             *body;
        struct c2_cob                 *cob;
        struct c2_fop_lookup          *req;
        struct c2_fop_lookup_rep      *rep;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  pfid;
        int                            rc;
        struct c2_buf                  name;

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
                        goto finish;
                rc = c2_fom_tick_generic(fom);
                return rc;
        }

        fop = fom->fo_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);
        body = &req->l_body;

        fop_rep = fom->fo_rep_fop;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);

        /**
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        c2_md_fid_wire2mem(&pfid, &body->b_pfid);

        c2_fom_block_enter(fom);

        c2_buf_init(&name, req->l_name.s_buf, req->l_name.s_len);

        C2_LOG(C2_DEBUG, "Lookup for \"%*s\" in object [%lx:%lx]",
               (int)name.b_nob, (char *)name.b_addr, pfid.f_container,
               pfid.f_key);

        rc = c2_mdstore_lookup(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                               &pfid, &name, &cob, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                C2_LOG(C2_DEBUG, "Lookup failed with %d", rc);
                c2_fom_block_leave(fom);
                goto out;
        }
        C2_LOG(C2_DEBUG, "Found object [%lx:%lx] go for getattr",
               cob->co_fid->f_container, cob->co_fid->f_key);

        rc = c2_mdstore_getattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                cob, &attr, &fom->fo_tx.tx_dbtx);
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
        if (rc == 0) {
                attr.ca_flags = C2_COB_ALL;
                c2_md_cob_mem2wire(&rep->l_body, &attr);
        } else {
                C2_LOG(C2_DEBUG, "Getattr on object [%lx:%lx] failed with %d",
                       cob->co_fid->f_container, cob->co_fid->f_key, rc);
        }
out:
        C2_LOG(C2_DEBUG, "Lookup finished with %d", rc);
        rep->l_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_getattr_tick(struct c2_fom *fom)
{
        struct c2_cob_attr             attr = { { 0 } };
        struct c2_fop_cob             *body;
        struct c2_cob                 *cob;
        struct c2_fop_getattr         *req;
        struct c2_fop_getattr_rep     *rep;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        int                            rc;

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        c2_md_fid_wire2mem(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_mdstore_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid,
                               &cob, C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
        if (rc != 0) {
                c2_fom_block_leave(fom);
                goto out;
        }

        rc = c2_mdstore_getattr(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                cob, &attr, &fom->fo_tx.tx_dbtx);
        c2_cob_put(cob);
        c2_fom_block_leave(fom);
        if (rc == 0) {
                attr.ca_flags = C2_COB_ALL;
                c2_md_cob_mem2wire(&rep->g_body, &attr);
        }
out:
        rep->g_body.b_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static void md_statfs_mem2wire(struct c2_fop_statfs_rep *rep,
                               struct c2_statfs *statfs)
{
        rep->f_type = statfs->sf_type;
        rep->f_bsize = statfs->sf_bsize;
        rep->f_blocks = statfs->sf_blocks;
        rep->f_bfree = statfs->sf_bfree;
        rep->f_bavail = statfs->sf_bavail;
        rep->f_files = statfs->sf_files;
        rep->f_ffree = statfs->sf_ffree;
        rep->f_namelen = statfs->sf_namelen;
        c2_md_fid_mem2wire(&rep->f_root, &statfs->sf_root);
}

static int c2_md_statfs_tick(struct c2_fom *fom)
{
        struct c2_fop_statfs          *req;
        struct c2_fop_statfs_rep      *rep;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_statfs               statfs;
        struct c2_fop_ctx             *ctx;
        int                            rc;

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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

        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        c2_fom_block_enter(fom);
        rc = c2_mdstore_statfs(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                               &statfs, &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);
        if (rc == 0)
                md_statfs_mem2wire(rep, &statfs);
out:
        rep->f_rc = rc;
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

/** Readdir fop data buffer size */
#define C2_MD_READDIR_BUFF_SIZE 4096

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
        struct c2_rdpg                 rdpg = { 0 };
        void                          *addr;
        int                            rc;

        C2_PRE(c2_fom_invariant(fom));

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /**
                 * Don't send reply in case there is no service running.
                 */
                if (fom->fo_service == NULL && c2_fom_phase(fom) == C2_FOPH_QUEUE_REPLY)
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
         * Init some fop fields (full path) that require mdstore and other
         * initialialized structures.
         */
        rc = c2_md_fop_init(fop, fom);
        if (rc != 0)
                goto out;

        body = &req->r_body;
        c2_md_fid_wire2mem(&fid, &body->b_tfid);

        c2_fom_block_enter(fom);
        rc = c2_mdstore_locate(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore, &fid,
                               &cob, C2_MD_LOCATE_STORED, &fom->fo_tx.tx_dbtx);
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

        rdpg.r_pos = c2_alloc(C2_MD_MAX_NAME_LEN);
        if (rdpg.r_pos == NULL) {
                c2_fom_block_leave(fom);
                c2_cob_put(cob);
                rc = -ENOMEM;
                goto out;
        }
        c2_bitstring_copy(rdpg.r_pos, (char *)req->r_pos.s_buf, req->r_pos.s_len);

        addr = c2_alloc(C2_MD_READDIR_BUFF_SIZE);
        if (addr == NULL) {
                c2_fom_block_leave(fom);
                c2_bitstring_free(rdpg.r_pos);
                c2_cob_put(cob);
                rc = -ENOMEM;
                goto out;
        }

        c2_buf_init(&rdpg.r_buf, addr, C2_MD_READDIR_BUFF_SIZE);

        rc = c2_mdstore_readdir(fom->fo_loc->fl_dom->fd_reqh->rh_mdstore,
                                cob, &rdpg, &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);
        c2_free(rdpg.r_pos);
        c2_cob_put(cob);
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
        memcpy(rep->r_end.s_buf, c2_bitstring_buf_get(rdpg.r_end),
               rep->r_end.s_len);
        c2_bitstring_free(rdpg.r_end);

        /*
         * Prepare buf with data.
         */
        rep->r_buf.b_count = rdpg.r_buf.b_nob;
        rep->r_buf.b_addr = rdpg.r_buf.b_addr;
out:
        rep->r_body.b_rc = rc;

        /*
         * Readddir return convention:
         * <0 - some error occured;
         *  0 - no errors, more data available for next readdir;
         * >0 - EOF detyected, more data available but this is the last page.
         *
         * Return code according to this convention should go to client but
         * local state machine requires "normal" errors. Let's adopt @rc.
         */
        rc = (rc < 0 ? rc : 0);
        c2_fom_phase_move(fom, rc, rc != 0 ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
        return C2_FSO_AGAIN;
finish:
        c2_fom_phase_move(fom, 0, C2_FOPH_FINISH);
        return C2_FSO_WAIT;
}

static int c2_md_req_path_get(struct c2_mdstore *mdstore,
                              struct c2_fid *fid,
                              struct c2_fop_str *str)
{
        int rc;

        rc = c2_mdstore_path(mdstore, fid, (char **)&str->s_buf);
        if (rc != 0)
                return rc;
        str->s_len = strlen((char *)str->s_buf);
        return 0;
}

static inline struct c2_fid *c2_md_fid_get(struct c2_fop_fid *fid)
{
        static struct c2_fid fid_fop2mem;
        c2_md_fid_wire2mem(&fid_fop2mem, fid);
        return &fid_fop2mem;
}

C2_INTERNAL int c2_md_fop_init(struct c2_fop *fop, struct c2_fom *fom)
{
        struct c2_fop_create    *create;
        struct c2_fop_unlink    *unlink;
        struct c2_fop_rename    *rename;
        struct c2_fop_link      *link;
        struct c2_fop_setattr   *setattr;
        struct c2_fop_getattr   *getattr;
        struct c2_fop_lookup    *lookup;
        struct c2_fop_open      *open;
        struct c2_fop_close     *close;
        struct c2_fop_readdir   *readdir;
        struct c2_mdstore       *md;
        int                      rc;

        C2_PRE(fop != NULL);
        md = fom->fo_loc->fl_dom->fd_reqh->rh_mdstore;

        switch (c2_fop_opcode(fop)) {
        case C2_MDSERVICE_CREATE_OPCODE:
                create = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&create->c_body.b_pfid),
                                        &create->c_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                link = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&link->l_body.b_pfid),
                                        &link->l_tpath);
                if (rc != 0)
                        return rc;
                rc = c2_md_req_path_get(md,
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
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&unlink->u_body.b_pfid),
                                        &unlink->u_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                rename = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&rename->r_sbody.b_pfid),
                                        &rename->r_spath);
                if (rc != 0)
                        return rc;
                rc = c2_md_req_path_get(md,
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
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&open->o_body.b_tfid),
                                        &open->o_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                close = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&close->c_body.b_tfid),
                                        &close->c_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                setattr = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&setattr->s_body.b_tfid),
                                        &setattr->s_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                getattr = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&getattr->g_body.b_tfid),
                                        &getattr->g_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_LOOKUP_OPCODE:
                lookup = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&lookup->l_body.b_pfid),
                                        &lookup->l_path);
                if (rc != 0)
                        return rc;
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                readdir = c2_fop_data(fop);
                rc = c2_md_req_path_get(md,
                                        c2_md_fid_get(&readdir->r_body.b_tfid),
                                        &readdir->r_path);
                if (rc != 0)
                        return rc;
                break;
        default:
                rc = 0;
                break;
        }

        return rc;
}

static void c2_md_req_fom_fini(struct c2_fom *fom)
{
        struct c2_fom_md         *fom_obj;
        struct c2_local_service  *svc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        /* Let local sevice know that we have finished. */
        svc = fom->fo_loc->fl_dom->fd_reqh->rh_svc;
        if (svc != NULL && svc->s_ops->lso_fini)
                svc->s_ops->lso_fini(svc, fom);
        /* Fini fom itself. */
        c2_fom_fini(fom);
        c2_free(fom_obj);
}

static size_t c2_md_req_fom_locality_get(const struct c2_fom *fom)
{
        C2_PRE(fom != NULL);
        C2_PRE(fom->fo_fop != NULL);

        return c2_fop_opcode(fom->fo_fop);
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

static const struct c2_fom_ops c2_md_fom_lookup_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_lookup_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_statfs_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_statfs_tick,
        .fo_fini   = c2_md_req_fom_fini
};

static const struct c2_fom_ops c2_md_fom_readdir_ops = {
        .fo_home_locality = c2_md_req_fom_locality_get,
        .fo_tick   = c2_md_readdir_tick,
        .fo_fini   = c2_md_req_fom_fini
};

C2_INTERNAL int c2_md_rep_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        return 0;
}

C2_INTERNAL int c2_md_req_fom_create(struct c2_fop *fop, struct c2_fom **m)
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

        switch (c2_fop_opcode(fop)) {
        case C2_MDSERVICE_CREATE_OPCODE:
                ops = &c2_md_fom_create_ops;
                rep_fopt = &c2_fop_create_rep_fopt;
                break;
        case C2_MDSERVICE_LOOKUP_OPCODE:
                ops = &c2_md_fom_lookup_ops;
                rep_fopt = &c2_fop_lookup_rep_fopt;
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
        case C2_MDSERVICE_STATFS_OPCODE:
                ops = &c2_md_fom_statfs_ops;
                rep_fopt = &c2_fop_statfs_rep_fopt;
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
        c2_fom_init(fom, &fop->f_type->ft_fom_type,
                    ops, fop, c2_fop_alloc(rep_fopt, NULL));

        if (fom->fo_rep_fop == NULL) {
                c2_fom_fini(fom);
                c2_free(fom_obj);
                return -ENOMEM;
        }
        *m = fom;

        return 0;
}
#undef C2_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
