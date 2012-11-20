/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Revision       : Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 10/14/2011
 */

#include "lib/misc.h"      /* C2_SET0() */
#include "lib/memory.h"    /* C2_ALLOC_PTR() */
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"           /* C2_LOG and C2_ENTRY */
#include "lib/bob.h"
#include "fop/fop.h"             /* c2_fop_alloc() */
#include "rpc/rpclib.h"          /* c2_rpc_client_call */
#include "rpc/rpc_opcodes.h"
#include "ioservice/io_device.h"
#include "colibri/magic.h"
#include "c2t1fs/linux_kernel/c2t1fs.h"

extern const struct c2_rpc_item_ops cob_req_rpc_item_ops;
extern void c2t1fs_inode_bob_init(struct c2t1fs_inode *bob);
extern bool c2t1fs_inode_bob_check(struct c2t1fs_inode *bob);

static int c2t1fs_create(struct inode     *dir,
			 struct dentry    *dentry,
			 int               mode,
			 struct nameidata *nd);

static struct dentry *c2t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd);

static int c2t1fs_readdir(struct file *f,
			  void        *dirent,
			  filldir_t    filldir);

static int c2t1fs_unlink(struct inode *dir, struct dentry *dentry);

static int c2t1fs_component_objects_op(struct c2t1fs_inode *ci,
				       int (*func)(struct c2t1fs_sb *csb,
					           const struct c2_fid *cfid,
						   const struct c2_fid *gfid));

static int c2t1fs_ios_cob_op(struct c2t1fs_sb    *csb,
			     const struct c2_fid *cob_fid,
			     const struct c2_fid *gob_fid,
			     struct c2_fop_type  *ftype);

static int c2t1fs_ios_cob_create(struct c2t1fs_sb    *csb,
			         const struct c2_fid *cob_fid,
			         const struct c2_fid *gob_fid);

static int c2t1fs_ios_cob_delete(struct c2t1fs_sb    *csb,
			         const struct c2_fid *cob_fid,
			         const struct c2_fid *gob_fid);

static int c2t1fs_ios_cob_fop_populate(struct c2t1fs_sb    *csb,
				       struct c2_fop       *fop,
				       const struct c2_fid *cob_fid,
				       const struct c2_fid *gob_fid);

const struct file_operations c2t1fs_dir_file_operations = {
	.read    = generic_read_dir,    /* provided by linux kernel */
	.readdir = c2t1fs_readdir,
	.fsync   = simple_fsync,	/* provided by linux kernel */
	.llseek  = generic_file_llseek, /* provided by linux kernel */
};

const struct inode_operations c2t1fs_dir_inode_operations = {
	.create = c2t1fs_create,
	.lookup = c2t1fs_lookup,
	.unlink = c2t1fs_unlink
};

static void fid_mem2wire(struct c2_fop_fid   *tgt,
                         const struct c2_fid *src)
{
        tgt->f_seq = src->f_container;
        tgt->f_oid = src->f_key;
}

static void fid_wire2mem(struct c2_fid     *tgt,
                         struct c2_fop_fid *src)
{
        tgt->f_container = src->f_seq;
        tgt->f_key = src->f_oid;
}

static int name_mem2wire(struct c2_fop_str *tgt,
                         const char *name, int namelen)
{
        tgt->s_buf = c2_alloc(namelen);
        if (tgt->s_buf == NULL)
                return -ENOMEM;
        tgt->s_len = namelen;
        memcpy(tgt->s_buf, name, namelen);
        return 0;
}

