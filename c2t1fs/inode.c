#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"
#include "lib/misc.h"

static int c2t1fs_inode_test(struct inode *inode, void *opaque);
static int c2t1fs_inode_set(struct inode *inode, void *opaque);

static struct kmem_cache *c2t1fs_inode_cachep = NULL;

bool c2t1fs_inode_is_root(struct inode *inode)
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
	ci->ci_nr_dir_ents = 0;
	C2_SET0(&ci->ci_data);

	END(0);
}
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

void c2t1fs_destroy_inode(struct inode *inode)
{
	struct c2t1fs_inode *ci;
	START();

	ci = C2T1FS_I(inode);
	TRACE("fid [%lu:%lu]\n", (unsigned long)ci->ci_fid.f_container,
				 (unsigned long)ci->ci_fid.f_key);

	kmem_cache_free(c2t1fs_inode_cachep, ci);
	END(0);
}

struct inode *c2t1fs_root_iget(struct super_block *sb)
{
	struct inode *inode;

	START();

	inode = c2t1fs_iget(sb, (struct c2_fid *)&c2t1fs_root_fid);

	END(inode);
	return inode;
}

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
	int                  rc;

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
static unsigned long fid_hash(struct c2_fid *fid)
{
	START();
	END(fid->f_key);
	return fid->f_key;
}

struct inode *c2t1fs_iget(struct super_block *sb, struct c2_fid *fid)
{
	struct inode *inode;
	unsigned long hash;
	int           err;

	START();

	hash = fid_hash(fid);

	inode = iget5_locked(sb, hash, c2t1fs_inode_test, c2t1fs_inode_set,
				fid);
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
