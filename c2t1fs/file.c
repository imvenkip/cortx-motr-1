#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"

struct file_operations c2t1fs_reg_file_operations = {
	NULL
};

struct inode_operations c2t1fs_reg_inode_operations = {
	NULL
};