static void body_mem2wire(struct c2_fop_cob *body, struct c2_cob_attr *attr, int valid)
{
        if (valid & C2_COB_ATIME)
                body->b_atime = attr->ca_atime;
        if (valid & C2_COB_CTIME)
                body->b_ctime = attr->ca_ctime;
        if (valid & C2_COB_MTIME)
                body->b_mtime = attr->ca_mtime;
        if (valid & C2_COB_BLOCKS)
                body->b_blocks = attr->ca_blocks;
        if (valid & C2_COB_SIZE)
                body->b_size = attr->ca_size;
        if (valid & C2_COB_MODE)
                body->b_mode = attr->ca_mode;
        if (valid & C2_COB_UID)
                body->b_uid = attr->ca_uid;
        if (valid & C2_COB_GID)
                body->b_gid = attr->ca_gid;
        if (valid & C2_COB_BLOCKS)
                body->b_blocks = attr->ca_blocks;
        if (valid & C2_COB_NLINK)
                body->b_nlink = attr->ca_nlink;

        body->b_valid = valid;
}


/**
   Allocate fid of global file.

   See "Containers and component objects" section in c2t1fs.h for
   more information.

   XXX temporary.
 */
static struct c2_fid c2t1fs_fid_alloc(struct c2t1fs_sb *csb)
{
	struct c2_fid fid;

	C2_PRE(c2t1fs_fs_is_locked(csb));

	fid.f_container = 0;
	fid.f_key       = csb->csb_next_key++;

	return fid;
}

static int c2t1fs_create(struct inode     *dir,
                         struct dentry    *dentry,
                         int               mode,
                         struct nameidata *nd)
{
	struct super_block       *sb = dir->i_sb;
	struct c2t1fs_sb         *csb = C2T1FS_SB(sb);
	struct c2_fop_create_rep *rep = NULL;
	struct c2t1fs_inode      *ci;
	struct c2t1fs_mdop        mo;
	struct inode             *inode;
	int                       rc;

	C2_ENTRY();

	/* Flat file system. create allowed only on root directory */
	C2_ASSERT(c2t1fs_inode_is_root(dir));

	/* new_inode() will call c2t1fs_alloc_inode() using super_operations */
	inode = new_inode(sb);
	if (inode == NULL) {
		C2_LEAVE("rc: %d", -ENOMEM);
		return -ENOMEM;
	}

	c2t1fs_fs_lock(csb);

	inode->i_uid    = 0;
	inode->i_gid    = 0;
	inode->i_mtime  = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blocks = 0;
	inode->i_op     = &c2t1fs_reg_inode_operations;
	inode->i_fop    = &c2t1fs_reg_file_operations;
	inode->i_mode   = mode;

	ci               = C2T1FS_I(inode);
	ci->ci_fid       = c2t1fs_fid_alloc(csb);

	ci->ci_layout_id = csb->csb_layout_id;

	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	rc = c2t1fs_inode_layout_init(ci);
	if (rc != 0)
		goto out;

        /** No hierarchy so far, all live in root */
        C2_SET0(&mo);
        mo.mo_attr.ca_uid       = inode->i_uid;
        mo.mo_attr.ca_gid       = inode->i_gid;
        mo.mo_attr.ca_atime     = inode->i_atime.tv_sec;
        mo.mo_attr.ca_ctime     = inode->i_ctime.tv_sec;
        mo.mo_attr.ca_mtime     = inode->i_mtime.tv_sec;
        mo.mo_attr.ca_blocks    = inode->i_blocks;
        mo.mo_attr.ca_mode      = inode->i_mode;
        mo.mo_attr.ca_pfid      = c2t1fs_root_fid;
        mo.mo_attr.ca_tfid      = ci->ci_fid;
        mo.mo_attr.ca_name      = (char *)dentry->d_name.name;
        mo.mo_attr.ca_namelen   = dentry->d_name.len;

        rc = c2t1fs_mds_cob_create(csb, &mo, &rep);
        if (rc != 0)
                goto out;

	rc = c2t1fs_component_objects_op(ci, c2t1fs_ios_cob_create);
	if (rc != 0)
		goto out;

	c2t1fs_fs_unlock(csb);

