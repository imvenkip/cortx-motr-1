#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "c2t1fs/c2t1fs.h"

struct file_operations c2t1fs_dir_operations = { NULL };
struct address_space_operations c2t1fs_dir_aops = { NULL };

