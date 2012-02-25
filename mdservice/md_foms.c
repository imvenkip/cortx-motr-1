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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
#include "site/site.h"

#include "mdservice/md_fops_u.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_foms.h"

#include "mdstore/mdstore.h"

/**
   Make in-memory fid from wire fid (wid).
*/
void c2_md_make_fid(struct c2_fid *fid, const struct c2_fop_fid *wid)
{
        fid->f_container = wid->f_seq;
        fid->f_key = wid->f_oid;
}

/**
   Make nskey from passed parent fid and child name.
*/
void c2_md_make_nskey(struct c2_cob_nskey **keyh, 
                      const struct c2_fop_fid *fid,
                      struct c2_fop_str *name)
{
        struct c2_fid cfid;
        
        c2_md_make_fid(&cfid, fid);
        c2_cob_make_nskey(keyh, &cfid, name->s_buf, name->s_len);
}

/**
   Make oikey from passed child fid and liunk number.
*/
void c2_md_make_oikey(struct c2_cob_oikey *oikey, 
                      const struct c2_fop_fid *fid,
                      int linkno)
{
        c2_md_make_fid(&oikey->cok_fid, fid);
        oikey->cok_linkno = linkno;
}

static void c2_md_fop_cob2attr(struct c2_cob_attr *attr, 
                               struct c2_fop_cob *body)
{
        C2_SET0(attr);
        c2_md_make_fid(&attr->ca_pfid, &body->b_pfid);
        c2_md_make_fid(&attr->ca_tfid, &body->b_tfid);
        attr->ca_flags = body->b_valid;
        if (body->b_valid & C2_MD_MODE)
                attr->ca_mode = body->b_mode;
        if (body->b_valid & C2_MD_UID)
                attr->ca_uid = body->b_uid;
        if (body->b_valid & C2_MD_GID)
                attr->ca_gid = body->b_gid;
        if (body->b_valid & C2_MD_ATIME)
                attr->ca_atime = body->b_atime;
        if (body->b_valid & C2_MD_MTIME)
                attr->ca_mtime = body->b_mtime;
        if (body->b_valid & C2_MD_CTIME)
                attr->ca_ctime = body->b_ctime;
        if (body->b_valid & C2_MD_NLINK)
                attr->ca_nlink = body->b_nlink;
        if (body->b_valid & C2_MD_RDEV)
                attr->ca_rdev = body->b_rdev;
        if (body->b_valid & C2_MD_SIZE)
                attr->ca_size = body->b_size;
        if (body->b_valid & C2_MD_BLKSIZE)
                attr->ca_blksize = body->b_blksize;
        if (body->b_valid & C2_MD_BLOCKS)
                attr->ca_blocks = body->b_blocks;
        attr->ca_version = body->b_version;
}