	d_instantiate(dentry, inode);
	C2_LEAVE("rc: 0");
	return 0;
out:
	inode_dec_link_count(inode);
	c2t1fs_fs_unlock(csb);
	iput(inode);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static struct dentry *c2t1fs_lookup(struct inode     *dir,
				    struct dentry    *dentry,
				    struct nameidata *nd)
{
	struct c2t1fs_sb         *csb;
	struct c2t1fs_inode      *ci;
	struct inode             *inode = NULL;
        struct c2_fop_lookup_rep *rep = NULL;
	struct c2t1fs_mdop        mo;
	int rc;


	C2_ENTRY();

	ci = C2T1FS_I(dir);

	if (dentry->d_name.len > C2T1FS_MAX_NAME_LEN) {
		C2_LEAVE("ERR_PTR: %p", ERR_PTR(-ENAMETOOLONG));
		return ERR_PTR(-ENAMETOOLONG);
	}

	C2_LOG(C2_DEBUG, "Name: \"%s\"", dentry->d_name.name);

	csb = C2T1FS_SB(dir->i_sb);

	c2t1fs_fs_lock(csb);

        C2_SET0(&mo);
        mo.mo_attr.ca_pfid = c2t1fs_root_fid;
        mo.mo_attr.ca_name = (char *)dentry->d_name.name;
        mo.mo_attr.ca_namelen = dentry->d_name.len;

	rc = c2t1fs_mds_cob_lookup(csb, &mo, &rep);
	if (rc != 0) {
	        C2_LOG(C2_ERROR, "Lookup \"%*s\" failed: %d",
	               mo.mo_attr.ca_namelen, mo.mo_attr.ca_name, rc);
	        return ERR_PTR(rc);
	}

        fid_wire2mem(&mo.mo_attr.ca_tfid, &rep->l_body.b_tfid);
        inode = c2t1fs_iget(dir->i_sb, &mo.mo_attr.ca_tfid, &rep->l_body);
	if (inode == NULL || IS_ERR(inode)) {
		C2_LEAVE("ERROR: %p", ERR_CAST(inode));
		return ERR_CAST(inode);
	}

