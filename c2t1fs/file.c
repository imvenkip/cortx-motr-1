#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/uio.h>
#include <linux/mm.h>

#include "lib/memory.h"
#include "c2t1fs/c2t1fs.h"
#include "layout/pdclust.h"

ssize_t c2t1fs_file_read(struct file *filp,
			 char __user *buf,
			 size_t       len,
			 loff_t      *ppos);

ssize_t c2t1fs_file_aio_read(struct kiocb       *iocb,
			     const struct iovec *iov,
			     unsigned long       nr_segs,
			     loff_t              pos);

ssize_t c2t1fs_file_aio_write(struct kiocb       *iocb,
			      const struct iovec *iov,
			      unsigned long       nr_segs,
			      loff_t              pos);

ssize_t c2t1fs_read_write(struct file *filp,
			  char        *buf,
			  size_t       count,
			  loff_t      *ppos,
			  int          rw);

ssize_t c2t1fs_internal_read_write(struct c2t1fs_inode *ci,
				   char                *buf,
				   size_t               count,
				   loff_t               pos,
				   int                  rw);

int c2t1fs_rpc_rw(struct c2_list *rw_desc_list, int rw);

struct file_operations c2t1fs_reg_file_operations = {
	.aio_read  = c2t1fs_file_aio_read,
	.aio_write = c2t1fs_file_aio_write,
	.read  = do_sync_read,
	.write = do_sync_write,
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

ssize_t c2t1fs_file_aio_write(struct kiocb       *iocb,
			      const struct iovec *iov,
			      unsigned long       nr_segs,
			      loff_t              pos)
{
	unsigned long i;
	ssize_t       result = 0;
	ssize_t       nr_bytes_read = 0;
	ssize_t       count = 0;

	START();

	TRACE("WRITE req: file %s pos %lu nr_segs %lu iov_len %lu\n",
			KIOCB_TO_FILE_NAME(iocb),
			(unsigned long)pos,
			nr_segs,
			iov_length(iov, nr_segs));

	if (nr_segs == 0)
		goto out;

	result = generic_segment_checks(iov, &nr_segs, &count, VERIFY_READ);
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
					   vec->iov_len, &iocb->ki_pos, WRITE);

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
bool address_is_page_aligned(unsigned long addr)
{
	START();

	TRACE("addr %lx mask %lx\n", addr, PAGE_SIZE - 1);
	return (addr & (PAGE_SIZE - 1)) == 0;
}

static bool io_req_spans_full_stripe(struct c2t1fs_inode *ci,
				     char                *buf,
				     size_t               count,
				     loff_t               pos)
{
	struct c2_pdclust_layout *pd_layout;
	uint64_t                  stripe_width;
	unsigned long             addr;
	bool                      result;

	START();

	addr = (unsigned long)buf;

	pd_layout = ci->ci_pd_layout;

	/* stripe width = number of data units * size of each unit */
	stripe_width = pd_layout->pl_N * ci->ci_unit_size;

	/*
	 * Requested IO size and position within file must be
	 * multiple of stripe width.
	 * Buffer address must be page aligned.
	 */
	TRACE("count = %lu\n", (unsigned long)count);
	TRACE("width %lu count %% width %lu pos %% width %lu\n",
			(unsigned long)stripe_width,
			(unsigned long)(count % stripe_width),
			(unsigned long)(pos % stripe_width));
	result = count % stripe_width == 0 &&
		 pos   % stripe_width == 0 &&
		 address_is_page_aligned(addr);

	END(result);
	return result;
}

int c2t1fs_pin_memory_area(char          *buf,
			   size_t         count,
			   int            rw,
			   struct page ***pinned_pages,
			   int           *nr_pinned_pages)
{
	struct page   **pages;
	unsigned long   addr;
	unsigned long   va;
	int             off;
	int             nr_pages;
	int             nr_pinned;
	int             i;
	int             rc = 0;

	START();

	addr = (unsigned long)buf;
	off  = (addr & (PAGE_SIZE - 1));
	addr &= PAGE_MASK;

	nr_pages = (off + count + PAGE_SIZE - 1) >> PAGE_SHIFT;

	C2_ALLOC_ARR(pages, nr_pages);
	if (pages == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	TRACE("addr %lu off %d nr_pages %d\n", addr, off, nr_pages);

	if (addr > PAGE_OFFSET) {
		/* addr points in kernel space */
		for (i = 0, va = addr; i < nr_pages; i++, va += PAGE_SIZE) {
			pages[i] = virt_to_page(pages[i]);
			get_page(pages[i]);
		}
		nr_pinned = nr_pages;
	} else {
		/* addr points in user space */
		down_read(&current->mm->mmap_sem);
		nr_pinned = get_user_pages(current, current->mm, addr, nr_pages,
				    rw == READ, 1, pages, NULL);
		up_read(&current->mm->mmap_sem);
	}

	if (nr_pinned != nr_pages) {
		TRACE("Failed: could pin only %d pages out of %d\n",
				rc, nr_pages);

		for (i = 0; i < nr_pinned; i++)
			put_page(pages[i]);

		c2_free(pages);
		rc = -EFAULT;
		goto out;
	}

	*pinned_pages    = pages;
	*nr_pinned_pages = nr_pinned;

	END(0);
	return 0;
out:
	*pinned_pages = NULL;
	*nr_pinned_pages = 0;

	END(rc);
	return rc;
}

ssize_t c2t1fs_read_write(struct file *file,
			  char        *buf,
			  size_t       count,
			  loff_t      *ppos,
			  int          rw)
{
	struct inode         *inode;
	struct c2t1fs_inode  *ci;
	struct page         **pinned_pages;
	int                   nr_pinned_pages;
	loff_t                pos = *ppos;
	int                   rc;
	int                   i;

	START();

	if (count == 0) {
		END(0);
		return 0;
	}

	inode = file->f_dentry->d_inode;
	ci = C2T1FS_I(inode);

	if (rw == READ) {
		if (pos > inode->i_size) {
			END(0);
			return 0;
		}

		/* check if io spans beyond file size */
		if (pos + count > inode->i_size)
			count = inode->i_size - pos;
	}

	if (rw == WRITE && (file->f_flags & O_APPEND) != 0)
		pos = inode->i_size;

	TRACE("%s %lu bytes at pos %lu to %p\n", rw == READ ? "Read" : "Write",
				(unsigned long)count,
				(unsigned long)pos, buf);

	if (!io_req_spans_full_stripe(ci, buf, count, pos)) {
		END(-EINVAL);
		return -EINVAL;
	}

	rc = c2t1fs_pin_memory_area(buf, count, rw, &pinned_pages,
						&nr_pinned_pages);
	if (rc != 0)
		goto out;

	rc = c2t1fs_internal_read_write(ci, buf, count, pos, rw);
	if (rc > 0) {
		pos += rc;
		if (rw == WRITE && pos > inode->i_size)
			inode->i_size = pos;
		*ppos = pos;
	}

	for (i = 0; i < nr_pinned_pages; i++)
		put_page(pinned_pages[i]);
out:
	END(rc);
	return rc;
}

struct c2t1fs_rw_desc
{
	struct c2_rpc_session *rd_session;
	struct c2_fid          rd_fid;
	loff_t                 rd_offset;
	size_t                 rd_count;
	struct c2_list         rd_buf_list;
	struct c2_list_link    rd_link;
};

struct c2t1fs_buf
{
	struct c2_buf             cb_buf;
	enum c2_pdclust_unit_type cb_type;
	struct c2_list_link       cb_link;
};

void c2t1fs_buf_init(struct c2t1fs_buf *buf, char *addr, size_t len,
			enum c2_pdclust_unit_type unit_type)
{
	START();

	TRACE("buf %p addr %p len %lu\n", buf, addr, (unsigned long)len);

	c2_buf_init(&buf->cb_buf, addr, len);
	c2_list_link_init(&buf->cb_link);
	buf->cb_type = unit_type;

	END(0);
}

void c2t1fs_buf_fini(struct c2t1fs_buf *buf)
{
	START();

	if (buf->cb_type == PUT_PARITY || buf->cb_type == PUT_SPARE)
		c2_free(buf->cb_buf.b_addr);

	c2_list_link_fini(&buf->cb_link);

	END(0);
}

struct c2t1fs_rw_desc * c2t1fs_rw_desc_get(struct c2_list *list,
					   struct c2_fid   fid)
{
	struct c2t1fs_rw_desc *rw_desc;

	START();
	TRACE("fid [%lu:%lu]\n", (unsigned long)fid.f_container,
				 (unsigned long)fid.f_key);

	c2_list_for_each_entry(list, rw_desc, struct c2t1fs_rw_desc, rd_link) {
		if (c2_fid_eq(&fid, &rw_desc->rd_fid))
			goto out;
	}

	C2_ALLOC_PTR(rw_desc);
	if (rw_desc == NULL)
		goto out;

	rw_desc->rd_fid     = fid;
	rw_desc->rd_offset  = C2_BSIGNED_MAX;
	rw_desc->rd_count   = 0;
	rw_desc->rd_session = NULL;

	c2_list_init(&rw_desc->rd_buf_list);
	c2_list_link_init(&rw_desc->rd_link);

	c2_list_add(list, &rw_desc->rd_link);
out:
	END(rw_desc);
	return rw_desc;
}

void c2t1fs_rw_desc_fini(struct c2t1fs_rw_desc *rw_desc)
{
	struct c2t1fs_buf   *buf;
	struct c2_list_link *link;

	START();

	while (!c2_list_is_empty(&rw_desc->rd_buf_list)) {

		link = c2_list_first(&rw_desc->rd_buf_list);
		C2_ASSERT(link != NULL);

		c2_list_del(link);

		buf = container_of(link, struct c2t1fs_buf, cb_link);
		c2t1fs_buf_fini(buf);
		c2_free(buf);
	}
	c2_list_fini(&rw_desc->rd_buf_list);
	c2_list_link_fini(&rw_desc->rd_link);

	END(0);
}

int c2t1fs_rw_desc_buf_add(struct c2t1fs_rw_desc *rw_desc,
			    char                  *addr,
			    size_t                 len,
			    enum c2_pdclust_unit_type type)
{
	struct c2t1fs_buf *buf;

	START();

	C2_ALLOC_PTR(buf);
	if (buf == NULL) {
		END(-ENOMEM);
		return -ENOMEM;
	}
	c2t1fs_buf_init(buf, addr, len, type);

	c2_list_add_tail(&rw_desc->rd_buf_list, &buf->cb_link);

	END(0);
	return 0;
}

ssize_t c2t1fs_internal_read_write(struct c2t1fs_inode *ci,
				   char                *buf,
				   size_t               count,
				   loff_t               gob_pos,
				   int                  rw)
{
	enum c2_pdclust_unit_type  unit_type;
	struct c2_pdclust_src_addr src_addr;
	struct c2_pdclust_tgt_addr tgt_addr;
	struct c2_pdclust_layout  *pd_layout;
	struct c2t1fs_rw_desc     *rw_desc;
	struct c2t1fs_sb          *csb;
	struct c2_list             rw_desc_list;
	struct c2_fid              gob_fid;
	struct c2_fid              tgt_fid;
	struct c2_buf             *data_bufs;
	struct c2_buf             *parity_bufs;
	loff_t                     pos;
	size_t                     offset_in_buf;
	uint64_t                   unit_size;
	uint64_t                   nr_data_bytes_per_group;
	char                      *ptr;
	int                        nr_groups_to_rw;
	int                        nr_units_per_group;
	int                        nr_data_units;
	int                        nr_parity_units;
	int                        parity_index;
	int                        unit;
	int                        i;
	int                        rc;

	START();

	csb = C2T1FS_SB(ci->ci_inode.i_sb);

	pd_layout = ci->ci_pd_layout;
	gob_fid   = ci->ci_fid;
	unit_size = ci->ci_unit_size;

	nr_data_units           = pd_layout->pl_N;
	nr_parity_units         = pd_layout->pl_K;
	nr_units_per_group      = nr_data_units + 2 * nr_parity_units;
	nr_data_bytes_per_group = nr_data_units * unit_size;
	/* only full stripe read write */
	nr_groups_to_rw         = count / nr_data_bytes_per_group;

	C2_ALLOC_ARR(data_bufs, nr_data_units);
	C2_ALLOC_ARR(parity_bufs, nr_parity_units);

	c2_list_init(&rw_desc_list);

	src_addr.sa_group = gob_pos / nr_data_bytes_per_group;
	offset_in_buf = 0;

	for (i = 0; i < nr_groups_to_rw; i++, src_addr.sa_group++) {
		for (unit = 0; unit < nr_units_per_group; unit++) {
			src_addr.sa_unit = unit;

			c2_pdclust_layout_map(pd_layout, &src_addr, &tgt_addr);

			TRACE("src [%lu:%lu] tgt [0:%lu]\n",
				(unsigned long)src_addr.sa_group,
				(unsigned long)src_addr.sa_unit,
				(unsigned long)tgt_addr.ta_obj);

			pos = tgt_addr.ta_frame * unit_size;

			tgt_fid = c2t1fs_target_fid(gob_fid,
						    tgt_addr.ta_obj + 1);

			rw_desc = c2t1fs_rw_desc_get(&rw_desc_list, tgt_fid);
			if (rw_desc == NULL) {
				rc = -ENOMEM;
				goto cleanup;
			}
			rw_desc->rd_offset = min_check(rw_desc->rd_offset,
							pos);
			rw_desc->rd_count += unit_size;
			rw_desc->rd_session = c2t1fs_container_id_to_session(
						     csb, tgt_fid.f_container);

			unit_type = c2_pdclust_unit_classify(pd_layout, unit);

			switch (unit_type) {
			case PUT_DATA:
				rc = c2t1fs_rw_desc_buf_add(rw_desc,
							   buf + offset_in_buf,
							   unit_size,
							   PUT_DATA);
				if (rc != 0)
					goto cleanup;

				c2_buf_init(&data_bufs[unit],
					    buf + offset_in_buf,
					    unit_size);

				offset_in_buf += unit_size;
				break;

			case PUT_PARITY:
				parity_index = unit - nr_data_units;
				//ptr = c2_alloc_aligned(unit_size, PAGE_SHIFT);
				ptr = c2_alloc(unit_size);
				rc = c2t1fs_rw_desc_buf_add(rw_desc,
						ptr, unit_size, PUT_PARITY);
				if (rc != 0)
					goto cleanup;

				c2_buf_init(&parity_bufs[parity_index], ptr,
						unit_size);
				if (parity_index == nr_parity_units - 1 &&
						rw == WRITE) {
					TRACE("Computing parity of group %d\n",
								i);
					c2_parity_math_calculate(
							&pd_layout->pl_math,
							data_bufs,
							parity_bufs);
				}
				break;

			case PUT_SPARE:
				//ptr = c2_alloc_aligned(unit_size, PAGE_SHIFT);
				ptr = c2_alloc(unit_size);
				rc = c2t1fs_rw_desc_buf_add(rw_desc,
						     ptr, unit_size, PUT_SPARE);
				if (rc != 0)
					goto cleanup;
				break;
			default:
				C2_ASSERT(0);
			}
		}
	}

	rc = c2t1fs_rpc_rw(&rw_desc_list, rw);

cleanup:
	while (!c2_list_is_empty(&rw_desc_list)) {
		struct c2_list_link *link;

		link = c2_list_first(&rw_desc_list);

		c2_list_del(link);

		rw_desc = container_of(link, struct c2t1fs_rw_desc, rd_link);
		c2t1fs_rw_desc_fini(rw_desc);
		c2_free(rw_desc);
	}
	c2_list_fini(&rw_desc_list);
	END(rc);
	return rc;
}

int c2t1fs_rpc_rw(struct c2_list *rw_desc_list, int rw)
{
	struct c2t1fs_rw_desc *rw_desc;
	struct c2t1fs_buf     *buf;
	int                    count = 0;

	START();

	TRACE("Operation: %s\n", rw == READ ? "READ" : "WRITE");

	if (c2_list_is_empty(rw_desc_list))
		TRACE("rw_desc_list is empty\n");

	c2_list_for_each_entry(rw_desc_list, rw_desc,
			struct c2t1fs_rw_desc, rd_link) {
		TRACE("fid: [%lu:%lu] offset: %lu count: %lu\n",
			(unsigned long)rw_desc->rd_fid.f_container,
			(unsigned long)rw_desc->rd_fid.f_key,
			(unsigned long)rw_desc->rd_offset,
			(unsigned long)rw_desc->rd_count);

		TRACE("Buf list\n");
		c2_list_for_each_entry(&rw_desc->rd_buf_list, buf,
					struct c2t1fs_buf, cb_link) {
			TRACE("addr %p len %lu type %s\n",
				buf->cb_buf.b_addr,
				(unsigned long)buf->cb_buf.b_nob,
				(buf->cb_type == PUT_DATA) ? "DATA" :
				 (buf->cb_type == PUT_PARITY) ? "PARITY" :
				 (buf->cb_type == PUT_SPARE) ? "SPARE" :
					"UNKNOWN");

			if (buf->cb_type == PUT_DATA)
				count += buf->cb_buf.b_nob;
		}
	}
	END(count);
	return count;
}
