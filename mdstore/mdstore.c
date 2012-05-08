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
 * Original creation date: 01/17/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/stat.h>    /* S_ISDIR */

#include "lib/misc.h"    /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/rwlock.h"

#include "fid/fid.h"
#include "addb/addb.h"
#include "db/db.h"
#include "fop/fop.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h"

#include "mdservice/md_fops_u.h"
#include "mdservice/md_foms.h"

static const struct c2_addb_ctx_type mdstore_addb_ctx = {
	.act_name = "mdstore"
};

static const struct c2_addb_loc mdstore_addb_loc = {
	.al_name = "mdstore"
};

/**
   Initialize mdstore on passed @id and db. Input argument @init_root
   controls whether root cob should be initialized.
*/
int c2_md_store_init(struct c2_md_store         *md, 
                     struct c2_cob_domain_id    *id,
                     struct c2_dbenv            *db, 
                     int                         init_root)
{
        struct c2_db_tx        tx;
        int                    rc;

        C2_ASSERT(md != NULL && id != NULL && db != NULL);
        
        C2_SET0(md);
        c2_addb_ctx_init(&md->md_addb, &mdstore_addb_ctx, 
                         &md->md_dom.cd_dbenv->d_addb);
        rc = c2_cob_domain_init(&md->md_dom, db, id);
        if (rc) {
                C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                            c2_addb_func_fail, "cob_domain_init", rc);
	        c2_addb_ctx_fini(&md->md_addb);
                return rc;
        }
        if (init_root) {
                c2_db_tx_init(&tx, db, 0);
                rc = c2_md_store_lookup(md, NULL, C2_COB_ROOT_NAME, 
                                        strlen(C2_COB_ROOT_NAME), 
                                        &md->md_root, &tx);
                C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                            c2_addb_func_fail, "md_root_lookup", rc);
                if (rc) {
                        c2_db_tx_abort(&tx);
                } else {
                        /**
                           Lets check for omgid terminator record present.
                           If new omgid may be allocted then we're fine.
                         */
                        rc = c2_cob_alloc_omgid(&md->md_dom, &tx, NULL);
                        if (rc)
                                c2_db_tx_abort(&tx);
                        else
                                c2_db_tx_commit(&tx);
                }
        }
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_store_init", rc);
        if (rc) {
	        c2_addb_ctx_fini(&md->md_addb);
                c2_cob_domain_fini(&md->md_dom);
        }
        return rc;
}

/**
   Finalize mdstore
 */
void c2_md_store_fini(struct c2_md_store *md)
{
        if (md->md_root)
                c2_cob_put(md->md_root);
        c2_cob_domain_fini(&md->md_dom);
	c2_addb_ctx_fini(&md->md_addb);
}

/**
   Handle create operation described by @attr on @cob. Input @cob
   is returned by c2_cob_alloc().
   
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_create(struct c2_md_store       *md,
                       struct c2_fid            *pfid,
                       struct c2_cob_attr       *attr,
                       struct c2_cob           **out,
                       struct c2_db_tx          *tx)
{
        struct c2_cob         *cob;
        struct c2_cob_nskey   *nskey;
        struct c2_cob_nsrec    nsrec;
        struct c2_cob_fabrec  *fabrec;
        struct c2_cob_omgrec   omgrec;
        int                    rc;

        C2_ASSERT(pfid != NULL);

        C2_SET0(&nsrec);
        C2_SET0(&omgrec);
        
        rc = c2_cob_alloc(&md->md_dom, &cob);
        if (rc)
                goto out;

        c2_cob_make_nskey(&nskey, pfid, attr->ca_name, 
                          attr->ca_namelen);

        nsrec.cnr_fid = attr->ca_tfid;
        C2_ASSERT(attr->ca_nlink > 0);
        nsrec.cnr_nlink = attr->ca_nlink;
        nsrec.cnr_rdev = attr->ca_rdev;
        nsrec.cnr_size = attr->ca_size;
        nsrec.cnr_blksize = attr->ca_blksize;
        nsrec.cnr_blocks = attr->ca_blocks;
        nsrec.cnr_version = attr->ca_version;
        nsrec.cnr_atime = attr->ca_atime;
        nsrec.cnr_mtime = attr->ca_mtime;
        nsrec.cnr_ctime = attr->ca_ctime;

        omgrec.cor_uid = attr->ca_uid;
        omgrec.cor_gid = attr->ca_gid;
        omgrec.cor_mode = attr->ca_mode;

        c2_cob_make_fabrec(&fabrec, attr->ca_link, 
                           attr->ca_link ? attr->ca_size : 0);
        rc = c2_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, tx);
        if (rc) {
                c2_cob_put(cob);
                c2_free(nskey);
                c2_free(fabrec);
        } else {
                *out = cob;
        }

out:
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_create", rc);
        return rc;
}

/**
   Handle link operation described by @pfid and @name. Input
   cob is so called statdata cob and returned by c2_cob_locate(). 
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_link(struct c2_md_store         *md, 
                     struct c2_fid              *pfid,
                     struct c2_cob              *cob,
                     const char                 *name,
                     int                         namelen,
                     struct c2_db_tx            *tx)
{
        struct c2_cob_nskey   *nskey;
        struct c2_cob_nsrec    nsrec;
        time_t                 now;
        int                    rc;
        
        C2_ASSERT(pfid != NULL);
        C2_ASSERT(cob != NULL);

        time(&now);
        C2_SET0(&nsrec);

        /*
         * Link @nskey to a file described with @cob
         */
        c2_cob_make_nskey(&nskey, pfid, name, namelen); 
        C2_PRE(c2_fid_is_set(&cob->co_nsrec.cnr_fid));

        nsrec.cnr_fid = cob->co_nsrec.cnr_fid;
        nsrec.cnr_linkno = cob->co_nsrec.cnr_cntr;

        rc = c2_cob_add_name(cob, nskey, &nsrec, tx);
        c2_free(nskey);
        if (rc)
                goto out;

        /*
         * Update nlink and links allocation counter in statdata and
         * save it to storage. 
         */
        cob->co_nsrec.cnr_cntr++;

        rc = c2_cob_update(cob, &cob->co_nsrec, NULL, NULL, tx);
