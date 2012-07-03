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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 *                  Anatoliy Bilenko
 * Original creation date: 05/04/2010
 */

#include <asm/uaccess.h>    /* VERIFY_READ, VERIFY_WRITE */
#include <linux/mm.h>       /* get_user_pages(), get_page(), put_page() */

#include "lib/memory.h"     /* c2_alloc(), c2_free() */
#include "lib/arith.h"      /* min_type() */
#include "layout/pdclust.h" /* PUT_* */
#include "c2t1fs/linux_kernel/c2t1fs.h"
#include "rpc/rpclib.h"     /* c2_rpc_client_call() */
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"      /* C2_LOG and C2_ENTRY */
#include "ioservice/io_fops.h"
#include "ioservice/io_fops_k.h"

/* Imports */
struct c2_net_domain;

static ssize_t c2t1fs_file_aio_read(struct kiocb       *iocb,
				    const struct iovec *iov,
				    unsigned long       nr_segs,
				    loff_t              pos);

static ssize_t c2t1fs_file_aio_write(struct kiocb       *iocb,
				     const struct iovec *iov,
				     unsigned long       nr_segs,
				     loff_t              pos);

static ssize_t c2t1fs_read_write(struct file *filp,
				 char        *buf,
				 size_t       count,
				 loff_t      *ppos,
				 int          rw);

static ssize_t c2t1fs_internal_read_write(struct c2t1fs_inode *ci,
					  char                *buf,
					  size_t               count,
					  loff_t               pos,
					  int                  rw);

static ssize_t c2t1fs_rpc_rw(const struct c2_tl *rw_desc_list, int rw);

static struct  c2_pdclust_layout * layout_to_pd_layout(struct c2_layout *l)
{
	return container_of(l, struct c2_pdclust_layout, pl_layout);
}

const struct file_operations c2t1fs_reg_file_operations = {
	.llseek    = generic_file_llseek,   /* provided by linux kernel */
	.aio_read  = c2t1fs_file_aio_read,
	.aio_write = c2t1fs_file_aio_write,
	.read      = do_sync_read,          /* provided by linux kernel */
	.write     = do_sync_write,         /* provided by linux kernel */
};

const struct inode_operations c2t1fs_reg_inode_operations;

#define KIOCB_TO_FILE_NAME(iocb) ((iocb)->ki_filp->f_path.dentry->d_name.name)

static ssize_t c2t1fs_file_aio_read(struct kiocb       *iocb,
				    const struct iovec *iov,
				    unsigned long       nr_segs,
				    loff_t              pos)
{
	unsigned long i;
	ssize_t       result = 0;
	ssize_t       nr_bytes_read = 0;
	ssize_t       count;

	C2_ENTRY();

	C2_LOG("Read req: file \"%s\" pos %lu nr_segs %lu iov_len %lu",
					KIOCB_TO_FILE_NAME(iocb),
					(unsigned long)pos,
					nr_segs,
					iov_length(iov, nr_segs));

	if (nr_segs == 0)
		goto out;

	result = generic_segment_checks(iov, &nr_segs, &count, VERIFY_WRITE);
	if (result != 0) {
		C2_LOG("Generic segment checks failed: %lu",
						(unsigned long)result);
		goto out;
	}

	if (count == 0)
		goto out;

	for (i = 0; i < nr_segs; i++) {
		const struct iovec *vec = &iov[i];

		C2_LOG("iov: base %p len %lu pos %lu", vec->iov_base,
					(unsigned long)vec->iov_len,
					(unsigned long)iocb->ki_pos);

		result = c2t1fs_read_write(iocb->ki_filp, vec->iov_base,
					   vec->iov_len, &iocb->ki_pos, READ);

		C2_LOG("result: %ld", (long)result);
		if (result <= 0)
			break;

		nr_bytes_read += result;

		if ((size_t)result < vec->iov_len)
			break;
	}
out:
	C2_LEAVE("bytes_read: %ld", nr_bytes_read ?: result);
	return nr_bytes_read ?: result;
}

