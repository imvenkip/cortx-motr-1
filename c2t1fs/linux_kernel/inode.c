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
 *                  Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 10/14/2011
 */

#include <linux/slab.h>         /* kmem_cache */

#include "layout/pdclust.h"     /* c2_pdclust_build(), c2_pdl_to_layout(),
				 * c2_pdclust_instance_build()           */
#include "layout/linear_enum.h" /* c2_linear_enum_build()                */
#include "lib/misc.h"           /* C2_SET0()                             */
#include "lib/memory.h"         /* C2_ALLOC_PTR(), c2_free()             */
#include "c2t1fs/linux_kernel/c2t1fs.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"          /* C2_LOG and C2_ENTRY */

static int c2t1fs_inode_test(struct inode *inode, void *opaque);
static int c2t1fs_inode_set(struct inode *inode, void *opaque);

static struct kmem_cache *c2t1fs_inode_cachep = NULL;

C2_TL_DESCR_DEFINE(dir_ents, "Dir entries", , struct c2t1fs_dir_ent,
			de_link, de_magic, MAGIC_DIRENT, MAGIC_DIRENTHD);

C2_TL_DEFINE(dir_ents, , struct c2t1fs_dir_ent);

static const struct c2_bob_type c2t1fs_inode_bob = {
	.bt_name         = "c2t1fs_inode",
	.bt_magix_offset = offsetof(struct c2t1fs_inode, ci_magic),
	.bt_magix        = MAGIC_C2T1FS_INODE,
	.bt_check        = NULL
};

C2_BOB_DEFINE(, &c2t1fs_inode_bob, c2t1fs_inode);

bool c2t1fs_inode_is_root(const struct inode *inode)
{
	struct c2t1fs_inode *ci;

	ci = C2T1FS_I(inode);
	return c2_fid_eq(&ci->ci_fid, &c2t1fs_root_fid);
}

static void init_once(void *foo)
{
	struct c2t1fs_inode *ci = foo;

	C2_ENTRY();

	ci->ci_fid.f_container = 0;
	ci->ci_fid.f_key       = 0;

	inode_init_once(&ci->ci_inode);

	C2_LEAVE();
}

int c2t1fs_inode_cache_init(void)
{
	int rc = 0;

	C2_ENTRY();

	c2t1fs_inode_cachep = kmem_cache_create("c2t1fs_inode_cache",
					sizeof(struct c2t1fs_inode),
					0, SLAB_HWCACHE_ALIGN, init_once);
	if (c2t1fs_inode_cachep == NULL)
		rc = -ENOMEM;

	C2_LEAVE("rc: %d", rc);
	return rc;
}

void c2t1fs_inode_cache_fini(void)
{
	C2_ENTRY();

	if (c2t1fs_inode_cachep != NULL) {
		kmem_cache_destroy(c2t1fs_inode_cachep);
		c2t1fs_inode_cachep = NULL;
	}

	C2_LEAVE();
}

void c2t1fs_inode_init(struct c2t1fs_inode *ci)
{
	C2_ENTRY("ci: %p", ci);

	C2_SET0(&ci->ci_fid);
	ci->ci_layout_instance = NULL;

	dir_ents_tlist_init(&ci->ci_dir_ents);
	c2t1fs_inode_bob_init(ci);

	C2_LEAVE();
}

void c2t1fs_inode_fini(struct c2t1fs_inode *ci)
{
	C2_ENTRY("ci: %p, is_root %s, layout_instance %p",
		 ci, c2_bool_to_str(c2t1fs_inode_is_root(&ci->ci_inode)),
		 ci->ci_layout_instance);

	C2_PRE(c2t1fs_inode_bob_check(ci));
	C2_PRE(dir_ents_tlist_is_empty(&ci->ci_dir_ents));

	dir_ents_tlist_fini(&ci->ci_dir_ents);
	if (!c2t1fs_inode_is_root(&ci->ci_inode)) {
		C2_ASSERT(ci->ci_layout_instance != NULL);
		ci->ci_layout_instance->li_ops->lio_fini(
						ci->ci_layout_instance);
	}
	c2t1fs_inode_bob_fini(ci);
	C2_LEAVE();
}

/**
   Implementation of super_operations::alloc_inode() interface.
 */