out:
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_link", rc);
        return rc;
}

/**
   Handle unlink operation described by @pfid and @name. Input
   cob is so called statdata cob and returned by c2_cob_locate(). 
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_unlink(struct c2_md_store       *md,
                       struct c2_fid            *pfid,
                       struct c2_cob            *cob,
                       const char               *name,
                       int                       namelen,
                       struct c2_db_tx          *tx)
{
        struct c2_cob         *ncob;
        struct c2_cob_nskey   *nskey;
        struct c2_cob_oikey    oikey;
        time_t                 now;
        int                    rc;
    
        C2_ASSERT(pfid != NULL);
        C2_ASSERT(cob != NULL);

        C2_PRE(cob->co_nsrec.cnr_nlink > 0);
        
        time(&now);
        
        /*
         * Check for hardlinks.
         */
        if (!S_ISDIR(cob->co_omgrec.cor_mode)) {
                c2_cob_make_nskey(&nskey, pfid, name, namelen);
                
                /*
                 * New stat data name should get updated nlink value.
                 */
                cob->co_nsrec.cnr_nlink--;

                /*
                 * Check if we're trying to kill stata data entry. We need to
                 * move stat data to another name if so.
                 */
                if (cob->co_nsrec.cnr_nlink > 0 && 
                    c2_cob_nskey_cmp(nskey, cob->co_nskey) == 0) {
                        /*
                         * Find another name (new stat data) in object index.
                         */
                        c2_cob_make_oikey(&oikey, cob->co_fid, 
                                          cob->co_nsrec.cnr_linkno + 1);
                
                        rc = c2_cob_locate(&md->md_dom, &oikey, &ncob, tx);
                        if (rc) {
                                c2_free(nskey);
                                goto out;
                        }

                        /*
                         * Copy nsrec from cob to ncob.
                         */
                        rc = c2_cob_update(ncob, &cob->co_nsrec, NULL, NULL, tx);
                        if (rc) {
                                c2_free(nskey);
                                goto out;
                        }

                        rc = c2_cob_del_name(cob, nskey, tx);
                        if (rc) {
                                c2_free(nskey);
                                goto out;
                        }
                } else {
                        if (cob->co_nsrec.cnr_nlink > 0) {
                                rc = c2_cob_del_name(cob, nskey, tx);
                                if (rc) {
                                        c2_free(nskey);
                                        goto out;
                                }
                                rc = c2_cob_update(cob, &cob->co_nsrec, 
                                                   NULL, NULL, tx);
                        } else {
                                rc = c2_cob_delete(cob, tx);
                        }
                }

                c2_free(nskey);
        } else {
                /*
                 * We ignore nlink for dirs and go for killing it.
                 * This is because we don't update parent nlink in
                 * case of killing subdirs. This results in a case
                 * that dir will have nlink > 0 becasue correct
                 * fop that will bring its nlink to zero will come
                 * later.
                 */
                cob->co_nsrec.cnr_nlink = 0;
                rc = c2_cob_delete(cob, tx);
        }