static ssize_t c2t1fs_file_aio_write(struct kiocb       *iocb,
				     const struct iovec *iov,
				     unsigned long       nr_segs,
				     loff_t              pos)
{
	unsigned long i;
	ssize_t       result = 0;
	ssize_t       nr_bytes_written = 0;
	size_t        count = 0;
	size_t        saved_count;

	C2_ENTRY();

	C2_LOG("WRITE req: file %s pos %lu nr_segs %lu iov_len %lu",
			KIOCB_TO_FILE_NAME(iocb),
			(unsigned long)pos,
			nr_segs,
			iov_length(iov, nr_segs));

	result = generic_segment_checks(iov, &nr_segs, &count, VERIFY_READ);
	if (result != 0) {
		C2_LOG("Generic segment checks failed: %lu",
						(unsigned long)result);
		goto out;
	}

	saved_count = count;
	result = generic_write_checks(iocb->ki_filp, &pos, &count, 0);
	if (result != 0) {
		C2_LOG("generic_write_checks() failed %lu",
						(unsigned long)result);
		goto out;
	}

	if (count == 0)
		goto out;

	if (count != saved_count) {
		nr_segs = iov_shorten((struct iovec *)iov, nr_segs, count);
		C2_LOG("write size changed to %lu", (unsigned long)count);
	}

	for (i = 0; i < nr_segs; i++) {
		const struct iovec *vec = &iov[i];

		C2_LOG("iov: base %p len %lu pos %lu", vec->iov_base,
					(unsigned long)vec->iov_len,
					(unsigned long)iocb->ki_pos);

		result = c2t1fs_read_write(iocb->ki_filp, vec->iov_base,
					   vec->iov_len, &iocb->ki_pos, WRITE);

		C2_LOG("result: %ld", (long)result);
		if (result <= 0)
			break;

		nr_bytes_written += result;

		if ((size_t)result < vec->iov_len)
			break;
	}
out:
	C2_LEAVE("bytes_written: %ld", nr_bytes_written ?: result);
	return nr_bytes_written ?: result;
}

static bool address_is_page_aligned(unsigned long addr)
{
	C2_LOG("addr %lx mask %lx", addr, PAGE_CACHE_SIZE - 1);
	return (addr & (PAGE_CACHE_SIZE - 1)) == 0;
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

	C2_ENTRY();

	addr = (unsigned long)buf;

	pd_layout = layout_to_pd_layout(ci->ci_layout);

	/* stripe width = number of data units * size of each unit */
	stripe_width = pd_layout->pl_N * pd_layout->pl_unit_size;

	/*
	 * Requested IO size and position within file must be
	 * multiple of stripe width.
	 * Buffer address must be page aligned.
	 */
	C2_LOG("count = %lu", (unsigned long)count);
	C2_LOG("width %lu count %% width %lu pos %% width %lu",
			(unsigned long)stripe_width,
			(unsigned long)(count % stripe_width),
			(unsigned long)(pos % stripe_width));
	result = count % stripe_width == 0 &&
		 pos   % stripe_width == 0 &&
		 address_is_page_aligned(addr);

	C2_LEAVE("result: %d", result);
	return result;
}

