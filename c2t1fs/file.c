#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/uio.h>
#include "c2t1fs/c2t1fs.h"

ssize_t c2t1fs_file_read(struct file *filp,
			 char __user *buf,
			 size_t       len,
			 loff_t      *ppos);

ssize_t c2t1fs_read_write(struct file *filp,
			  char        *buf,
			  size_t       count,
			  loff_t      *ppos,
			  int          rw);

struct file_operations c2t1fs_reg_file_operations = {
	.read = c2t1fs_file_read
};

struct inode_operations c2t1fs_reg_inode_operations = {
	NULL
};

ssize_t c2t1fs_file_read(struct file *filp,
			 char __user *buf,
			 size_t       len,
			 loff_t      *ppos)
{
	char    out_str[20];
	ssize_t n;

	/* XXX temporary routine */

	START();
	TRACE("read: %lu bytes\n", (unsigned long)len);

	if (*ppos == 0) {
		sprintf(out_str, "Hello world\n");
		n = strlen(out_str);
		if (copy_to_user(buf, out_str, n)) {
			TRACE("Seg Fault\n");
			END(-EFAULT);
			return -EFAULT;
		}
		*ppos = 1;
	} else {
		END(0);
		return 0;
	}

	END(n);
	return n;
}

#define KIOCB_TO_FILE_NAME(iocb) ((iocb)->ki_filp->f_path.dentry->d_name.name)

ssize_t c2t1fs_file_aio_read(struct kiocb       *iocb,
			     const struct iovec *iov,
			     unsigned long       nr_segs,
			     loff_t              pos)
{
	unsigned long i;
	ssize_t       result = 0;
	ssize_t       nr_bytes_read = 0;
	ssize_t       count = 0;

	START();

	TRACE("Read req: file %s pos %lu nr_segs %lu iov_len %lu\n",
			KIOCB_TO_FILE_NAME(iocb),
			(unsigned long)pos,
			nr_segs,
			iov_length(iov, nr_segs));

	if (nr_segs == 0)
		goto out;

	result = generic_segment_checks(iov, &nr_segs, &count, VERIFY_WRITE);
	if (result != 0) {
		TRACE("Generic segment checks failed: %lu\n",
						(unsigned long)result);
		goto out;
	}

	if (count == 0)
		goto out;

	for (i = 0; i < nr_segs; i++) {
		const struct iovec *vec = &iov[i];

		TRACE("iov: base %p len %lu pos %lu\n", vec->iov_base,
					(unsigned long)vec->iov_len,
					(unsigned long)iocb->ki_pos);

		result = c2t1fs_read_write(iocb->ki_filp, vec->iov_base,
					   vec->iov_len, &iocb->ki_pos, READ);

		TRACE("result: %ld\n", (long)result);
		if (result <= 0)
			break;

		if ((size_t)result < vec->iov_len)
			break;

		nr_bytes_read += result;
	}
out:
	END(nr_bytes_read ?: result);
	return nr_bytes_read ?: result;
}

ssize_t c2t1fs_read_write(struct file *filp,
			  char        *buf,
			  size_t       count,
			  loff_t      *ppos,
			  int          rw)
{
	return 0;
}