out:
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_unlink", rc);
        return rc;
}

/**
   Handle open operation described by @flags on @cob. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_open(struct c2_md_store         *md, 
                     struct c2_cob              *cob,
                     int                         flags,
                     struct c2_db_tx            *tx)
{
        int rc = 0;
        
        C2_ASSERT(cob != NULL);

        /* 
         * @todo: Place cob to open files table. 
         */

        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_open", rc);
        return rc;
}

/**
   Handle close operation on @cob. Input @cob is so called statdata
   cob and returned by c2_cob_locate(). 

   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_close(struct c2_md_store        *md, 
                      struct c2_cob             *cob,
                      struct c2_db_tx           *tx)
{
        int rc = 0;
        
        C2_ASSERT(cob != NULL);

        /*
         * @todo:
         *   - orphans handling?
         *   - quota handling?
         */
        
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_close", rc);
        return rc;
}

/**
   Handle rename operation described by params. Input cobs are
   statdata cobs and returned by c2_cob_locate(). Rest of the
   arguments are self explanatory.

   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_rename(struct c2_md_store       *md, 
                       struct c2_fid            *pfid_tgt,
                       struct c2_fid            *pfid_src,
                       struct c2_cob            *cob_tgt,
                       struct c2_cob            *cob_src,
                       const char               *tname,
                       int                       tnamelen,
                       const char               *sname,
                       int                       snamelen,
                       struct c2_db_tx          *tx)
{
        struct c2_cob_nskey  *srckey = NULL;
        struct c2_cob_nskey  *tgtkey = NULL;
        struct c2_cob        *tncob = NULL;
        time_t                now;
        int                   rc;

        C2_ASSERT(pfid_tgt != NULL);
        C2_ASSERT(pfid_src != NULL);

        time(&now);

        /*
         * Let's kill existing target name.
         */
        rc = c2_md_store_lookup(md, pfid_tgt, tname, tnamelen,
                                &tncob, tx);
        if (!c2_fid_eq(cob_tgt->co_fid, cob_src->co_fid) ||
            (tncob && tncob->co_nsrec.cnr_linkno != 0)) {
                rc = c2_md_store_unlink(md, pfid_tgt, cob_tgt,
                                        tname, tnamelen, tx);
                if (rc) {
                        if (tncob)
                                c2_cob_put(tncob);
                        goto out;
                }
        }
        if (tncob)
                c2_cob_put(tncob);
        /*
         * Prepare src and dst keys.
         */                
        c2_cob_make_nskey(&srckey, pfid_src, sname, snamelen);
        c2_cob_make_nskey(&tgtkey, pfid_tgt, tname, tnamelen);

        rc = c2_cob_update_name(cob_src, srckey, tgtkey, tx);

        c2_free(srckey);
        c2_free(tgtkey);
out:
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_rename", rc);
        return rc;
}

/**
   Handle setattr operation described by @attr on @cob. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_setattr(struct c2_md_store      *md,
                        struct c2_cob           *cob,
                        struct c2_cob_attr      *attr,
                        struct c2_db_tx         *tx)
{
        struct c2_cob_nsrec   *nsrec = NULL;
        struct c2_cob_fabrec  *fabrec = NULL;
        struct c2_cob_omgrec  *omgrec = NULL;
        int                    rc;

        C2_ASSERT(cob != NULL);        

        /*
         * Handle basic stat fields update.
         */
        if (cob->co_valid & CA_NSREC) {
                nsrec = &cob->co_nsrec;
                if (attr->ca_flags & C2_COB_ATIME)
                        nsrec->cnr_atime = attr->ca_atime;
                if (attr->ca_flags & C2_COB_MTIME)
                        nsrec->cnr_mtime = attr->ca_mtime;
                if (attr->ca_flags & C2_COB_CTIME)
                        nsrec->cnr_ctime = attr->ca_ctime;
                if (attr->ca_flags & C2_COB_SIZE)
                        nsrec->cnr_size = attr->ca_size;
                if (attr->ca_flags & C2_COB_RDEV)
                        nsrec->cnr_rdev = attr->ca_rdev;
                if (attr->ca_flags & C2_COB_BLOCKS)
                        nsrec->cnr_blocks = attr->ca_blocks;
                if (attr->ca_flags & C2_COB_BLKSIZE)
                        nsrec->cnr_blksize = attr->ca_blksize;
                if (attr->ca_flags & C2_COB_NLINK) {
                        C2_ASSERT(attr->ca_nlink > 0);
                        nsrec->cnr_nlink = attr->ca_nlink;
                }
                nsrec->cnr_version = attr->ca_version;
        }

        /*
         * Handle uid/gid/mode update.
         */
        if (cob->co_valid & CA_OMGREC) {
                omgrec = &cob->co_omgrec;
                if (attr->ca_flags & C2_COB_UID)
                        omgrec->cor_uid = attr->ca_uid;
                if (attr->ca_flags & C2_COB_GID)
                        omgrec->cor_gid = attr->ca_gid;
                if (attr->ca_flags & C2_COB_MODE)
                        omgrec->cor_mode = attr->ca_mode;
        }
        
        /*
         * @todo: update fabrec.
         */
        if (cob->co_valid & CA_FABREC)
                fabrec = cob->co_fabrec;

        rc = c2_cob_update(cob, nsrec, fabrec, omgrec, tx);
        
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_setattr", rc);
        return rc;
}