	c2t1fs_fs_unlock(csb);
	d_add(dentry, inode);
	C2_LEAVE("NULL");
	return NULL;
}

struct c2_dirent *dirent_next(struct c2_dirent *ent)
{
        if (ent->d_reclen > 0)
                return (void *)ent + ent->d_reclen;
        else
                return NULL;
}

struct c2_dirent *dirent_first(struct c2_fop_readdir_rep *rep)
{
        return (struct c2_dirent *)rep->r_buf.b_addr;
}

static int c2t1fs_readdir(struct file *f,
			  void        *buf,
			  filldir_t    filldir)
{
	struct c2t1fs_inode             *ci;
	struct c2t1fs_mdop               mo;
	struct c2t1fs_sb                *csb;
	struct c2_fop_readdir_rep       *rep;
	struct dentry                   *dentry;
	struct inode                    *dir;
	ino_t                            ino;
	int                              i;
	int                              rc;

	C2_ENTRY();

	dentry = f->f_path.dentry;
	dir    = dentry->d_inode;
	ci     = C2T1FS_I(dir);
	csb    = C2T1FS_SB(dir->i_sb);
	i      = f->f_pos;

        /**
           TODO: Server side should be able to position by integer pos
           (usually taken from f->f_pos). Hash by name can be used if
           needed but this requires some big changes in mdservice code.
         */
        C2_SET0(&mo);
        mo.mo_pos = ".";
        mo.mo_poslen = 1;
        mo.mo_attr.ca_tfid = ci->ci_fid;

        c2t1fs_fs_lock(csb);

        do {
                /**
                   Return codes are the following:
                   - <0 - some error om server side while handling fop;
                   -  0 - EOF of dir signaled by mdservice;
                   - >0 - some number of entries is sent by mdservice.
                 */
                rc = c2t1fs_mds_cob_readdir(csb, &mo, &rep);
                if (rc < 0) {
                        C2_LOG(C2_ERROR,
                               "Failed to read dir from pos \"%*s\". Error %d",
                               mo.mo_poslen, mo.mo_pos, rc);
                        goto out;
                } else if (rc > 0) {
                        struct c2_dirent *ent;

                        for (ent = dirent_first(rep); ent != NULL; ent = dirent_next(ent)) {
                                int type;

                                if (ent->d_namelen == 1 && memcmp(ent->d_name, ".", 1) == 0) {
                                        ino = dir->i_ino;
                                        type = DT_DIR;
                                } else if (ent->d_namelen == 2 && memcmp(ent->d_name, "..", 2) == 0) {
                                        ino = parent_ino(dentry);
                                        type = DT_DIR;
                                } else {
                                        ino = i++;
                                        type = DT_REG;
                                }

                                C2_LOG(C2_DEBUG, "filled off %lu ino %lu name \"%*s\"", (unsigned long)f->f_pos,
                                       (unsigned long)ino, ent->d_namelen, (char *)ent->d_name);

                                /** TODO: Entry type is hardcoded to regular files. */
                                rc = filldir(buf, ent->d_name, ent->d_namelen, f->f_pos,
                                             ino, type);
                                if (rc < 0)
                                        goto out;
                                f->f_pos++;
                        }
                        mo.mo_pos = rep->r_end.s_buf;
                        mo.mo_poslen = rep->r_end.s_len;
                }
        } while (rc == 0);

out:
        c2t1fs_fs_unlock(csb);
        C2_LEAVE("rc: 0");
        return 0;
}

static int c2t1fs_unlink(struct inode *dir, struct dentry *dentry)
{
        struct c2_fop_lookup_rep        *lookup_rep;
        struct c2_fop_unlink_rep        *unlink_rep;
	struct c2t1fs_sb                *csb;
	struct inode                    *inode;
	struct c2t1fs_inode             *ci;
	struct c2t1fs_mdop               mo;
	int                              rc;

	C2_ENTRY();

	C2_LOG(C2_INFO, "Name: \"%s\"", dentry->d_name.name);

	inode = dentry->d_inode;
	csb   = C2T1FS_SB(inode->i_sb);
	ci    = C2T1FS_I(inode);
	C2_ASSERT(c2t1fs_inode_bob_check(ci));

	c2t1fs_fs_lock(csb);

        C2_SET0(&mo);
        mo.mo_attr.ca_pfid = c2t1fs_root_fid;
        mo.mo_attr.ca_tfid  = ci->ci_fid;
        mo.mo_attr.ca_name = (char *)dentry->d_name.name;
        mo.mo_attr.ca_namelen = dentry->d_name.len;

	rc = c2t1fs_mds_cob_lookup(csb, &mo, &lookup_rep);
	if (rc != 0)
	        goto out;
	
	rc = c2t1fs_mds_cob_unlink(csb, &mo, &unlink_rep);
	if (rc != 0) {
		C2_LOG(C2_ERROR, "mdservive unlink fop failed: %d\n", rc);
		goto out;
	}
	
	rc = c2t1fs_component_objects_op(ci, c2t1fs_ios_cob_delete);
	if (rc != 0) {
		C2_LOG(C2_ERROR, "ioservice delete fop failed: %d", rc);
		goto out;
	}

	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	inode->i_ctime = dir->i_ctime;
	
	/** XXX: We may need to set nlinks from unlink fop reply. */
	inode_dec_link_count(inode);

out:
	c2t1fs_fs_unlock(csb);
	C2_LEAVE("rc: %d", rc);
	return rc;
}

/**
   See "Containers and component objects" section in c2t1fs.h for
   more information.
 */
struct c2_fid c2t1fs_ios_cob_fid(const struct c2t1fs_inode *ci, int index)
{
	struct c2_layout_enum *le;
	struct c2_fid          fid;

	C2_PRE(ci->ci_fid.f_container == 0);
	C2_PRE(ci->ci_layout_instance != NULL);
	C2_PRE(index >= 0);

	le = c2_layout_instance_to_enum(ci->ci_layout_instance);

	c2_layout_enum_get(le, index, &ci->ci_fid, &fid);

