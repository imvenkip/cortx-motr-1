#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/parser.h>

#include "c2t1fs/c2t1fs.h"
#include "lib/misc.h"

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent);

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts);
static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts);
static int  c2t1fs_mnt_opts_validate(struct c2t1fs_mnt_opts *mnt_opts);
static int  c2t1fs_mnt_opts_parse(char                   *options,
				  struct c2t1fs_mnt_opts *mnt_opts);

static struct super_operations c2t1fs_super_operations = {
	.alloc_inode   = c2t1fs_alloc_inode,
	.destroy_inode = c2t1fs_destroy_inode,
	.drop_inode    = generic_delete_inode
};

const struct c2_fid c2t1fs_root_fid = {
	.f_container = 0,
	.f_key = 2
};

int c2t1fs_get_sb(struct file_system_type *fstype,
			 int                      flags,
			 const char              *devname,
			 void                    *data,
			 struct vfsmount         *mnt)
{
	int rc;

	TRACE("flags: 0x%x, devname: %s, data: %s\n", flags, devname,
							(char *)data);

	rc = get_sb_nodev(fstype, flags, data, c2t1fs_fill_super, mnt);
	if (rc != 0) {
		END(rc);
		return rc;
	}

	/* Establish connections and sessions with all the services */

	END(rc);
	return rc;
}

static int c2t1fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct c2t1fs_sb *csb;
	struct inode     *root_inode;
	int               rc;

	START();

	csb = kmalloc(sizeof (*csb), GFP_KERNEL);
	if (csb == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2t1fs_sb_init(csb);
	if (rc != 0) {
		kfree(csb);
		csb = NULL;
		goto out;
	}

	rc = c2t1fs_mnt_opts_parse(data, &csb->csb_mnt_opts);
	if (rc != 0)
		goto out;

	sb->s_fs_info = csb;

	sb->s_blocksize      = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic          = C2T1FS_SUPER_MAGIC;
	sb->s_maxbytes       = MAX_LFS_FILESIZE;
	sb->s_op             = &c2t1fs_super_operations;

	/* XXX Talk to confd and fetch configuration */
	root_inode = c2t1fs_root_iget(sb);
	if (root_inode == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL) {
		iput(root_inode);
		rc = -ENOMEM;
		goto out;
	}
	return 0;

out:
	if (csb != NULL) {
		c2t1fs_sb_fini(csb);
		kfree(csb);
	}
	sb->s_fs_info = NULL;
	END(rc);
	return rc;
}

void c2t1fs_kill_sb(struct super_block *sb)
{
	struct c2t1fs_sb *sbi;

	START();

	sbi = C2T1FS_SB(sb);
	c2t1fs_sb_fini(sbi);
	kfree(sbi);
	kill_anon_super(sb);

	END(0);
}

int c2t1fs_sb_init(struct c2t1fs_sb *csb)
{
	START();

	c2_mutex_init(&csb->csb_mutex);
	csb->csb_flags = 0;
	c2t1fs_mnt_opts_init(&csb->csb_mnt_opts);

	END(0);
	return 0;
}
void c2t1fs_sb_fini(struct c2t1fs_sb *csb)
{
	START();

	c2_mutex_fini(&csb->csb_mutex);
	c2t1fs_mnt_opts_fini(&csb->csb_mnt_opts);

	END(0);
}

enum {
	C2T1FS_MNTOPT_MDS = 1,
	C2T1FS_MNTOPT_IOS,
	C2T1FS_MNTOPT_ERR,
};

static const match_table_t c2t1fs_mntopt_tokens = {
	{ C2T1FS_MNTOPT_MDS, "mds=%s" },
	{ C2T1FS_MNTOPT_IOS, "ios=%s" },
	{ C2T1FS_MNTOPT_ERR, NULL },
};

static void c2t1fs_mnt_opts_init(struct c2t1fs_mnt_opts *mntopts)
{
	START();

	C2_SET0(mntopts);

	END(0);
}

static void c2t1fs_mnt_opts_fini(struct c2t1fs_mnt_opts *mntopts)
{
	int i;

	START();

	for (i = 0; i < mntopts->mo_nr_ios_ep; i++) {
		C2_ASSERT(mntopts->mo_ios_ep[i] != NULL);
		kfree(mntopts->mo_ios_ep[i]);
	}
	for (i = 0; i < mntopts->mo_nr_mds_ep; i++) {
		C2_ASSERT(mntopts->mo_mds_ep[i] != NULL);
		kfree(mntopts->mo_mds_ep[i]);
	}
	if (mntopts->mo_options != NULL)
		kfree(mntopts->mo_options);

	END(0);
}

static int c2t1fs_mnt_opts_validate(struct c2t1fs_mnt_opts *mnt_opts)
{
	START();
	END(0);
	return 0;
}

static int c2t1fs_mnt_opts_parse(char                   *options,
				 struct c2t1fs_mnt_opts *mnt_opts)
{
	substring_t  args[MAX_OPT_ARGS];
	char        *value;
	char        *op;
	int          token;
	int          rc = 0;

	START();

	TRACE("options: %p\n", options);

	if (options == NULL) {
		rc = -EINVAL;
		goto out;
	}

	mnt_opts->mo_options = kstrdup(options, GFP_KERNEL);
	if (mnt_opts->mo_options == NULL)
		rc = -ENOMEM;

	while ((op = strsep(&options, ",")) != NULL) {
		TRACE("Processing \"%s\"\n", op);
		if (*op == '\0')
			continue;

		token = match_token(op, c2t1fs_mntopt_tokens, args);
		switch (token) {

		case C2T1FS_MNTOPT_IOS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			TRACE("ioservice: %s\n", value);
			mnt_opts->mo_ios_ep[mnt_opts->mo_nr_ios_ep++] = value;
			break;

		case C2T1FS_MNTOPT_MDS:
			value = match_strdup(args);
			if (value == NULL) {
				rc = -ENOMEM;
				goto out;
			}
			TRACE("mdservice: %s\n", value);
			mnt_opts->mo_mds_ep[mnt_opts->mo_nr_mds_ep++] = value;
			break;

		default:
			TRACE("Unrecognized options: %s\n", op);
			rc = -EINVAL;
			goto out;
		}
	}
	rc = c2t1fs_mnt_opts_validate(mnt_opts);

out:
	if (rc != 0)
		c2t1fs_mnt_opts_fini(mnt_opts);

	END(rc);
	return rc;
}