/**
   Get attributes of @cob into passed @attr. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_getattr(struct c2_md_store      *md, 
                        struct c2_cob           *cob,
                        struct c2_cob_attr      *attr,
                        struct c2_db_tx         *tx)
{
        int                rc = 0;

        C2_ASSERT(cob != NULL);

        attr->ca_flags = 0;
        
        /*
         * Copy permissions and owner info into rep.
         */
        if (cob->co_valid & CA_OMGREC) {
                attr->ca_flags |= C2_COB_UID | C2_COB_GID | C2_COB_MODE;
                attr->ca_uid = cob->co_omgrec.cor_uid;
                attr->ca_gid = cob->co_omgrec.cor_gid;
                attr->ca_mode = cob->co_omgrec.cor_mode;
        }

        /*
         * Copy nsrec fields into response.
         */
        if (cob->co_valid & CA_NSREC) {
                attr->ca_flags |= C2_COB_ATIME | C2_COB_CTIME | C2_COB_MTIME |
                                  C2_COB_SIZE | C2_COB_BLKSIZE | C2_COB_BLOCKS |
                                  C2_COB_RDEV;
                attr->ca_atime = cob->co_nsrec.cnr_atime;
                attr->ca_ctime = cob->co_nsrec.cnr_ctime;
                attr->ca_mtime = cob->co_nsrec.cnr_mtime;
                attr->ca_blksize = cob->co_nsrec.cnr_blksize;
                attr->ca_blocks = cob->co_nsrec.cnr_blocks;
                attr->ca_nlink = cob->co_nsrec.cnr_nlink;
                attr->ca_rdev = cob->co_nsrec.cnr_rdev;
                attr->ca_size = cob->co_nsrec.cnr_size;
                attr->ca_version = cob->co_nsrec.cnr_version;
        }
        
        /*
         * @todo: Copy fab fields.
         */
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_getattr", rc);
        return rc;
}

/**
   Handle readdir operation described by @rdpg on @cob. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   
   Error code is returned in error case or something >= 0 otherwise.
*/
int c2_md_store_readdir(struct c2_md_store      *md, 
                        struct c2_cob           *cob,
                        struct c2_rdpg          *rdpg,
                        struct c2_db_tx         *tx)
{
        struct c2_cob_iterator         it;
        struct c2_dirent              *ent;
        struct c2_dirent              *last = NULL;
        char                          *name = ".";
        int                            nob;
        int                            recsize;
        int                            len = 1;
        int                            first;
        int                            second;
        int                            rc;
        
        C2_ASSERT(cob != NULL);

        first = c2_bitstring_len_get(rdpg->r_pos) == 1 &&
                !strncmp(c2_bitstring_buf_get(rdpg->r_pos), ".", 1);
        second = 0;

        rc = c2_cob_iterator_init(cob, &it, rdpg->r_pos, tx);
        if (rc)
                goto out;

        rc = c2_cob_iterator_get(&it);
        if (rc == 0) {
                /* 
                 * Not exact position found and we are on least key
                 * let's do one step forward.
                 */
                rc = c2_cob_iterator_next(&it);
        } else if (rc > 0)
                rc = 0;
    