struct inode *c2t1fs_alloc_inode(struct super_block *sb)
{
	struct c2t1fs_inode *ci;

	C2_ENTRY("sb: %p", sb);

	ci = kmem_cache_alloc(c2t1fs_inode_cachep, GFP_KERNEL);
	if (ci == NULL) {
		C2_LEAVE("inode: %p", NULL);
		return NULL;
	}

	c2t1fs_inode_init(ci);

	C2_LEAVE("inode: %p", &ci->ci_inode);
	return &ci->ci_inode;
}

/**
   Implementation of super_operations::destroy_inode() interface.
 */
void c2t1fs_destroy_inode(struct inode *inode)
{
	struct c2t1fs_inode *ci;

	C2_ENTRY("inode: %p", inode);

	ci = C2T1FS_I(inode);
	C2_LOG("fid [%lu:%lu]", (unsigned long)ci->ci_fid.f_container,
				(unsigned long)ci->ci_fid.f_key);

	c2t1fs_inode_fini(ci);
	kmem_cache_free(c2t1fs_inode_cachep, ci);

	C2_LEAVE();
}

struct inode *c2t1fs_root_iget(struct super_block *sb)
{
	struct inode *inode;

	C2_ENTRY("sb: %p", sb);

	/*
	 * Currently it is assumed, that fid of root of file-system is
	 * well-known. But this can change when configuration modules are
	 * implemented. And we might need to get fid of root directory from
	 * configuration module. c2t1fs_root_iget() hides these details from
	 * mount code path.
	 */
	inode = c2t1fs_iget(sb, &c2t1fs_root_fid);

	C2_LEAVE("root_inode: %p", inode);
	return inode;
}

/**
   In file-systems like c2t1fs or nfs, inode number is not enough to identify
   a file. For such file-systems structure and semantics of file identifier
   are file-system specific e.g. fid in case of c2t1fs, file handle for nfs.

   c2t1fs_inode_test() and c2t1fs_inode_set() are the implementation of
   interfaces that are used by generic vfs code, to compare identities of
   inodes, in a generic manner.
 */