static void c2_md_fop_attr2cob(struct c2_fop_cob *body, 
                               struct c2_cob_attr *attr)
{
        body->b_valid = attr->ca_flags;
        if (body->b_valid & C2_MD_UID)
                body->b_mode = attr->ca_mode;
        if (body->b_valid & C2_MD_UID)
                body->b_uid = attr->ca_uid;
        if (body->b_valid & C2_MD_UID)
                body->b_gid = attr->ca_gid;
        if (body->b_valid & C2_MD_ATIME)
                body->b_atime = attr->ca_atime;
        if (body->b_valid & C2_MD_MTIME)
                body->b_mtime = attr->ca_mtime;
        if (body->b_valid & C2_MD_CTIME)
                body->b_ctime = attr->ca_ctime;
        if (body->b_valid & C2_MD_NLINK)
                body->b_nlink = attr->ca_nlink;
        if (body->b_valid & C2_MD_RDEV)
                body->b_rdev = attr->ca_rdev;
        if (body->b_valid & C2_MD_SIZE)
                body->b_size = attr->ca_size;
        if (body->b_valid & C2_MD_BLKSIZE)
                body->b_blksize = attr->ca_blksize;
        if (body->b_valid & C2_MD_BLOCKS)
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

static int c2_md_create_fom_state(struct c2_fom *fom)
{
        struct c2_cob_attr        attr;
        struct c2_fop_cob        *body;
        struct c2_site           *site;
        struct c2_fop_create     *req;
        struct c2_fop_create_rep *rep;
        struct c2_fom_md         *fom_obj;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             pfid;
        struct c2_fid             tfid;
        struct c2_service        *svc;
        int                       rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        body = &req->c_body;
        c2_md_fop_cob2attr(&attr, body);

        attr.ca_name = req->c_name.s_buf;
        attr.ca_namelen = req->c_name.s_len;
        
        if (S_ISLNK(attr.ca_mode))
                attr.ca_link = req->c_target.s_buf;

        c2_md_make_fid(&pfid, &body->b_pfid);
        c2_md_make_fid(&tfid, &body->b_tfid);

        rc = c2_md_create(site->s_mdstore, &pfid, &tfid,
                          &attr, &ctx->fc_tx->tx_dbtx);
        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static int c2_md_link_fom_state(struct c2_fom *fom)
{
        struct c2_fop_cob        *body;
        struct c2_site           *site;
        struct c2_fop_link       *req;
        struct c2_fop_link_rep   *rep;
        struct c2_fom_md         *fom_obj;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             tfid;
        struct c2_fid             pfid;
        struct c2_service        *svc;
        struct c2_cob_attr        attr;
        int                       rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        body = &req->l_body;
        c2_md_fop_cob2attr(&attr, body);
        attr.ca_name = req->l_name.s_buf;
        attr.ca_namelen = req->l_name.s_len;

        c2_md_make_fid(&pfid, &body->b_pfid);
        c2_md_make_fid(&tfid, &body->b_tfid);

        rc = c2_md_create(site->s_mdstore, &pfid, &tfid,
                          &attr, &ctx->fc_tx->tx_dbtx);
        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static int c2_md_unlink_fom_state(struct c2_fom *fom)
{
        struct c2_cob_attr        attr;
        struct c2_fop_cob        *body;
        struct c2_site           *site;
        struct c2_cob            *scob = NULL;
        struct c2_fop_unlink     *req;
        struct c2_fop_unlink_rep *rep;
        struct c2_fom_md         *fom_obj;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             tfid;
        struct c2_fid             pfid;
        struct c2_service        *svc;
        struct c2_db_tx          *tx;
        struct c2_md_store       *md;
        int                       rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        md = site->s_mdstore;
        tx = &ctx->fc_tx->tx_dbtx;

        body = &req->u_body;
        c2_md_fop_cob2attr(&attr, body);
        attr.ca_name = req->u_name.s_buf;
        attr.ca_namelen = req->u_name.s_len;

        c2_md_make_fid(&pfid, &body->b_pfid);
        c2_md_make_fid(&tfid, &body->b_tfid);

        rc = c2_md_store_locate(md, &tfid, &scob, 
                                C2_MD_LOCATE_STORED,
                                &ctx->fc_tx->tx_dbtx);
        if (rc)
                goto out;
                
        rc = c2_md_store_unlink(md, &pfid, scob, attr.ca_name,
                                attr.ca_namelen, tx);
        if (rc == 0 && scob->co_nsrec.cnr_nlink > 0)
                rc = c2_md_store_setattr(md, scob, &attr, tx);
        c2_cob_put(scob);
        
        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
out:
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
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
        if (rc)
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

static int c2_md_rename_fom_state(struct c2_fom *fom)
{
        struct c2_fop_cob        *sbody;
        struct c2_fop_cob        *tbody;
        struct c2_site           *site;
        struct c2_fop_rename     *req;
        struct c2_fom_md         *fom_obj;
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
        struct c2_service        *svc;
        struct c2_db_tx          *tx;
        struct c2_md_store       *md;
        int                       rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        tx = &ctx->fc_tx->tx_dbtx;
        md = site->s_mdstore;

        req = c2_fop_data(fop);
        sbody = &req->r_sbody;
        tbody = &req->r_tbody;

        c2_md_make_fid(&pfid_src, &sbody->b_pfid);
        c2_md_make_fid(&pfid_tgt, &tbody->b_pfid);

        c2_md_make_fid(&tfid_src, &sbody->b_tfid);
        c2_md_make_fid(&tfid_tgt, &tbody->b_tfid);

        c2_md_fop_cob2attr(&tattr, tbody);
        tattr.ca_name = req->r_tname.s_buf;
        tattr.ca_namelen = req->r_tname.s_len;

        c2_md_fop_cob2attr(&sattr, sbody);
        sattr.ca_name = req->r_sname.s_buf;
        sattr.ca_namelen = req->r_sname.s_len;

        rc = c2_md_store_locate(md, &tfid_src, &scob,
                                C2_MD_LOCATE_STORED, tx);
        if (rc)
                goto out;
        if (c2_fid_eq(&tfid_tgt, &tfid_src)) {
                rc = c2_md_rename(md, &pfid_tgt, &pfid_src, &tfid_tgt,
                                  &tfid_src, &tattr, &sattr, scob, scob, tx);
        } else {
                rc = c2_md_store_locate(md, &tfid_tgt, &tcob,
                                        C2_MD_LOCATE_STORED, tx);
                if (rc)
                        goto out;
                rc = c2_md_rename(md, &pfid_tgt, &pfid_src, &tfid_tgt,
                                  &tfid_src, &tattr, &sattr, tcob, scob, tx);
                c2_cob_put(tcob);
        }
        c2_cob_put(scob);
out:
        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}

	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static int c2_md_open_fom_state(struct c2_fom *fom)
{
        struct c2_fop_cob        *body;
        struct c2_site           *site;
        struct c2_cob            *cob;
        struct c2_fop_open       *req;
        struct c2_fop_open_rep   *rep;
        struct c2_fom_md         *fom_obj;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             fid;
        struct c2_service        *svc;
        struct c2_cob_attr        attr;
        int                       rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        body = &req->o_body;
        c2_md_fop_cob2attr(&attr, body);

        c2_md_make_fid(&fid, &body->b_tfid);

        rc = c2_md_store_locate(site->s_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED,
                                &ctx->fc_tx->tx_dbtx);
        if (rc == 0) {
                rc = c2_md_store_open(site->s_mdstore, cob, 
                                      body->b_flags, 
                                      &ctx->fc_tx->tx_dbtx);
                if (rc == 0 &&
                    (!(attr.ca_flags & C2_MD_NLINK) || attr.ca_nlink > 0)) {
                        /*
                         * Mode contains open flags that we don't need
                         * to store to db.
                         */
                        attr.ca_flags &= ~C2_MD_MODE;
                        rc = c2_md_store_setattr(site->s_mdstore,
                                                 cob, &attr, 
                                                 &ctx->fc_tx->tx_dbtx);
                }
                c2_cob_put(cob);
        } else if (rc == -ENOENT) {
                /*
                 * Lustre has create before open in case of OPEN_CREATE.
                 * We don't have to create anything here as file already
                 * should exist, let's just check this.
                 */
                C2_ASSERT(!(body->b_flags & C2_MD_OPEN_CREAT));
        } else if (rc)
                goto out;

        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
out:
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static int c2_md_close_fom_state(struct c2_fom *fom)
{
        struct c2_fop_cob        *body;
        struct c2_site           *site;
        struct c2_cob            *cob;
        struct c2_fop_close      *req;
        struct c2_fop_close_rep  *rep;
        struct c2_fom_md         *fom_obj;
        struct c2_fop            *fop;
        struct c2_fop            *fop_rep;
        struct c2_fop_ctx        *ctx;
        struct c2_fid             fid;
        struct c2_service        *svc;
        struct c2_cob_attr        attr;
        int                       rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        body = &req->c_body;
        c2_md_fop_cob2attr(&attr, body);

        c2_md_make_fid(&fid, &body->b_tfid);

        /*
         * @todo: This should lookup for cobs in special opened
         * cobs table. But so far orphans and open/close are not
         * quite implemented and we lookup on main store to make
         * ut happy.
         */
        rc = c2_md_store_locate(site->s_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED/*OPENED*/,
                                &ctx->fc_tx->tx_dbtx);
        if (rc)
                goto out;

        rc = c2_md_store_close(site->s_mdstore, cob, 
                               &ctx->fc_tx->tx_dbtx);
        if (rc == 0 && 
            (!(attr.ca_flags & C2_MD_NLINK) || attr.ca_nlink > 0)) {
                /*
                 * Mode contains open flags that we don't need
                 * to store to db.
                 */
                attr.ca_flags &= ~C2_MD_MODE;
                rc = c2_md_store_setattr(site->s_mdstore, cob,
                                         &attr, &ctx->fc_tx->tx_dbtx);
        }
        c2_cob_put(cob);
        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
out:
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static int c2_md_setattr_fom_state(struct c2_fom *fom)
{
        struct c2_cob_attr             attr;
        struct c2_fop_cob             *body;
        struct c2_site                *site;
        struct c2_cob                 *cob;
        struct c2_fop_setattr         *req;
        struct c2_fop_setattr_rep     *rep;
        struct c2_fom_md              *fom_obj;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        struct c2_service             *svc;
        int                            rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        body = &req->s_body;
        c2_md_fop_cob2attr(&attr, body);

        c2_md_make_fid(&fid, &body->b_tfid);

        /*
         * Setattr fop does not carry enough information to create
         * an object in case there is no target yet. This is why
         * we return quickly if no object is found.
         */
        rc = c2_md_store_locate(site->s_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED,
                                &ctx->fc_tx->tx_dbtx);
        if (rc)
                goto out;

        rc = c2_md_store_setattr(site->s_mdstore, cob, &attr,
                                 &ctx->fc_tx->tx_dbtx);
        c2_cob_put(cob);

        if (rc == 0) {
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
out:
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static int c2_md_getattr_fom_state(struct c2_fom *fom)
{
        struct c2_cob_attr             attr;
        struct c2_fop_cob             *body;
        struct c2_site                *site;
        struct c2_cob                 *cob;
        struct c2_fop_getattr         *req;
        struct c2_fop_getattr_rep     *rep;
        struct c2_fom_md              *fom_obj;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        struct c2_service             *svc;
        int                            rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);
        body = &req->g_body;

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        c2_md_make_fid(&fid, &body->b_tfid);

        rc = c2_md_store_locate(site->s_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED,
                                &ctx->fc_tx->tx_dbtx);
        if (rc)
                goto out;

        rc = c2_md_store_getattr(site->s_mdstore, cob, &attr,
                                 &ctx->fc_tx->tx_dbtx);
        c2_cob_put(cob);
        if (rc == 0) {
                c2_md_fop_attr2cob(&rep->g_body, &attr);
                svc = fom->fo_fop_ctx->fc_service;
	        svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
	}
out:
	ctx->fc_retval = rc;
        if (rc)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

#define C2_MD_READDIR_BUF_ALLOC 4096

static int c2_md_readdir_fom_state(struct c2_fom *fom)
{
        struct c2_fop_cob             *body;
        struct c2_site                *site;
        struct c2_cob                 *cob;
        struct c2_fop_readdir         *req;
        struct c2_fop_readdir_rep     *rep;
        struct c2_fom_md              *fom_obj;
        struct c2_fop                 *fop;
        struct c2_fop                 *fop_rep;
        struct c2_fop_ctx             *ctx;
        struct c2_fid                  fid;
        struct c2_service             *svc;
        struct c2_rdpg                 rdpg;
        void                          *addr;
        int                            rc;

        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);

        fop = fom_obj->fm_fop;
        C2_ASSERT(fop != NULL);
        req = c2_fop_data(fop);

        fop_rep = fom_obj->fm_fop_rep;
        C2_ASSERT(fop_rep != NULL);
        rep = c2_fop_data(fop_rep);
	
        ctx = fom->fo_fop_ctx;
        C2_ASSERT(ctx != NULL);

        site = ctx->fc_site;
        C2_ASSERT(site != NULL);
        body = &req->r_body;

        c2_md_make_fid(&fid, &body->b_tfid);

        rc = c2_md_store_locate(site->s_mdstore, &fid, &cob, 
                                C2_MD_LOCATE_STORED,
                                &ctx->fc_tx->tx_dbtx);
        if (rc)
                goto out;

        if (!S_ISDIR(cob->co_omgrec.cor_mode)) {
                rc = -ENOTDIR;
                c2_cob_put(cob);
                goto out;
        }

        rdpg.r_pos = c2_bitstring_alloc(req->r_pos.s_buf, 
                                        req->r_pos.s_len);
        if (rdpg.r_pos == NULL) {
                c2_cob_put(cob);
                rc = -ENOMEM;
                goto out;
        }

        addr = c2_alloc(C2_MD_READDIR_BUF_ALLOC);
        if (addr == NULL) {
                c2_bitstring_free(rdpg.r_pos);
                c2_cob_put(cob);
                rc = -ENOMEM;
                goto out;
        }
        
        c2_buf_init(&rdpg.r_buf, addr, C2_MD_READDIR_BUF_ALLOC);

        rc = c2_md_store_readdir(site->s_mdstore, cob, &rdpg,
                                 &ctx->fc_tx->tx_dbtx);
        c2_bitstring_free(rdpg.r_pos);
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
        strncpy(rep->r_end.s_buf, c2_bitstring_buf_get(rdpg.r_end),
                rep->r_end.s_len);

        /* 
         * Prepare buf with data.
         */
        rep->r_buf.b_count = rdpg.r_buf.b_nob;
        rep->r_buf.b_addr = rdpg.r_buf.b_addr;
        
        /*
         * Post reply in non-error cases.
         */
        if (rc >= 0) {
                svc = fom->fo_fop_ctx->fc_service;
                svc->s_ops->so_reply_post(svc, fop_rep, ctx->fc_cookie);
        }
out:
	ctx->fc_retval = rc;
        if (rc < 0)
                c2_fop_free(fop_rep);
	fom->fo_phase = rc < 0 ? FOPH_FAILED : FOPH_DONE;
        return FSO_AGAIN;
}

static void c2_md_req_fom_fini(struct c2_fom *fom)
{
        struct c2_fom_md        *fom_obj;
        struct c2_fop           *fop;
        
        fom_obj = container_of(fom, struct c2_fom_md, fm_fom);
        C2_ASSERT(fom_obj->fm_fop != NULL);
        fop = fom_obj->fm_fop;
        c2_free(fom_obj);
}

static struct c2_fom_ops c2_md_fom_create_ops = {
	.fo_state  = c2_md_create_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_link_ops = {
	.fo_state  = c2_md_link_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_unlink_ops = {
	.fo_state  = c2_md_unlink_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_rename_ops = {
	.fo_state  = c2_md_rename_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_open_ops = {
	.fo_state  = c2_md_open_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_close_ops = {
	.fo_state = c2_md_close_fom_state,
	.fo_fini  = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_setattr_ops = {
	.fo_state  = c2_md_setattr_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_getattr_ops = {
	.fo_state  = c2_md_getattr_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_ops c2_md_fom_readdir_ops = {
	.fo_state  = c2_md_readdir_fom_state,
	.fo_fini   = c2_md_req_fom_fini
};

static struct c2_fom_type c2_md_fom_type_pch = {0,};

int c2_md_rep_fom_init(struct c2_fop *fop, 
                       struct c2_fop_ctx *ctx, 
                       struct c2_fom **m)
{
        return 0;
}

static int c2_md_req_path_get(struct c2_md_store *mdstore,
                              struct c2_fid *fid,
                              struct c2_fop_str *str)
{
        int rc;

        rc = c2_md_store_path(mdstore, fid, &str->s_buf);
        if (rc)
                return rc;
        str->s_len = strlen(str->s_buf);
        return 0;
}

static inline struct c2_fid *c2_md_fid_get(struct c2_fop_fid *fid)
{
        static struct c2_fid fid_fop2mem;
        c2_md_make_fid(&fid_fop2mem, fid);
        return &fid_fop2mem;
}

int c2_md_req_fom_init(struct c2_fop *fop, 
                       struct c2_fop_ctx *ctx, 
                       struct c2_fom **m)
{
        struct c2_fom           *fom;
        struct c2_site          *site;
        struct c2_fom_md        *fom_obj;
        struct c2_fom_type      *fom_type;
        struct c2_fop_type      *fop_type;
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
        C2_PRE(m != NULL);

        fom_obj= c2_alloc(sizeof(struct c2_fom_md));
        if (fom_obj == NULL)
                return -ENOMEM;
        
        /* 
         * To cheat a bit on foms generic code. 
         */
        fom_type = &c2_md_fom_type_pch;
        fop->f_type->ft_fom_type = *fom_type;
        fom = &fom_obj->fm_fom;
        fom->fo_type = fom_type;
        fom->fo_phase = FOPH_INIT;
        fom->fo_fop_ctx = ctx;
	
        site = ctx->fc_site;
        C2_ASSERT(site != NULL);

        switch (fop->f_type->ft_code) {
        case C2_FOP_CREATE:
		fom->fo_ops = &c2_md_fom_create_ops;
		fop_type = &c2_fop_create_rep_fopt;

		create = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&create->c_body.b_pfid),
                                        &create->c_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_LINK:
		fom->fo_ops = &c2_md_fom_link_ops;
		fop_type = &c2_fop_link_rep_fopt;

		link = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&link->l_body.b_pfid),
                                        &link->l_tpath);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&link->l_body.b_tfid),
                                        &link->l_spath);
                if (rc) {
                        c2_free(link->l_tpath.s_buf);
                        link->l_tpath.s_buf = NULL;
                        link->l_tpath.s_len = 0;
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_UNLINK:
		fom->fo_ops = &c2_md_fom_unlink_ops;
		fop_type = &c2_fop_unlink_rep_fopt;

		unlink = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&unlink->u_body.b_pfid),
                                        &unlink->u_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_RENAME:
		fom->fo_ops = &c2_md_fom_rename_ops;
		fop_type = &c2_fop_rename_rep_fopt;

		rename = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&rename->r_sbody.b_pfid),
                                        &rename->r_spath);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&rename->r_tbody.b_pfid),
                                        &rename->r_tpath);
                if (rc) {
                        c2_free(rename->r_spath.s_buf);
                        rename->r_spath.s_buf = NULL;
                        rename->r_spath.s_len = 0;
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_OPEN:
		fom->fo_ops = &c2_md_fom_open_ops;
		fop_type = &c2_fop_open_rep_fopt;

		open = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&open->o_body.b_tfid),
                                        &open->o_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_CLOSE:
		fom->fo_ops = &c2_md_fom_close_ops;
		fop_type = &c2_fop_close_rep_fopt;

	        close = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&close->c_body.b_tfid),
                                        &close->c_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_SETATTR:
		fom->fo_ops = &c2_md_fom_setattr_ops;
		fop_type = &c2_fop_setattr_rep_fopt;

		setattr = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&setattr->s_body.b_tfid),
                                        &setattr->s_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_GETATTR:
		fom->fo_ops = &c2_md_fom_getattr_ops;
		fop_type = &c2_fop_getattr_rep_fopt;

		getattr = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore, 
                                        c2_md_fid_get(&getattr->g_body.b_tfid),
                                        &getattr->g_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        case C2_FOP_READDIR:
		fom->fo_ops = &c2_md_fom_readdir_ops;
		fop_type = &c2_fop_readdir_rep_fopt;

		readdir = c2_fop_data(fop);
		rc = c2_md_req_path_get(site->s_mdstore,
                                        c2_md_fid_get(&readdir->r_body.b_tfid),
                                        &readdir->r_path);
                if (rc) {
                        c2_free(fom_obj);
                        return rc;
                }
	        break;
        default:
                c2_free(fom_obj);
                return -EOPNOTSUPP;
        }

        fom_obj->fm_fop_rep = c2_fop_alloc(fop_type, NULL);
	if (fom_obj->fm_fop_rep == NULL) {
                c2_free(fom_obj);
	        return -ENOMEM;
	}

	fom_obj->fm_fop = fop;
	*m = &fom_obj->fm_fom;
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