        ent = rdpg->r_buf.b_addr;
        nob = rdpg->r_buf.b_nob;
        while (rc == 0 || first || second) {
                int next_step = 0;

                if (first) {
                        name = ".";
                        len = 1;
                        second = 1;
                        first = 0;
                } else if (second) {
                        name = "..";
                        len = 2;
                        second = 0;
                } else {
                        if (!c2_fid_eq(&it.ci_key->cnk_pfid, cob->co_fid)) {
                                rc = 1;
                                break;
                        }
                
                        name = c2_bitstring_buf_get(&it.ci_key->cnk_name);
                        len = c2_bitstring_len_get(&it.ci_key->cnk_name);
                        next_step = 1;
                }

                recsize = ((sizeof(*ent) + len) + 7) & ~7;

                if (nob >= recsize) {
                        strncpy(ent->d_name, name, len);
                        ent->d_namelen = len;
                        ent->d_reclen = recsize;
                } else {
                        if (last) {
                                last->d_reclen += nob;
                                rc = 0;
                        } else {
                                rc = -EINVAL;
                        }
                        goto out_end;
                }
                last = ent;
                ent = (void *)ent + recsize;
                nob -= recsize;

                if (next_step)
                        rc = c2_cob_iterator_next(&it);
        }
out_end:
        c2_cob_iterator_fini(&it);
        if (rc >= 0) {
                if (last)
                        last->d_reclen = 0;
                rdpg->r_end = c2_bitstring_alloc(name, len);
        }
out:
        C2_ADDB_ADD(&md->md_addb, &mdstore_addb_loc, 
                    c2_addb_func_fail, "md_readdir", rc);
        return rc;
}

/**
   Find cob by fid.
*/
int c2_md_store_locate(struct c2_md_store       *md, 
                       const struct c2_fid      *fid,
                       struct c2_cob           **cob, 
                       int                       flags, 
                       struct c2_db_tx          *tx)
{
        struct c2_cob_oikey oikey;
        int                 rc;

        c2_cob_make_oikey(&oikey, fid, 0);

        if (flags == C2_MD_LOCATE_STORED) {
                rc = c2_cob_locate(&md->md_dom, &oikey, cob, tx);
        } else {
                /*
                 * @todo: locate cob in opened cobs table.
                 */
                rc = -EOPNOTSUPP;
        }

        return rc;
}

/**
   Find cob by parent and name.
*/
int c2_md_store_lookup(struct c2_md_store       *md, 
                       struct c2_fid            *pfid,
                       const char               *name, 
                       int                       namelen, 
                       struct c2_cob           **cob, 
                       struct c2_db_tx          *tx)
{
        struct c2_cob_nskey *nskey;
        int flags;

        if (pfid == NULL)
                pfid = &C2_COB_ROOT_FID;

        c2_cob_make_nskey(&nskey, pfid, name, namelen);
        flags = (CA_NSKEY_FREE | CA_FABREC | CA_OMGREC);
        return c2_cob_lookup(&md->md_dom, nskey, flags,
                             cob, tx);
}

/**
   Get path by @fid. Path @path is allocated by c2_alloc() on
   success and path is saved there. When it is not longer needed
   it may be freed with c2_free().
   
   Error code is returned on error or zero on success.
 */
int c2_md_store_path(struct c2_md_store *md,
                     struct c2_fid *fid,
                     char **path)
{
        struct c2_cob   *cob;
        struct c2_fid    pfid;
        struct c2_db_tx  tx;
        int              rc;
        
        *path = c2_alloc(PATH_MAX);
        if (*path == NULL)
                return -ENOMEM;

restart:
        pfid = *fid;

        c2_db_tx_init(&tx, md->md_dom.cd_dbenv, 0);
        do {
                char name[NAME_MAX] = {0,};

                rc = c2_md_store_locate(md, &pfid, &cob, C2_MD_LOCATE_STORED, &tx);
                if (rc)
                        goto out;

	        if (!c2_fid_eq(cob->co_fid, md->md_root->co_fid)) {
                        strncat(name, 
                                c2_bitstring_buf_get(&cob->co_nskey->cnk_name),
                                c2_bitstring_len_get(&cob->co_nskey->cnk_name));
                }
                if (!c2_fid_eq(cob->co_fid, fid) || 
                    c2_fid_eq(cob->co_fid, md->md_root->co_fid))
                        strcat(name, "/");
                memmove(*path + strlen(name), *path, strlen(*path));
                memcpy(*path, name, strlen(name));
                pfid = cob->co_nskey->cnk_pfid;
                c2_cob_put(cob);
        } while (!c2_fid_eq(&pfid, &C2_COB_ROOT_FID));
out:
        if (rc) {
                c2_db_tx_abort(&tx);
                if (rc == -EDEADLK) {
                        memset(*path, 0, PATH_MAX);
                        goto restart;
                }
                c2_free(*path);
                *path = NULL;
        } else {
                c2_db_tx_commit(&tx);
        }
        return rc;
}