	C2_LEAVE("fid: [%lu:%lu]", (unsigned long)fid.f_container,
				   (unsigned long)fid.f_key);
	return fid;
}

static int c2t1fs_component_objects_op(struct c2t1fs_inode *ci,
				       int (*func)(struct c2t1fs_sb *csb,
			               const struct c2_fid *cfid,
				       const struct c2_fid *gfid))
{
	struct c2t1fs_sb *csb;
	struct c2_fid     cob_fid;
	int               pool_width;
	int               i;
	int               rc;

	C2_PRE(ci != NULL);
	C2_PRE(func != NULL);

	C2_ENTRY();

	C2_LOG(C2_DEBUG, "Component object %s for [%lu:%lu]",
		func == c2t1fs_ios_cob_create? "create" : "delete",
		(unsigned long)ci->ci_fid.f_container,
		(unsigned long)ci->ci_fid.f_key);

	csb = C2T1FS_SB(ci->ci_inode.i_sb);
	pool_width = csb->csb_pool_width;
	C2_ASSERT(pool_width >= 1);

	for (i = 0; i < pool_width; ++i) {
		cob_fid = c2t1fs_ios_cob_fid(ci, i);
		rc      = func(csb, &cob_fid, &ci->ci_fid);
		if (rc != 0) {
			C2_LOG(C2_ERROR, "Failed: cob %s : [%lu:%lu]",
				func == c2t1fs_ios_cob_create ? "create" : "delete",
				(unsigned long)cob_fid.f_container,
				(unsigned long)cob_fid.f_key);
			goto out;
		}
	}
out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int c2t1fs_mds_cob_fop_populate(struct c2t1fs_mdop     *mo,
                                       struct c2_fop          *fop)
{
        struct c2_fop_create    *create;
        struct c2_fop_unlink    *unlink;
        struct c2_fop_lookup    *lookup;
        struct c2_fop_getattr   *getattr;
        struct c2_fop_readdir   *readdir;
        struct c2_fop_cob       *req;
        int                      rc = 0;

        switch (c2_fop_opcode(fop)) {
        case C2_MDSERVICE_CREATE_OPCODE:
                create = c2_fop_data(fop);
                req = &create->c_body;

                body_mem2wire(req, &mo->mo_attr,
                              (C2_COB_UID | C2_COB_GID | C2_COB_ATIME |
                               C2_COB_CTIME | C2_COB_MTIME | C2_COB_MODE |
                               C2_COB_BLOCKS | C2_COB_SIZE));

                /**
                   Both fids are known before the rpc as fids are allocated
                   on client. Initialize them both.
                 */
                fid_mem2wire(&req->b_tfid, &mo->mo_attr.ca_tfid);
                fid_mem2wire(&req->b_pfid, &mo->mo_attr.ca_pfid);

                name_mem2wire(&create->c_name, mo->mo_attr.ca_name,
                              mo->mo_attr.ca_namelen);
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                unlink = c2_fop_data(fop);
                req = &unlink->u_body;

                /** We may need both fids here */
                fid_mem2wire(&req->b_tfid, &mo->mo_attr.ca_tfid);
                fid_mem2wire(&req->b_pfid, &mo->mo_attr.ca_pfid);

                name_mem2wire(&create->c_name, mo->mo_attr.ca_name,
                              mo->mo_attr.ca_namelen);
                break;
        case C2_MDSERVICE_LOOKUP_OPCODE:
                lookup = c2_fop_data(fop);
                req = &lookup->l_body;

                /** Only parent and name are known */
                fid_mem2wire(&req->b_pfid, &mo->mo_attr.ca_pfid);
                name_mem2wire(&create->c_name, mo->mo_attr.ca_name,
                              mo->mo_attr.ca_namelen);
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                getattr = c2_fop_data(fop);
                req = &getattr->g_body;

                /** Both fids are set from @tfid, no name. */
                fid_mem2wire(&req->b_tfid, &mo->mo_attr.ca_tfid);
                fid_mem2wire(&req->b_pfid, &mo->mo_attr.ca_tfid);
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                readdir = c2_fop_data(fop);
                req = &readdir->r_body;

                /** Both fids are set from @tfid, no name. */
                fid_mem2wire(&req->b_tfid, &mo->mo_attr.ca_tfid);
                name_mem2wire(&readdir->r_pos, mo->mo_pos, mo->mo_poslen);
                break;
        default:
                rc = -ENOTSUPP;
                break;
        }

        return rc;
}

static int c2t1fs_mds_cob_op(struct c2t1fs_sb      *csb,
                             struct c2t1fs_mdop    *mo,
                             struct c2_fop_type    *ftype,
                             void                 **rep)
{
        int                          rc;
        struct c2_fop               *fop;
        struct c2_rpc_session       *session;
        struct c2_fop_create_rep    *create_rep;
        struct c2_fop_unlink_rep    *unlink_rep;
        struct c2_fop_rename_rep    *rename_rep;
        struct c2_fop_link_rep      *link_rep;
        struct c2_fop_setattr_rep   *setattr_rep;
        struct c2_fop_getattr_rep   *getattr_rep;
        struct c2_fop_lookup_rep    *lookup_rep;
        struct c2_fop_open_rep      *open_rep;
        struct c2_fop_close_rep     *close_rep;
        struct c2_fop_readdir_rep   *readdir_rep;

	C2_PRE(mo != NULL);
	C2_PRE(ftype != NULL);

	C2_ENTRY();

        /**
           TODO: This needs to be fixed later.

           Container 0 and its session are currently reserved for mdservice.
           We hardcoding its using here temporary because of the following:
           - we can't use @mo->mo_attr.ca_pfid because it is set to root
             fid, which has container != 0 and we cannot change it as it
             will conflict with cob io objects in case they share the same
             db;
           - we can't use @mo->mo_attr.ca_tfid is not always set, some ops
             do not require it;
           - using @ci is not an option as well as it is not always used.
             For example, it is not available for lookup.
         */
	session = c2t1fs_container_id_to_session(csb, 0);
	C2_ASSERT(session != NULL);

	fop = c2_fop_alloc(ftype, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		C2_LOG(C2_ERROR, "Memory allocation for struct"
				 " c2_fop_cob_create failed");
		goto out;
	}

	rc = c2t1fs_mds_cob_fop_populate(mo, fop);
	if (rc != 0) {
		c2_fop_free(fop);
		goto out;
	}

	C2_LOG(C2_DEBUG, "Send md operation %x to session %lu\n",
		c2_fop_opcode(fop), (unsigned long)session->s_session_id);

	rc = c2_rpc_client_call(fop, session, &c2_fop_default_item_ops,
				0 /* deadline */, C2T1FS_RPC_TIMEOUT);

	if (rc != 0)
		goto out;


        switch (c2_fop_opcode(fop)) {
        case C2_MDSERVICE_CREATE_OPCODE:
                create_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = create_rep->c_body.b_rc;
                *rep = create_rep;
                break;
        case C2_MDSERVICE_LOOKUP_OPCODE:
                lookup_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = lookup_rep->l_body.b_rc;
                *rep = lookup_rep;
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                link_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = link_rep->l_body.b_rc;
                *rep = link_rep;
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                unlink_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = unlink_rep->u_body.b_rc;
                *rep = unlink_rep;
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                rename_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = rename_rep->r_body.b_rc;
                *rep = rename_rep;
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                setattr_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = setattr_rep->s_body.b_rc;
                *rep = setattr_rep;
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                getattr_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = getattr_rep->g_body.b_rc;
                *rep = getattr_rep;
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                open_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = open_rep->o_body.b_rc;
                *rep = open_rep;
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                close_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = close_rep->c_body.b_rc;
                *rep = close_rep;
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                readdir_rep = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
                rc = readdir_rep->r_body.b_rc;
                *rep = readdir_rep;
                break;
        default:
                C2_LOG(C2_ERROR, "Unexpected fop opcode %x", c2_fop_opcode(fop));
                rc = -ENOTSUPP;
                goto out;
        }

        if (rc != 0) {
                C2_LOG(C2_ERROR, "Operation %x failed with rc %d",
                       c2_fop_opcode(fop), rc);
                goto out;
        }

        /*
         * Fop is deallocated by rpc layer using
         * cob_req_rpc_item_ops->rio_free() rpc item ops.
         */
out:
        C2_LEAVE("%d", rc);
        return rc;
}

int c2t1fs_mds_cob_create(struct c2t1fs_sb          *csb,
                          struct c2t1fs_mdop        *mo,
                          struct c2_fop_create_rep **rep)
{
	return c2t1fs_mds_cob_op(csb, mo, &c2_fop_create_fopt, (void **)rep);
}

int c2t1fs_mds_cob_unlink(struct c2t1fs_sb          *csb,
                          struct c2t1fs_mdop        *mo,
                          struct c2_fop_unlink_rep **rep)
{
	return c2t1fs_mds_cob_op(csb, mo, &c2_fop_unlink_fopt, (void **)rep);
}

int c2t1fs_mds_cob_lookup(struct c2t1fs_sb          *csb,
                          struct c2t1fs_mdop        *mo,
                          struct c2_fop_lookup_rep **rep)
{
	return c2t1fs_mds_cob_op(csb, mo, &c2_fop_lookup_fopt, (void **)rep);
}

int c2t1fs_mds_cob_getattr(struct c2t1fs_sb           *csb,
                           struct c2t1fs_mdop         *mo,
                           struct c2_fop_getattr_rep **rep)
{
	return c2t1fs_mds_cob_op(csb, mo, &c2_fop_getattr_fopt, (void **)rep);
}

int c2t1fs_mds_cob_readdir(struct c2t1fs_sb           *csb,
                           struct c2t1fs_mdop         *mo,
                           struct c2_fop_readdir_rep **rep)
{
	return c2t1fs_mds_cob_op(csb, mo, &c2_fop_readdir_fopt, (void **)rep);
}

static int c2t1fs_ios_cob_create(struct c2t1fs_sb    *csb,
			         const struct c2_fid *cob_fid,
			         const struct c2_fid *gob_fid)
{
	return c2t1fs_ios_cob_op(csb, cob_fid, gob_fid, &c2_fop_cob_create_fopt);
}

static int c2t1fs_ios_cob_delete(struct c2t1fs_sb *csb,
			         const struct c2_fid *cob_fid,
			         const struct c2_fid *gob_fid)
{
	return c2t1fs_ios_cob_op(csb, cob_fid, gob_fid, &c2_fop_cob_delete_fopt);
}

static int c2t1fs_ios_cob_op(struct c2t1fs_sb    *csb,
			     const struct c2_fid *cob_fid,
			     const struct c2_fid *gob_fid,
			     struct c2_fop_type  *ftype)
{
	int                         rc;
	bool                        cobcreate;
	struct c2_fop              *fop;
	struct c2_rpc_session      *session;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(csb != NULL);
	C2_PRE(cob_fid != NULL);
	C2_PRE(gob_fid != NULL);
	C2_PRE(ftype != NULL);

	C2_ENTRY();

	session = c2t1fs_container_id_to_session(csb, cob_fid->f_container);
	C2_ASSERT(session != NULL);

	fop = c2_fop_alloc(ftype, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		C2_LOG(C2_ERROR, "Memory allocation for struct"
				 " c2_fop_cob_create failed");
		goto out;
	}

	C2_ASSERT(c2_is_cob_create_delete_fop(fop));
	cobcreate = c2_is_cob_create_fop(fop);

	rc = c2t1fs_ios_cob_fop_populate(csb, fop, cob_fid, gob_fid);
	if (rc != 0) {
		c2_fop_free(fop);
		goto out;
	}

	C2_LOG(C2_DEBUG, "Send %s [%lu:%lu] to session %lu\n",
		cobcreate ? "cob_create" : "cob_delete",
		(unsigned long)cob_fid->f_container,
		(unsigned long)cob_fid->f_key,
		(unsigned long)session->s_session_id);

	rc = c2_rpc_client_call(fop, session, &cob_req_rpc_item_ops,
				0 /* deadline */, C2T1FS_RPC_TIMEOUT);

	if (rc != 0)
		goto out;

	/*
	 * The reply fop received event is a generic event which does not
	 * distinguish between types of rpc item/fop. Custom c2_rpc_item_ops
	 * vector can be used if type specific things need to be done for
	 * given fop type only.
	 */
	reply = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
	if (reply->cor_rc == C2_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
		struct c2_pool_version_numbers *cli;
		struct c2_pool_version_numbers *srv;
		struct c2_fv_event             *event;
		uint32_t                        i = 0;

		/* Retrieve the latest server version and updates and apply
		 * to the client's copy. When -EAGAIN is return, this system
		 * call will be restarted.
		 */
		rc = -EAGAIN;
		cli = &csb->csb_pool.po_mach->pm_state.pst_version;
		srv = (struct c2_pool_version_numbers *)&reply->cor_fv_version;
		*cli = *srv;
		while (i < reply->cor_fv_updates.fvu_count) {
			event = &reply->cor_fv_updates.fvu_events[i];
			c2_poolmach_state_transit(csb->csb_pool.po_mach,
						  (struct c2_pool_event*)event);
			i++;
		}
	} else
		rc = reply->cor_rc;

	/*
	 * Fop is deallocated by rpc layer using
	 * cob_req_rpc_item_ops->rio_free() rpc item ops.
	 */

out:
	C2_LEAVE("%d", rc);
	return rc;
}

static int c2t1fs_ios_cob_fop_populate(struct c2t1fs_sb    *csb,
				       struct c2_fop       *fop,
				       const struct c2_fid *cob_fid,
				       const struct c2_fid *gob_fid)
{
	struct c2_fop_cob_create       *cc;
	struct c2_fop_cob_common       *common;
	struct c2_pool_version_numbers *cli;
	struct c2_pool_version_numbers  curr;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(cob_fid != NULL);
	C2_PRE(gob_fid != NULL);

	C2_ENTRY();

	common = c2_cobfop_common_get(fop);
	C2_ASSERT(common != NULL);

	/* fill in the current client known version */
	c2_poolmach_current_version_get(csb->csb_pool.po_mach, &curr);
	cli = (struct c2_pool_version_numbers*)&common->c_version;
	*cli = curr;

	common->c_gobfid.f_seq = gob_fid->f_container;
	common->c_gobfid.f_oid = gob_fid->f_key;
	common->c_cobfid.f_seq = cob_fid->f_container;
	common->c_cobfid.f_oid = cob_fid->f_key;

	if (c2_is_cob_create_fop(fop)) {
		cc = c2_fop_data(fop);
		C2_ALLOC_ARR(cc->cc_cobname.cn_name, C2T1FS_COB_ID_STRLEN);
		if (cc->cc_cobname.cn_name == NULL) {
			C2_LOG(C2_ERROR, "Memory allocation failed for"
					 " cob_name.");
			C2_LEAVE("%d", -ENOMEM);
			return -ENOMEM;
		}

		snprintf((char*)cc->cc_cobname.cn_name, C2T1FS_COB_ID_STRLEN,
			 "%16lx:%16lx",
			 (unsigned long)cob_fid->f_container,
			 (unsigned long)cob_fid->f_key);

		/*
		 * 1 is added to string length so that standard string
		 * library calls which depend on NULL char at end of string
		 * don't fail.
		 */
		cc->cc_cobname.cn_count = strlen((char*)cc->cc_cobname.cn_name)
					  + 1;
	}

	C2_LEAVE("%d", 0);
	return 0;
}
