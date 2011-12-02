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

#include <linux/slab.h>      /* kmem_cache */

#include "layout/pdclust.h"  /* c2_pdclust_build(), c2_pdclust_fini() */
#include "pool/pool.h"       /* c2_pool_init(), c2_pool_fini()        */
#include "lib/misc.h"        /* C2_SET0()                             */
#include "lib/memory.h"      /* C2_ALLOC_PTR(), c2_free()             */
#include "c2t1fs/c2t1fs.h"

static int c2t1fs_inode_test(struct inode *inode, void *opaque);
static int c2t1fs_inode_set(struct inode *inode, void *opaque);

static struct kmem_cache *c2t1fs_inode_cachep = NULL;

bool c2t1fs_inode_is_root(const struct inode *inode)
{
	struct c2t1fs_inode *ci;

	ci = C2T1FS_I(inode);
	return c2_fid_eq(&ci->ci_fid, &c2t1fs_root_fid);
}

static void init_once(void *foo)
{
	struct c2t1fs_inode *ci = foo;

	START();

	ci->ci_fid.f_container = 0;
	ci->ci_fid.f_key = 0;

	inode_init_once(&ci->ci_inode);

	END(0);
}

int c2t1fs_inode_cache_init(void)
{
	int rc = 0;

	START();

	c2t1fs_inode_cachep = kmem_cache_create("c2t1fs_inode_cache",
					sizeof(struct c2t1fs_inode),
					0, SLAB_HWCACHE_ALIGN, init_once);
	if (c2t1fs_inode_cachep == NULL)
		rc = -ENOMEM;

	END(rc);
	return rc;
}

void c2t1fs_inode_cache_fini(void)
{
	START();

	if (c2t1fs_inode_cachep == NULL) {
		END(0);
		return;
	}

	kmem_cache_destroy(c2t1fs_inode_cachep);
	c2t1fs_inode_cachep = NULL;

	END(0);
}

void c2t1fs_inode_init(struct c2t1fs_inode *ci)
{
	START();

	C2_SET0(&ci->ci_fid);
	ci->ci_pd_layout = NULL;

	ci->ci_nr_dir_ents = 0;
	C2_SET0(&ci->ci_dir_ents);

	END(0);
}

void c2t1fs_inode_fini(struct c2t1fs_inode *ci)
{
	struct c2_pdclust_layout *pd_layout;

	START();

	pd_layout = ci->ci_pd_layout;
	if (pd_layout != NULL) {
		c2_pool_fini(pd_layout->pl_pool);
		c2_free(pd_layout->pl_pool);
		c2_pdclust_fini(pd_layout);
	}

	END(0);
}

/**
   Implementation of super_operations::alloc_inode() interface.
 */
struct inode *c2t1fs_alloc_inode(struct super_block *sb)
{
	struct c2t1fs_inode *ci;

	START();

	ci = kmem_cache_alloc(c2t1fs_inode_cachep, GFP_KERNEL);
	if (ci == NULL) {
		END(NULL);
		return NULL;
	}

	c2t1fs_inode_init(ci);

	END(&ci->ci_inode);
	return &ci->ci_inode;
}

/**
   Implementation of super_operations::destroy_inode() interface.
 */
void c2t1fs_destroy_inode(struct inode *inode)
{
	struct c2t1fs_inode *ci;
	START();

	ci = C2T1FS_I(inode);
	TRACE("fid [%lu:%lu]\n", (unsigned long)ci->ci_fid.f_container,
				 (unsigned long)ci->ci_fid.f_key);

	c2t1fs_inode_fini(ci);
	kmem_cache_free(c2t1fs_inode_cachep, ci);

	END(0);
}

struct inode *c2t1fs_root_iget(struct super_block *sb)
{
	struct inode *inode;

	START();

	/*
	 * Currently it is assumed, that fid of root of file-system is
	 * well-known. But this can change when configuration modules are
	 * implemented. And we might need to get fid of root directory from
	 * configuration module. c2t1fs_root_iget() hides these details from
	 * mount code path.
	 */
	inode = c2t1fs_iget(sb, &c2t1fs_root_fid);

	END(inode);
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

	START();

	ci = C2T1FS_I(inode);

	TRACE("inode(%p) [%lu:%lu] opaque [%lu:%lu]\n", inode,
		(unsigned long)ci->ci_fid.f_container,
		(unsigned long)ci->ci_fid.f_key,
		(unsigned long)fid->f_container,
		(unsigned long)fid->f_key);

	rc = c2_fid_eq(&ci->ci_fid, fid);

	END(rc);
	return rc;
}

static int c2t1fs_inode_set(struct inode *inode, void *opaque)
{
	struct c2t1fs_inode *ci;
	struct c2_fid       *fid = opaque;

	START();
	TRACE("inode(%p) [%lu:%lu]\n", inode,
			(unsigned long)fid->f_container,
			(unsigned long)fid->f_key);

	ci = C2T1FS_I(inode);
	ci->ci_fid = *fid;

	END(0);
	return 0;
}

static int c2t1fs_inode_refresh(struct inode *inode)
{
	START();

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

	END(0);
	return 0;
}

static int c2t1fs_inode_read(struct inode *inode)
{
	int rc;

	START();

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
	END(rc);
	return rc;
}

/**
   XXX Temporary implementation of simple hash on fid
 */
static unsigned long fid_hash(const struct c2_fid *fid)
{
	START();
	END(fid->f_key);
	return fid->f_key;
}

struct inode *c2t1fs_iget(struct super_block *sb, const struct c2_fid *fid)
{
	struct inode *inode;
	unsigned long hash;
	int           err;

	START();

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
			END(inode);
			return inode;
		}
		err = c2t1fs_inode_read(inode);
		if (err != 0)
			goto out_err;
		unlock_new_inode(inode);
	}
	return inode;

out_err:
	iget_failed(inode);
	return ERR_PTR(-EIO);
}

int c2t1fs_inode_layout_init(struct c2t1fs_inode *ci, int N, int K, int P,
				uint64_t unit_size)
{
	struct c2_uint128  layout_id;
	struct c2_uint128  seed;
	struct c2_pool    *pool;
	int                rc;

	START();

	TRACE("fid[%lu:%lu]: N: %d K: %d P: %d\n",
			(unsigned long)ci->ci_fid.f_container,
			(unsigned long)ci->ci_fid.f_key,
			N, K, P);

	C2_ALLOC_PTR(pool);
	if (pool == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	c2_uint128_init(&layout_id, "jinniesisjillous");
	c2_uint128_init(&seed,      "upjumpandpumpim,");

	rc = c2_pool_init(pool, P);
	if (rc != 0)
		goto out_free;

	rc = c2_pdclust_build(pool, &layout_id, N, K, unit_size,
				&seed, &ci->ci_pd_layout);
	if (rc != 0)
		goto out_fini;

	ci->ci_unit_size = unit_size;

	END(0);
	return 0;

out_fini:
	c2_pool_fini(pool);
out_free:
	c2_free(pool);
out:
	C2_ASSERT(rc != 0);
	END(rc);
	return rc;
}