static int c2t1fs_pin_memory_area(char          *buf,
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

	C2_ENTRY();

	addr = (unsigned long)buf & PAGE_CACHE_MASK;
	off  = (unsigned long)buf & (PAGE_CACHE_SIZE - 1);
	/* as we've already confirmed that buf is page aligned,
		should always be 0 */
	C2_PRE(off == 0);

	nr_pages = (off + count + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	C2_ALLOC_ARR(pages, nr_pages);
	if (pages == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	C2_LOG("addr 0x%lx off %d nr_pages %d", addr, off, nr_pages);

	if (current->mm != NULL && access_ok(rw == READ, addr, count)) {

		/* addr points in user space */
		down_read(&current->mm->mmap_sem);
		nr_pinned = get_user_pages(current, current->mm, addr, nr_pages,
				    rw == READ, 0, pages, NULL);
		up_read(&current->mm->mmap_sem);

	} else {

		/* addr points in kernel space */
		for (i = 0, va = addr; i < nr_pages; i++,
						     va += PAGE_CACHE_SIZE) {
			pages[i] = virt_to_page(va);
			get_page(pages[i]);
		}
		nr_pinned = nr_pages;

	}

	if (nr_pinned != nr_pages) {
		C2_LOG("Failed: could pin only %d pages out of %d",
				rc, nr_pages);

		for (i = 0; i < nr_pinned; i++)
			put_page(pages[i]);

		c2_free(pages);
		rc = -EFAULT;
		goto out;
	}
	for (i = 0; i < nr_pages; i++)
		C2_LOG("Pinned page[0x%p] buf [0x%p] count [%lu]",
				pages[i], buf, (unsigned long)count);

	*pinned_pages    = pages;
	*nr_pinned_pages = nr_pinned;

	C2_LEAVE("rc: 0");
	return 0;
out:
	*pinned_pages    = NULL;
	*nr_pinned_pages = 0;

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static ssize_t c2t1fs_read_write(struct file *file,
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
	ssize_t               rc;
	int                   i;

	C2_ENTRY();

	C2_PRE(count != 0);

	inode = file->f_dentry->d_inode;
	ci    = C2T1FS_I(inode);

	if (rw == READ) {
		if (pos > inode->i_size) {
			rc = 0;
			goto out;
		}

		/* check if io spans beyond file size */
		if (pos + count > inode->i_size) {
			count = inode->i_size - pos;
			C2_LOG("i_size %lu, read truncated to %lu",
					(unsigned long)inode->i_size,
					(unsigned long)count);
		}
	}

	C2_LOG("%s %lu bytes at pos %lu to %p",
				(char *)(rw == READ ? "Read" : "Write"),
				(unsigned long)count,
				(unsigned long)pos, buf);

	if (!io_req_spans_full_stripe(ci, buf, count, pos)) {
		rc = -EINVAL;
		goto out;
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
	C2_LEAVE("rc: %ld", rc);
	return rc;
}

enum {
	MAGIC_BUFLSTHD = 0x4255464c53544844, /* "BUFLSTHD" */
	MAGIC_C2T1BUF  = 0x43325431425546,   /* "C2T1BUF" */
	MAGIC_RW_DESC  = 0x52575f44455343,   /* "RW_DESC" */
	MAGIC_RWDLSTHD = 0x5257444c53544844, /* "RWDLSTHD" */
};

/**
   Read/write descriptor that describes io on cob identified by rd_fid.
 */
struct rw_desc {
	/** io fop should be sent on this session */
	struct c2_rpc_session *rd_session;

	/** fid of component object */
	struct c2_fid          rd_fid;

	/** offset within component object */
	loff_t                 rd_offset;

	/** number of bytes to [read from|write to] rd_offset */
	size_t                 rd_count;

	/** List of c2t1fs_buf objects hanging off cb_link */
	struct c2_tl           rd_buf_list;

	/** link within a local list created by c2t1fs_internal_read_write() */
	struct c2_tlink        rd_link;

	/** magic = MAGIC_RW_DESC */
	uint64_t               rd_magic;
};

C2_TL_DESCR_DEFINE(rwd, "rw descriptors", static, struct rw_desc, rd_link,
			rd_magic, MAGIC_RW_DESC, MAGIC_RWDLSTHD);

C2_TL_DEFINE(rwd, static, struct rw_desc);

struct c2t1fs_buf {
	/** <addr, len> giving memory area, target location for read operation,
	    and source for write operation */
	struct c2_buf             cb_buf;

	/** type of contents in the cb_buf data, parity or spare */
	enum c2_pdclust_unit_type cb_type;

	/** link in rw_desc::rd_buf_list */
	struct c2_tlink           cb_link;

	/** magic = MAGIC_C2T1BUF */
	uint64_t                  cb_magic;
};

C2_TL_DESCR_DEFINE(bufs, "buf list", static, struct c2t1fs_buf, cb_link,
			cb_magic, MAGIC_C2T1BUF, MAGIC_BUFLSTHD);

C2_TL_DEFINE(bufs, static, struct c2t1fs_buf);

static void c2t1fs_buf_init(struct c2t1fs_buf *buf, char *addr, size_t len,
			enum c2_pdclust_unit_type unit_type)
{
	C2_LOG("buf %p addr %p len %lu", buf, addr, (unsigned long)len);

	c2_buf_init(&buf->cb_buf, addr, len);
	bufs_tlink_init(buf);
	buf->cb_type = unit_type;
	buf->cb_magic = MAGIC_C2T1BUF;
}

static void c2t1fs_buf_fini(struct c2t1fs_buf *buf)
{
	if (buf->cb_type == PUT_PARITY || buf->cb_type == PUT_SPARE)
		c2_free(buf->cb_buf.b_addr);

	bufs_tlink_fini(buf);
	buf->cb_magic = 0;
}

static struct rw_desc * rw_desc_get(struct c2_tl        *list,
				    const struct c2_fid *fid)
{
	struct rw_desc *rw_desc;

	C2_ENTRY("fid [%lu:%lu]", (unsigned long)fid->f_container,
	                          (unsigned long)fid->f_key);

	c2_tl_for(rwd, list, rw_desc) {

		if (c2_fid_eq(fid, &rw_desc->rd_fid))
			goto out;

	} c2_tl_endfor;

	C2_ALLOC_PTR(rw_desc);
	if (rw_desc == NULL)
		goto out;

	rw_desc->rd_fid     = *fid;
	rw_desc->rd_offset  = C2_BSIGNED_MAX;
	rw_desc->rd_count   = 0;
	rw_desc->rd_session = NULL;
	rw_desc->rd_magic   = MAGIC_RW_DESC;

	bufs_tlist_init(&rw_desc->rd_buf_list);

	rwd_tlink_init_at_tail(rw_desc, list);
out:
	C2_LEAVE("rw_desc: %p", rw_desc);
	return rw_desc;
}

static void rw_desc_fini(struct rw_desc *rw_desc)
{
	struct c2t1fs_buf   *buf;

	C2_ENTRY();

	c2_tl_for(bufs, &rw_desc->rd_buf_list, buf) {

		bufs_tlist_del(buf);
		c2t1fs_buf_fini(buf);
		c2_free(buf);

	} c2_tl_endfor;
	bufs_tlist_fini(&rw_desc->rd_buf_list);

	rwd_tlink_fini(rw_desc);
	rw_desc->rd_magic = 0;

	C2_LEAVE();
}

static int rw_desc_add(struct rw_desc    *rw_desc,
		       char                     *addr,
		       size_t                    len,
		       enum c2_pdclust_unit_type type)
{
	struct c2t1fs_buf *buf;

	C2_ENTRY();

	C2_ALLOC_PTR(buf);
	if (buf == NULL) {
		C2_LEAVE("rc: %d", -ENOMEM);
		return -ENOMEM;
	}
	c2t1fs_buf_init(buf, addr, len, type);

	bufs_tlist_add_tail(&rw_desc->rd_buf_list, buf);

	C2_LEAVE("rc: 0");
	return 0;
}

static ssize_t c2t1fs_internal_read_write(struct c2t1fs_inode *ci,
					  char                *buf,
					  size_t               count,
					  loff_t               gob_pos,
					  int                  rw)
{
	enum   c2_pdclust_unit_type  unit_type;
	struct c2_pdclust_src_addr   src_addr;
	struct c2_pdclust_tgt_addr   tgt_addr;
	struct c2_pdclust_layout    *pd_layout;
	struct rw_desc              *rw_desc;
	struct c2t1fs_sb            *csb;
	struct c2_tl                 rw_desc_list;
	struct c2_fid                gob_fid;
	struct c2_fid                tgt_fid;
	struct c2_buf               *data_bufs;
	struct c2_buf               *parity_bufs;
	loff_t                       pos;
	size_t                       offset_in_buf;
	uint64_t                     unit_size;
	uint64_t                     nr_data_bytes_per_group;
	ssize_t                      rc = 0;
	char                        *ptr;
	uint32_t                     nr_groups_to_rw;
	uint32_t                     nr_units_per_group;
	uint32_t                     nr_data_units;
	uint32_t                     nr_parity_units;
	int                          parity_index;
	int                          unit;
	int                          i;

	C2_ENTRY();

	csb = C2T1FS_SB(ci->ci_inode.i_sb);

	pd_layout = layout_to_pd_layout(ci->ci_layout);
	gob_fid   = ci->ci_fid;
	unit_size = pd_layout->pl_unit_size;

	C2_LOG("Unit size: %lu", (unsigned long)unit_size);

	/* unit_size should be multiple of PAGE_CACHE_SIZE */
	C2_ASSERT((unit_size & (PAGE_CACHE_SIZE - 1)) == 0);

	nr_data_units           = pd_layout->pl_N;
	nr_parity_units         = pd_layout->pl_K;
	nr_units_per_group      = nr_data_units + 2 * nr_parity_units;
	nr_data_bytes_per_group = nr_data_units * unit_size;
	/* only full stripe read write */
	nr_groups_to_rw         = count / nr_data_bytes_per_group;

	C2_ALLOC_ARR(data_bufs, nr_data_units);
	C2_ALLOC_ARR(parity_bufs, nr_parity_units);

	rwd_tlist_init(&rw_desc_list);

	src_addr.sa_group = gob_pos / nr_data_bytes_per_group;
	offset_in_buf = 0;

	for (i = 0; i < nr_groups_to_rw; i++, src_addr.sa_group++) {

		for (unit = 0; unit < nr_units_per_group; unit++) {

			unit_type = c2_pdclust_unit_classify(pd_layout, unit);
			if (unit_type == PUT_SPARE) {
				/* No need to read/write spare units */
				C2_LOG("Skipped spare unit %d", unit);
				continue;
			}

			src_addr.sa_unit = unit;

			c2_pdclust_layout_map(pd_layout, &src_addr, &tgt_addr);

			C2_LOG("src [%lu:%lu] maps to tgt [0:%lu]",
					(unsigned long)src_addr.sa_group,
					(unsigned long)src_addr.sa_unit,
					(unsigned long)tgt_addr.ta_obj);

			pos = tgt_addr.ta_frame * unit_size;

			/*
			 * 1 must be added to tgt_addr.ta_obj, because ta_obj
			 * is in range [0, P - 1] inclusive. But our component
			 * objects are indexed in range [1, P] inclusive.
			 * For more info see "Containers and component objects"
			 * section in c2t1fs.h
			 */
			tgt_fid = c2t1fs_cob_fid(&gob_fid, tgt_addr.ta_obj + 1);

			rw_desc = rw_desc_get(&rw_desc_list, &tgt_fid);
			if (rw_desc == NULL) {
				rc = -ENOMEM;
				goto cleanup;
			}
			rw_desc->rd_offset  = min_check(rw_desc->rd_offset,
							pos);
			rw_desc->rd_count  += unit_size;
			rw_desc->rd_session = c2t1fs_container_id_to_session(
						     csb, tgt_fid.f_container);

			switch (unit_type) {
			case PUT_DATA:
				/* add data buffer to rw_desc */
				rc = rw_desc_add(rw_desc, buf + offset_in_buf,
						     unit_size, PUT_DATA);
				if (rc != 0)
					goto cleanup;

				c2_buf_init(&data_bufs[unit],
					    buf + offset_in_buf,
					    unit_size);

				offset_in_buf += unit_size;
				break;

			case PUT_PARITY:
				/* Allocate buffer for parity and add it to
				   rw_desc */
				parity_index = unit - nr_data_units;
				ptr = c2_alloc(unit_size);
				/* rpc bulk api require page aligned addr */
				C2_ASSERT(
				  address_is_page_aligned((unsigned long)ptr));

				rc = rw_desc_add(rw_desc, ptr, unit_size,
							PUT_PARITY);
				if (rc != 0) {
					c2_free(ptr);
					goto cleanup;
				}

				c2_buf_init(&parity_bufs[parity_index], ptr,
						unit_size);
				/*
				 * If this is last parity buffer and operation
				 * is write, then compute all parity buffers
				 * for the entire current group.
				 */
				if (parity_index == nr_parity_units - 1 &&
						rw == WRITE) {
					C2_LOG("Compute parity of grp %lu",
					     (unsigned long)src_addr.sa_group);
					c2_parity_math_calculate(
							&pd_layout->pl_math,
							data_bufs,
							parity_bufs);
				}
				break;

			case PUT_SPARE:
				/* we've decided to skip spare units. So we
				   shouldn't reach here */
				C2_ASSERT(0);
				break;

			default:
				C2_ASSERT(0);
			}
		}
	}

	rc = c2t1fs_rpc_rw(&rw_desc_list, rw);

cleanup:
	c2_tl_for(rwd, &rw_desc_list, rw_desc) {

		rwd_tlist_del(rw_desc);

		rw_desc_fini(rw_desc);
		c2_free(rw_desc);

	} c2_tl_endfor;
	rwd_tlist_fini(&rw_desc_list);

	C2_LEAVE("rc: %ld", rc);
	return rc;
}

static struct page *addr_to_page(void *addr)
{
	struct page   *pg = NULL;
	unsigned long  ul_addr;
	int            nr_pinned;

	enum {
		NR_PAGES      = 1,
		WRITABLE      = 1,
		FORCE         = 0,
	};

	C2_ENTRY();
	C2_LOG("addr: %p", addr);

	ul_addr = (unsigned long)addr;
	C2_ASSERT(address_is_page_aligned(ul_addr));

	if (current->mm != NULL &&
	    access_ok(VERIFY_READ, addr, PAGE_CACHE_SIZE)) {

		/* addr points in user space */
		down_read(&current->mm->mmap_sem);
		nr_pinned = get_user_pages(current, current->mm, ul_addr,
					   NR_PAGES, !WRITABLE, FORCE, &pg,
					   NULL);
		up_read(&current->mm->mmap_sem);

		if (nr_pinned <= 0) {
			C2_LOG("get_user_pages() failed: [%d]", nr_pinned);
			pg = NULL;
		} else {
			/*
			 * The page is already pinned by
			 * c2t1fs_pin_memory_area(). we're only interested in
			 * page*, so drop the ref
			 */
			put_page(pg);
		}

	} else {

		/* addr points in kernel space */
		pg = virt_to_page(addr);
	}

	C2_LEAVE("pg: %p", pg);
	return pg;
}

int rw_desc_to_io_fop(const struct rw_desc *rw_desc,
		      int                   rw,
		      struct c2_io_fop    **out)
{
	struct c2_net_domain   *ndom;
	struct c2_fop_type     *fopt;
	struct c2_fop_cob_rw   *rwfop;
	struct c2t1fs_buf      *cbuf;
	struct c2_io_fop       *iofop;
	struct c2_rpc_bulk     *rbulk;
	struct c2_rpc_bulk_buf *rbuf;
	struct page            *page;
	void                   *addr;
	uint64_t                buf_size;
	uint64_t                offset_in_stob;
	uint64_t                count;
	int                     nr_segments;
	int                     remaining_segments;
	int                     seg;
	int                     nr_pages_per_buf;
	int                     rc;
	int                     i;

#define SESSION_TO_NDOM(session) \
	(session)->s_conn->c_rpc_machine->rm_tm.ntm_dom

	int add_rpc_buffer(void)
	{
		int      max_nr_segments;
		uint64_t max_buf_size;

		C2_ENTRY();

		max_nr_segments = c2_net_domain_get_max_buffer_segments(ndom);
		max_buf_size    = c2_net_domain_get_max_buffer_size(ndom);

		/* Assuming each segment is of size PAGE_CACHE_SIZE */
		nr_segments = min_type(int, max_nr_segments,
					max_buf_size / PAGE_CACHE_SIZE);
		nr_segments = min_type(int, nr_segments, remaining_segments);

		C2_LOG("max_nr_seg [%d] remaining [%d]", max_nr_segments,
							 remaining_segments);

		rbuf = NULL;
		rc = c2_rpc_bulk_buf_add(rbulk, nr_segments, ndom, NULL, &rbuf);
		if (rc == 0) {
			C2_ASSERT(rbuf != NULL);
			rbuf->bb_nbuf->nb_qtype = (rw == READ)
						? C2_NET_QT_PASSIVE_BULK_RECV
					        : C2_NET_QT_PASSIVE_BULK_SEND;
		}

		C2_LEAVE("rc: %d", rc);
		return rc;
	}

	C2_ENTRY();

	C2_ASSERT(rw_desc != NULL && out != NULL);
	*out = NULL;

	C2_ALLOC_PTR(iofop);
	if (iofop == NULL) {
		C2_LOG("iofop allocation failed");
		rc = -ENOMEM;
		goto out;
	}

	fopt = (rw == READ) ? &c2_fop_cob_readv_fopt : &c2_fop_cob_writev_fopt;

	rc = c2_io_fop_init(iofop, fopt);
	if (rc != 0) {
		C2_LOG("io_fop_init() failed rc [%d]", rc);
		goto iofop_free;
	}

	rwfop = io_rw_get(&iofop->if_fop);
	C2_ASSERT(rwfop != NULL);

	rwfop->crw_fid.f_seq = rw_desc->rd_fid.f_container;
	rwfop->crw_fid.f_oid = rw_desc->rd_fid.f_key;

	cbuf = bufs_tlist_head(&rw_desc->rd_buf_list);

	/*
	 * ASSUMING all c2t1fs_buf objects in rw_desc->rd_buf_list have same
	 * number of bytes i.e. c2t1fs_buf::cb_buf.b_nob. This holds true for
	 * now because, only full-stripe width io is supported. So each
	 * c2t1fs_buf is representing in-memory location of one stripe unit.
	 */

	buf_size = cbuf->cb_buf.b_nob;

	/*
	 * Make sure, buf_size is multiple of page size. Because cbuf is
	 * stripe unit and this implementation assumes stripe unit is
	 * multiple of PAGE_CACHE_SIZE.
	 */

	C2_ASSERT((buf_size & (PAGE_CACHE_SIZE - 1)) == 0);
	nr_pages_per_buf = buf_size >> PAGE_CACHE_SHIFT;

	remaining_segments = bufs_tlist_length(&rw_desc->rd_buf_list) *
				nr_pages_per_buf;
	C2_ASSERT(remaining_segments > 0);

	C2_LOG("bufsize [%lu] pg/buf [%d] nr_bufs [%d] rem_segments [%d]",
			(unsigned long)buf_size, nr_pages_per_buf,
			(int)bufs_tlist_length(&rw_desc->rd_buf_list),
			remaining_segments);

	ndom            = SESSION_TO_NDOM(rw_desc->rd_session);

	offset_in_stob  = rw_desc->rd_offset;
	count           = 0;
	seg             = 0;

	rbulk           = &iofop->if_rbulk;

	rc = add_rpc_buffer();
	if (rc != 0)
		goto buflist_empty;

	c2_tl_for(bufs, &rw_desc->rd_buf_list, cbuf) {
		addr = cbuf->cb_buf.b_addr;

		/* See comments earlier in this function, to understand
		   following assertion. */
		C2_ASSERT(nr_pages_per_buf * PAGE_CACHE_SIZE ==
				cbuf->cb_buf.b_nob);

		for (i = 0; i < nr_pages_per_buf; i++) {

			page = addr_to_page(addr);
			C2_ASSERT(page != NULL);

retry:
			rc = c2_rpc_bulk_buf_databuf_add(rbuf,
						page_address(page),
						PAGE_CACHE_SIZE,
						offset_in_stob, ndom);

			if (rc == -EMSGSIZE) {
				/* add_rpc_buffer() is nested function.
				   add_rpc_buffer() modifies rbuf */
				rc = add_rpc_buffer();
				if (rc != 0)
					goto buflist_empty;
				C2_LOG("rpc buffer added");
				goto retry;
			}

			if (rc != 0)
				goto buflist_empty;

			offset_in_stob += PAGE_CACHE_SIZE;
			count          += PAGE_CACHE_SIZE;
			addr           += PAGE_CACHE_SIZE;

			seg++;
			remaining_segments--;

			C2_LOG("Added: pg [0x%p] addr [0x%p] off [%lu] "
				 "count [%lu] seg [%d] remaining [%d]",
				 page, addr,
				(unsigned long)offset_in_stob,
				(unsigned long)count, seg, remaining_segments);

		}
	} c2_tl_endfor;

	C2_ASSERT(count == rw_desc->rd_count);

        rc = c2_io_fop_prepare(&iofop->if_fop);
	if (rc != 0) {
		C2_LOG("io_fop_prepare() failed: rc [%d]", rc);
		goto buflist_empty;
	}

	*out = iofop;

	C2_LEAVE("rc: %d", rc);
	return rc;

buflist_empty:
	c2_rpc_bulk_buflist_empty(rbulk);

	c2_io_fop_fini(iofop);

iofop_free:
	c2_free(iofop);

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int io_fop_do_sync_io(struct c2_io_fop     *iofop,
			     struct c2_rpc_session *session)
{
	struct c2_fop_cob_rw_reply *rw_reply;
	struct c2_fop_cob_rw       *rwfop;
	int                         rc;

	C2_ENTRY();

	C2_ASSERT(iofop != NULL && session != NULL);

	rwfop = io_rw_get(&iofop->if_fop);
	C2_ASSERT(rwfop != NULL);

	rc = c2_rpc_bulk_store(&iofop->if_rbulk, session->s_conn,
					rwfop->crw_desc.id_descs);
	if (rc != 0)
		goto out;

	/*
	 * XXX For simplicity, doing IO to multiple cobs, one after other,
	 * serially. This should be modified, so that io requests on
	 * cobs are processed parallely.
	 */

	rc = c2_rpc_client_call(&iofop->if_fop, session,
					iofop->if_fop.f_item.ri_ops,
					C2T1FS_RPC_TIMEOUT);
	if (rc != 0)
		goto out;

	rw_reply = io_rw_rep_get(
			c2_rpc_item_to_fop(iofop->if_fop.f_item.ri_reply));
	rc = rw_reply->rwr_rc;

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

static ssize_t c2t1fs_rpc_rw(const struct c2_tl *rw_desc_list, int rw)
{
	struct rw_desc        *rw_desc;
	struct c2t1fs_buf     *buf;
	struct c2_io_fop      *iofop;
	ssize_t                count = 0;
	int                    rc;

	C2_ENTRY();

	C2_LOG("Operation: %s", (char *)(rw == READ ? "READ" : "WRITE"));

	if (rwd_tlist_is_empty(rw_desc_list))
		C2_LOG("rw_desc_list is empty");

	c2_tl_for(rwd, rw_desc_list, rw_desc) {

		C2_LOG("fid: [%lu:%lu] offset: %lu count: %lu",
				(unsigned long)rw_desc->rd_fid.f_container,
				(unsigned long)rw_desc->rd_fid.f_key,
				(unsigned long)rw_desc->rd_offset,
				(unsigned long)rw_desc->rd_count);

		C2_LOG("Buf list");

		c2_tl_for(bufs, &rw_desc->rd_buf_list, buf) {

			C2_LOG("addr %p len %lu type %s",
				buf->cb_buf.b_addr,
				(unsigned long)buf->cb_buf.b_nob,
				(char *)(
				(buf->cb_type == PUT_DATA) ? "DATA" :
				 (buf->cb_type == PUT_PARITY) ? "PARITY" :
				 (buf->cb_type == PUT_SPARE) ? "SPARE" :
					"UNKNOWN"));

			if (buf->cb_type == PUT_DATA)
				count += buf->cb_buf.b_nob;

		} c2_tl_endfor;

		rc = rw_desc_to_io_fop(rw_desc, rw, &iofop);
		if (rc != 0) {
			/* For now, if one io fails, fail entire IO. */
			C2_LOG("rw_desc_to_io_fop() failed: rc [%d]", rc);
			C2_LEAVE("%d", rc);
			return rc;
		}

		rc = io_fop_do_sync_io(iofop, rw_desc->rd_session);
		if (rc != 0) {
			/* For now, if one io fails, fail entire IO. */
			C2_LOG("io_fop_do_sync_io() failed: rc [%d]", rc);
			C2_LEAVE("%d", rc);
			return rc;
		}
	} c2_tl_endfor;

	C2_LEAVE("count: %ld", count);
	return count;
}