static int c2t1fs_inode_test(struct inode *inode, void *opaque)
{
	struct c2t1fs_inode *ci;
	struct c2_fid       *fid = opaque;
	int                  rc;

	C2_ENTRY();

	ci = C2T1FS_I(inode);

	C2_LOG("inode(%p) [%lu:%lu] opaque [%lu:%lu]", inode,
				(unsigned long)ci->ci_fid.f_container,
				(unsigned long)ci->ci_fid.f_key,
				(unsigned long)fid->f_container,
				(unsigned long)fid->f_key);

	rc = c2_fid_eq(&ci->ci_fid, fid);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int c2t1fs_inode_set(struct inode *inode, void *opaque)
{
	struct c2t1fs_inode *ci;
	struct c2_fid       *fid = opaque;

	C2_ENTRY();
	C2_LOG("inode(%p) [%lu:%lu]", inode,
			(unsigned long)fid->f_container,
			(unsigned long)fid->f_key);

	ci           = C2T1FS_I(inode);
	ci->ci_fid   = *fid;
	inode->i_ino = fid->f_key;

	C2_LEAVE("rc: 0");
	return 0;
}

static int c2t1fs_inode_refresh(struct inode *inode)
{
	C2_ENTRY();

	/* XXX Make rpc call to fetch attributes of cob having fid == @fid */
	if (c2t1fs_inode_is_root(inode)) {
		inode->i_mode = S_IFDIR | 0755;
	} else {
		/*
		 * Flat file structure. root is the only directory. Rest are
		 * regular files
		 */
		inode->i_mode = S_IFREG | 0755;
	}

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_uid   = 0;
	inode->i_gid   = 0;
	inode->i_nlink = 1;

	C2_LEAVE("rc: 0");
	return 0;
}

static int c2t1fs_inode_read(struct inode *inode)
{
	int rc;

	C2_ENTRY();

	rc = c2t1fs_inode_refresh(inode);
	if (rc != 0)
		goto out;

	if (S_ISREG(inode->i_mode)) {

		inode->i_op   = &c2t1fs_reg_inode_operations;
		inode->i_fop  = &c2t1fs_reg_file_operations;

	} else if (S_ISDIR(inode->i_mode)) {

		inode->i_op   = &c2t1fs_dir_inode_operations;
		inode->i_fop  = &c2t1fs_dir_file_operations;
		inc_nlink(inode);

	} else {

		rc = -ENOSYS;

	}

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

/**
   XXX Temporary implementation of simple hash on fid
 */
static unsigned long fid_hash(const struct c2_fid *fid)
{
	C2_ENTRY();
	C2_LEAVE("hash: %lu", (unsigned long) fid->f_key);
	return fid->f_key;
}

struct inode *c2t1fs_iget(struct super_block *sb, const struct c2_fid *fid)
{
	struct inode *inode;
	unsigned long hash;
	int           err;

	C2_ENTRY();

	hash = fid_hash(fid);

	/*
	 * Search inode cache for an inode that has matching @fid.
	 * Use c2t1fs_inode_test() to compare fid_s. If not found, allocate a
	 * new inode. Set its fid to @fid using c2t1fs_inode_set(). Also
	 * set I_NEW flag in inode->i_state for newly allocated inode.
	 */
	inode = iget5_locked(sb, hash, c2t1fs_inode_test, c2t1fs_inode_set,
				(void *)fid);
	if (inode != NULL) {
		if ((inode->i_state & I_NEW) == 0) {
			/* Not a new inode. No need to read it again */
			C2_LEAVE("inode: %p", inode);
			return inode;
		}
		err = c2t1fs_inode_read(inode);
		if (err != 0)
			goto out_err;
		unlock_new_inode(inode);
	}
	C2_LEAVE("inode: %p", inode);
	return inode;

out_err:
	iget_failed(inode);
	C2_LEAVE("ERR: %p", ERR_PTR(-EIO));
	return ERR_PTR(-EIO);
}

int c2t1fs_inode_layout_init(struct c2t1fs_inode *ci,
			     struct c2_pool      *pool,
			     uint32_t             N,
			     uint32_t             K,
			     uint64_t             unit_size)
{
	uint64_t                      layout_id;
	struct c2_layout_linear_attr  lin_attr;
	struct c2_layout_linear_enum *le;
	struct c2_pdclust_attr        pl_attr;
	struct c2_pdclust_layout     *pl;
	struct c2_pdclust_instance   *pi;
	int                           rc;

	C2_ENTRY();
	C2_PRE(ci != NULL && pool != NULL && pool->po_width > 0);

	C2_LOG("fid[%lu:%lu]: N: %d K: %d P: %d",
			(unsigned long)ci->ci_fid.f_container,
			(unsigned long)ci->ci_fid.f_key,
			N, K, pool->po_width);

	/**
	 * @todo A dummy enumeration object is being created here.
	 * c2t1fs code is not making use of this enumeration object, at this
	 * point. It will be taken care of by the task c2t1fs.LayoutDB.
	 */
	lin_attr.lla_nr = pool->po_width;
	lin_attr.lla_A  = 100;
	lin_attr.lla_B  = 200;
	rc = c2_linear_enum_build(&c2t1fs_globals.g_layout_dom,
				  &lin_attr, &le);
	if (rc == 0) {
		layout_id = 0x4A494E4E49455349; /* "jinniesi" */
		pl_attr.pa_N         = N;
		pl_attr.pa_K         = K;
		pl_attr.pa_P         = pool->po_width;
		pl_attr.pa_unit_size = unit_size;
		c2_uint128_init(&pl_attr.pa_seed, "upjumpandpumpim,");
		rc = c2_pdclust_build(&c2t1fs_globals.g_layout_dom, layout_id,
				      &pl_attr, &le->lle_base, &pl);
		if (rc == 0)
			rc = c2_pdclust_instance_build(pl, &ci->ci_fid, &pi);
			if (rc == 0) {
				/*
				 * c2_pdclust_instance_build() has now
				 * acquired an additional reference on the
				 * layout object 'pl'.
				 */
				ci->ci_layout_instance = &pi->pi_base;
			}
		else
			le->lle_base.le_ops->leo_fini(&le->lle_base);
	}
	C2_LEAVE("rc: %d", rc);
	return rc;
}

