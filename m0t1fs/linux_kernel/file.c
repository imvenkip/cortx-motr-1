/* -*- C -*- */
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
 * Original author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 07/28/2012
 */

#include <asm/uaccess.h>    /* VERIFY_READ, VERIFY_WRITE */
#include <linux/mm.h>       /* get_user_pages, get_page, put_page */
#include <linux/fs.h>       /* struct file_operations */
#include <linux/mount.h>    /* struct vfsmount (f_path.mnt) */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0T1FS
#include "lib/trace.h"

#include "fop/fom_generic.h"/* m0_rpc_item_is_generic_reply_fop */
#include "lib/memory.h"     /* m0_alloc, m0_free */
#include "lib/misc.h"       /* m0_round_{up/down} */
#include "lib/bob.h"        /* m0_bob_type */
#include "lib/ext.h"        /* m0_ext */
#include "lib/arith.h"      /* min_type */
#include "lib/finject.h"    /* M0_FI_ENABLED */
#include "layout/pdclust.h" /* M0_PUT_*, m0_layout_to_pdl,
			     * m0_pdclust_instance_map */
#include "lib/bob.h"        /* m0_bob_type */
#include "ioservice/io_fops.h"    /* m0_io_fop */
#include "ioservice/io_device.h"
#include "mero/magic.h"  /* M0_T1FS_IOREQ_MAGIC */
#include "m0t1fs/linux_kernel/m0t1fs.h" /* m0t1fs_sb */
#include "file/file.h"
#include "lib/hash.h"	    /* m0_htable */
#include "sns/parity_repair.h"  /*m0_sns_repair_spare_map() */
#include "m0t1fs/linux_kernel/file_internal.h"
#include "m0t1fs/m0t1fs_addb.h"


/**
   @page iosnsrepair I/O with SNS and SNS repair.

   - @ref iosnsrepair-ovw
   - @ref iosnsrepair-def
   - @ref iosnsrepair-req
   - @ref iosnsrepair-depends
   - @ref iosnsrepair-highlights
   - @ref iosnsrepair-lspec
      - @ref iosnsrepair-lspec-comps
      - @ref iosnsrepair-lspec-state
      - @ref iosnsrepair-lspec-thread
      - @ref iosnsrepair-lspec-numa
   - @ref iosnsrepair-conformance
   - @ref iosnsrepair-ut
   - @ref iosnsrepair-st
   - @ref iosnsrepair-O
   - @ref iosnsrepair-ref
   - @ref iosnsrepair-impl-plan


   <hr>
   @section iosnsrepair-ovw Overview
   @note This DLD is written by Huang Hua (hua_huang@xyratex.com), 2012/10/10.

   This DLD describes how the m0t1fs does I/O with SNS in normal condition, in
   de-graded mode, and when SNS repair is completed.

   A file (also known as global object) in Mero is stored in multiple component
   objects, spreading on multiple servers. This is usually also called Server
   Network Striping, a.k.a SNS. Layout is used to describe the mapping of a
   file to its objects. A read request to some specific offset within a file
   will be directed to some parts of its component objects, according to its
   layout. A write request does the same. Some files don't store redundancy
   information in the file system, like RAID0. But in Mero, the default and
   typical mode is to have files with redundancy data stored somewhere. So the
   write requests may include updates to redundancy data.

   In case of node or device failure, lost data may be re-constructed from
   redundancy information. A read request to lost data needs to be satisfied
   by re-constructing data from its parity data. When SNS repair is completed
   for the failed node or device, a read or write request can be served by
   re-directing to its spare unit.

   Write requests to failed node or device should be handled in another way,
   cooperated with SNS repair and NBA (Non-Blocking Availability). Now it is
   out of the scope of this DLD.

   Each client has a cache of Failure Vectors of a pool. With failure vector
   information, clients know whether to re-construct data from other data units
   and parity units, or read from spare units (which contains repaired data).
   The detailed will be discussed in the following logical specification.

   <hr>
   @section iosnsrepair-def Definitions
   Previously defined terms:
   - <b>layout</b> A mapping from Mero file (global object) to component
         objects. See @ref layout for more details.
   - <b>SNS</b> Server Network Striping. See @ref SNS for more details.

   <hr>
   @section iosnsrepair-req Requirements
   - @b R.iosnsrepair.read Read request should be served in normal case, during
        SNS repair, and after SNS repair completes.
   - @b R.iosnsrepair.write Write request should be served in normal case, and
        after SNS repair completes.
   - @b R.iosnsrepair.code Code should be re-used and shared with other m0t1fs
        client features, especially the rmw feature.

   <hr>
   @section iosnsrepair-depends Dependencies
   The feature depends on the following features:
   - layout.
   - SNS and failure vector.

   The implementation of this feature may depend on the m0t1fs read-modify-write
   (rmw) feature, which is under development.

   <hr>
   @section iosnsrepair-highlights Design Highlights
   M0t1fs read-modify-write (rmw) feature has some similar concepts with this
   feature. The same code path will be used to serve both the features.

   <hr>
   @section iosnsrepair-lspec Logical Specification

   - @ref iosnsrepair-lspec-comps
   - @ref iosnsrepair-lspec-state
   - @ref iosnsrepair-lspec-thread
   - @ref iosnsrepair-lspec-numa

   @subsection iosnsrepair-lspec-comps Component Overview
   When an I/O request (read, write, or other) comes to client, m0t1fs first
   checks its cached failure vector to see the status of pool nodes and devices.
   A read or write request will span some node(s) or device(s). If these node(s)
   or device(s) are ONLINE, this is the normal case. If some node or device is
   FAILED or REPAIRING or REPAIRED, it will change the state of a pool. When
   all nodes and devices are in ONLINE status, the pool is ONLINE. I/O requests
   are handled normally. If less than or equal failures happen than the pool is
   configured to sustain, the pool is DEGRADED. I/O requests will be handled
   with the help of parity information or spare units. If more failures happen
   than the pool is configured to sustain , the pool is in DUD state, where all
   I/O requests will fail with -EIO error. The pool states define how client IO
   is made, specifically whether writes use NBA and whether read and writes use
   degraded mode. The pool states can be calculated from the failure vector.

   If the special action is taken to serve this
   request. The following table illustrate the actions:

                           Table 1   I/O request handling
   -----------------------------------------------------------------------------
   |      |  ONLINE    | read from the target device                           |
   |      |------------|-------------------------------------------------------|
   |      |  OFFLINE   | same as FAILED                                        |
   |      |------------|-------------------------------------------------------|
   | read |  FAILED    | read from other data unit(s) and parity unit(s) and   |
   |      |  REPAIRING | re-construct the datai. If NBA** exists, use new      |
   |      |            | layout to do reading if necessary.                    |
   |      |            | See more detail for this degraded read (1)            |
   |      |------------|-------------------------------------------------------|
   |      |  REPAIRED  | read from the repaired spare unit or use new layout   |
   |      |            | if NBA**                                              |
   |------|------------|-------------------------------------------------------|
   |      |  ONLINE    | write to the target device                            |
   |      |------------|-------------------------------------------------------|
   |      |  OFFLINE   | same as FAILED                                        |
   |      |------------|-------------------------------------------------------|
   |write |  FAILED    | NBA** determines to use new layout or old layout      |
   |      |            | if old layout is used, this is called degraded        |
   |      |            | write. See more detail in the following (2)           |
   |      |------------|-------------------------------------------------------|
   |      |  REPAIRING | Concurrent++ write I/O and sns repairing is out of    |
   |      |            | the scope of this DLD.  Not supported currently.      |
   |      |            | -ENOTSUP will be returned at this moment.             |
   |      |            | This is @TODO Concurrent r/w in SNS repair            |
   |      |------------|-------------------------------------------------------|
   |      |  REPAIRED  | write to the repaired spare unit or new layout        |
   |      |            | if NBA**                                              |
   ----------------------------------------------------------------------------|
   NBA** Non-Blocking Availability. When a device/node is not available for
   a write request, the system switches the file to use a new layout, and so the
   data is written to devices in new layout. By such means, the writing request
   will not be blocked waiting the device to be fixed, or SNS repaire to be
   completed. Device/node becomes un-available when it is OFFLINE or FAILED.
   Concurrent++ This should be designed in other module.

   A device never goes from repaired to online. When the re-balancing process
   that moves data from spare space to a new device completes, the *new* device
   goes from REBALANCING to ONLINE state. If the old device is ever "fixed"
   somehow, it becomes a new device in ONLINE state.

   A degraded read request is handled with the following steps:
   (1) Calculate its parity group, find out related data units and parity units.
       This needs help from the file's layout.
   (2) Send read requests to necessary data units and/or parity units
       asynchronously. The read request itself is blocked and waiting
       for those replies. For a N+K+K (N data units, K parity units, K
       spare units) layout, N units of data or parity units are needed
       to re-compute the lost data.
   (3) When those read replies come, ASTs will be called to re-compute the data
       iteratively. Temporary result is stored in the buffer of the original
       read request. This async read request and its reply can be released (no
       cache at this moment).
   (4) When all read replies come back, and the data is finally re-computed,
       the original read request has its data, and can be returned to user.

   A degraded write request is handled as the following:
   (1) Calculate its parity group, find out related data units and parity units.
       This needs help from the file's layout.
   (2) Prepare to async read data and/or parity units.
       (2.1) If this is a full-stripe write request, skip to step (4).
       (2.2) If write request only spans ONLINE devices, this is similar to a
             Read-Modify-Write (rmw), except a little difference: only async
             read the spanned data unit(s). Async read all spanned data units.
       (2.3) If write request spans FAILED/OFFLINE devices, async read all
             survival and un-spanned data units and necessary parity unit(s).
   (3) When these async read requests complete, replies come back to client.
       (3.1) for (2.2) case, prepare new parity units from the old data and
             new data.
       (3.2) for (2.3) case, first, re-calculate the lost data unit, and do
             the same as 3.1.
   (4) Send write request(s) to data units, along with all the new parity data,
       except the failed device(s). Please note here: write to the failed
       devices are excluded to send.
   (5) When those write requests complete, return to user space.

   The same thread used by the rmw will be used here to run the ASTs. The basic
   algorithm is similar in these two features. No new data structures are
   introduced by this feature.

   Pool's failure vector is cached on clients. Every I/O request to ioservices
   is tagged with client known failure vector version, and this version is
   checked against the lastest one by ioservices. If the client known version
   is stale, new version and failure vector updates will be returnen back to
   clients and clients need to apply this update and do I/O request according
   to the latest version. Please see @ref pool and @ref poolmach for more
   details.

   Which spare space to use in the SNS repair is managed by failure vector.
   After SNS repair, client can query this information from failure vector and
   send r/w request to corresponding spare space.

   @subsection iosnsrepair-lspec-state State Specification
   N/A

   @subsection iosnsrepair-lspec-thread Threading and Concurrency Model
   See @ref rmw_io_dld for more information.

   @subsection iosnsrepair-lspec-numa NUMA optimizations
   See @ref rmw_io_dld for more information.

   @section iosnsrepair-conformance Conformance
   - @b I.iosnsrepair.read Read request handling is described in logic
        specification. Every node/device state are covered.
   - @b I.iosnsrepair.read Write request handling is described in logic
        specification. Every node/device state are covered.
   - @b I.iosnsrepair.code In logic specification, the design says the same
        code and algorithm will be used to handle io request in SNS repair
        and rmw.

   <hr>
   @section iosnsrepair-ut Unit Tests
   Unit tests for read and write requests in different devices state are
   needed. These states includes: ONLINE, OFFLINE, FAILED, REPAIRING,
   REPAIRED.

   <hr>
   @section iosnsrepair-st System Tests
   System tests are needed to verify m0t1fs can serve read/write properly
   when node/device is in various states, and changes its state from one
   to another. For example:
   - read/write requests in normal case.
   - read/write requests when a device changes from ONLINE to FAILED.
   - read/write requests when a device changes from FAILED to REPAIRING.
   - read/write requests when a device changes from REPAIRING to REPAIRED.

   <hr>
   @section iosnsrepair-O Analysis
   See @ref rmw for more information.

   <hr>
   @section iosnsrepair-ref References

   - <a href="https://docs.google.com/a/xyratex.com/document/d/
1vgs3jeskGKApvE016XxwXAxGyUoNxbKudTTNRSoL-Vg/edit">
HLD of SNS repair</a>,
   - @ref rmw_io_dld m0t1fs client read-modify-write DLD
   - @ref layout Layout
   - @ref pool Pool and @ref poolmach Pool Machine.

   <hr>
   @section iosnsrepair-impl-plan Implementation Plan
   The code implementation depends on the m0t1fs rmw which is under development.
   The rmw is in code inspection phase right now. When this DLD is approved,
   code maybe can start.

 */

struct io_mem_stats iommstats;

M0_INTERNAL void iov_iter_advance(struct iov_iter *i, size_t bytes);

/* Imports */
struct m0_net_domain;
M0_INTERNAL bool m0t1fs_inode_bob_check(struct m0t1fs_inode *bob);
M0_TL_DECLARE(rpcbulk, M0_INTERNAL, struct m0_rpc_bulk_buf);
M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);

M0_TL_DESCR_DEFINE(iofops, "List of IO fops", static,
		   struct io_req_fop, irf_link, irf_magic,
		   M0_T1FS_IOFOP_MAGIC, M0_T1FS_TIOREQ_MAGIC);

M0_TL_DEFINE(iofops,  static, struct io_req_fop);
M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);
M0_TL_DECLARE(rpcbulk, M0_INTERNAL, struct m0_rpc_bulk_buf);

static const struct m0_bob_type tioreq_bobtype;
static struct m0_bob_type iofop_bobtype;
static const struct m0_bob_type ioreq_bobtype;
static const struct m0_bob_type pgiomap_bobtype;
static const struct m0_bob_type nwxfer_bobtype;
static const struct m0_bob_type dtbuf_bobtype;

M0_BOB_DEFINE(static, &tioreq_bobtype,	target_ioreq);
M0_BOB_DEFINE(static, &iofop_bobtype,	io_req_fop);
M0_BOB_DEFINE(static, &pgiomap_bobtype, pargrp_iomap);
M0_BOB_DEFINE(static, &ioreq_bobtype,	io_request);
M0_BOB_DEFINE(static, &nwxfer_bobtype,	nw_xfer_request);
M0_BOB_DEFINE(static, &dtbuf_bobtype,	data_buf);

static const struct m0_bob_type ioreq_bobtype = {
	.bt_name	 = "io_request_bobtype",
	.bt_magix_offset = offsetof(struct io_request, ir_magic),
	.bt_magix	 = M0_T1FS_IOREQ_MAGIC,
	.bt_check	 = NULL,
};

static const struct m0_bob_type pgiomap_bobtype = {
	.bt_name	 = "pargrp_iomap_bobtype",
	.bt_magix_offset = offsetof(struct pargrp_iomap, pi_magic),
	.bt_magix	 = M0_T1FS_PGROUP_MAGIC,
	.bt_check	 = NULL,
};

static const struct m0_bob_type nwxfer_bobtype = {
	.bt_name	 = "nw_xfer_request_bobtype",
	.bt_magix_offset = offsetof(struct nw_xfer_request, nxr_magic),
	.bt_magix	 = M0_T1FS_NWREQ_MAGIC,
	.bt_check	 = NULL,
};

static const struct m0_bob_type dtbuf_bobtype = {
	.bt_name	 = "data_buf_bobtype",
	.bt_magix_offset = offsetof(struct data_buf, db_magic),
	.bt_magix	 = M0_T1FS_DTBUF_MAGIC,
	.bt_check	 = NULL,
};

static const struct m0_bob_type tioreq_bobtype = {
	.bt_name         = "target_ioreq",
	.bt_magix_offset = offsetof(struct target_ioreq, ti_magic),
	.bt_magix        = M0_T1FS_TIOREQ_MAGIC,
	.bt_check        = NULL,
};

/*
 * These are used as macros since they are used as lvalues which is
 * not possible by using static inline functions.
 */
#define INDEX(ivec, i) ((ivec)->iv_index[(i)])

#define COUNT(ivec, i) ((ivec)->iv_vec.v_count[(i)])

#define SEG_NR(vec)    ((vec)->iv_vec.v_nr)


static inline m0_bcount_t seg_endpos(const struct m0_indexvec *ivec, uint32_t i)
{
	M0_PRE(ivec != NULL);

	return ivec->iv_index[i] + ivec->iv_vec.v_count[i];
}

static inline struct inode *file_to_inode(const struct file *file)
{
	return file->f_dentry->d_inode;
}

static inline struct m0t1fs_inode *file_to_m0inode(const struct file *file)
{
	return M0T1FS_I(file_to_inode(file));
}

static inline const struct m0_fid *file_to_fid(struct file *file)
{
	return m0t1fs_inode_fid(file_to_m0inode(file));
}

static inline struct m0t1fs_sb *file_to_sb(struct file *file)
{
	return M0T1FS_SB(file_to_inode(file)->i_sb);
}

static inline struct m0_sm_group *file_to_smgroup(struct file *file)
{
	return &file_to_sb(file)->csb_iogroup;
}

static inline uint64_t page_nr(m0_bcount_t size)
{
	return size >> PAGE_CACHE_SHIFT;
}

static inline struct m0_layout_instance *layout_instance(const struct io_request
							 *req)
{
	return file_to_m0inode(req->ir_file)->ci_layout_instance;
}

static inline struct m0_pdclust_instance *pdlayout_instance(struct
		m0_layout_instance *li)
{
	return m0_layout_instance_to_pdi(li);
}

static inline struct m0_pdclust_layout *pdlayout_get(const struct io_request
						     *req)
{
	return m0_layout_to_pdl(layout_instance(req)->li_l);
}

static inline uint32_t layout_n(struct m0_pdclust_layout *play)
{
	return play->pl_attr.pa_N;
}

static inline uint32_t layout_k(struct m0_pdclust_layout *play)
{
	return play->pl_attr.pa_K;
}

static inline uint64_t layout_unit_size(struct m0_pdclust_layout *play)
{
	return play->pl_attr.pa_unit_size;
}

static inline uint64_t parity_units_page_nr(struct m0_pdclust_layout *play)
{
	return page_nr(layout_unit_size(play)) * layout_k(play);
}

static inline uint64_t indexvec_page_nr(struct m0_vec *vec)
{
	return page_nr(m0_vec_count(vec));
}

static inline uint64_t data_size(struct m0_pdclust_layout *play)
{
	return layout_n(play) * layout_unit_size(play);
}

static inline struct m0_parity_math *parity_math(struct io_request *req)
{
	return &pdlayout_instance(layout_instance(req))->pi_math;
}

static inline uint64_t group_id(m0_bindex_t index, m0_bcount_t dtsize)
{
	return index / dtsize;
}

static inline uint64_t target_offset(uint64_t		       frame,
				     struct m0_pdclust_layout *play,
				     m0_bindex_t	       gob_offset)
{
	return frame * layout_unit_size(play) +
	       (gob_offset % layout_unit_size(play));
}

static uint64_t tioreqs_hash_func(const struct m0_htable *htable, const void *k)
{
	const uint64_t *key = (uint64_t *)k;

	return *key % htable->h_bucket_nr;
}

static bool tioreq_key_eq(const void *key1, const void *key2)
{
	const uint64_t *k1 = (uint64_t *)key1;
	const uint64_t *k2 = (uint64_t *)key2;

	return *k1 == *k2;
}

M0_HT_DESCR_DEFINE(tioreqht, "Hash of target_ioreq objects", static,
		   struct target_ioreq, ti_link, ti_magic,
		   M0_T1FS_TIOREQ_MAGIC, M0_T1FS_TLIST_HEAD_MAGIC,
		   ti_fid.f_container, tioreqs_hash_func, tioreq_key_eq);

M0_HT_DEFINE(tioreqht, static, struct target_ioreq, uint64_t);

/* Finds out pargrp_iomap::pi_grpid from target index. */
static inline uint64_t pargrp_id_find(m0_bindex_t index,
		                      uint64_t    unit_size)
{
	M0_PRE(unit_size > 0);

	return index / unit_size;
}

static inline m0_bindex_t gfile_offset(m0_bindex_t                 toff,
		                       struct pargrp_iomap        *map,
				       struct m0_pdclust_layout   *play,
				       struct m0_pdclust_src_addr *src)
{
	m0_bindex_t goff;

	M0_PRE(map  != NULL);
	M0_PRE(play != NULL);

	M0_ENTRY("grpid = %llu, target_off = %llu", map->pi_grpid, toff);

	goff = map->pi_grpid * data_size(play) +
	       src->sa_unit * layout_unit_size(play) +
	       toff % layout_unit_size(play);
	M0_LEAVE("global file offset = %llu", goff);

	return goff;
}

static inline struct m0_fid target_fid(const struct io_request	  *req,
				       struct m0_pdclust_tgt_addr *tgt)
{
	return m0t1fs_ios_cob_fid(file_to_m0inode(req->ir_file), tgt->ta_obj);
}

static inline struct m0_rpc_session *target_session(struct io_request *req,
						    struct m0_fid      tfid)
{
	return m0t1fs_container_id_to_session(file_to_sb(req->ir_file),
					      tfid.f_container);
}

static inline uint64_t page_id(m0_bindex_t offset)
{
	return offset >> PAGE_CACHE_SHIFT;
}

static inline uint32_t data_row_nr(struct m0_pdclust_layout *play)
{
	return page_nr(layout_unit_size(play));
}

static inline uint32_t data_col_nr(struct m0_pdclust_layout *play)
{
	return layout_n(play);
}

static inline uint32_t parity_col_nr(struct m0_pdclust_layout *play)
{
	return layout_k(play);
}

static inline uint32_t parity_row_nr(struct m0_pdclust_layout *play)
{
	return data_row_nr(play);
}

#if !defined(round_down)
static inline uint64_t round_down(uint64_t val, uint64_t size)
{
	M0_PRE(m0_is_po2(size));

	/*
	 * Returns current value if it is already a multiple of size,
	 * else m0_round_down() is invoked.
	 */
	return (val & (size - 1)) == 0 ?
	       val : m0_round_down(val, size);
}
#endif

#if !defined(round_up)
static inline uint64_t round_up(uint64_t val, uint64_t size)
{
	M0_PRE(m0_is_po2(size));

	/*
	 * Returns current value if it is already a multiple of size,
	 * else m0_round_up() is invoked.
	 */
	return (val & (size - 1)) == 0 ?
	       val : m0_round_up(val, size);
}
#endif

/* Returns the position of page in matrix of data buffers. */
static void page_pos_get(struct pargrp_iomap *map,
		         m0_bindex_t          index,
			 uint32_t            *row,
			 uint32_t            *col)
{
	uint64_t		  pg_id;
	struct m0_pdclust_layout *play;

	M0_PRE(map != NULL);
	M0_PRE(row != NULL);
	M0_PRE(col != NULL);

	play = pdlayout_get(map->pi_ioreq);

	pg_id = page_id(index - data_size(play) * map->pi_grpid);
	*row  = pg_id % data_row_nr(play);
	*col  = pg_id / data_row_nr(play);
}

/*
 * Returns the starting offset of page given its position in data matrix.
 * Acts as opposite of page_pos_get() API.
 */
static void data_page_offset_get(struct pargrp_iomap *map,
		                uint32_t              row,
			        uint32_t              col,
		                m0_bindex_t          *out)
{
	struct m0_pdclust_layout *play;

	M0_ENTRY("row = %u, col = %u", row, col);
	M0_PRE(map != NULL);
	M0_PRE(out != NULL);

	play = pdlayout_get(map->pi_ioreq);

	M0_ASSERT(row < data_row_nr(play));
	M0_ASSERT(col < data_col_nr(play));

	*out = data_size(play) * map->pi_grpid +
	       col * layout_unit_size(play) + row * PAGE_CACHE_SIZE;
}

/* Invoked during m0t1fs mount. */
M0_INTERNAL void io_bob_tlists_init(void)
{
	M0_ASSERT(tioreq_bobtype.bt_magix == M0_T1FS_TIOREQ_MAGIC);
	m0_bob_type_tlist_init(&iofop_bobtype, &iofops_tl);
	M0_ASSERT(iofop_bobtype.bt_magix == M0_T1FS_IOFOP_MAGIC);
}

static void device_state_reset(struct nw_xfer_request *xfer, bool rmw);
static int io_spare_map(const struct pargrp_iomap *map,
			const struct m0_pdclust_src_addr *src,
			uint32_t *spare_slot, uint32_t *spare_slot_prev,
			enum m0_pool_nd_state *eff_state);

static int unit_state(const struct m0_pdclust_src_addr *src,
			 const struct io_request *req,
			 enum m0_pool_nd_state *state);

static void io_rpc_item_cb (struct m0_rpc_item *item);
static void io_req_fop_release(struct m0_ref *ref);

/*
 * io_rpc_item_cb can not be directly invoked from io fops code since it
 * leads to build dependency of ioservice code over kernel-only code (m0t1fs).
 * Hence, a new m0_rpc_item_ops structure is used for fops dispatched
 * by m0t1fs io requests.
 */
static const struct m0_rpc_item_ops m0t1fs_item_ops = {
	.rio_replied = io_rpc_item_cb,
};

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer);

static int  nw_xfer_io_distribute(struct nw_xfer_request *xfer);
static void nw_xfer_req_complete (struct nw_xfer_request *xfer,
				  bool			 rmw);
static int  nw_xfer_req_dispatch (struct nw_xfer_request *xfer);

static int  nw_xfer_tioreq_map	 (struct nw_xfer_request	   *xfer,
				  const struct m0_pdclust_src_addr *src,
				  struct m0_pdclust_tgt_addr       *tgt,
				  struct target_ioreq             **out);

static int  nw_xfer_tioreq_get	 (struct nw_xfer_request *xfer,
				  struct m0_fid		*fid,
				  struct m0_rpc_session	*session,
				  uint64_t		 size,
				  struct target_ioreq   **out);

static const struct nw_xfer_ops xfer_ops = {
	.nxo_distribute  = nw_xfer_io_distribute,
	.nxo_complete	 = nw_xfer_req_complete,
	.nxo_dispatch	 = nw_xfer_req_dispatch,
	.nxo_tioreq_map	 = nw_xfer_tioreq_map,
};

static int  pargrp_iomap_populate     (struct pargrp_iomap	*map,
				       const struct m0_indexvec *ivec,
				       struct m0_ivec_cursor	*cursor);

static bool pargrp_iomap_spans_seg    (struct pargrp_iomap *map,
				       m0_bindex_t	    index,
				       m0_bcount_t	    count);

static int  pargrp_iomap_readrest     (struct pargrp_iomap *map);


static int  pargrp_iomap_seg_process  (struct pargrp_iomap *map,
				       uint64_t		    seg,
				       bool		    rmw);

static int  pargrp_iomap_parity_recalc(struct pargrp_iomap *map);

static uint64_t pargrp_iomap_fullpages_count(struct pargrp_iomap *map);

static int pargrp_iomap_readold_auxbuf_alloc(struct pargrp_iomap *map);

static int pargrp_iomap_paritybufs_alloc(struct pargrp_iomap *map);

static int pargrp_iomap_dgmode_process (struct pargrp_iomap *map,
		                         struct target_ioreq *tio,
		                         m0_bindex_t         *index,
					 uint32_t             count);

static int pargrp_iomap_dgmode_postprocess(struct pargrp_iomap *map);

static int pargrp_iomap_dgmode_recover  (struct pargrp_iomap *map);

static const struct pargrp_iomap_ops iomap_ops = {
	.pi_populate		 = pargrp_iomap_populate,
	.pi_spans_seg		 = pargrp_iomap_spans_seg,
	.pi_readrest		 = pargrp_iomap_readrest,
	.pi_fullpages_find	 = pargrp_iomap_fullpages_count,
	.pi_seg_process		 = pargrp_iomap_seg_process,
	.pi_readold_auxbuf_alloc = pargrp_iomap_readold_auxbuf_alloc,
	.pi_parity_recalc	 = pargrp_iomap_parity_recalc,
	.pi_paritybufs_alloc	 = pargrp_iomap_paritybufs_alloc,
	.pi_dgmode_process       = pargrp_iomap_dgmode_process,
	.pi_dgmode_postprocess   = pargrp_iomap_dgmode_postprocess,
	.pi_dgmode_recover       = pargrp_iomap_dgmode_recover,
};

static bool pargrp_iomap_invariant_nr (const struct io_request *req);
static bool target_ioreq_invariant    (const struct target_ioreq *ti);

static void target_ioreq_fini	      (struct target_ioreq *ti);

static int target_ioreq_iofops_prepare(struct target_ioreq *ti,
				       enum page_attr       filter);

static void target_ioreq_seg_add(struct target_ioreq              *ti,
				 const struct m0_pdclust_src_addr *src,
				 const struct m0_pdclust_tgt_addr *tgt,
				 m0_bindex_t	                   gob_offset,
				 m0_bcount_t	                   count,
				 struct pargrp_iomap              *map);

static const struct target_ioreq_ops tioreq_ops = {
	.tio_seg_add	    = target_ioreq_seg_add,
	.tio_iofops_prepare = target_ioreq_iofops_prepare,
};

static int io_req_fop_dgmode_read(struct io_req_fop *irfop);

static const struct io_req_fop_ops irfop_ops = {
	.irfo_dgmode_read = io_req_fop_dgmode_read,
};

static struct data_buf *data_buf_alloc_init(enum page_attr pattr);

static void data_buf_dealloc_fini(struct data_buf *buf);

static void io_bottom_half(struct m0_sm_group *grp, struct m0_sm_ast *ast);

static int  ioreq_iomaps_prepare(struct io_request *req);

static void ioreq_iomaps_destroy(struct io_request *req);

static int ioreq_user_data_copy (struct io_request   *req,
				 enum copy_direction  dir,
				 enum page_attr	      filter);

static int ioreq_parity_recalc	(struct io_request *req);

static int ioreq_iosm_handle	(struct io_request *req);

static int  ioreq_file_lock     (struct io_request *req);
static void ioreq_file_unlock   (struct io_request *req);

static int ioreq_dgmode_read    (struct io_request *req, bool rmw);
static int ioreq_dgmode_write   (struct io_request *req, bool rmw);
static int ioreq_dgmode_recover (struct io_request *req);

static const struct io_request_ops ioreq_ops = {
	.iro_iomaps_prepare = ioreq_iomaps_prepare,
	.iro_iomaps_destroy = ioreq_iomaps_destroy,
	.iro_user_data_copy = ioreq_user_data_copy,
	.iro_parity_recalc  = ioreq_parity_recalc,
	.iro_iosm_handle    = ioreq_iosm_handle,
	.iro_file_lock      = ioreq_file_lock,
	.iro_file_unlock    = ioreq_file_unlock,
	.iro_dgmode_read    = ioreq_dgmode_read,
	.iro_dgmode_write   = ioreq_dgmode_write,
	.iro_dgmode_recover = ioreq_dgmode_recover,
};

static void failure_vector_mismatch(struct io_req_fop *irfop);

static inline uint32_t ioreq_sm_state(const struct io_request *req)
{
	return req->ir_sm.sm_state;
}

static void ioreq_sm_failed(struct io_request *req, int rc)
{
	m0_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	m0_sm_fail(&req->ir_sm, IRS_FAILED, rc);
	m0_mutex_unlock(&req->ir_sm.sm_grp->s_lock);
}

static struct m0_sm_state_descr io_states[] = {
	[IRS_INITIALIZED]       = {
		.sd_flags       = M0_SDF_INITIAL,
		.sd_name        = "IO_initial",
		.sd_allowed     = M0_BITS(IRS_LOCK_ACQUIRED,
					  IRS_FAILED,  IRS_REQ_COMPLETE)
	},
	[IRS_LOCK_ACQUIRED]     = {
		.sd_name        = "IO_dist_lock_acquired",
		.sd_allowed     = M0_BITS(IRS_READING, IRS_WRITING),
	},
	[IRS_READING]	        = {
		.sd_name        = "IO_reading",
		.sd_allowed     = M0_BITS(IRS_READ_COMPLETE, IRS_FAILED)
	},
	[IRS_READ_COMPLETE]     = {
		.sd_name        = "IO_read_complete",
		.sd_allowed     = M0_BITS(IRS_WRITING, IRS_REQ_COMPLETE,
			                  IRS_DEGRADED_READING, IRS_FAILED,
					  IRS_READING, IRS_LOCK_RELINQUISHED)
	},
	[IRS_DEGRADED_READING]  = {
		.sd_name        = "IO_degraded_read",
		.sd_allowed     = M0_BITS(IRS_READ_COMPLETE, IRS_FAILED)
	},
	[IRS_DEGRADED_WRITING]  = {
		.sd_name        = "IO_degraded_write",
		.sd_allowed     = M0_BITS(IRS_WRITE_COMPLETE, IRS_FAILED)
	},
	[IRS_WRITING]           = {
		.sd_name        = "IO_writing",
		.sd_allowed     = M0_BITS(IRS_WRITE_COMPLETE, IRS_FAILED)
	},
	[IRS_WRITE_COMPLETE]    = {
		.sd_name        = "IO_write_complete",
		.sd_allowed     = M0_BITS(IRS_REQ_COMPLETE, IRS_FAILED,
				          IRS_DEGRADED_WRITING,
					  IRS_LOCK_RELINQUISHED)
	},
	[IRS_LOCK_RELINQUISHED] = {
		.sd_name        = "IO_dist_lock_relinquished",
		.sd_allowed     = M0_BITS(IRS_FAILED, IRS_REQ_COMPLETE),
	},
	[IRS_FAILED]            = {
		.sd_flags       = M0_SDF_FAILURE,
		.sd_name        = "IO_req_failed",
		.sd_allowed     = M0_BITS(IRS_REQ_COMPLETE)
	},
	[IRS_REQ_COMPLETE]      = {
		.sd_flags       = M0_SDF_TERMINAL,
		.sd_name        = "IO_req_complete",
	},
};

static const struct m0_sm_conf io_sm_conf = {
	.scf_name      = "IO request state machine configuration",
	.scf_nr_states = ARRAY_SIZE(io_states),
	.scf_state     = io_states,
};

static void ioreq_sm_state_set(struct io_request *req, int state)
{
	M0_LOG(M0_INFO, "IO request %p current state = %s",
	       req, io_states[ioreq_sm_state(req)].sd_name);
	m0_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	m0_sm_state_set(&req->ir_sm, state);
	m0_mutex_unlock(&req->ir_sm.sm_grp->s_lock);
	M0_LOG(M0_INFO, "IO request state changed to %s",
	       io_states[ioreq_sm_state(req)].sd_name);
}

static bool io_request_invariant(const struct io_request *req)
{
	return
	       io_request_bob_check(req) &&
	       req->ir_type   <= IRT_TYPE_NR &&
	       req->ir_iovec  != NULL &&
	       req->ir_ops    != NULL &&
	       m0_fid_is_valid(file_to_fid(req->ir_file)) &&

	       ergo(ioreq_sm_state(req) == IRS_READING,
		    !tioreqht_htable_is_empty(&req->ir_nwxfer.
			    nxr_tioreqs_hash)) &&

	       ergo(ioreq_sm_state(req) == IRS_WRITING,
		    !tioreqht_htable_is_empty(&req->ir_nwxfer.
			    nxr_tioreqs_hash)) &&

	       ergo(ioreq_sm_state(req) == IRS_WRITE_COMPLETE,
		    req->ir_nwxfer.nxr_iofop_nr == 0) &&

	       m0_vec_count(&req->ir_ivec.iv_vec) > 0 &&

	       m0_forall(i, req->ir_ivec.iv_vec.v_nr - 1,
			 req->ir_ivec.iv_index[i] +
			 req->ir_ivec.iv_vec.v_count[i] <=
			 req->ir_ivec.iv_index[i+1]) &&

	       pargrp_iomap_invariant_nr(req) &&

	       nw_xfer_request_invariant(&req->ir_nwxfer);
}

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer)
{
	return
	       nw_xfer_request_bob_check(xfer) &&
	       xfer->nxr_state <= NXS_STATE_NR &&

	       ergo(xfer->nxr_state == NXS_INITIALIZED,
		    (xfer->nxr_rc == xfer->nxr_bytes) ==
		    (xfer->nxr_iofop_nr == 0)) &&

	       ergo(xfer->nxr_state == NXS_INFLIGHT,
		    !tioreqht_htable_is_empty(&xfer->nxr_tioreqs_hash)) &&

	       ergo(xfer->nxr_state == NXS_COMPLETE,
		    xfer->nxr_iofop_nr == 0) &&

	       m0_htable_forall(tioreqht, tioreq, &xfer->nxr_tioreqs_hash,
			       target_ioreq_invariant(tioreq));
}

static bool data_buf_invariant(const struct data_buf *db)
{
	return
	       db != NULL &&
	       data_buf_bob_check(db) &&
	       ergo(db->db_buf.b_addr != NULL, db->db_buf.b_nob > 0);
}

static bool data_buf_invariant_nr(const struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	struct m0_pdclust_layout *play;

	play = pdlayout_get(map->pi_ioreq);
	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {
			if (map->pi_databufs[row][col] != NULL &&
			    !data_buf_invariant(map->pi_databufs[row][col]))
				return false;
		}
	}

	if (map->pi_paritybufs != NULL) {
		for (row = 0; row < parity_row_nr(play); ++row) {
			for (col = 0; col < parity_col_nr(play); ++col) {
				if (map->pi_paritybufs[row][col] != NULL &&
				    !data_buf_invariant(map->pi_paritybufs
				    [row][col]))
					return false;
			}
		}
	}
	return true;
}

static bool io_req_fop_invariant(const struct io_req_fop *fop)
{
	return
		_0C(io_req_fop_bob_check(fop)) &&
		_0C(fop->irf_tioreq      != NULL) &&
		_0C(fop->irf_ast.sa_cb   != NULL) &&
		_0C(fop->irf_ast.sa_mach != NULL);
}

static bool target_ioreq_invariant(const struct target_ioreq *ti)
{
	return
		_0C(target_ioreq_bob_check(ti)) &&
		_0C(ti->ti_session       != NULL) &&
		_0C(ti->ti_nwxfer        != NULL) &&
		_0C(ti->ti_bufvec.ov_buf != NULL) &&
		_0C(m0_fid_is_valid(&ti->ti_fid)) &&
		m0_tl_forall(iofops, iofop, &ti->ti_iofops,
			     io_req_fop_invariant(iofop));
}

static bool pargrp_iomap_invariant(const struct pargrp_iomap *map)
{
	return
	       pargrp_iomap_bob_check(map) &&
	       map->pi_ops	!= NULL &&
	       map->pi_rtype	 < PIR_NR &&
	       map->pi_databufs != NULL &&
	       map->pi_ioreq	!= NULL &&
	       ergo(m0_vec_count(&map->pi_ivec.iv_vec) > 0 &&
		    map->pi_ivec.iv_vec.v_nr >= 2,
		    m0_forall(i, map->pi_ivec.iv_vec.v_nr - 1,
			      map->pi_ivec.iv_index[i] +
			      map->pi_ivec.iv_vec.v_count[i] <=
			      map->pi_ivec.iv_index[i+1])) &&
	       data_buf_invariant_nr(map);
}

static bool pargrp_iomap_invariant_nr(const struct io_request *req)
{
	return m0_forall(i, req->ir_iomap_nr,
			 pargrp_iomap_invariant(req->ir_iomaps[i]));
}

static void nw_xfer_request_init(struct nw_xfer_request *xfer)
{
	struct io_request        *req;
	struct m0_pdclust_layout *play;

	M0_ENTRY("nw_xfer_request : %p", xfer);
	M0_PRE(xfer != NULL);

	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
	nw_xfer_request_bob_init(xfer);
	xfer->nxr_rc	= 0;
	xfer->nxr_bytes = 0;
	xfer->nxr_iofop_nr = 0;
	xfer->nxr_state = NXS_INITIALIZED;
	xfer->nxr_ops	= &xfer_ops;

	play = pdlayout_get(req);
	xfer->nxr_rc = tioreqht_htable_init(&xfer->nxr_tioreqs_hash,
				layout_n(play) + 2 * layout_k(play));

	M0_POST_EX(nw_xfer_request_invariant(xfer));
	M0_LEAVE();
}

static void nw_xfer_request_fini(struct nw_xfer_request *xfer)
{
	M0_ENTRY("nw_xfer_request : %p", xfer);
	M0_PRE(xfer != NULL && xfer->nxr_state == NXS_COMPLETE);
	M0_PRE_EX(nw_xfer_request_invariant(xfer));

	xfer->nxr_ops = NULL;
	nw_xfer_request_bob_fini(xfer);
	tioreqht_htable_fini(&xfer->nxr_tioreqs_hash);
	M0_LEAVE();
}

/* Typically used while copying data to/from user space. */
static bool page_copy_filter(m0_bindex_t start, m0_bindex_t end,
			     enum page_attr filter)
{
	M0_PRE(end - start <= PAGE_CACHE_SIZE);
	M0_PRE(ergo(filter != PA_NONE,
		    filter & (PA_FULLPAGE_MODIFY | PA_PARTPAGE_MODIFY)));

	if (filter & PA_FULLPAGE_MODIFY) {
		return (end - start == PAGE_CACHE_SIZE);
	} else if (filter & PA_PARTPAGE_MODIFY) {
		return (end - start <  PAGE_CACHE_SIZE);
	} else
		return true;
}

static int user_data_copy(struct pargrp_iomap *map,
			  m0_bindex_t	       start,
			  m0_bindex_t	       end,
			  struct iov_iter     *it,
			  enum copy_direction  dir,
			  enum page_attr       filter)
{
	/*
	 * iov_iter should be able to be used with copy_to_user() as well
	 * since it is as good as a vector cursor.
	 * Present kernel 2.6.32 has no support for such requirement.
	 */
	uint64_t		  bytes;
	uint32_t		  row;
	uint32_t		  col;
	struct page		 *page;
	struct m0_pdclust_layout *play;

	M0_ENTRY("Copy %s user-space, start = %llu, end = %llu",
		 dir == CD_COPY_FROM_USER ? (char *)"from" : (char *)"to",
		 start, end);
	M0_PRE_EX(pargrp_iomap_invariant(map));
	M0_PRE(it != NULL);
	M0_PRE(M0_IN(dir, (CD_COPY_FROM_USER, CD_COPY_TO_USER)));

	/* Finds out the page from pargrp_iomap::pi_databufs. */
	play = pdlayout_get(map->pi_ioreq);
	page_pos_get(map, start, &row, &col);
	M0_ASSERT(map->pi_databufs[row][col] != NULL);
	page = virt_to_page(map->pi_databufs[row][col]->db_buf.b_addr);

	if (dir == CD_COPY_FROM_USER) {
		if (page_copy_filter(start, end, filter)) {
			M0_ASSERT(ergo(filter != PA_NONE,
				       map->pi_databufs[row][col]->db_flags &
				       filter));

			if (map->pi_databufs[row][col]->db_flags &
			    PA_COPY_FRMUSR_DONE)
				return M0_RC(0);

			/*
			 * Copies page to auxiliary buffer before it gets
			 * overwritten by user data. This is needed in order
			 * to calculate delta parity in case of read-old
			 * approach.
			 */
			if (map->pi_databufs[row][col]->db_auxbuf.b_addr !=
			    NULL && map->pi_rtype == PIR_READOLD) {
				if (filter == 0)
					memcpy(map->pi_databufs[row][col]->
					       db_auxbuf.b_addr,
					       map->pi_databufs[row][col]->
					       db_buf.b_addr,
					       PAGE_CACHE_SIZE);
				else
					return M0_RC(0);
			}

			pagefault_disable();
			/* Copies to appropriate offset within page. */
			bytes = iov_iter_copy_from_user_atomic(page, it,
					start & (PAGE_CACHE_SIZE - 1),
					end - start);
			pagefault_enable();

			M0_LOG(M0_DEBUG, "%llu bytes copied from user-space "
					 "from offset %llu", bytes, start);

			map->pi_ioreq->ir_copied_nr += bytes;
			map->pi_databufs[row][col]->db_flags |=
				PA_COPY_FRMUSR_DONE;

			if (bytes != end - start)
				return M0_ERR(-EFAULT, "Failed to "
					       "copy_from_user");
		}
	} else {
		bytes = copy_to_user(it->iov->iov_base + it->iov_offset,
				     map->pi_databufs[row][col]->
				     db_buf.b_addr +
				     (start & (PAGE_CACHE_SIZE - 1)),
				     end - start);

		map->pi_ioreq->ir_copied_nr += end - start - bytes;

		M0_LOG(M0_DEBUG, "%llu bytes copied to user-space from offset "
				 "%llu", end - start - bytes, start);

		if (bytes != 0)
			return M0_ERR(-EFAULT, "Failed to copy_to_user");
	}

	return M0_RC(0);
}

static int pargrp_iomap_parity_recalc(struct pargrp_iomap *map)
{
	int			  rc;
	uint32_t		  row;
	uint32_t		  col;
	struct m0_buf		 *dbufs;
	struct m0_buf		 *pbufs;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map = %p", map);
	M0_PRE_EX(pargrp_iomap_invariant(map));

	play = pdlayout_get(map->pi_ioreq);
	M0_ALLOC_ARR_ADDB(dbufs, layout_n(play), &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_PARITY_RECALC_DBUFS,
	                  &m0t1fs_addb_ctx);
	M0_ALLOC_ARR_ADDB(pbufs, layout_k(play), &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_PARITY_RECALC_PBUFS,
	                  &m0t1fs_addb_ctx);

	if (dbufs == NULL || pbufs == NULL) {
		rc = -ENOMEM;
		goto last;
	}

	if ((map->pi_ioreq->ir_type == IRT_WRITE && map->pi_rtype == PIR_NONE)
	    || map->pi_rtype == PIR_READREST) {

		unsigned long zpage;

		zpage = get_zeroed_page(GFP_KERNEL);
		if (zpage == 0) {
			rc = -ENOMEM;
			goto last;
		}

		for (row = 0; row < data_row_nr(play); ++row) {
			for (col = 0; col < data_col_nr(play); ++col)
				if (map->pi_databufs[row][col] != NULL) {
					dbufs[col] = map->pi_databufs
						     [row][col]->db_buf;
				} else {
					dbufs[col].b_addr = (void *)zpage;
					dbufs[col].b_nob  = PAGE_CACHE_SIZE;
				}

			for (col = 0; col < layout_k(play); ++col)
				pbufs[col] = map->pi_paritybufs[row][col]->
					     db_buf;

			m0_parity_math_calculate(parity_math(map->pi_ioreq),
						 dbufs, pbufs);
		}
		rc = 0;
		free_page(zpage);
		M0_LOG(M0_DEBUG, "Parity recalculated for %s",
		       map->pi_rtype == PIR_READREST ? "read-rest" :
		       "aligned write");

	} else {
		struct m0_buf *old;

		M0_ALLOC_ARR_ADDB(old, layout_n(play), &m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_PARITY_RECALC_OLD_BUFS,
		                  &m0t1fs_addb_ctx);

		if (old == NULL) {
			rc = -ENOMEM;
			goto last;
		}

		for (row = 0; row < data_row_nr(play); ++row) {
			for (col = 0; col < layout_k(play); ++col)
				pbufs[col] = map->pi_paritybufs[row][col]->
					db_buf;

			for (col = 0; col < data_col_nr(play); ++col) {
				if (map->pi_databufs[row][col] == NULL)
					continue;

				dbufs[col] = map->pi_databufs[row][col]->db_buf;
				old[col]   = map->pi_databufs[row][col]->
					db_auxbuf;

				m0_parity_math_diff(parity_math(map->pi_ioreq),
						    old, dbufs, pbufs, col);
			}

		}
		m0_free(old);
		rc = 0;
	}
last:
	m0_free(dbufs);
	m0_free(pbufs);
	return M0_RC(rc);
}

static int ioreq_parity_recalc(struct io_request *req)
{
	int	 rc;
	uint64_t map;

	M0_ENTRY("io_request : %p", req);
	M0_PRE_EX(io_request_invariant(req));

	for (map = 0; map < req->ir_iomap_nr; ++map) {
		rc = req->ir_iomaps[map]->pi_ops->pi_parity_recalc(req->
				ir_iomaps[map]);
		if (rc != 0)
			return M0_ERR(rc, "Parity recalc failed for "
				       "grpid : %llu",
				       req->ir_iomaps[map]->pi_grpid);
	}
	return M0_RC(0);
}

/* Finds out pargrp_iomap from array of such structures in io_request. */
static void ioreq_pgiomap_find(struct io_request    *req,
		               uint64_t              grpid,
			       uint64_t             *cursor,
			       struct pargrp_iomap **out)
{
	uint64_t id;

	M0_ENTRY("group_id = %llu, cursor = %llu", grpid, *cursor);
	M0_PRE(req    != NULL);
	M0_PRE(out    != NULL);
	M0_PRE(cursor != NULL);
	M0_PRE(*cursor < req->ir_iomap_nr);

	for (id = *cursor; id < req->ir_iomap_nr; ++id)
		if (req->ir_iomaps[id]->pi_grpid == grpid) {
			*out = req->ir_iomaps[id];
			*cursor = id;
			break;
		}

	M0_POST(id < req->ir_iomap_nr);
	M0_LEAVE();
}

static int ioreq_user_data_copy(struct io_request   *req,
				enum copy_direction  dir,
				enum page_attr	     filter)
{
	int			  rc;
	uint64_t		  map;
	m0_bindex_t		  grpstart;
	m0_bindex_t		  grpend;
	m0_bindex_t		  pgstart;
	m0_bindex_t		  pgend;
	m0_bcount_t		  count;
	struct iov_iter		  it;
	struct m0_ivec_cursor	  srccur;
	struct m0_pdclust_layout *play;

	M0_ENTRY("io_request : %p, %s user-space. filter = 0x%x", req,
		 dir == CD_COPY_FROM_USER ? (char *)"from" : (char *)"to",
		 filter);
	M0_PRE_EX(io_request_invariant(req));
	M0_PRE(dir < CD_NR);

	iov_iter_init(&it, req->ir_iovec, req->ir_ivec.iv_vec.v_nr,
		      m0_vec_count(&req->ir_ivec.iv_vec), 0);
	m0_ivec_cursor_init(&srccur, &req->ir_ivec);
	play = pdlayout_get(req);

	for (map = 0; map < req->ir_iomap_nr; ++map) {

		M0_ASSERT_EX(pargrp_iomap_invariant(req->ir_iomaps[map]));

		count    = 0;
		grpstart = data_size(play) * req->ir_iomaps[map]->pi_grpid;
		grpend	 = grpstart + data_size(play);

		while (!m0_ivec_cursor_move(&srccur, count) &&
		       m0_ivec_cursor_index(&srccur) < grpend) {

			pgstart	 = m0_ivec_cursor_index(&srccur);
			pgend = min64u(m0_round_up(pgstart + 1,
				       PAGE_CACHE_SIZE),
				       pgstart + m0_ivec_cursor_step(&srccur));
			count = pgend - pgstart;

			/*
			 * This takes care of finding correct page from
			 * current pargrp_iomap structure from pgstart
			 * and pgend.
			 */
			rc = user_data_copy(req->ir_iomaps[map], pgstart, pgend,
					    &it, dir, filter);
			if (rc != 0)
				return M0_ERR(rc, "Copy failed");

			iov_iter_advance(&it, count);
		}
	}

	return M0_RC(0);
}

static void indexvec_sort(struct m0_indexvec *ivec)
{
	uint32_t i;
	uint32_t j;

	M0_ENTRY("indexvec = %p", ivec);
	M0_PRE(ivec != NULL && !m0_vec_is_empty(&ivec->iv_vec));

	/*
	 * TODO Should be replaced by an efficient sorting algorithm,
	 * something like heapsort which is fairly inexpensive in kernel
	 * mode with the least worst case scenario.
	 * Existing heap sort from kernel code can not be used due to
	 * apparent disconnect between index vector and its associated
	 * count vector for same index.
	 */
	for (i = 0; i < SEG_NR(ivec); ++i) {
		for (j = i+1; j < SEG_NR(ivec); ++j) {
			if (INDEX(ivec, i) > INDEX(ivec, j)) {
				M0_SWAP(INDEX(ivec, i), INDEX(ivec, j));
				M0_SWAP(COUNT(ivec, i), COUNT(ivec, j));
			}
		}
	}
	M0_LEAVE();
}

static int pargrp_iomap_init(struct pargrp_iomap *map,
			     struct io_request	 *req,
			     uint64_t		  grpid)
{
	int			  rc;
	int			  pg;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map = %p, ioreq = %p, grpid = %llu", map, req, grpid);
	M0_PRE(map != NULL);
	M0_PRE(req != NULL);

	pargrp_iomap_bob_init(map);
	play		   = pdlayout_get(req);
	map->pi_ops	   = &iomap_ops;
	map->pi_rtype	   = PIR_NONE;
	map->pi_grpid	   = grpid;
	map->pi_ioreq	   = req;
	map->pi_state      = PI_HEALTHY;
	map->pi_paritybufs = NULL;

	rc = m0_indexvec_alloc(&map->pi_ivec, page_nr(data_size(play)),
	                       &m0t1fs_addb_ctx,
	                       M0T1FS_ADDB_LOC_IOMAP_INIT_IV);
	if (rc != 0)
		goto fail;

	/*
	 * This number is incremented only when a valid segment
	 * is added to the index vector.
	 */
	map->pi_ivec.iv_vec.v_nr = 0;

	M0_ALLOC_ARR_ADDB(map->pi_databufs, data_row_nr(play), &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_IOMAP_INIT_DBUFS_ROW,
	                  &m0t1fs_addb_ctx);
	if (map->pi_databufs == NULL)
		goto fail;

	for (pg = 0; pg < data_row_nr(play); ++pg) {
		M0_ALLOC_ARR_ADDB(map->pi_databufs[pg], layout_n(play),
		                  &m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_IOMAP_INIT_DBUFS_COL,
		                  &m0t1fs_addb_ctx);
		if (map->pi_databufs[pg] == NULL)
			goto fail;
	}

	if (req->ir_type == IRT_WRITE) {
		M0_ALLOC_ARR_ADDB(map->pi_paritybufs, parity_row_nr(play),
                                  &m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_IOMAP_INIT_PBUFS_ROW,
		                  &m0t1fs_addb_ctx);
		if (map->pi_paritybufs == NULL)
			goto fail;

		for (pg = 0; pg < parity_row_nr(play); ++pg) {
			M0_ALLOC_ARR_ADDB(map->pi_paritybufs[pg],
			                  parity_col_nr(play), &m0_addb_gmc,
			                  M0T1FS_ADDB_LOC_IOMAP_INIT_PBUFS_COL,
			                  &m0t1fs_addb_ctx);
			if (map->pi_paritybufs[pg] == NULL)
				goto fail;
		}
	}
	M0_POST_EX(pargrp_iomap_invariant(map));
	return M0_RC(0);

fail:
	m0_indexvec_free(&map->pi_ivec);

	if (map->pi_databufs != NULL) {
		for (pg = 0; pg < data_row_nr(play); ++pg)
			m0_free0(&map->pi_databufs[pg]);
		m0_free(map->pi_databufs);
	}
	if (map->pi_paritybufs != NULL) {
		for (pg = 0; pg < parity_row_nr(play); ++pg)
			m0_free0(&map->pi_paritybufs[pg]);
		m0_free(map->pi_paritybufs);
	}
	return M0_ERR(-ENOMEM, "Memory allocation failed");
}

static void pargrp_iomap_fini(struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map %p", map);
	M0_PRE_EX(pargrp_iomap_invariant(map));

	play	     = pdlayout_get(map->pi_ioreq);
	map->pi_ops  = NULL;
	map->pi_rtype = PIR_NONE;
	map->pi_state = PI_NONE;

	pargrp_iomap_bob_fini(map);
	m0_indexvec_free(&map->pi_ivec);

	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {
			if (map->pi_databufs[row][col] != NULL) {
				data_buf_dealloc_fini(map->
						pi_databufs[row][col]);
				map->pi_databufs[row][col] = NULL;
			}
		}
		m0_free0(&map->pi_databufs[row]);
	}

	if (map->pi_paritybufs != NULL) {
		for (row = 0; row < parity_row_nr(play); ++row) {
			for (col = 0; col < parity_col_nr(play); ++col) {
				if (map->pi_paritybufs[row][col] != NULL) {
					data_buf_dealloc_fini(map->
						pi_paritybufs[row][col]);
					map->pi_paritybufs[row][col] = NULL;
				}
			}
			m0_free0(&map->pi_paritybufs[row]);
		}
	}

	m0_free0(&map->pi_databufs);
	m0_free0(&map->pi_paritybufs);
	map->pi_ioreq = NULL;
	M0_LEAVE();
}

static bool pargrp_iomap_spans_seg(struct pargrp_iomap *map,
				   m0_bindex_t index, m0_bcount_t count)
{
	uint32_t seg;

	M0_PRE_EX(pargrp_iomap_invariant(map));

	for (seg = 0; seg < map->pi_ivec.iv_vec.v_nr; ++seg) {
		if (index >= INDEX(&map->pi_ivec, seg) &&
		    index + count <= seg_endpos(&map->pi_ivec, seg))
			return true;
	}
	return false;
}

static int pargrp_iomap_databuf_alloc(struct pargrp_iomap *map,
				      uint32_t		   row,
				      uint32_t		   col)
{
	M0_PRE(map != NULL);
	M0_PRE(map->pi_databufs[row][col] == NULL);

	M0_ENTRY("row %u col %u", row, col);
	map->pi_databufs[row][col] = data_buf_alloc_init(0);

	return map->pi_databufs[row][col] == NULL ?  -ENOMEM : 0;
}

/* Allocates data_buf structures as needed and populates the buffer flags. */
static int pargrp_iomap_seg_process(struct pargrp_iomap *map,
				    uint64_t		 seg,
				    bool		 rmw)
{
	int			  rc;
	bool			  ret;
	uint32_t		  row;
	uint32_t		  col;
	uint64_t		  count = 0;
	m0_bindex_t		  start;
	m0_bindex_t		  end;
	struct inode		 *inode;
	struct data_buf		 *dbuf;
	struct m0_ivec_cursor	  cur;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map %p, seg %llu, %s", map, seg, rmw ? "rmw" : "aligned");
	play  = pdlayout_get(map->pi_ioreq);
	inode = map->pi_ioreq->ir_file->f_dentry->d_inode;
	m0_ivec_cursor_init(&cur, &map->pi_ivec);
	ret = m0_ivec_cursor_move_to(&cur, map->pi_ivec.iv_index[seg]);
	M0_ASSERT(!ret);

	while (!m0_ivec_cursor_move(&cur, count)) {

		start = m0_ivec_cursor_index(&cur);
		end   = min64u(m0_round_up(start + 1, PAGE_CACHE_SIZE),
			       start + m0_ivec_cursor_step(&cur));
		count = end - start;

		page_pos_get(map, start, &row, &col);

		rc = pargrp_iomap_databuf_alloc(map, row, col);
		if (rc != 0)
			goto err;

		dbuf = map->pi_databufs[row][col];

		if (map->pi_ioreq->ir_type == IRT_WRITE) {
			dbuf->db_flags |= PA_WRITE;

			dbuf->db_flags |= count == PAGE_CACHE_SIZE ?
				PA_FULLPAGE_MODIFY : PA_PARTPAGE_MODIFY;

			/*
			 * Even if PA_PARTPAGE_MODIFY flag is set in
			 * this buffer, the auxiliary buffer can not be
			 * allocated until ::pi_rtype is selected.
			 */
			if (rmw && dbuf->db_flags & PA_PARTPAGE_MODIFY &&
			    (end < inode->i_size ||
			     (inode->i_size > 0 &&
			      page_id(end - 1) == page_id(inode->i_size - 1))))
				dbuf->db_flags |= PA_READ;
		} else
			/*
			 * For read IO requests, file_aio_read() has already
			 * delimited the index vector to EOF boundary.
			 */
			dbuf->db_flags |= PA_READ;

		M0_LOG(M0_DEBUG, "start %llu count %llu row %u col %u flag %x",
				start, count, row, col, dbuf->db_flags);
	}

	return M0_RC(0);
err:
	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {
			if (map->pi_databufs[row][col] != NULL) {
				data_buf_dealloc_fini(map->pi_databufs
						      [row][col]);
				map->pi_databufs[row][col] = NULL;
			}
		}
	}
	return M0_ERR(rc, "databuf_alloc failed");
}

static uint64_t pargrp_iomap_fullpages_count(struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	uint64_t		  nr = 0;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map %p", map);
	M0_PRE_EX(pargrp_iomap_invariant(map));

	play = pdlayout_get(map->pi_ioreq);

	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {

			if (map->pi_databufs[row][col] &&
			    map->pi_databufs[row][col]->db_flags &
			    PA_FULLPAGE_MODIFY)
				++nr;
		}
	}
	M0_LEAVE();
	return nr;
}

static uint64_t pargrp_iomap_auxbuf_alloc(struct pargrp_iomap *map,
					  uint32_t	       row,
					  uint32_t	       col)
{
	M0_ENTRY();
	M0_PRE_EX(pargrp_iomap_invariant(map));
	M0_PRE(map->pi_rtype == PIR_READOLD);

	map->pi_databufs[row][col]->db_auxbuf.b_addr = (void *)
		get_zeroed_page(GFP_KERNEL);

	if (map->pi_databufs[row][col]->db_auxbuf.b_addr == NULL)
		return -ENOMEM;
	++iommstats.a_page_nr;
	map->pi_databufs[row][col]->db_auxbuf.b_nob = PAGE_CACHE_SIZE;

	return M0_RC(0);
}

/*
 * Allocates auxiliary buffer for data_buf structures in
 * pargrp_iomap structure.
 */
static int pargrp_iomap_readold_auxbuf_alloc(struct pargrp_iomap *map)
{
	int			  rc = 0;
	uint64_t		  start;
	uint64_t		  end;
	uint64_t		  count = 0;
	uint32_t		  row;
	uint32_t		  col;
	struct inode             *inode;
	struct m0_ivec_cursor	  cur;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map %p", map);
	M0_PRE_EX(pargrp_iomap_invariant(map));
	M0_PRE(map->pi_rtype == PIR_READOLD);

	inode = map->pi_ioreq->ir_file->f_dentry->d_inode;
	play  = pdlayout_get(map->pi_ioreq);
	m0_ivec_cursor_init(&cur, &map->pi_ivec);

	while (!m0_ivec_cursor_move(&cur, count)) {
		start = m0_ivec_cursor_index(&cur);
		end   = min64u(m0_round_up(start + 1, PAGE_CACHE_SIZE),
			       start + m0_ivec_cursor_step(&cur));
		count = end - start;
		page_pos_get(map, start, &row, &col);

		if (map->pi_databufs[row][col] != NULL) {
			/*
			 * In Readold approach, all valid pages have to
			 * be read regardless of whether they are fully
			 * occupied or partially occupied.
			 * This is needed in order to calculate correct
			 * parity in differential manner.
			 * Also, read flag should be set only for pages
			 * which lie within end-of-file boundary.
			 */
			if (end < inode->i_size ||
			    (inode->i_size > 0 &&
			     page_id(end - 1) == page_id(inode->i_size - 1)))
				map->pi_databufs[row][col]->db_flags |=
					PA_READ;

			rc = pargrp_iomap_auxbuf_alloc(map, row, col);
			if (rc != 0)
				return M0_ERR(rc, "auxbuf_alloc failed");
		}
	}
	return M0_RC(rc);
}

/*
 * A read request from rmw IO request can lead to either
 *
 * read_old - Read the old data for the extent spanned by current
 * IO request, along with the old parity unit. This approach needs
 * to calculate new parity in _iterative_ manner. This approach is
 * selected only if current IO extent lies within file size.
 *
 * read_rest - Read rest of the parity group, which is _not_ spanned
 * by current IO request, so that data for whole parity group can
 * be availble for parity calculation.
 * This approach reads the extent from start of parity group to the
 * point where a page is completely spanned by incoming IO request.
 *
 * Typically, the approach which leads to least size of data to be
 * read and written from server is selected.
 *
 *   N = 5, P = 1, K = 1, unit_size = 4k
 *   F	=> Fully occupied
 *   P' => Partially occupied
 *   #	=> Parity unit
 *   *	=> Spare unit
 *   x	=> Start of actual file extent.
 *   y	=> End of actual file extent.
 *   a	=> Rounded down value of x.
 *   b	=> Rounded up value of y.
 *
 *  Read-rest approach
 *
 *   a	   x
 *   +---+---+---+---+---+---+---+
 *   |	 | P'| F | F | F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | P'|	 | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *		   y	 b
 *
 *  Read-old approach
 *
 *   a	   x
 *   +---+---+---+---+---+---+---+
 *   |	 |   |	 | P'| F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | P'|	 |   |	 | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *		   y	 b
 *
 */
static int pargrp_iomap_readrest(struct pargrp_iomap *map)
{
	int			  rc;
	uint32_t		  row;
	uint32_t		  col;
	uint32_t		  seg;
	uint32_t		  seg_nr;
	m0_bindex_t		  grpstart;
	m0_bindex_t		  grpend;
	m0_bindex_t		  start;
	m0_bindex_t		  end;
	m0_bcount_t               count = 0;
	struct inode             *inode;
	struct m0_indexvec	 *ivec;
	struct m0_ivec_cursor	  cur;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map %p", map);
	M0_PRE_EX(pargrp_iomap_invariant(map));
	M0_PRE(map->pi_rtype == PIR_READREST);

	play	 = pdlayout_get(map->pi_ioreq);
	ivec	 = &map->pi_ivec;
	seg_nr	 = map->pi_ivec.iv_vec.v_nr;
	grpstart = data_size(play) * map->pi_grpid;
	grpend	 = grpstart + data_size(play);

	/* Extends first segment to align with start of parity group. */
	COUNT(ivec, 0) += (INDEX(ivec, 0) - grpstart);
	INDEX(ivec, 0)	= grpstart;

	/* Extends last segment to align with end of parity group. */
	COUNT(ivec, seg_nr - 1) = grpend - INDEX(ivec, seg_nr - 1);

	/*
	 * All io extents _not_ spanned by pargrp_iomap::pi_ivec
	 * need to be included so that _all_ pages from parity group
	 * are available to do IO.
	 */
	for (seg = 1; seg_nr > 2 && seg <= seg_nr - 2; ++seg) {
		if (seg_endpos(ivec, seg) < INDEX(ivec, seg + 1))
			COUNT(ivec, seg) += INDEX(ivec, seg + 1) -
					    seg_endpos(ivec, seg);
	}

	inode = file_to_inode(map->pi_ioreq->ir_file);
	m0_ivec_cursor_init(&cur, &map->pi_ivec);

	while (!m0_ivec_cursor_move(&cur, count)) {

		start = m0_ivec_cursor_index(&cur);
		end   = min64u(m0_round_up(start + 1, PAGE_CACHE_SIZE),
			       start + m0_ivec_cursor_step(&cur));
		count = end - start;
		page_pos_get(map, start, &row, &col);

		if (map->pi_databufs[row][col] == NULL) {
			rc = pargrp_iomap_databuf_alloc(map, row, col);
			if (rc != 0)
				return M0_ERR(rc, "databuf_alloc failed");

			if (end <= inode->i_size || (inode->i_size > 0 &&
			    page_id(end - 1) == page_id(inode->i_size - 1)))
				map->pi_databufs[row][col]->db_flags |=
					PA_READ;
		}
	}

	return M0_RC(0);
}

static int pargrp_iomap_paritybufs_alloc(struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	struct m0_pdclust_layout *play;

	M0_ENTRY("map %p", map);
	M0_PRE_EX(pargrp_iomap_invariant(map));

	play = pdlayout_get(map->pi_ioreq);
	for (row = 0; row < parity_row_nr(play); ++row) {
		for (col = 0; col < parity_col_nr(play); ++col) {

			map->pi_paritybufs[row][col] = data_buf_alloc_init(0);
			if (map->pi_paritybufs[row][col] == NULL)
				goto err;

			map->pi_paritybufs[row][col]->db_flags |= PA_WRITE;

			if (map->pi_rtype == PIR_READOLD &&
			    file_to_inode(map->pi_ioreq->ir_file)->i_size >
			    data_size(play) * map->pi_grpid)
				map->pi_paritybufs[row][col]->db_flags |=
					PA_READ;
		}
	}
	return M0_RC(0);
err:
	for (row = 0; row < parity_row_nr(play); ++row) {
		for (col = 0; col < parity_col_nr(play); ++col)
			m0_free0(&map->pi_paritybufs[row][col]);
	}
	return M0_ERR(-ENOMEM, "Memory allocation failed for data_buf.");
}

static m0_bcount_t seg_collate(struct pargrp_iomap   *map,
			       struct m0_ivec_cursor *cursor)
{
	uint32_t                  seg;
	uint32_t                  cnt;
	m0_bindex_t               start;
	m0_bindex_t               grpend;
	m0_bcount_t               segcount;
	struct m0_pdclust_layout *play;

	M0_PRE(map    != NULL);
	M0_PRE(cursor != NULL);

	cnt    = 0;
	play   = pdlayout_get(map->pi_ioreq);
	grpend = map->pi_grpid * data_size(play) + data_size(play);
	start  = m0_ivec_cursor_index(cursor);

	for (seg = cursor->ic_cur.vc_seg; start < grpend &&
	     seg < cursor->ic_cur.vc_vec->v_nr - 1; ++seg) {

		segcount = seg == cursor->ic_cur.vc_seg ?
			 m0_ivec_cursor_step(cursor) :
			 cursor->ic_cur.vc_vec->v_count[seg];

		if (start + segcount ==
		    map->pi_ioreq->ir_ivec.iv_index[seg + 1]) {

			if (start + segcount >= grpend) {
				start = grpend;
				break;
			}
			start += segcount;
		} else
			break;
		++cnt;
	}

	if (cnt == 0)
		return 0;

	/* If this was last segment in vector, add its count too. */
	if (seg == cursor->ic_cur.vc_vec->v_nr - 1) {
		if (start + cursor->ic_cur.vc_vec->v_count[seg] >= grpend)
			start = grpend;
		else
			start += cursor->ic_cur.vc_vec->v_count[seg];
	}

	return start - m0_ivec_cursor_index(cursor);
}

static int pargrp_iomap_populate(struct pargrp_iomap	  *map,
				 const struct m0_indexvec *ivec,
				 struct m0_ivec_cursor	  *cursor)
{
	int			  rc;
	bool			  rmw;
	uint64_t		  seg;
	uint64_t		  size = 0;
	uint64_t		  grpsize;
	m0_bcount_t		  count = 0;
	m0_bindex_t               endpos = 0;
	m0_bcount_t               segcount = 0;
	/* Number of pages _completely_ spanned by incoming io vector. */
	uint64_t		  nr = 0;
	/* Number of pages to be read + written for read-old approach. */
	uint64_t		  ro_page_nr;
	/* Number of pages to be read + written for read-rest approach. */
	uint64_t		  rr_page_nr;
	m0_bindex_t		  grpstart;
	m0_bindex_t		  grpend;
	m0_bindex_t		  currindex;
	struct m0_pdclust_layout *play;
	struct inode		 *inode;

	M0_ENTRY("map %p, indexvec %p", map, ivec);
	M0_PRE(map  != NULL);
	M0_PRE(ivec != NULL);

	play	 = pdlayout_get(map->pi_ioreq);
	grpsize	 = data_size(play);
	grpstart = grpsize * map->pi_grpid;
	grpend	 = grpstart + grpsize;
	for (seg = cursor->ic_cur.vc_seg; seg < SEG_NR(ivec) &&
	     INDEX(ivec, seg) < grpend; ++seg) {
		currindex = seg == cursor->ic_cur.vc_seg ?
			    m0_ivec_cursor_index(cursor) : INDEX(ivec, seg);
		size += min64u(seg_endpos(ivec, seg), grpend) - currindex;
	}
	inode = map->pi_ioreq->ir_file->f_dentry->d_inode;
	rmw = size < grpsize && map->pi_ioreq->ir_type == IRT_WRITE &&
	 grpstart < inode->i_size;
	M0_LOG(M0_INFO, "Group id %llu is %s", map->pi_grpid,
	       rmw ? "rmw" : "aligned");

	size = map->pi_ioreq->ir_file->f_dentry->d_inode->i_size;

	for (seg = 0; !m0_ivec_cursor_move(cursor, count) &&
	     m0_ivec_cursor_index(cursor) < grpend;) {
		/*
		 * Skips the current segment if it is completely spanned by
		 * rounding up/down of earlier segment.
		 */
		if (map->pi_ops->pi_spans_seg(map, m0_ivec_cursor_index(cursor),
					      m0_ivec_cursor_step(cursor))) {
			count = m0_ivec_cursor_step(cursor);
			continue;
		}

		INDEX(&map->pi_ivec, seg) = m0_ivec_cursor_index(cursor);
		endpos = min64u(grpend, m0_ivec_cursor_index(cursor) +
				m0_ivec_cursor_step(cursor));

		segcount = seg_collate(map, cursor);
		if (segcount > 0)
			endpos = INDEX(&map->pi_ivec, seg) + segcount;

		COUNT(&map->pi_ivec, seg) = endpos - INDEX(&map->pi_ivec, seg);

		/* For read IO request, IO should not go beyond EOF. */
		if (map->pi_ioreq->ir_type == IRT_READ &&
		    seg_endpos(&map->pi_ivec, seg) > size) {
			if (INDEX(&map->pi_ivec, seg) + 1 < size)
				COUNT(&map->pi_ivec, seg) = size -
					INDEX(&map->pi_ivec, seg);
			else
				COUNT(&map->pi_ivec, seg) = 0;
			if (COUNT(&map->pi_ivec, seg) == 0) {
				count = m0_ivec_cursor_step(cursor);
				continue;
			}
		}

		/*
		 * If current segment is _partially_ spanned by previous
		 * segment in pargrp_iomp::pi_ivec, start of segment is
		 * rounded up to move to next page.
		 */
		if (seg > 0 && INDEX(&map->pi_ivec, seg) <
		    seg_endpos(&map->pi_ivec, seg - 1)) {
			m0_bindex_t newindex;

			newindex = m0_round_up(INDEX(&map->pi_ivec, seg) + 1,
					       PAGE_CACHE_SIZE);
			COUNT(&map->pi_ivec, seg) -= (newindex -
					INDEX(&map->pi_ivec, seg));

			INDEX(&map->pi_ivec, seg)  = newindex;
		}

		++map->pi_ivec.iv_vec.v_nr;
		rc = map->pi_ops->pi_seg_process(map, seg, rmw);
		if (rc != 0)
			return M0_ERR(rc, "seg_process failed");

		INDEX(&map->pi_ivec, seg) =
			round_down(INDEX(&map->pi_ivec, seg),
				   PAGE_CACHE_SIZE);

		COUNT(&map->pi_ivec, seg) =
			round_up(endpos, PAGE_CACHE_SIZE) -
			INDEX(&map->pi_ivec, seg);

		M0_LOG(M0_DEBUG, "seg %llu index %llu count %llu", seg,
				INDEX(&map->pi_ivec, seg),
				COUNT(&map->pi_ivec, seg));

		count = endpos - m0_ivec_cursor_index(cursor);
		++seg;
	}

	/*
	 * Decides whether to undertake read-old approach or read-rest for
	 * an rmw IO request.
	 * By default, the segments in index vector pargrp_iomap::pi_ivec
	 * are suitable for read-old approach.
	 * Hence the index vector is changed only if read-rest approach
	 * is selected.
	 */
	if (rmw) {
		nr = map->pi_ops->pi_fullpages_find(map);

		/*
		 * Can use number of data_buf structures instead of using
		 * indexvec_page_nr().
		 */
		ro_page_nr = /* Number of pages to be read.
			      @todo: 'nr' can not be directly eliminated. If
			      page contents are within_eof, then even if
			      they are fully modified, older contents need
			      to be read in read-old approach. If fully
			      modified pages are not within_eof, then they
			      need not be read.*/
			     indexvec_page_nr(&map->pi_ivec.iv_vec) - nr +
			     parity_units_page_nr(play) +
			     /* Number of pages to be written. */
			     indexvec_page_nr(&map->pi_ivec.iv_vec) +
			     parity_units_page_nr(play);

		rr_page_nr = /* Number of pages to be read. */
			     page_nr(grpend - grpstart) - nr +
			     /* Number of pages to be written. */
			     indexvec_page_nr(&map->pi_ivec.iv_vec) +
			     parity_units_page_nr(play);

		if (rr_page_nr < ro_page_nr) {
			M0_LOG(M0_INFO,"Read-rest approach selected");
			map->pi_rtype = PIR_READREST;
			rc = map->pi_ops->pi_readrest(map);
			if (rc != 0)
				return M0_ERR(rc, "readrest failed");
		} else {
			M0_LOG(M0_INFO,"Read-old approach selected");
			map->pi_rtype = PIR_READOLD;
			rc = map->pi_ops->pi_readold_auxbuf_alloc(map);
		}
	}

	if (map->pi_ioreq->ir_type == IRT_WRITE)
		rc = pargrp_iomap_paritybufs_alloc(map);

	M0_POST_EX(ergo(rc == 0, pargrp_iomap_invariant(map)));

	return M0_RC(rc);
}

static int pargrp_iomap_pages_mark(struct pargrp_iomap       *map,
		                   enum m0_pdclust_unit_type  type)
{
	int                         rc = 0;
	uint32_t                    row;
	uint32_t                    row_nr;
	uint32_t                    col;
	uint32_t                    col_nr;
	struct data_buf          ***bufs;
	struct m0_pdclust_layout   *play;

	M0_ENTRY();
	M0_PRE(map != NULL);
	M0_PRE(M0_IN(type, (M0_PUT_DATA, M0_PUT_SPARE)));

	play = pdlayout_get(map->pi_ioreq);

	if (type == M0_PUT_DATA) {
		M0_ASSERT(map->pi_databufs != NULL);
		row_nr = data_row_nr(play);
		col_nr = data_col_nr(play);
		bufs   = map->pi_databufs;
	} else {
		row_nr = parity_row_nr(play);
		col_nr = parity_col_nr(play);
		bufs   = map->pi_paritybufs;
	}

	/*
	 * Allocates data_buf structures from either ::pi_databufs
	 * or ::pi_paritybufs array.
	 * The loop traverses the matrix, column (unit) by column (unit).
	 */
	for (col = 0; col < col_nr; ++col) {
		for (row = 0; row < row_nr; ++row) {
			/*
			 * If the page is marked as PA_READ_FAILED, all
			 * other pages belonging to the unit same as
			 * the failed one, are also marked as PA_READ_FAILED,
			 * hence the loop breaks from here.
			 */
			if (bufs[row][col] != NULL &&
			    bufs[row][col]->db_flags & PA_READ_FAILED)
				break;
		}

		if (row == row_nr)
			continue;

		for (row = 0; row < row_nr; ++row) {
			if (bufs[row][col] == NULL) {
				bufs[row][col] = data_buf_alloc_init(0);
				if (bufs[row][col] == NULL) {
					rc = -ENOMEM;
					break;
				}
			}
			bufs[row][col]->db_flags |= PA_READ_FAILED;
		}
	}
	return M0_RC(rc);
}

static void page_update(struct pargrp_iomap *map, uint32_t row, uint32_t col,
			enum page_attr page_type)
{
	struct m0_pdclust_layout  *play;
	struct m0_pdclust_src_addr src;
	struct m0t1fs_sb          *csb;
	enum m0_pool_nd_state      state;
	uint32_t		   spare_slot;
	uint32_t		   spare_prev;
	int			   rc;

	M0_PRE(M0_IN(page_type,(PA_DATA, PA_PARITY)));
	M0_PRE(ergo(page_type == PA_DATA, map->pi_databufs[row][col] != NULL));
	M0_PRE(ergo(page_type == PA_PARITY,
		    map->pi_paritybufs[row][col] != NULL));

	csb = file_to_sb(map->pi_ioreq->ir_file);
	play = pdlayout_get(map->pi_ioreq);
	src.sa_group = map->pi_grpid;
	if (page_type == PA_DATA)
		src.sa_unit = col;
	else
		src.sa_unit = col + layout_n(play);

	rc = unit_state(&src, map->pi_ioreq, &state);
	M0_ASSERT(rc == 0);
	if (state == M0_PNDS_SNS_REPAIRED) {
		rc = io_spare_map(map, &src, &spare_slot, &spare_prev,
				  &state);
		M0_ASSERT(rc == 0);
	}
	if (M0_IN(state, (M0_PNDS_FAILED, M0_PNDS_OFFLINE,
			  M0_PNDS_SNS_REPAIRING))) {
		if (page_type == PA_DATA)
			map->pi_databufs[row][col]->db_flags |=
				PA_READ_FAILED;
		else
			map->pi_paritybufs[row][col]->db_flags |=
				PA_READ_FAILED;
	}
}

static int pargrp_iomap_dgmode_process(struct pargrp_iomap *map,
		                       struct target_ioreq *tio,
		                       m0_bindex_t         *index,
			               uint32_t             count)
{
	int                        rc = 0;
	uint32_t                   row;
	uint32_t                   col;
	uint32_t                   tgt_id;
	m0_bindex_t                goff;
	struct m0_layout_enum     *le;
	struct m0_pdclust_layout  *play;
	struct m0_pdclust_src_addr src;
	struct m0_pdclust_tgt_addr tgt;
	enum m0_pool_nd_state	   dev_state;
	struct m0t1fs_sb	  *msb;
	uint32_t		   spare_slot;
	uint32_t		   spare_slot_prev;

	M0_PRE_EX(pargrp_iomap_invariant(map));
	M0_ENTRY("grpid = %llu, count = %u\n", map->pi_grpid, count);
	M0_PRE(tio   != NULL);
	M0_PRE(index != NULL);
	M0_PRE(count >  0);

	/*
	 * Finds out the id of target object to which failed IO fop
	 * was sent.
	 */
	le = m0_layout_instance_to_enum(file_to_m0inode(map->pi_ioreq->
					ir_file)->ci_layout_instance);
	tgt_id = m0_layout_enum_find(le, file_to_fid(map->pi_ioreq->ir_file),
			             &tio->ti_fid);
	msb = file_to_sb(map->pi_ioreq->ir_file);
	rc = m0_poolmach_device_state(msb->csb_pool.po_mach,
				      tio->ti_fid.f_container, &dev_state);
	play = pdlayout_get(map->pi_ioreq);
	tgt.ta_frame = index[0] / layout_unit_size(play);
	tgt.ta_obj = tgt_id;

	/*
	 * Finds out reverse mapping of layout and gives
	 * the source object.
	 */
	m0_pdclust_instance_inv(pdlayout_instance(
				layout_instance(map->pi_ioreq)),
				&tgt, &src);
	M0_ASSERT(src.sa_group == map->pi_grpid);
	M0_ASSERT(src.sa_unit  <  layout_n(play) + layout_k(play));
	if (dev_state == M0_PNDS_SNS_REPAIRED) {
		rc = io_spare_map(map, &src, &spare_slot, &spare_slot_prev,
			          &dev_state);
		M0_ASSERT(rc == 0);
		if (dev_state == M0_PNDS_SNS_REPAIRED)
			return rc;
	}
	map->pi_state = PI_DEGRADED;
	++map->pi_ioreq->ir_dgmap_nr;
	/* Segment belongs to a data unit. */
	if (src.sa_unit < layout_n(play)) {
		goff = gfile_offset(index[0], map, play, &src);
		page_pos_get(map, goff, &row, &col);
		M0_ASSERT(map->pi_databufs[row][col] != NULL);
		map->pi_databufs[row][col]->db_flags |= PA_READ_FAILED;
	} else {
		/* Segment belongs to a parity unit. */
		M0_ASSERT(map->pi_paritybufs[page_nr(index[0]) %
				page_nr(layout_unit_size(play))]
				[src.sa_unit - layout_n(play)] != NULL);
		map->pi_paritybufs[(page_nr(index[0]) %
				   page_nr(layout_unit_size(play)))]
			[src.sa_unit - layout_n(play)]->db_flags |=
			PA_READ_FAILED;
	}
	/*
	 * Since m0_parity_math_recover() API will recover one or more
	 * _whole_ units, all pages from a failed unit can be marked as
	 * PA_READ_FAILED. These pages need not be read again.
	 */
	rc = pargrp_iomap_pages_mark(map, M0_PUT_DATA);
	if (rc != 0)
		return M0_ERR(rc, "Failed to mark pages from parity group");

	/*
	 * If parity buffers are not allocated, they should be allocated
	 * since they are needed for recovering lost data.
	 */
	if (map->pi_paritybufs == NULL) {
		M0_ALLOC_ARR_ADDB(map->pi_paritybufs, parity_row_nr(play),
				  &m0_addb_gmc,
				  M0T1FS_ADDB_LOC_DGMODE_PROCESS_1,
				  &m0t1fs_addb_ctx);
		if (map->pi_paritybufs == NULL)
			return M0_ERR(-ENOMEM, "Failed to allocate parity"
				       "buffers");

		for (row = 0; row < parity_row_nr(play); ++row) {
			M0_ALLOC_ARR_ADDB(map->pi_paritybufs[row],
					  parity_col_nr(play), &m0_addb_gmc,
					  M0T1FS_ADDB_LOC_DGMODE_PROCESS_2,
					  &m0t1fs_addb_ctx);
			if (map->pi_paritybufs[row] == NULL) {
				rc = -ENOMEM;
				goto par_fail;
			}
		}
	}
	rc = pargrp_iomap_pages_mark(map, M0_PUT_SPARE);
	return M0_RC(rc);

par_fail:
	M0_ASSERT(rc != 0);
	for (row = 0; row < parity_row_nr(play); ++row)
		m0_free0(&map->pi_paritybufs[row]);
	m0_free0(&map->pi_paritybufs);

	return M0_ERR(rc, "dgmode_process failed");
}

static int io_spare_map(const struct pargrp_iomap *map,
			const struct m0_pdclust_src_addr *src,
			uint32_t *spare_slot, uint32_t *spare_slot_prev,
			enum m0_pool_nd_state *eff_state)
{

	struct m0_pdclust_layout   *play;
	struct m0_pdclust_instance *play_instance;
	const struct m0_fid	   *gfid;
	struct m0_pdclust_src_addr  spare;
	struct m0t1fs_sb	   *msb;
	int			    rc;

	play = pdlayout_get(map->pi_ioreq);
	play_instance = pdlayout_instance(layout_instance(map->pi_ioreq));
	gfid = file_to_fid(map->pi_ioreq->ir_file);
	msb = file_to_sb(map->pi_ioreq->ir_file);
	rc = m0_sns_repair_spare_map(msb->csb_pool.po_mach,
				     gfid, play, play_instance,
				     src->sa_group, src->sa_unit,
				     spare_slot, spare_slot_prev);
	if (rc != 0) {
		M0_ADDB_FUNC_FAIL(&m0_addb_gmc,
				  M0T1FS_ADDB_LOC_TIOREQ_MAP_QSPSLOT,
				  rc, &m0t1fs_addb_ctx);
		return M0_RC(rc);
	}
	/* Check if there is an effective failure of unit. */
	spare.sa_group = src->sa_group;
	spare.sa_unit = *spare_slot_prev;
	rc = unit_state(&spare, map->pi_ioreq, eff_state);
	return rc;
}

static int unit_state(const struct m0_pdclust_src_addr *src,
		      const struct io_request *req,
		      enum m0_pool_nd_state *state)
{
	struct m0_pdclust_layout   *play;
	struct m0_pdclust_instance *play_instance;
	struct m0_pdclust_tgt_addr  tgt;
	struct m0_fid		    tfid;
	int			    rc;
	struct m0t1fs_sb	   *msb;

	msb = file_to_sb(req->ir_file);
	play = pdlayout_get(req);
	play_instance = pdlayout_instance(layout_instance(req));
	m0_pdclust_instance_map(play_instance, src, &tgt);
	tfid = target_fid(req, &tgt);
	rc = m0_poolmach_device_state(msb->csb_pool.po_mach, tfid.f_container,
				      state);
	if (rc != 0) {
		M0_ADDB_FUNC_FAIL(&m0_addb_gmc,
				  M0T1FS_ADDB_LOC_TIOREQ_MAP_QSPSLOT,
				  rc, &m0t1fs_addb_ctx);
		return M0_RC(rc);
	}
	return rc;
}

static int pargrp_iomap_dgmode_postprocess(struct pargrp_iomap *map)
{
	int                       rc = 0;
	bool                      within_eof;
	uint32_t                  row;
	uint32_t                  col;
	m0_bindex_t               start;
	struct inode             *inode;
	struct data_buf          *dbuf;
	struct m0_pdclust_layout *play;

	/*
	 * read_old: Reads unavailable data subject to condition that
	 * data lies within file size. Parity is already read.
	 * read_rest: Reads parity units. Data for parity group is already
	 * read.
	 * simple_read: Reads unavailable data subject to condition that
	 * data lies within file size. Parity also has to be read.
	 */
	M0_ENTRY("parity group id %llu, state = %d",
		 map->pi_grpid, map->pi_state);
	M0_PRE_EX(pargrp_iomap_invariant(map));

	inode = map->pi_ioreq->ir_file->f_dentry->d_inode;
	play = pdlayout_get(map->pi_ioreq);

	/*
	 * Data matrix from parity group.
	 * The loop traverses column by column to be in sync with
	 * increasing file offset.
	 * This is necessary in order to generate correct index vector.
	 */
	for (col = 0; col < data_col_nr(play); ++col) {
		for (row = 0; row < data_row_nr(play); ++row) {

			data_page_offset_get(map, row, col, &start);
			within_eof = start + PAGE_CACHE_SIZE < inode->i_size ||
			             (inode->i_size > 0 &&
				      page_id(start + PAGE_CACHE_SIZE - 1) ==
				      page_id(inode->i_size - 1));
			M0_LOG(M0_DEBUG, "within_eof = %d\n",
			       within_eof ? 1 : 0);

			if (map->pi_databufs[row][col] != NULL) {
				if (map->pi_databufs[row][col]->db_flags &
				    PA_READ_FAILED)
					continue;
			} else {
				/*
				 * If current parity group map is degraded,
				 * then recovery is needed and a new
				 * data buffer needs to be allocated subject to
				 * limitation of file size.
				 */
				if (map->pi_state == PI_DEGRADED &&
				    within_eof) {
					map->pi_databufs[row][col] =
						data_buf_alloc_init(0);
					if (map->pi_databufs[row][col] ==
					    NULL) {
						rc = -ENOMEM;
						break;
					}
					page_update(map, row, col, PA_DATA);
				}
				if (map->pi_state == PI_HEALTHY)
					continue;
			}
			dbuf = map->pi_databufs[row][col];
			if (within_eof && dbuf->db_flags & PA_READ_FAILED)
				continue;
			/*
			 * Marks only those data buffers which lie within EOF.
			 * Since all IO fops receive VERSION_MISMATCH error
			 * once sns repair starts (M0_PNDS_SNS_REPAIRING state)
			 * read is not done for any of these fops.
			 * Hence all pages other than the one which encountered
			 * failure (PA_READ_FAILED flag set) are read in
			 * degraded mode.
			 */
			if (within_eof) {
				dbuf->db_flags |= PA_DGMODE_READ;
				M0_LOG(M0_DEBUG, "[%u][%u], flag = %d\n",
				       row, col, dbuf->db_flags);
			}
		}
	}

	if (rc != 0)
		goto err;
	/* If parity group is healthy, there is no need to read parity. */
	if (map->pi_state != PI_DEGRADED)
		return M0_RC(0);

	/*
	 * Populates the index vector if original read IO request did not
	 * span it. Since recovery is needed using parity algorithms,
	 * whole parity group needs to be read subject to file size limitation.
	 * Ergo, parity group index vector contains only one segment
	 * worth the parity group in size.
	 */
	INDEX(&map->pi_ivec, 0) = map->pi_grpid * data_size(play);
	COUNT(&map->pi_ivec, 0) = min64u(INDEX(&map->pi_ivec, 0) +
					 data_size(play),
					 inode->i_size) -
				  INDEX(&map->pi_ivec, 0);
	SEG_NR(&map->pi_ivec)   = 1;

	/* parity matrix from parity group. */
	for (row = 0; row < parity_row_nr(play); ++row) {
		for (col = 0; col < parity_col_nr(play); ++col) {

			if (map->pi_paritybufs[row][col] == NULL) {
				map->pi_paritybufs[row][col] =
					data_buf_alloc_init(0);
				if (map->pi_paritybufs[row][col] == NULL) {
					rc = -ENOMEM;
					break;
				}
			}
			dbuf = map->pi_paritybufs[row][col];
			page_update(map, row, col, PA_PARITY);
			/* Skips the page if it is marked as PA_READ_FAILED. */
			if (dbuf->db_flags & PA_READ_FAILED)
				continue;
			if (M0_IN(map->pi_rtype, (PIR_READREST, PIR_NONE)))
				dbuf->db_flags |= PA_DGMODE_READ;
		}
	}
	if (rc != 0)
		goto err;
	return M0_RC(rc);
err:
	return M0_ERR(rc,"%s", rc == -ENOMEM ?  "Failed to allocate "
		       "data buffer": "Illegal device queried for status");
}

static int pargrp_iomap_dgmode_recover(struct pargrp_iomap *map)
{
	int                       rc = 0;
	uint8_t                  *fail;
	uint32_t                  row;
	uint32_t                  col;
	unsigned long             zpage;
	struct m0_buf            *data;
	struct m0_buf            *parity;
	struct m0_buf             failed;
	struct m0_pdclust_layout *play;

	M0_ENTRY();
	M0_PRE_EX(pargrp_iomap_invariant(map));
	M0_PRE(map->pi_state == PI_DEGRADED);

	play = pdlayout_get(map->pi_ioreq);

	M0_ALLOC_ARR_ADDB(data, layout_n(play), &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_DGMODE_RECOV_DATA, &m0t1fs_addb_ctx);
	if (data == NULL)
		return M0_ERR(-ENOMEM, "Failed to allocate memory"
			       " for data buf");

	M0_ALLOC_ARR_ADDB(parity, layout_k(play), &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_DGMODE_RECOV_PARITY,
			  &m0t1fs_addb_ctx);
	if (parity == NULL) {
		m0_free(data);
		return M0_ERR(-ENOMEM, "Failed to allocate memory"
			       " for parity buf");
	}

	zpage = get_zeroed_page(GFP_KERNEL);
	if (zpage == 0) {
		m0_free(data);
		m0_free(parity);
		return M0_ERR(-ENOMEM, "Failed to allocate page.");
	}

	failed.b_nob = layout_n(play) + layout_k(play);
	M0_ALLOC_ARR_ADDB(failed.b_addr, failed.b_nob, &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_DGMODE_RECOV_FAILVEC,
			  &m0t1fs_addb_ctx);
	if (failed.b_addr == NULL) {
		m0_free(data);
		m0_free(parity);
		free_page(zpage);
		return M0_ERR(-ENOMEM, "Failed to allocate memory for m0_buf");
	}

	fail = failed.b_addr;

	/* Populates data and failed buffers. */
	for (row = 0; row < data_row_nr(play); ++row) {

		memset(fail, 0, failed.b_nob);
		for (col = 0; col < data_col_nr(play); ++col) {

			data[col].b_nob = PAGE_CACHE_SIZE;

			if (map->pi_databufs[row][col] == NULL) {
				data[col].b_addr = (void *)zpage;
				*(fail + col) = 0;
				continue;
			}

			/* Here, unit number is same as column number. */
			*(fail + col) = (map->pi_databufs[row][col]->db_flags &
					PA_READ_FAILED) ? 1 : 0;
			data[col].b_addr = map->pi_databufs[row][col]->
				           db_buf.b_addr;
		}
		for (col = 0; col < parity_col_nr(play); ++col) {

			M0_ASSERT(map->pi_paritybufs[row][col] != NULL);
			parity[col].b_addr = map->pi_paritybufs[row][col]->
				db_buf.b_addr;
			parity[col].b_nob  = PAGE_CACHE_SIZE;

			*(fail + layout_n(play) + col) =
				(map->pi_paritybufs[row][col]->db_flags &
				 PA_READ_FAILED) ? 1 : 0;
		}
		m0_parity_math_recover(parity_math(map->pi_ioreq), data,
				       parity, &failed);
	}

	m0_free(data);
	m0_free(parity);
	m0_free(failed.b_addr);
	free_page(zpage);

	return M0_RC(rc);
}

static int ioreq_iomaps_prepare(struct io_request *req)
{
	int			  rc;
	uint64_t		  seg;
	uint64_t		  grp;
	uint64_t		  id;
	uint64_t		  grpstart;
	uint64_t		  grpend;
	uint64_t		 *grparray;
	struct m0_pdclust_layout *play;
	struct m0_ivec_cursor	  cursor;

	M0_ENTRY("io_request %p", req);
	M0_PRE(req != NULL);

	play = pdlayout_get(req);

	M0_ALLOC_ARR_ADDB(grparray,
	       max64u(m0_vec_count(&req->ir_ivec.iv_vec) / data_size(play) + 1,
	              SEG_NR(&req->ir_ivec)),
	                  &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_IOMAPS_PREP_GRPARR,
	                  &m0t1fs_addb_ctx);
	if (grparray == NULL)
		return M0_ERR(-ENOMEM, "Failed to allocate memory"
			       " for int array");

	/*
	 * Finds out total number of parity groups spanned by
	 * io_request::ir_ivec.
	 */
	for (seg = 0; seg < SEG_NR(&req->ir_ivec); ++seg) {
		grpstart = group_id(INDEX(&req->ir_ivec, seg), data_size(play));
		grpend	 = group_id(seg_endpos(&req->ir_ivec, seg) - 1,
				    data_size(play));
		for (grp = grpstart; grp <= grpend; ++grp) {
			/*
			 * grparray is a temporary array to record found groups.
			 * Scan this array for [grpstart, grpend].
			 * If not found, record it in this array and
			 * increase ir_iomap_nr.
			 */
			for (id = 0; id < req->ir_iomap_nr; ++id)
				if (grparray[id] == grp)
					break;
			if (id == req->ir_iomap_nr) {
				grparray[id] = grp;
				++req->ir_iomap_nr;
			}
		}
	}
	m0_free(grparray);

	M0_LOG(M0_DEBUG, "Number of pargrp_iomap structures : %llu",
	       req->ir_iomap_nr);

	/* req->ir_iomaps is zeroed out on allocation. */
	M0_ALLOC_ARR_ADDB(req->ir_iomaps, req->ir_iomap_nr, &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_IOMAPS_PREP_MAPS,
	                  &m0t1fs_addb_ctx);
	if (req->ir_iomaps == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	m0_ivec_cursor_init(&cursor, &req->ir_ivec);

	/*
	 * cursor is advanced maximum by parity group size in one iteration
	 * of this loop.
	 * This is done by pargrp_iomap::pi_ops::pi_populate().
	 */
	for (id = 0; !m0_ivec_cursor_move(&cursor, 0); ++id) {

		M0_ASSERT(id < req->ir_iomap_nr);
		M0_ASSERT(req->ir_iomaps[id] == NULL);
		M0_ALLOC_PTR_ADDB(req->ir_iomaps[id], &m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_IOMAPS_PREP_MAP,
		                  &m0t1fs_addb_ctx);
		if (req->ir_iomaps[id] == NULL) {
			rc = -ENOMEM;
			goto failed;
		}

		++iommstats.a_pargrp_iomap_nr;
		rc = pargrp_iomap_init(req->ir_iomaps[id], req,
				       group_id(m0_ivec_cursor_index(&cursor),
						data_size(play)));
		if (rc != 0)
			goto failed;

		rc = req->ir_iomaps[id]->pi_ops->pi_populate(req->
				ir_iomaps[id], &req->ir_ivec, &cursor);
		if (rc != 0)
			goto failed;
		M0_LOG(M0_INFO, "pargrp_iomap id : %llu populated",
		       req->ir_iomaps[id]->pi_grpid);
	}

	return M0_RC(0);
failed:
	req->ir_ops->iro_iomaps_destroy(req);
	return M0_ERR(rc, "iomaps_prepare failed");
}

static void ioreq_iomaps_destroy(struct io_request *req)
{
	uint64_t id;

	M0_ENTRY("io_request %p", req);

	M0_PRE(req != NULL);
	M0_PRE(req->ir_iomaps != NULL);

	for (id = 0; id < req->ir_iomap_nr; ++id) {
		if (req->ir_iomaps[id] != NULL) {
			pargrp_iomap_fini(req->ir_iomaps[id]);
			m0_free0(&req->ir_iomaps[id]);
			++iommstats.d_pargrp_iomap_nr;
		}
	}
	m0_free0(&req->ir_iomaps);
	req->ir_iomap_nr = 0;
}

static int dgmode_rwvec_alloc_init(struct target_ioreq *ti)
{
	int                       rc;
	uint64_t                  cnt;
	struct io_request        *req;
	struct dgmode_rwvec      *dg;
	struct m0_pdclust_layout *play;

	M0_ENTRY();
	M0_PRE(ti           != NULL);
	M0_PRE(ti->ti_dgvec == NULL);

	M0_ALLOC_PTR_ADDB(dg, &m0_addb_gmc, M0T1FS_ADDB_LOC_READVEC_ALLOC_INIT,
			  &m0t1fs_addb_ctx);
	if (dg == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	req = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
		     &ioreq_bobtype);
	play = pdlayout_get(req);
	dg->dr_tioreq = ti;

	cnt = page_nr(req->ir_iomap_nr * layout_unit_size(play) *
		      (layout_n(play) + layout_k(play)));
	rc  = m0_indexvec_alloc(&dg->dr_ivec, cnt, &m0t1fs_addb_ctx,
			        M0T1FS_ADDB_LOC_READVEC_ALLOC_IVEC_FAIL);
	if (rc != 0)
		goto failed;

	M0_ALLOC_ARR_ADDB(dg->dr_bufvec.ov_buf, cnt, &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_READVEC_ALLOC_BVEC, &m0t1fs_addb_ctx);
	if (dg->dr_bufvec.ov_buf == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	M0_ALLOC_ARR_ADDB(dg->dr_bufvec.ov_vec.v_count, cnt, &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_READVEC_ALLOC_BVEC_CNT,
			  &m0t1fs_addb_ctx);
	if (dg->dr_bufvec.ov_vec.v_count == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	M0_ALLOC_ARR_ADDB(dg->dr_pageattrs, cnt, &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_READVEC_ALLOC_PAGEATTR,
			  &m0t1fs_addb_ctx);
	if (dg->dr_pageattrs == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	/*
	 * This value is incremented every time a new segment is added
	 * to this index vector.
	 */
	dg->dr_ivec.iv_vec.v_nr = 0;

	ti->ti_dgvec = dg;
	return M0_RC(0);
failed:
	ti->ti_dgvec = NULL;
	if (dg->dr_bufvec.ov_buf != NULL)
		m0_free(dg->dr_bufvec.ov_buf);
	if (dg->dr_bufvec.ov_vec.v_count != NULL)
		m0_free(dg->dr_bufvec.ov_vec.v_count);
	m0_free(dg);
	return M0_ERR(rc, "Dgmode read vector allocation failed");
}

static void dgmode_rwvec_dealloc_fini(struct dgmode_rwvec *dg)
{
	M0_ENTRY();

	M0_PRE(dg != NULL);

	dg->dr_tioreq = NULL;
	/*
	 * Will need to go through array of parity groups to find out
	 * exact number of segments allocated for the index vector.
	 * Instead, a fixed number of segments is enough to avoid
	 * triggering the assert from m0_indexvec_free().
	 * The memory allocator knows the size of memory area held by
	 * dg->dr_ivec.iv_index and dg->dr_ivec.iv_vec.v_count.
	 */
	if (dg->dr_ivec.iv_vec.v_nr == 0)
		++dg->dr_ivec.iv_vec.v_nr;

	m0_indexvec_free(&dg->dr_ivec);
	m0_free(dg->dr_bufvec.ov_buf);
	m0_free(dg->dr_bufvec.ov_vec.v_count);
	m0_free(dg->dr_pageattrs);
}

/*
 * Distributes file data into target_ioreq objects as required and populates
 * target_ioreq::ti_ivec and target_ioreq::ti_bufvec.
 */
static int nw_xfer_io_distribute(struct nw_xfer_request *xfer)
{
	int			    rc;
	uint64_t		    map;
	uint64_t		    unit;
	uint64_t		    unit_size;
	uint64_t		    count;
	uint64_t		    pgstart;
	uint64_t		    pgend;
	/* Extent representing a data unit. */
	struct m0_ext		    u_ext;
	/* Extent representing resultant extent. */
	struct m0_ext		    r_ext;
	/* Extent representing a segment from index vector. */
	struct m0_ext		    v_ext;
	struct io_request	   *req;
	struct target_ioreq	   *ti;
	struct m0_ivec_cursor	    cursor;
	struct m0_pdclust_layout   *play;
	enum m0_pdclust_unit_type   unit_type;
	struct m0_pdclust_src_addr  src;
	struct m0_pdclust_tgt_addr  tgt;

	M0_ENTRY("nw_xfer_request %p", xfer);
	M0_PRE_EX(nw_xfer_request_invariant(xfer));

	req	  = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
	play	  = pdlayout_get(req);
	unit_size = layout_unit_size(play);

	for (map = 0; map < req->ir_iomap_nr; ++map) {

		count        = 0;
		pgstart	     = data_size(play) *
					req->ir_iomaps[map]->pi_grpid;
		pgend	     = pgstart + data_size(play);
		src.sa_group = req->ir_iomaps[map]->pi_grpid;

		/* Cursor for pargrp_iomap::pi_ivec. */
		m0_ivec_cursor_init(&cursor, &req->ir_iomaps[map]->pi_ivec);

		while (!m0_ivec_cursor_move(&cursor, count)) {

			unit = (m0_ivec_cursor_index(&cursor) - pgstart) /
			       unit_size;

			u_ext.e_start = pgstart + unit * unit_size;
			u_ext.e_end   = u_ext.e_start + unit_size;

			v_ext.e_start  = m0_ivec_cursor_index(&cursor);
			v_ext.e_end    = v_ext.e_start +
					  m0_ivec_cursor_step(&cursor);

			m0_ext_intersection(&u_ext, &v_ext, &r_ext);
			if (!m0_ext_is_valid(&r_ext))
				continue;

			count	  = m0_ext_length(&r_ext);
			unit_type = m0_pdclust_unit_classify(play, unit);
			if (unit_type == M0_PUT_SPARE ||
			    unit_type == M0_PUT_PARITY)
				continue;

			src.sa_unit = unit;
			rc = xfer->nxr_ops->nxo_tioreq_map(xfer, &src, &tgt,
							   &ti);
			if (rc != 0)
				goto err;

			ti->ti_ops->tio_seg_add(ti, &src, &tgt, r_ext.e_start,
						m0_ext_length(&r_ext),
						req->ir_iomaps[map]);
		}

		if (req->ir_type == IRT_WRITE ||
		    (ioreq_sm_state(req) == IRS_DEGRADED_READING &&
		     req->ir_iomaps[map]->pi_state == PI_DEGRADED)) {

			/*
			 * The loop iterates layout_k() times since
			 * number of spare units equal number of parity units.
			 * In case of spare units, no IO is done.
			 */
			for (unit = 0; unit < layout_k(play); ++unit) {

				src.sa_unit = layout_n(play) + unit;
				rc = xfer->nxr_ops->nxo_tioreq_map(xfer, &src,
								   &tgt, &ti);
				if (rc != 0)
					goto err;

				if (m0_pdclust_unit_classify(play,
				    src.sa_unit) == M0_PUT_PARITY)

					ti->ti_ops->tio_seg_add(ti, &src, &tgt,
							pgstart,
							layout_unit_size(play),
							req->ir_iomaps[map]);
			}
		}
	}

	return M0_RC(0);
err:
	m0_htable_for(tioreqht, ti, &xfer->nxr_tioreqs_hash) {
		tioreqht_htable_del(&xfer->nxr_tioreqs_hash, ti);
		target_ioreq_fini(ti);
		m0_free0(&ti);
		++iommstats.d_target_ioreq_nr;
	} m0_htable_endfor;

	return M0_ERR(rc, "io_prepare failed");
}

static inline int ioreq_sm_timedwait(struct io_request *req,
			             uint64_t           state)
{
	int rc;

	M0_ENTRY("ioreq = %p, current state = %d, waiting for state %llu",
		 req, ioreq_sm_state(req), state);
	M0_PRE(req != NULL);

	m0_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	rc = m0_sm_timedwait(&req->ir_sm, (1 << state), M0_TIME_NEVER);
	m0_mutex_unlock(&req->ir_sm.sm_grp->s_lock);

	return M0_RC(rc);
}

static int ioreq_dgmode_recover(struct io_request *req)
{
	int      rc = 0;
	uint64_t cnt;

	M0_ENTRY();
	M0_PRE_EX(io_request_invariant(req));
	M0_PRE(ioreq_sm_state(req) == IRS_READ_COMPLETE);

	for (cnt = 0; cnt < req->ir_iomap_nr; ++cnt) {
		if (req->ir_iomaps[cnt]->pi_state == PI_DEGRADED) {
			rc = req->ir_iomaps[cnt]->pi_ops->
				pi_dgmode_recover(req->ir_iomaps[cnt]);
			if (rc != 0)
				return M0_ERR(rc, "Failed to recover data");
		}
	}

	return M0_RC(rc);
}

/*
 * Returns number of failed devices or -EIO if number of failed devices exceed
 * the value of K (number of spare devices in parity group).
 */
static int device_check(struct io_request *req)
{
	int                       rc = 0;
	uint32_t                  st_cnt = 0;
	struct m0t1fs_sb         *csb;
	struct target_ioreq      *ti;
	enum m0_pool_nd_state     state;
	struct m0_pdclust_layout *play;

	M0_ENTRY();
	M0_PRE(req != NULL);
	M0_PRE(M0_IN(ioreq_sm_state(req), (IRS_READ_COMPLETE,
					   IRS_WRITE_COMPLETE)));
	csb = file_to_sb(req->ir_file);

	m0_htable_for (tioreqht, ti, &req->ir_nwxfer.nxr_tioreqs_hash) {
		rc = m0_poolmach_device_state(csb->csb_pool.po_mach,
				              ti->ti_fid.f_container, &state);
		if (rc != 0)
			return M0_ERR(rc, "Failed to retrieve target device"
				       " state");
		ti->ti_state = state;
		if (M0_IN(state, (M0_PNDS_FAILED, M0_PNDS_OFFLINE,
			          M0_PNDS_SNS_REPAIRING)))
			st_cnt++;
	} m0_htable_endfor;

	/*
	 * Since m0t1fs IO only supports XOR at the moment, max number of
	 * failed units could be 1.
	 */
	play = pdlayout_get(req);
	if (st_cnt > layout_k(play))
		return M0_ERR(-EIO, "Failed to recover data since "
			       "number of failed"
			  " data units exceed number of parity units in"
			  " parity group");
	return M0_RC(st_cnt);
}

static int ioreq_dgmode_write(struct io_request *req, bool rmw)
{
	int                      rc;
	struct target_ioreq     *ti;
	struct m0_addb_io_stats *stats;
	m0_time_t                start;
	struct m0t1fs_sb        *csb;

	M0_ENTRY();
	M0_PRE_EX(io_request_invariant(req));

	rc = device_check(req);
	if (req->ir_nwxfer.nxr_rc == 0)
		return M0_RC(req->ir_nwxfer.nxr_rc);
	else if (rc < 0)
		return M0_RC(rc);

	csb = file_to_sb(req->ir_file);
	ioreq_sm_state_set(req, IRS_DEGRADED_WRITING);
	start = m0_time_now();
	/*
	 * This IO request has already acquired distributed lock on the
	 * file by this time.
	 * Degraded mode write needs to handle 2 prime use-cases.
	 * 1. SNS repair still to start on associated global fid.
	 * 2. SNS repair has completed for associated global fid.
	 * Both use-cases imply unavailability of one or more devices.
	 *
	 * In first use-case, repair is yet to start on file. Hence,
	 * rest of the file data which goes on healthy devices can be
	 * written safely.
	 * In this case, the fops meant for failed device(s) will be simply
	 * dropped and rest of the fops will be sent to respective ioservice
	 * instances for writing data to servers.
	 * Later when this IO request relinquishes the distributed lock on
	 * associated global fid and SNS repair starts on the file, the lost
	 * data will be regenerated using parity recovery algorithms.
	 *
	 * The second use-case implies completion of SNS repair for associated
	 * global fid and the lost data is regenerated on distributed spare
	 * units.
	 * Ergo, all the file data meant for lost device(s) will be redirected
	 * towards corresponding spare unit(s). Later when SNS rebalance phase
	 * commences, it will migrate the data from spare to a new device, thus
	 * making spare available for recovery again.
	 * In this case, old fops will be discarded and all pages spanned by
	 * IO request will be reshuffled by redirecting pages meant for
	 * failed device(s) to its corresponding spare unit(s).
	 */

	/*
	 * Finalizes current fops which are not valid anymore.
	 * Fops need to be finalized in either case since old network buffers
	 * from IO fops are still enqueued in transfer machine and removal
	 * of these buffers would lead to finalization of rpc bulk object.
	 */
	req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, rmw);
	if (M0_IN(req->ir_sns_state, (SRS_UNINITIALIZED, SRS_REPAIR_NOTDONE))) {
		/*
		 * Resets count of data bytes and parity bytes along with
		 * return status.
		 * Fops meant for failed devices are dropped in
		 * nw_xfer_req_dispatch().
		 */
		m0_htable_for(tioreqht, ti, &req->ir_nwxfer.nxr_tioreqs_hash) {
			ti->ti_databytes = 0;
			ti->ti_parbytes  = 0;
			ti->ti_rc        = 0;
		} m0_htable_endfor;

	} else {
		/*
		 * Redistributes all pages by routing pages for failed devices
		 * to spare units for each parity group.
		 */
		rc = req->ir_nwxfer.nxr_ops->nxo_distribute(&req->ir_nwxfer);
		if (rc != 0)
			return M0_ERR(rc, "Failed to prepare dgmode"
				       "write fops");
	}

	req->ir_nwxfer.nxr_rc = 0;
	req->ir_rc = req->ir_nwxfer.nxr_rc;

	rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
	if (rc != 0)
		return M0_ERR(rc, "Failed to dispatch degraded mode"
			       "write IO fops");

	rc = ioreq_sm_timedwait(req, IRS_WRITE_COMPLETE);
	if (rc != 0)
		return M0_ERR(rc, "Degraded mode write IO failed");

	stats = &csb->csb_dgio_stats[IRT_WRITE];
	m0_addb_counter_update(&stats->ais_times_cntr,
			       (uint64_t) m0_time_sub(m0_time_now(), start) /
			       1000); /* uS */
	m0_addb_counter_update(&stats->ais_sizes_cntr,
			       (uint64_t) req->ir_nwxfer.nxr_bytes);
	return M0_RC(req->ir_nwxfer.nxr_rc);
}

static int ioreq_dgmode_read(struct io_request *req, bool rmw)
{
	int                      rc            = 0;
	uint64_t                 id;
	struct m0t1fs_sb        *csb;
	struct io_req_fop       *irfop;
	struct target_ioreq     *ti;
	struct m0_addb_io_stats *stats;
	m0_time_t                start;
	enum m0_pool_nd_state    state;

	M0_ENTRY();
	M0_PRE_EX(io_request_invariant(req));

	rc = device_check(req);
	/*
	 * Number of failed devices is not a criteria good enough
	 * by itself. Even if one/more devices failed but IO request
	 * could complete if IO request did not send any pages to
	 * failed device(s) at all.
	 */
	if (req->ir_nwxfer.nxr_rc == 0)
		return M0_RC(req->ir_nwxfer.nxr_rc);
	else if (rc < 0)
		return M0_RC(rc);

	csb = file_to_sb(req->ir_file);
	start = m0_time_now();
	m0_htable_for(tioreqht, ti, &req->ir_nwxfer.nxr_tioreqs_hash) {
		rc = m0_poolmach_device_state(csb->csb_pool.po_mach,
				ti->ti_fid.f_container, &state);
		if (rc != 0)
			return M0_ERR(rc, "Failed to retrieve device state");
		M0_LOG(M0_INFO, "device state for "FID_F" is %d",
		       FID_P(&ti->ti_fid), state);
		if (!M0_IN(state, (M0_PNDS_FAILED, M0_PNDS_OFFLINE,
			   M0_PNDS_SNS_REPAIRING, M0_PNDS_SNS_REPAIRED)))
			continue;
		/*
		 * Finds out parity groups for which read IO failed and marks
		 * them as DEGRADED. This is necessary since read IO request
		 * could be reading only a part of a parity group but if it
		 * failed, rest of the parity group also needs to be read
		 * (subject to file size) in order to re-generate lost data.
		 */

		m0_tl_for (iofops, &ti->ti_iofops, irfop) {
			rc = irfop->irf_ops->irfo_dgmode_read(irfop);
			if (rc != 0)
				break;
		} m0_tl_endfor;
	} m0_htable_endfor;

	if (rc != 0)
		return M0_ERR(rc, "dgmode failed");

	/*
	 * Starts processing the pages again if any of the parity groups is
	 * in the state PI_DEGRADED.
	 */
	if (req->ir_dgmap_nr > 0) {
		for (id = 0; id < req->ir_iomap_nr; ++id) {
			if (req->ir_iomaps[id]->pi_state != PI_DEGRADED)
				continue;
			if (ioreq_sm_state(req) == IRS_READ_COMPLETE)
				ioreq_sm_state_set(req, IRS_DEGRADED_READING);
			rc = req->ir_iomaps[id]->pi_ops->
			     pi_dgmode_postprocess(req->ir_iomaps[id]);
			if (rc != 0)
				break;
		}
	} else {
		M0_ASSERT(ioreq_sm_state(req) == IRS_READ_COMPLETE);
		ioreq_sm_state_set(req, IRS_READING);
		/*
		 * By this time, the page count in target_ioreq::ti_ivec and
		 * target_ioreq::ti_bufvec is greater than 1, but it is
		 * invalid since the distribution is about to change.
		 * Ergo, page counts in index and buffer vectors are reset.
		 */

		m0_htable_for(tioreqht, ti,
			      &req->ir_nwxfer.nxr_tioreqs_hash) {
			ti->ti_ivec.iv_vec.v_nr = 0;
		} m0_htable_endfor;
	}

	req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, rmw);

	m0_htable_for(tioreqht, ti, &req->ir_nwxfer.nxr_tioreqs_hash) {
		ti->ti_databytes = 0;
		ti->ti_parbytes  = 0;
		ti->ti_rc        = 0;
	} m0_htable_endfor;

	/* Resets the status code before starting degraded mode read IO. */
	if (req->ir_nwxfer.nxr_rc != 0)
		req->ir_nwxfer.nxr_rc = 0;
	req->ir_rc = req->ir_nwxfer.nxr_rc;

	rc = req->ir_nwxfer.nxr_ops->nxo_distribute(&req->ir_nwxfer);
	if (rc != 0)
		return M0_ERR(rc, "Failed to prepare dgmode IO fops.");

	rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
	if (rc != 0)
		return M0_ERR(rc, "Failed to dispatch degraded mode IO.");

	rc = ioreq_sm_timedwait(req, IRS_READ_COMPLETE);
	if (rc != 0)
		return M0_ERR(rc, "Degraded mode read IO failed.");

	if (req->ir_nwxfer.nxr_rc != 0)
		return M0_ERR(req->ir_nwxfer.nxr_rc,
			       "Degraded mode read IO failed.");
	stats = &csb->csb_dgio_stats[IRT_READ];
	m0_addb_counter_update(&stats->ais_times_cntr,
			       (uint64_t) m0_time_sub(m0_time_now(), start) /
			       1000); /* uS */
	m0_addb_counter_update(&stats->ais_sizes_cntr,
			       (uint64_t) req->ir_nwxfer.nxr_bytes);
	/*
	 * Recovers lost data using parity recovery algorithms only if
	 * one or more devices were in FAILED, OFFLINE, REPAIRING state.
	 */
	if (req->ir_dgmap_nr > 0) {
		rc = req->ir_ops->iro_dgmode_recover(req);
		if (rc != 0)
			return M0_ERR(rc, "Failed to recover lost data.");
	}

	return M0_RC(rc);
}

static int ioreq_file_lock(struct io_request *req)
{
	int                  rc;
	struct m0t1fs_inode *mi;

	M0_PRE(req != NULL);
	M0_ENTRY();

	mi = file_to_m0inode(req->ir_file);
	m0_file_lock(&mi->ci_fowner, &req->ir_in);
	m0_rm_owner_lock(&mi->ci_fowner);
	rc = m0_sm_timedwait(&req->ir_in.rin_sm,
			     M0_BITS(RI_SUCCESS, RI_FAILURE),
			     M0_TIME_NEVER);
	m0_rm_owner_unlock(&mi->ci_fowner);
	rc = rc ?: req->ir_in.rin_rc;
	if (rc == 0)
		ioreq_sm_state_set(req, IRS_LOCK_ACQUIRED);

	return M0_RC(rc);
}

static void ioreq_file_unlock(struct io_request *req)
{
	M0_PRE(req != NULL);
	m0_file_unlock(&req->ir_in);
	ioreq_sm_state_set(req, IRS_LOCK_RELINQUISHED);
}

static int ioreq_iosm_handle(struct io_request *req)
{
	int		     rc;
	int		     res;
	bool		     rmw;
	uint64_t	     map;
	struct inode	    *inode;
	struct target_ioreq *ti;

	M0_ENTRY("io_request %p sb = %p", req, file_to_sb(req->ir_file));
	M0_PRE_EX(io_request_invariant(req));

	for (map = 0; map < req->ir_iomap_nr; ++map) {
		if (M0_IN(req->ir_iomaps[map]->pi_rtype,
			  (PIR_READOLD, PIR_READREST)))
			break;
	}

	/*
	 * Acquires lock before proceeding to do actual IO.
	 */
	rc = req->ir_ops->iro_file_lock(req);
	if (rc != 0)
		goto fail;

	/* @todo Do error handling based on m0_sm::sm_rc. */
	/*
	 * Since m0_sm is part of io_request, for any parity group
	 * which is partial, read-modify-write state transition is followed
	 * for all parity groups.
	 */
	if (map == req->ir_iomap_nr) {
		enum io_req_state state;

		rmw = false;
		state = req->ir_type == IRT_READ ? IRS_READING :
						   IRS_WRITING;
		if (state == IRS_WRITING) {
			rc = req->ir_ops->iro_user_data_copy(req,
					CD_COPY_FROM_USER, 0);
			if (rc != 0)
				goto fail_locked;

			rc = req->ir_ops->iro_parity_recalc(req);
			if (rc != 0)
				goto fail_locked;
		}
		ioreq_sm_state_set(req, state);
		rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail_locked;

		state = req->ir_type == IRT_READ ? IRS_READ_COMPLETE:
						   IRS_WRITE_COMPLETE;
		rc    = ioreq_sm_timedwait(req, state);
		if (rc != 0)
			goto fail_locked;

		M0_ASSERT(ioreq_sm_state(req) == state);

		if (req->ir_rc != 0) {
			rc = req->ir_rc;
			goto fail_locked;
		}
		if (state == IRS_READ_COMPLETE) {

			/*
			 * Returns immediately if all devices are
			 * in healthy state.
			 */
			rc = req->ir_ops->iro_dgmode_read(req, rmw);
			if (rc != 0)
				goto fail_locked;

			rc = req->ir_ops->iro_user_data_copy(req,
					CD_COPY_TO_USER, 0);
			if (rc != 0)
				goto fail_locked;
		} else {
			M0_ASSERT(state == IRS_WRITE_COMPLETE);
			/*
			 * Returns immediately if all devices are
			 * in healthy state.
			 */
			rc = req->ir_ops->iro_dgmode_write(req, rmw);
			if (rc != 0)
				goto fail_locked;
		}
	} else {
		uint32_t    seg;
		m0_bcount_t read_pages = 0;

		rmw = true;
		m0_htable_for(tioreqht, ti, &req->ir_nwxfer.nxr_tioreqs_hash) {
			for (seg = 0; seg < ti->ti_bufvec.ov_vec.v_nr; ++seg)
				if (ti->ti_pageattrs[seg] & PA_READ)
					++read_pages;
		} m0_htable_endfor;

		/* Read IO is issued only if byte count > 0. */
		if (read_pages > 0) {
			ioreq_sm_state_set(req, IRS_READING);
			rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->
								  ir_nwxfer);
			if (rc != 0)
				goto fail_locked;
		}

		/* Waits for read completion if read IO was issued. */
		if (read_pages > 0) {
			rc = ioreq_sm_timedwait(req, IRS_READ_COMPLETE);

			if (rc != 0) {
				goto fail_locked;
			}

			/*
			 * Returns immediately if all devices are
			 * in healthy state.
			 */
			rc = req->ir_ops->iro_dgmode_read(req, rmw);
			if (rc != 0)
				goto fail_locked;
		}

		/*
		 * If fops dispatch fails, we need to wait till all io fop
		 * callbacks are acked since IO fops have already been
		 * dispatched.
		 *
		 * Only fully modified pages from parity groups which have
		 * chosen read-rest approach or aligned parity groups,
		 * are copied since read-old approach needs reading of
		 * all spanned pages,
		 * (no matter fully modified or paritially modified)
		 * in order to calculate parity correctly.
		 */
		res = req->ir_ops->iro_user_data_copy(req, CD_COPY_FROM_USER,
						      PA_FULLPAGE_MODIFY);
		if (res != 0) {
			rc = res;
			goto fail_locked;
		}

		/* Copies
		 * - fully modified pages from parity groups which have
		 *   chosen read_old approach and
		 * - partially modified pages from all parity groups.
		 */
		rc = req->ir_ops->iro_user_data_copy(req, CD_COPY_FROM_USER, 0);
		if (rc != 0)
			goto fail_locked;

		/* Finalizes the old read fops. */
		if (read_pages > 0) {
			req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer,
							     rmw);
			if (req->ir_rc != 0)
				goto fail_locked;
			device_state_reset(&req->ir_nwxfer, rmw);
		}
		ioreq_sm_state_set(req, IRS_WRITING);
		rc = req->ir_ops->iro_parity_recalc(req);
		if (rc != 0)
			goto fail_locked;
		rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail_locked;

		rc = ioreq_sm_timedwait(req, IRS_WRITE_COMPLETE);
		if (rc != 0)
			goto fail_locked;

		/* Returns immediately if all devices are in healthy state. */
		rc = req->ir_ops->iro_dgmode_write(req, rmw);
		if (rc != 0)
			goto fail_locked;
	}

	/*
	 * Updates file size on successful write IO.
	 * New file size is maximum value between old file size and
	 * valid file position written in current write IO call.
	 */
	inode = req->ir_file->f_dentry->d_inode;
	if (ioreq_sm_state(req) == IRS_WRITE_COMPLETE) {
	        uint64_t newsize = max64u(inode->i_size,
				seg_endpos(&req->ir_ivec,
					req->ir_ivec.iv_vec.v_nr - 1));
	        m0t1fs_size_update(inode, newsize);
		M0_LOG(M0_INFO, "File size set to %llu", inode->i_size);
	}

	req->ir_ops->iro_file_unlock(req);

	req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, rmw);

	if (rmw)
		ioreq_sm_state_set(req, IRS_REQ_COMPLETE);

	return M0_RC(0);

fail_locked:
	req->ir_ops->iro_file_unlock(req);
fail:
	ioreq_sm_failed(req, rc);
	ioreq_sm_state_set(req, IRS_REQ_COMPLETE);
	req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, false);
	return M0_ERR(rc, "ioreq_iosm_handle failed");
}

static void device_state_reset(struct nw_xfer_request *xfer, bool rmw)
{
	struct io_request   *req;
	struct target_ioreq *ti;

	M0_PRE(xfer != NULL);
	M0_PRE(xfer->nxr_state == NXS_COMPLETE);

	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);

	m0_htable_for(tioreqht, ti, &xfer->nxr_tioreqs_hash) {
		ti->ti_state = M0_PNDS_ONLINE;
	} m0_htable_endfor;
}

static int io_request_init(struct io_request  *req,
			   struct file	      *file,
			   const struct iovec *iov,
			   struct m0_indexvec *ivec,
			   enum io_req_type    rw)
{
	int                  rc;
	uint32_t             seg;

	M0_ENTRY("io_request %p, rw %d", req, rw);
	M0_PRE(req  != NULL);
	M0_PRE(file != NULL);
	M0_PRE(iov  != NULL);
	M0_PRE(ivec != NULL);
	M0_PRE(M0_IN(rw, (IRT_READ, IRT_WRITE)));

	req->ir_rc	  = 0;
	req->ir_ops	  = &ioreq_ops;
	req->ir_file	  = file;
	req->ir_type	  = rw;
	req->ir_iovec	  = iov;
	req->ir_iomap_nr  = 0;
	req->ir_copied_nr = 0;
	req->ir_sns_state = SRS_UNINITIALIZED;

	io_request_bob_init(req);
	nw_xfer_request_init(&req->ir_nwxfer);
	if (req->ir_nwxfer.nxr_rc != 0)
		return M0_ERR(req->ir_nwxfer.nxr_rc, "nw_xfer_req_init()"
			       " failed");

	m0_sm_init(&req->ir_sm, &io_sm_conf, IRS_INITIALIZED,
		   file_to_smgroup(req->ir_file));

	rc = m0_indexvec_alloc(&req->ir_ivec, ivec->iv_vec.v_nr,
	                       &m0t1fs_addb_ctx,
	                       M0T1FS_ADDB_LOC_IOREQ_INIT_IV);

	if (rc != 0)
		return M0_ERR(-ENOMEM, "Allocation failed for m0_indexvec");

	for (seg = 0; seg < ivec->iv_vec.v_nr; ++seg) {
		req->ir_ivec.iv_index[seg] = ivec->iv_index[seg];
		req->ir_ivec.iv_vec.v_count[seg] = ivec->iv_vec.v_count[seg];
	}

	/* Sorts the index vector in increasing order of file offset. */
	indexvec_sort(&req->ir_ivec);

	M0_POST_EX(ergo(rc == 0, io_request_invariant(req)));
	return M0_RC(rc);
}

static void io_request_fini(struct io_request *req)
{
	struct target_ioreq *ti;

	M0_ENTRY("io_request %p", req);
	M0_PRE_EX(io_request_invariant(req));

	m0_sm_fini(&req->ir_sm);
	io_request_bob_fini(req);
	req->ir_file   = NULL;
	req->ir_iovec  = NULL;
	req->ir_iomaps = NULL;
	req->ir_ops    = NULL;
	m0_indexvec_free(&req->ir_ivec);

	m0_htable_for(tioreqht, ti, &req->ir_nwxfer.nxr_tioreqs_hash) {
		tioreqht_htable_del(&req->ir_nwxfer.nxr_tioreqs_hash, ti);
		/*
		 * All io_req_fop structures in list target_ioreq::ti_iofops
		 * are already finalized in nw_xfer_req_complete().
		 */
		target_ioreq_fini(ti);
		m0_free(ti);
		++iommstats.d_target_ioreq_nr;
	} m0_htable_endfor;

	nw_xfer_request_fini(&req->ir_nwxfer);
	M0_LEAVE();
}

static void data_buf_init(struct data_buf *buf, void *addr, uint64_t flags)
{
	M0_PRE(buf  != NULL);
	M0_PRE(addr != NULL);

	data_buf_bob_init(buf);
	buf->db_flags = flags;
	m0_buf_init(&buf->db_buf, addr, PAGE_CACHE_SIZE);
}

static void data_buf_fini(struct data_buf *buf)
{
	M0_PRE(buf != NULL);

	data_buf_bob_fini(buf);
	buf->db_flags = PA_NONE;
}

static int nw_xfer_tioreq_map(struct nw_xfer_request           *xfer,
			      const struct m0_pdclust_src_addr *src,
			      struct m0_pdclust_tgt_addr       *tgt,
			      struct target_ioreq             **out)
{
	struct m0_fid		    tfid;
	const struct m0_fid	   *gfid;
	struct io_request	   *req;
	struct m0_rpc_session	   *session;
	struct m0_pdclust_layout   *play;
	struct m0_pdclust_instance *play_instance;
	struct m0t1fs_sb           *csb;
	enum m0_pool_nd_state       device_state;
	enum m0_pool_nd_state       device_state_prev;
	uint32_t                    spare_slot;
	uint32_t                    spare_slot_prev;
	struct m0_pdclust_src_addr  spare;
	int			    rc;

	M0_ENTRY("nw_xfer_request %p", xfer);
	M0_PRE_EX(nw_xfer_request_invariant(xfer));
	M0_PRE(src != NULL);
	M0_PRE(tgt != NULL);

	req  = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
	play = pdlayout_get(req);
	play_instance = pdlayout_instance(layout_instance(req));
	spare = *src;

	m0_pdclust_instance_map(play_instance, src, tgt);
	tfid = target_fid(req, tgt);

	M0_LOG(M0_DEBUG, "src_id[%llu:%llu] -> dest_id[%llu:%llu] @ tfid "FID_F,
	       src->sa_group, src->sa_unit, tgt->ta_frame, tgt->ta_obj,
	       FID_P(&tfid));

	csb = file_to_sb(req->ir_file);
	rc = m0_poolmach_device_state(csb->csb_pool.po_mach,
				      tfid.f_container, &device_state);
	if (rc != 0) {
		M0_ADDB_FUNC_FAIL(&m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_TIOREQ_MAP_QDEVST, rc,
				  &m0t1fs_addb_ctx);
		return M0_RC(rc);
	}

	if (M0_FI_ENABLED("poolmach_client_repaired_device1")) {
		if (tfid.f_container == 1)
			device_state = M0_PNDS_SNS_REPAIRED;
	}

	/*
	 * Listed here are various possible combinations of different
	 * parameters. The cumulative result of these values decide
	 * whether given IO request should be redirected to spare
	 * or not.
	 * Note: For normal IO, M0_IN(ioreq_sm_state,
	 * (IRS_READING, IRS_WRITING)), this redirection is not needed with
	 * the exception of read IO case where the failed device is in
	 * REPAIRED state.
	 * Also, req->ir_sns_state member is used only to differentiate
	 * between 2 possible use cases during degraded mode write.
	 * This flag is not used elsewhere.
	 *
	 * Parameters:
	 * - State of IO request.
	 *   Sample set {IRS_DEGRADED_READING, IRS_DEGRADED_WRITING}
	 *
	 * - State of current device.
	 *   Sample set {M0_PNDS_SNS_REPAIRING, M0_PNDS_SNS_REPAIRED}
	 *
	 * - State of SNS repair process with respect to current global fid.
	 *   Sample set {SRS_REPAIR_DONE, SRS_REPAIR_NOTDONE}
	 *
	 * Common case:
	 * req->ir_state == IRS_DEGRADED_READING &&
	 * M0_IN(req->ir_sns_state, (SRS_REPAIR_DONE || SRS_REPAIR_NOTDONE)
	 *
	 * 1. device_state == M0_PNDS_SNS_REPAIRING
	 *    In this case, data to failed device is not redirected to
	 *    spare device.
	 *    The extent is assigned to the failed device itself but
	 *    it is filtered at the level of io_req_fop.
	 *
	 * 2. device_state == M0_PNDS_SNS_REPAIRED
	 *    Here, data to failed device is redirected to respective spare
	 *    unit.
	 *
	 * Common case:
	 * req->ir_state == IRS_DEGRADED_WRITING.
	 *
	 * 1. M0_IN(device_state,
	 *          (M0_PNDS_SNS_REPAIRING, M0_PNDS_SNS_REPAIRED)) &&
	 *    req->ir_sns_state == SRS_REPAIR_DONE.
	 *    Here, repair has finished for current global fid but has not
	 *    finished completely. Ergo, data is redirected towards
	 *    respective spare unit.
	 *
	 * 2. device_state      == M0_PNDS_SNS_REPAIRING &&
	 *    req->ir_sns_state == SRS_REPAIR_NOTDONE.
	 *    In this case, data to failed device is not redirected to the
	 *    spare unit since we drop all pages directed towards failed
	 *    device.
	 *
	 * 3. device_state      == M0_PNDS_SNS_REPAIRED &&
	 *    req->ir_sns_state == SRS_REPAIR_NOTDONE.
	 *    Unlikely case! What to do in this case?
	 */
	M0_LOG(M0_INFO, "device state = %d\n", device_state);
	if ((M0_IN(ioreq_sm_state(req),
	           (IRS_READING, IRS_DEGRADED_READING)) &&
	     device_state        == M0_PNDS_SNS_REPAIRED) ||

	    (ioreq_sm_state(req) == IRS_DEGRADED_WRITING &&
	     req->ir_sns_state   == SRS_REPAIR_DONE &&
	     M0_IN(device_state,
		   (M0_PNDS_SNS_REPAIRING, M0_PNDS_SNS_REPAIRED)))) {
		gfid = file_to_fid(req->ir_file);
		rc = m0_sns_repair_spare_map(csb->csb_pool.po_mach, gfid, play,
					     play_instance, src->sa_group,
					     src->sa_unit, &spare_slot,
					     &spare_slot_prev);
		if (M0_FI_ENABLED("poolmach_client_repaired_device1")) {
			if (tfid.f_container == 1) {
				rc = 0;
				spare_slot = layout_n(play) + layout_k(play);
			}
		}

		if (rc != 0) {
			M0_ADDB_FUNC_FAIL(&m0_addb_gmc,
					  M0T1FS_ADDB_LOC_TIOREQ_MAP_QSPSLOT,
					  rc, &m0t1fs_addb_ctx);
			return M0_RC(rc);
		}
		/* Check if there is an effective-failure. */
		if (spare_slot_prev != src->sa_unit) {
			spare.sa_unit = spare_slot_prev;
			m0_pdclust_instance_map(play_instance, &spare, tgt);
			tfid = target_fid(req, tgt);
			rc = m0_poolmach_device_state(csb->csb_pool.po_mach,
						      tfid.f_container,
						      &device_state_prev);
			if (rc != 0) {
				M0_ADDB_FUNC_FAIL(&m0_addb_gmc,
						  M0T1FS_ADDB_LOC_TIOREQ_MAP_QDEVST,
						  rc, &m0t1fs_addb_ctx);
				return M0_RC(rc);
			}
		} else
			device_state_prev = M0_PNDS_SNS_REPAIRED;

		if (device_state_prev == M0_PNDS_SNS_REPAIRED) {
			spare.sa_unit = spare_slot;
			m0_pdclust_instance_map(play_instance, &spare, tgt);
			tfid = target_fid(req, tgt);
		}
		device_state = device_state_prev;
		M0_LOG(M0_DEBUG, "REPAIRED: [%llu:%llu] -> [%llu:%llu] @ tfid "
		       FID_F,
		       spare.sa_group, spare.sa_unit,
		       tgt->ta_frame, tgt->ta_obj, FID_P(&tfid));
	}

	session = target_session(req, tfid);

	rc = nw_xfer_tioreq_get(xfer, &tfid, session,
				layout_unit_size(play) * req->ir_iomap_nr,
				out);

	if (M0_IN(ioreq_sm_state(req), (IRS_DEGRADED_READING,
					IRS_DEGRADED_WRITING)) &&
	    device_state != M0_PNDS_SNS_REPAIRED)
		(*out)->ti_state = device_state;
	return M0_RC(rc);
}

static int target_ioreq_init(struct target_ioreq    *ti,
			     struct nw_xfer_request *xfer,
			     const struct m0_fid    *cobfid,
			     struct m0_rpc_session  *session,
			     uint64_t		     size)
{
	int rc;

	M0_ENTRY("target_ioreq %p, nw_xfer_request %p, "FID_F,
		 ti, xfer, FID_P(cobfid));
	M0_PRE(ti      != NULL);
	M0_PRE(xfer    != NULL);
	M0_PRE(cobfid  != NULL);
	M0_PRE(session != NULL);
	M0_PRE(size    >  0);

	ti->ti_rc        = 0;
	ti->ti_ops       = &tioreq_ops;
	ti->ti_fid       = *cobfid;
	ti->ti_nwxfer    = xfer;
	ti->ti_dgvec     = NULL;
	/*
	 * Target object is usually in ONLINE state unless explicitly
	 * told otherwise.
	 */
	ti->ti_state     = M0_PNDS_ONLINE;
	ti->ti_session   = session;
	ti->ti_parbytes  = 0;
	ti->ti_databytes = 0;

	iofops_tlist_init(&ti->ti_iofops);
	tioreqht_tlink_init(ti);
	target_ioreq_bob_init(ti);

	rc = m0_indexvec_alloc(&ti->ti_ivec, page_nr(size),
	                       &m0t1fs_addb_ctx,
	                       M0T1FS_ADDB_LOC_TI_REQ_INIT_IV);
	if (rc != 0)
		goto out;

	ti->ti_bufvec.ov_vec.v_nr = page_nr(size);
	M0_ALLOC_ARR_ADDB(ti->ti_bufvec.ov_vec.v_count,
	                  ti->ti_bufvec.ov_vec.v_nr, &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_IOREQ_INIT_BVECC,
	                  &m0t1fs_addb_ctx);
	if (ti->ti_bufvec.ov_vec.v_count == NULL)
		goto fail;

	M0_ALLOC_ARR_ADDB(ti->ti_bufvec.ov_buf, ti->ti_bufvec.ov_vec.v_nr,
	                  &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_IOREQ_INIT_BVECB,
	                  &m0t1fs_addb_ctx);
	if (ti->ti_bufvec.ov_buf == NULL)
		goto fail;

	M0_ALLOC_ARR_ADDB(ti->ti_pageattrs, ti->ti_bufvec.ov_vec.v_nr,
	                  &m0_addb_gmc,
			  M0T1FS_ADDB_LOC_IOREQ_INIT_PGATTRS,
	                  &m0t1fs_addb_ctx);
	if (ti->ti_pageattrs == NULL)
		goto fail;

	/*
	 * This value is incremented when new segments are added to the
	 * index vector in target_ioreq_seg_add().
	 */
	ti->ti_ivec.iv_vec.v_nr = 0;

	M0_POST_EX(target_ioreq_invariant(ti));
	return M0_RC(0);
fail:
	m0_indexvec_free(&ti->ti_ivec);
	m0_free(ti->ti_bufvec.ov_vec.v_count);
	m0_free(ti->ti_bufvec.ov_buf);
out:
	return M0_ERR(-ENOMEM, "Failed to allocate memory in "
		       "target_ioreq_init");
}

static void target_ioreq_fini(struct target_ioreq *ti)
{
	M0_ENTRY("target_ioreq %p", ti);
	M0_PRE_EX(target_ioreq_invariant(ti));

	target_ioreq_bob_fini(ti);
	tioreqht_tlink_fini(ti);
	iofops_tlist_fini(&ti->ti_iofops);
	ti->ti_ops     = NULL;
	ti->ti_session = NULL;
	ti->ti_nwxfer  = NULL;

	/* Resets the number of segments in vector. */
	if (ti->ti_ivec.iv_vec.v_nr == 0)
		ti->ti_ivec.iv_vec.v_nr = ti->ti_bufvec.ov_vec.v_nr;

	m0_indexvec_free(&ti->ti_ivec);
	m0_free0(&ti->ti_bufvec.ov_buf);
	m0_free0(&ti->ti_bufvec.ov_vec.v_count);
	m0_free0(&ti->ti_pageattrs);
	if (ti->ti_dgvec != NULL)
		dgmode_rwvec_dealloc_fini(ti->ti_dgvec);
	M0_LEAVE();
}

static struct target_ioreq *target_ioreq_locate(struct nw_xfer_request *xfer,
						struct m0_fid	       *fid)
{
	struct target_ioreq *ti;

	M0_ENTRY("nw_xfer_request %p, fid %p", xfer, fid);
	M0_PRE_EX(nw_xfer_request_invariant(xfer));
	M0_PRE(fid != NULL);

	ti = tioreqht_htable_lookup(&xfer->nxr_tioreqs_hash, &fid->f_container);
	M0_ASSERT(ergo(ti != NULL, m0_fid_cmp(fid, &ti->ti_fid) == 0));

	M0_LEAVE();
	return ti;
}

static int nw_xfer_tioreq_get(struct nw_xfer_request *xfer,
			      struct m0_fid	     *fid,
			      struct m0_rpc_session  *session,
			      uint64_t		      size,
			      struct target_ioreq   **out)
{
	int		     rc = 0;
	struct target_ioreq *ti;
	struct io_request   *req;

	M0_PRE_EX(nw_xfer_request_invariant(xfer));
	M0_PRE(fid     != NULL);
	M0_PRE(session != NULL);
	M0_PRE(out     != NULL);
	M0_ENTRY("nw_xfer_request %p, "FID_F, xfer, FID_P(fid));

	ti = target_ioreq_locate(xfer, fid);
	if (ti == NULL) {
		M0_ALLOC_PTR_ADDB(ti, &m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_TIOREQ_GET_TI,
		                  &m0t1fs_addb_ctx);
		if (ti == NULL)
			return M0_ERR(-ENOMEM, "Failed to allocate memory"
				       "for target_ioreq");

		rc = target_ioreq_init(ti, xfer, fid, session, size);
		if (rc == 0) {
			tioreqht_htable_add(&xfer->nxr_tioreqs_hash, ti);
			M0_LOG(M0_INFO, "New target_ioreq added for "FID_F,
			       FID_P(fid));
		}
		else
			m0_free(ti);
		++iommstats.a_target_ioreq_nr;
	}
	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
	if (ti->ti_dgvec == NULL && M0_IN(ioreq_sm_state(req),
	    (IRS_DEGRADED_READING, IRS_DEGRADED_WRITING)))
		rc = dgmode_rwvec_alloc_init(ti);

	*out = ti;
	return M0_RC(rc);
}

static struct data_buf *data_buf_alloc_init(enum page_attr pattr)
{
	struct data_buf *buf;
	unsigned long	 addr;

	M0_ENTRY();
	addr = get_zeroed_page(GFP_KERNEL);
	if (addr == 0) {
		M0_LOG(M0_ERROR, "Failed to get free page");
		return NULL;
	}

	++iommstats.a_page_nr;
	M0_ALLOC_PTR_ADDB(buf, &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_DBUF_ALLOI_BUF,
	                  &m0t1fs_addb_ctx);
	if (buf == NULL) {
		free_page(addr);
		M0_LOG(M0_ERROR, "Failed to allocate data_buf");
		return NULL;
	}

	++iommstats.a_data_buf_nr;
	data_buf_init(buf, (void *)addr, pattr);
	M0_POST(data_buf_invariant(buf));
	M0_LEAVE();
	return buf;
}

static void buf_page_free(struct m0_buf *buf)
{
	M0_PRE(buf != NULL);

	free_page((unsigned long)buf->b_addr);
	++iommstats.d_page_nr;
	buf->b_addr = NULL;
	buf->b_nob  = 0;
}

static void data_buf_dealloc_fini(struct data_buf *buf)
{
	M0_ENTRY("data_buf %p", buf);
	M0_PRE(data_buf_invariant(buf));

	if (buf->db_buf.b_addr != NULL)
		buf_page_free(&buf->db_buf);

	if (buf->db_auxbuf.b_addr != NULL)
		buf_page_free(&buf->db_auxbuf);

	data_buf_fini(buf);
	m0_free(buf);
	++iommstats.d_data_buf_nr;
	M0_LEAVE();
}

static void target_ioreq_seg_add(struct target_ioreq              *ti,
				 const struct m0_pdclust_src_addr *src,
				 const struct m0_pdclust_tgt_addr *tgt,
				 m0_bindex_t	                   gob_offset,
				 m0_bcount_t	                   count,
				 struct pargrp_iomap              *map)
{
	uint32_t		   seg;
	m0_bindex_t		   toff;
	m0_bindex_t		   goff;
	m0_bindex_t		   pgstart;
	m0_bindex_t		   pgend;
	struct data_buf		  *buf;
	struct io_request	  *req;
	struct m0_pdclust_layout  *play;
	uint64_t	           frame = tgt->ta_frame;
	uint64_t	           unit  = src->sa_unit;
	struct m0_indexvec        *ivec;
	struct m0_bufvec          *bvec;
	enum m0_pdclust_unit_type  unit_type;
	enum page_attr            *pattr;

	M0_ENTRY("tio req %p, gob_offset %llu, count %llu frame %llu unit %llu",
		 ti, gob_offset, count, frame, unit);
	M0_PRE_EX(target_ioreq_invariant(ti));
	M0_PRE(map != NULL);

	req	= bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
			 &ioreq_bobtype);
	play	= pdlayout_get(req);

	unit_type = m0_pdclust_unit_classify(play, unit);
	M0_ASSERT(M0_IN(unit_type, (M0_PUT_DATA, M0_PUT_PARITY)));

	toff	= target_offset(frame, play, gob_offset);
	pgstart = toff;
	goff    = unit_type == M0_PUT_DATA ? gob_offset : 0;

	M0_LOG(M0_DEBUG, "[gpos %llu, count %llu] [%llu,%llu]->[%llu,%llu] %c",
	       gob_offset, count, src->sa_group, src->sa_unit,
	       tgt->ta_frame, tgt->ta_obj,
	       unit_type == M0_PUT_DATA ? 'D' : 'P');

	if (ioreq_sm_state(req) == IRS_DEGRADED_READING ||
	    (ioreq_sm_state(req) == IRS_DEGRADED_WRITING &&
	     req->ir_sns_state   == SRS_REPAIR_DONE)) {
		M0_ASSERT(ti->ti_dgvec != NULL);
		ivec  = &ti->ti_dgvec->dr_ivec;
		bvec  = &ti->ti_dgvec->dr_bufvec;
		pattr = ti->ti_dgvec->dr_pageattrs;
	} else {
		ivec  = &ti->ti_ivec;
		bvec  = &ti->ti_bufvec;
		pattr = ti->ti_pageattrs;
	}

	while (pgstart < toff + count) {
		pgend = min64u(pgstart + PAGE_CACHE_SIZE, toff + count);
		seg   = SEG_NR(ivec);

		INDEX(ivec, seg) = pgstart;
		COUNT(ivec, seg) = pgend - pgstart;

		bvec->ov_vec.v_count[seg] = pgend - pgstart;

		if (unit_type == M0_PUT_DATA) {
			uint32_t row;
			uint32_t col;

			page_pos_get(map, goff, &row, &col);
			buf = map->pi_databufs[row][col];

			pattr[seg] |= PA_DATA;
			M0_LOG(M0_DEBUG, "Data seg added");
		} else {
			buf = map->pi_paritybufs[page_id(goff)]
						[unit % data_col_nr(play)];
			pattr[seg] |= PA_PARITY;
			M0_LOG(M0_DEBUG, "Parity seg added");
		}

		bvec->ov_buf[seg]  = buf->db_buf.b_addr;
		pattr[seg] |= buf->db_flags;
		M0_LOG(M0_DEBUG, "pageaddr = %p, index = %llu, size = %llu\n",
			bvec->ov_buf[seg], INDEX(ivec, seg), COUNT(ivec, seg));
		M0_LOG(M0_DEBUG, "Seg id %d [%llu, %llu] added to target_ioreq "
		       "with "FID_F" with flags 0x%x: ", seg,
		       INDEX(ivec, seg), COUNT(ivec, seg),
		       FID_P(&ti->ti_fid), pattr[seg]);

		goff += COUNT(ivec, seg);
		++ivec->iv_vec.v_nr;
		pgstart = pgend;
	}
	M0_LEAVE();
}

static int io_req_fop_init(struct io_req_fop   *fop,
			   struct target_ioreq *ti,
			   enum page_attr       pattr)
{
	int		   rc;
	struct io_request *req;

	M0_ENTRY("io_req_fop %p, target_ioreq %p", fop, ti);
	M0_PRE(fop != NULL);
	M0_PRE(ti  != NULL);
	M0_PRE(M0_IN(pattr, (PA_DATA, PA_PARITY)));

	io_req_fop_bob_init(fop);
	iofops_tlink_init(fop);
	fop->irf_pattr     = pattr;
	fop->irf_tioreq	   = ti;
	fop->irf_ops       = &irfop_ops;
	fop->irf_reply_rc  = 0;
	fop->irf_ast.sa_cb = io_bottom_half;

	req = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
		     &ioreq_bobtype);
	M0_ASSERT(M0_IN(ioreq_sm_state(req),
		  (IRS_READING, IRS_DEGRADED_READING,
		   IRS_WRITING, IRS_DEGRADED_WRITING)));

	fop->irf_ast.sa_mach = &req->ir_sm;

	rc  = m0_io_fop_init(&fop->irf_iofop, file_to_fid(req->ir_file),
			     M0_IN(ioreq_sm_state(req),
			           (IRS_WRITING, IRS_DEGRADED_WRITING)) ?
			     &m0_fop_cob_writev_fopt : &m0_fop_cob_readv_fopt,
			     io_req_fop_release);
	/*
	 * Changes ri_ops of rpc item so as to execute m0t1fs's own
	 * callback on receiving a reply.
	 */
	fop->irf_iofop.if_fop.f_item.ri_ops = &m0t1fs_item_ops;

	M0_POST(ergo(rc == 0, io_req_fop_invariant(fop)));
	return M0_RC(rc);
}

static void io_req_fop_fini(struct io_req_fop *fop)
{
	M0_ENTRY("io_req_fop %p", fop);
	M0_PRE(io_req_fop_invariant(fop));

	/*
	 * IO fop is finalized (m0_io_fop_fini()) through rpc sessions code
	 * using m0_rpc_item::m0_rpc_item_ops::rio_free().
	 * see m0_io_item_free().
	 */

	iofops_tlink_fini(fop);
	fop->irf_ops = NULL;

	/*
	 * io_req_bob_fini() is not done here so that struct io_req_fop
	 * can be retrieved from struct m0_rpc_item using bob_of() and
	 * magic numbers can be checked.
	 */

	fop->irf_tioreq = NULL;
	fop->irf_ast.sa_cb = NULL;
	fop->irf_ast.sa_mach = NULL;
	M0_LEAVE();
}

static void irfop_fini(struct io_req_fop *irfop)
{
	M0_ENTRY("io_req_fop %p", irfop);
	M0_PRE(irfop != NULL);

	m0_rpc_bulk_buflist_empty(&irfop->irf_iofop.if_rbulk);
	io_req_fop_fini(irfop);
	m0_free(irfop);
	M0_LEAVE();
}

static m0_time_t m0t1fs_addb_interval = M0_MKTIME(M0_ADDB_DEF_STAT_PERIOD_S, 0);

static void m0t1fs_addb_stat_post_counters(struct m0t1fs_sb *csb)
{
	int              i;
	static m0_time_t next_post;
	m0_time_t        now = m0_time_now();

#undef CNTR_POST
#define CNTR_POST(_mode_, _n_)						\
	io_stats = &csb->csb_##_mode_##_stats[i];			\
	m0_addb_post_cntr(&m0_addb_gmc,					\
			  M0_ADDB_CTX_VEC(&m0t1fs_addb_ctx),		\
			  &io_stats->ais_##_n_##_cntr);

	if (now >= next_post || next_post == 0) {
		for (i = 0; i < ARRAY_SIZE(csb->csb_io_stats); ++i) {
			struct m0_addb_io_stats *io_stats;

			CNTR_POST(io, sizes);
			CNTR_POST(io, times);
			CNTR_POST(dgio, sizes);
			CNTR_POST(dgio, times);
			next_post = m0_time_add(now, m0t1fs_addb_interval);
		}
	}

#undef CNTR_POST
}

/*
 * This function can be used by the ioctl which supports fully vectored
 * scatter-gather IO. The caller is supposed to provide an index vector
 * aligned with user buffers in struct iovec array.
 * This function is also used by file->f_op->aio_{read/write} path.
 */
M0_INTERNAL ssize_t m0t1fs_aio(struct kiocb       *kcb,
			       const struct iovec *iov,
			       struct m0_indexvec *ivec,
			       enum io_req_type    rw)
{
	int                      rc;
	ssize_t                  count;
	struct io_request       *req;
	struct m0_addb_io_stats *stats;
	struct m0t1fs_sb        *csb;
	m0_time_t                start;
	uint64_t                 time_io;

	M0_ENTRY("indexvec %p, rw %d", ivec, rw);
	M0_PRE(kcb  != NULL);
	M0_PRE(iov  != NULL);
	M0_PRE(ivec != NULL);
	M0_PRE(M0_IN(rw, (IRT_READ, IRT_WRITE)));

	start = m0_time_now();
	csb   = file_to_sb(kcb->ki_filp);
again:
	M0_ALLOC_PTR_ADDB(req, &m0_addb_gmc, M0T1FS_ADDB_LOC_AIO_REQ,
	                  &m0t1fs_addb_ctx);
	if (req == NULL)
		return M0_ERR(-ENOMEM, "Failed to allocate memory"
			       " for io_request");
	++iommstats.a_ioreq_nr;

	rc = io_request_init(req, kcb->ki_filp, iov, ivec, rw);
	if (rc != 0) {
		count = 0;
		goto last;
	}

	rc = req->ir_ops->iro_iomaps_prepare(req);
	if (rc != 0) {
		io_request_fini(req);
		count = 0;
		goto last;
	}

	rc = req->ir_nwxfer.nxr_ops->nxo_distribute(&req->ir_nwxfer);
	if (rc != 0) {
		req->ir_ops->iro_iomaps_destroy(req);
		req->ir_nwxfer.nxr_state = NXS_COMPLETE;
		io_request_fini(req);
		count = 0;
		goto last;
	}

	rc = req->ir_ops->iro_iosm_handle(req);
	if (rc == 0)
		rc = req->ir_rc;
	M0_LOG(M0_INFO, "nxr_bytes = %llu, copied_nr = %llu",
		req->ir_nwxfer.nxr_bytes, req->ir_copied_nr);
	count = min64u(req->ir_nwxfer.nxr_bytes, req->ir_copied_nr);

	req->ir_ops->iro_iomaps_destroy(req);

	io_request_fini(req);
last:
	m0_free(req);
	++iommstats.d_ioreq_nr;

	M0_LOG(M0_DEBUG, "rc = %d, io request returned %lu bytes", rc, count);
	if (rc == -EAGAIN)
		goto again;

	time_io = m0_time_sub(m0_time_now(), start);
	stats = &csb->csb_io_stats[rw == IRT_WRITE ? 1 : 0];
	m0_addb_counter_update(&stats->ais_times_cntr, (uint64_t) time_io /
			       1000); /* uS */
	m0_addb_counter_update(&stats->ais_sizes_cntr, (uint64_t) count);
	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_m0t1fs_io_finish,
		     M0_ADDB_CTX_VEC(&m0t1fs_addb_ctx), rw, count, time_io);
	m0t1fs_addb_stat_post_counters(csb);

	M0_LEAVE();
	return rc != 0 ? rc : count;
}

static struct m0_indexvec *indexvec_create(unsigned long       seg_nr,
					   const struct iovec *iov,
					   loff_t	       pos,
					   const unsigned      loc)
{
	int		    rc;
	uint32_t	    i;
	struct m0_indexvec *ivec;

	/*
	 * Apparently, we need to use a new API to process io request
	 * which can accept m0_indexvec so that it can be reused by
	 * the ioctl which provides fully vectored scatter-gather IO
	 * to cluster library users.
	 * For that, we need to prepare a m0_indexvec and supply it
	 * to this function.
	 */
	M0_ENTRY("seg_nr %lu position %llu", seg_nr, pos);
	M0_ALLOC_PTR_ADDB(ivec, &m0_addb_gmc,
	                  M0T1FS_ADDB_LOC_IVEC_CREAT_IV,
	                  &m0t1fs_addb_ctx);
	if (ivec == NULL) {
		M0_LEAVE();
		return NULL;
	}

	rc = m0_indexvec_alloc(ivec, seg_nr, &m0t1fs_addb_ctx, loc);
	if (rc != 0) {
		m0_free(ivec);
		M0_LEAVE();
		return NULL;
	}

	for (i = 0; i < seg_nr; ++i) {
		ivec->iv_index[i] = pos;
		ivec->iv_vec.v_count[i] = iov[i].iov_len;
		pos += iov[i].iov_len;
	}
	M0_POST(m0_vec_count(&ivec->iv_vec) > 0);

	M0_LEAVE();
	return ivec;
}

static ssize_t file_aio_write(struct kiocb	 *kcb,
			      const struct iovec *iov,
			      unsigned long	  seg_nr,
			      loff_t		  pos)
{
	int		    rc;
	size_t		    count = 0;
	size_t		    saved_count;
	struct m0_indexvec *ivec;

	M0_ENTRY("struct iovec %p position %llu seg_nr %lu", iov, pos, seg_nr);
	M0_PRE(kcb != NULL);
	M0_PRE(iov != NULL);
	M0_PRE(seg_nr > 0);

	if (!file_to_sb(kcb->ki_filp)->csb_active) {
		M0_LEAVE();
		return -EINVAL;
	}

	rc = generic_segment_checks(iov, &seg_nr, &count, VERIFY_READ);
	if (rc != 0) {
		M0_LEAVE();
		return 0;
	}

	saved_count = count;
	rc = generic_write_checks(kcb->ki_filp, &pos, &count, 0);
	if (rc != 0 || count == 0) {
		M0_LEAVE();
		return 0;
	}

	if (count != saved_count)
		seg_nr = iov_shorten((struct iovec *)iov, seg_nr, count);

	ivec = indexvec_create(seg_nr, iov, pos,
	                       M0T1FS_ADDB_LOC_AIO_WRITE);
	if (ivec == NULL) {
		M0_LEAVE();
		return 0;
	}

	M0_LOG(M0_INFO, "Write vec-count = %llu seg_nr %lu",
			m0_vec_count(&ivec->iv_vec), seg_nr);
	count = m0t1fs_aio(kcb, iov, ivec, IRT_WRITE);

	/* Updates file position. */
	kcb->ki_pos = pos + count;

	m0_indexvec_free(ivec);
	m0_free(ivec);
	M0_LEAVE();
	return count;
}

static ssize_t file_aio_read(struct kiocb	*kcb,
			     const struct iovec *iov,
			     unsigned long	 seg_nr,
			     loff_t		 pos)
{
	int		    seg;
	ssize_t		    count = 0;
	ssize_t		    res;
	struct inode	   *inode;
	struct m0_indexvec *ivec;

	M0_ENTRY("struct iovec %p position %llu", iov, pos);
	M0_PRE(kcb != NULL);
	M0_PRE(iov != NULL);
	M0_PRE(seg_nr > 0);

	/* Returns if super block is inactive. */
	if (!file_to_sb(kcb->ki_filp)->csb_active) {
		M0_LEAVE();
		return -EINVAL;
	}

	/*
	 * Checks for access privileges and adjusts all segments
	 * for proper count and total number of segments.
	 */
	res = generic_segment_checks(iov, &seg_nr, &count, VERIFY_WRITE);
	if (res != 0) {
		M0_LEAVE();
		return res;
	}

	/* Index vector has to be created before io_request is created. */
	ivec = indexvec_create(seg_nr, iov, pos,
	                       M0T1FS_ADDB_LOC_AIO_READ);
	if (ivec == NULL)
		return M0_RC(0);

	/*
	 * For read IO, if any segment from index vector goes beyond EOF,
	 * they are dropped and the index vector is truncated to EOF boundary.
	 */
	inode = kcb->ki_filp->f_dentry->d_inode;
	for (seg = 0; seg < SEG_NR(ivec); ++seg) {
		if (INDEX(ivec, seg) > inode->i_size) {
			ivec->iv_vec.v_nr = seg + 1;
			break;
		}
		if (seg_endpos(ivec, seg) > inode->i_size) {
			COUNT(ivec, seg) = inode->i_size - INDEX(ivec, seg);
			ivec->iv_vec.v_nr = seg + 1;
			break;
		}
	}

	if (m0_vec_count(&ivec->iv_vec) == 0) {
		m0_indexvec_free(ivec);
		m0_free(ivec);
		return M0_RC(0);
	}

	M0_LOG(M0_INFO, "Read vec-count = %llu", m0_vec_count(&ivec->iv_vec));
	count = m0t1fs_aio(kcb, iov, ivec, IRT_READ);

	/* Updates file position. */
	kcb->ki_pos = pos + count;

	m0_indexvec_free(ivec);
	m0_free(ivec);
	M0_LEAVE();
	return count;
}

const struct file_operations m0t1fs_reg_file_operations = {
	.llseek	   = generic_file_llseek,
	.aio_read  = file_aio_read,
	.aio_write = file_aio_write,
	.read	   = do_sync_read,
	.write	   = do_sync_write,
	.fsync     = simple_fsync,  /* XXX: just to prevent -EINVAL */
};

static int io_fops_async_submit(struct m0_io_fop      *iofop,
				struct m0_rpc_session *session,
				struct m0_addb_ctx    *addb_ctx)
{
	int		      rc;
	struct m0_fop_cob_rw *rwfop;

	M0_ENTRY("m0_io_fop %p m0_rpc_session %p", iofop, session);
	M0_PRE(iofop   != NULL);
	M0_PRE(session != NULL);

	rwfop = io_rw_get(&iofop->if_fop);
	M0_ASSERT(rwfop != NULL);

	rc = m0_rpc_bulk_store(&iofop->if_rbulk, session->s_conn,
			       rwfop->crw_desc.id_descs);
	if (rc != 0)
		goto out;

	rc = m0_addb_ctx_export(addb_ctx, &rwfop->crw_addb_ctx_id);
	if (rc != 0)
		goto out;

	iofop->if_fop.f_item.ri_session = session;
	rc = m0_rpc_post(&iofop->if_fop.f_item);
	M0_LOG(M0_INFO, "IO fops submitted to rpc, rc = %d", rc);

out:
	return M0_RC(rc);
}

static void io_req_fop_release(struct m0_ref *ref)
{
	struct m0_fop	  *fop;
	struct m0_io_fop  *iofop;
	struct io_req_fop *reqfop;
	struct m0_fop_cob_rw *rwfop;

	M0_ENTRY("ref %p", ref);
	M0_PRE(ref != NULL);

	fop    = container_of(ref, struct m0_fop, f_ref);
	iofop  = container_of(fop, struct m0_io_fop, if_fop);
	reqfop = bob_of(iofop, struct io_req_fop, irf_iofop, &iofop_bobtype);
	/* see io_req_fop_fini(). */
	io_req_fop_bob_fini(reqfop);

	/*
	 * Release the net buffers if rpc bulk object is still dirty.
	 * And wait on channel till all net buffers are deleted from
	 * transfer machine.
	 */
	if (!m0_tlist_is_empty(&rpcbulk_tl,
			       &reqfop->irf_iofop.if_rbulk.rb_buflist)) {
		struct m0_clink clink;

		m0_clink_init(&clink, NULL);
		m0_clink_add(&reqfop->irf_iofop.if_rbulk.rb_chan, &clink);
		m0_rpc_bulk_store_del(&reqfop->irf_iofop.if_rbulk);

		m0_chan_wait(&clink);
		m0_clink_del(&clink);
		m0_clink_fini(&clink);
	}

	rwfop = io_rw_get(&iofop->if_fop);
	M0_ASSERT(rwfop != NULL);
	if (rwfop->crw_addb_ctx_id.au64s_nr > 0)
		m0_addb_ctx_id_free(&rwfop->crw_addb_ctx_id);

	m0_io_fop_fini(iofop);
	m0_free(reqfop);
	++iommstats.d_io_req_fop_nr;
}

static void io_rpc_item_cb(struct m0_rpc_item *item)
{
	struct m0_fop	  *fop;
	struct m0_fop     *rep_fop;
	struct m0_io_fop  *iofop;
	struct io_req_fop *reqfop;
	struct io_request *ioreq;

	M0_ENTRY("rpc_item %p", item);
	M0_PRE(item != NULL);

	fop    = m0_rpc_item_to_fop(item);
	iofop  = container_of(fop, struct m0_io_fop, if_fop);
	reqfop = bob_of(iofop, struct io_req_fop, irf_iofop, &iofop_bobtype);
	ioreq  = bob_of(reqfop->irf_tioreq->ti_nwxfer, struct io_request,
			ir_nwxfer, &ioreq_bobtype);
	/*
	 * Acquires a reference on IO reply fop since its contents
	 * are needed for policy decisions in io_bottom_half().
	 * io_bottom_half() takes care of releasing the reference.
	 */
	rep_fop = m0_rpc_item_to_fop(item->ri_reply);
	m0_fop_get(rep_fop);

	M0_LOG(M0_INFO, "io_req_fop %p, target_ioreq %p io_request %p",
			reqfop, reqfop->irf_tioreq, ioreq);
	m0_sm_ast_post(ioreq->ir_sm.sm_grp, &reqfop->irf_ast);
	M0_LEAVE();
}

static void failure_vector_mismatch(struct io_req_fop *irfop)
{
	struct m0_pool_version_numbers *cli;
	struct m0_pool_version_numbers *srv;
	struct m0t1fs_sb               *csb;
	struct m0_fop                  *reply;
	struct m0_rpc_item             *reply_item;
	struct m0_fop_cob_rw_reply     *rw_reply;
	struct m0_fv_version           *reply_version;
	struct m0_fv_updates           *reply_updates;
	struct m0_pool_event           *event;
	struct io_request              *req;
	uint32_t                        i = 0;

	M0_PRE(irfop != NULL);

	req = bob_of(irfop->irf_tioreq->ti_nwxfer, struct io_request, ir_nwxfer,
		     &ioreq_bobtype);
	csb = file_to_sb(req->ir_file);

	reply_item = irfop->irf_iofop.if_fop.f_item.ri_reply;
	reply      = m0_rpc_item_to_fop(reply_item);
	rw_reply   = io_rw_rep_get(reply);
	reply_version = &rw_reply->rwr_fv_version;
	reply_updates = &rw_reply->rwr_fv_updates;
	srv = (struct m0_pool_version_numbers *)reply_version;
	cli = &csb->csb_pool.po_mach->pm_state->pst_version;
	M0_LOG(M0_DEBUG, ">>>VERSION MISMATCH!");
	m0_poolmach_version_dump(cli);
	m0_poolmach_version_dump(srv);
	/*
	 * Retrieve the latest server version and
	 * updates and apply to the client's copy.
	 * When -EAGAIN is return, this system
	 * call will be restarted.
	 */
	while (i < reply_updates->fvu_count) {
		event = (struct m0_pool_event*)&reply_updates->fvu_events[i];
		m0_poolmach_event_dump(event);
		m0_poolmach_state_transit(csb->csb_pool.po_mach, event, NULL);
		i++;
	}
	M0_LOG(M0_DEBUG, "<<<VERSION MISMATCH!");
}

M0_INTERNAL struct m0t1fs_sb *m0_fop_to_sb(struct m0_fop *fop)
{
	struct m0_io_fop  *iofop;
	struct io_req_fop *irfop;
	struct io_request *ioreq;

	iofop = container_of(fop, struct m0_io_fop, if_fop);
	irfop = bob_of(iofop, struct io_req_fop, irf_iofop, &iofop_bobtype);
	ioreq  = bob_of(irfop->irf_tioreq->ti_nwxfer, struct io_request,
			ir_nwxfer, &ioreq_bobtype);
	return file_to_sb(ioreq->ir_file);
}

static void io_bottom_half(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	int                         rc;
	struct io_req_fop          *irfop;
	struct io_request          *req;
	struct target_ioreq        *tioreq;
	struct m0_fop              *reply_fop = NULL;
	struct m0_rpc_item         *req_item;
	struct m0_rpc_item         *reply_item;
	struct m0_fop_cob_rw_reply *rw_reply;

	M0_ENTRY("sm_group %p sm_ast %p", grp, ast);
	M0_PRE(grp != NULL);
	M0_PRE(ast != NULL);

	irfop  = bob_of(ast, struct io_req_fop, irf_ast, &iofop_bobtype);
	tioreq = irfop->irf_tioreq;
	req    = bob_of(tioreq->ti_nwxfer, struct io_request, ir_nwxfer,
			&ioreq_bobtype);

	M0_ASSERT(M0_IN(irfop->irf_pattr, (PA_DATA, PA_PARITY)));
	M0_ASSERT(M0_IN(ioreq_sm_state(req), (IRS_READING, IRS_WRITING,
					      IRS_DEGRADED_READING,
					      IRS_DEGRADED_WRITING)));

	req_item   = &irfop->irf_iofop.if_fop.f_item;
	reply_item = req_item->ri_reply;
	rc = req_item->ri_error ?: m0_rpc_item_generic_reply_rc(reply_item);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "reply error: rc=%d", rc);
		goto ref_dec;
	}
	M0_ASSERT(reply_item != NULL &&
		  !m0_rpc_item_is_generic_reply_fop(reply_item));
	reply_fop = m0_rpc_item_to_fop(reply_item);
	if (!m0_is_io_fop_rep(reply_fop)) {
		M0_LOG(M0_ERROR, "invalid fop reply rcvd: %s",
			reply_fop->f_type->ft_name);
	}
	rw_reply  = io_rw_rep_get(reply_fop);
	rc        = rw_reply->rwr_rc;
	req->ir_sns_state = rw_reply->rwr_repair_done;
	M0_LOG(M0_INFO, "reply received = %d, sns state = %d", rc,
			 req->ir_sns_state);

	if (rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
		M0_ASSERT(rw_reply != NULL);
		M0_LOG(M0_INFO, "VERSION_MISMATCH received on "FID_F,
		       FID_P(&tioreq->ti_fid));
		failure_vector_mismatch(irfop);
	}
	irfop->irf_reply_rc = rc;

	if (tioreq->ti_rc == 0)
		tioreq->ti_rc = rc;

	if (tioreq->ti_nwxfer->nxr_rc == 0) {
		tioreq->ti_nwxfer->nxr_rc = rc;
		M0_LOG(M0_INFO, "nwxfer rc = %d", tioreq->ti_nwxfer->nxr_rc);
	}

	if (irfop->irf_pattr == PA_DATA) {
		tioreq->ti_databytes += irfop->irf_iofop.if_rbulk.rb_bytes;
		M0_LOG(M0_INFO, "Returned no of bytes = %llu",
		       irfop->irf_iofop.if_rbulk.rb_bytes);
	} else
		tioreq->ti_parbytes  += irfop->irf_iofop.if_rbulk.rb_bytes;

ref_dec:
	/* Drops reference on reply fop. */
	m0_fop_put(reply_fop);
	M0_CNT_DEC(tioreq->ti_nwxfer->nxr_iofop_nr);
	m0_atomic64_dec(&file_to_sb(req->ir_file)->csb_pending_io_nr);
	M0_LOG(M0_INFO, "Due io fops = %llu", tioreq->ti_nwxfer->nxr_iofop_nr);

	if (tioreq->ti_nwxfer->nxr_iofop_nr == 0)
		m0_sm_state_set(&req->ir_sm,
				(M0_IN(ioreq_sm_state(req),
				 (IRS_READING, IRS_DEGRADED_READING)) ?
				IRS_READ_COMPLETE : IRS_WRITE_COMPLETE));

	M0_LEAVE();
}

static int nw_xfer_req_dispatch(struct nw_xfer_request *xfer)
{
	int		         rc = 0;
	struct io_req_fop       *irfop;
	struct io_request       *req;
	struct target_ioreq     *ti;
	struct m0_addb_ctx_type *ct;
	struct m0t1fs_sb        *csb;

	M0_ENTRY();

	M0_PRE(xfer != NULL);
	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);

	if (ioreq_sm_state(req) == IRS_READING)
		ct = &m0_addb_ct_m0t1fs_op_read;
	else
		ct = &m0_addb_ct_m0t1fs_op_write;
	csb = req->ir_file->f_path.mnt->mnt_sb->s_fs_info;
	m0t1fs_fs_lock(csb);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &req->ir_addb_ctx, ct,
	                 &csb->csb_addb_ctx);
	m0t1fs_fs_unlock(csb);

	m0_htable_for(tioreqht, ti, &xfer->nxr_tioreqs_hash) {
		if (ti->ti_state != M0_PNDS_ONLINE) {
			M0_LOG(M0_INFO, "Skipped iofops prepare for "FID_F,
			       FID_P(&ti->ti_fid));
			continue;
		}
		ti->ti_start_time = m0_time_now();
		rc = ti->ti_ops->tio_iofops_prepare(ti, PA_DATA);
		if (rc != 0)
			return M0_ERR(rc, "data fop failed");

		rc = ti->ti_ops->tio_iofops_prepare(ti, PA_PARITY);
		if (rc != 0)
			return M0_ERR(rc, "parity fop failed");
	} m0_htable_endfor;

	m0_htable_for(tioreqht, ti, &xfer->nxr_tioreqs_hash) {
		/* Skips the target device if it is not online. */
		if (ti->ti_state != M0_PNDS_ONLINE) {
			M0_LOG(M0_INFO, "Skipped device "FID_F,
			       FID_P(&ti->ti_fid));
			continue;
		}

		m0_tl_for (iofops, &ti->ti_iofops, irfop) {
			rc = io_fops_async_submit(&irfop->irf_iofop,
						  ti->ti_session,
						  &req->ir_addb_ctx);

			M0_LOG(M0_INFO, "Submitted fops for device "FID_F,
		               FID_P(&ti->ti_fid));
			if (rc != 0)
				goto out;
			else
				m0_atomic64_inc(&file_to_sb(req->ir_file)->
						csb_pending_io_nr);
		} m0_tl_endfor;

	} m0_htable_endfor;

out:
	xfer->nxr_state = NXS_INFLIGHT;
	return M0_RC(rc);
}

static void nw_xfer_req_complete(struct nw_xfer_request *xfer, bool rmw)
{
	struct io_request   *req;
	struct target_ioreq *ti;

	M0_ENTRY("nw_xfer_request %p, rmw %s", xfer,
		 rmw ? (char *)"true" : (char *)"false");
	M0_PRE(xfer != NULL);

	xfer->nxr_state = NXS_COMPLETE;
	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);

	m0_htable_for(tioreqht, ti, &xfer->nxr_tioreqs_hash) {
		struct io_req_fop *irfop;

		/* Maintains only the first error encountered. */
		if (xfer->nxr_rc == 0)
			xfer->nxr_rc = ti->ti_rc;

		xfer->nxr_bytes += ti->ti_databytes;
		ti->ti_databytes = 0;

		if (ti->ti_rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH)
			/* Resets status code before dgmode read IO. */
			ti->ti_rc = 0;

		m0_tl_teardown(iofops, &ti->ti_iofops, irfop) {
			io_req_fop_fini(irfop);
			/* see io_req_fop_release() */
			m0_fop_put(&irfop->irf_iofop.if_fop);
		}

		M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_m0t1fs_cob_io_finish,
			     M0_ADDB_CTX_VEC(&req->ir_addb_ctx, NULL),
			     ti->ti_fid.f_container, ti->ti_fid.f_key,
			     ti->ti_databytes + ti->ti_parbytes,
			     m0_time_sub(m0_time_now(), ti->ti_start_time));
	} m0_htable_endfor;

	M0_LOG(M0_INFO, "Number of bytes %s = %llu",
	       ioreq_sm_state(req) == IRS_READ_COMPLETE ? "read" : "written",
	       xfer->nxr_bytes);

	/*
	 * This function is invoked from 4 states - IRS_READ_COMPLETE,
	 * IRS_WRITE_COMPLETE, IRS_DEGRADED_READING, IRS_DEGRADED_WRITING.
	 * And the state change is applicable only for healthy state IO,
	 * meaning for states IRS_READ_COMPLETE and IRS_WRITE_COMPLETE.
	 */
	if (M0_IN(ioreq_sm_state(req),
		  (IRS_READ_COMPLETE, IRS_WRITE_COMPLETE,
		   IRS_LOCK_RELINQUISHED))) {
		if (!rmw)
			ioreq_sm_state_set(req, IRS_REQ_COMPLETE);
		else if (ioreq_sm_state(req) == IRS_READ_COMPLETE)
			xfer->nxr_bytes = 0;
	}

	req->ir_rc = xfer->nxr_rc;

	m0_addb_ctx_fini(&req->ir_addb_ctx);

	M0_LEAVE();
}

static int io_req_fop_dgmode_read(struct io_req_fop *irfop)
{
	int                         rc;
	uint32_t                    cnt;
	uint32_t                    seg;
	uint32_t                    seg_nr;
	uint64_t                    grpid;
	uint64_t                    unit_size;
	uint64_t                    pgcur = 0;
	m0_bindex_t                *index;
	struct io_request          *req;
	struct m0_rpc_bulk         *rbulk;
	struct pargrp_iomap        *map = NULL;
	struct m0_rpc_bulk_buf     *rbuf;
	struct m0_pdclust_layout   *play;

	M0_PRE(irfop != NULL);
	M0_ENTRY("target fid = "FID_F, FID_P(&irfop->irf_tioreq->ti_fid));

	req       = bob_of(irfop->irf_tioreq->ti_nwxfer, struct io_request,
		           ir_nwxfer, &ioreq_bobtype);
	play      = pdlayout_get(req);
	rbulk     = &irfop->irf_iofop.if_rbulk;
	unit_size = layout_unit_size(play);

	m0_tl_for (rpcbulk, &rbulk->rb_buflist, rbuf) {

		index  = rbuf->bb_zerovec.z_index;
		seg_nr = rbuf->bb_zerovec.z_bvec.ov_vec.v_nr;

		for (seg = 0; seg < seg_nr; ) {

			grpid = pargrp_id_find(*(index + seg), unit_size);
			for (cnt = 1, ++seg; seg < seg_nr; ++seg) {

				M0_ASSERT(ergo(seg > 0, index[seg] >
					       index[seg - 1]));
				M0_ASSERT((index[seg] &
					  (PAGE_CACHE_SHIFT - 1)) == 0);

				if (seg < seg_nr && grpid ==
				    pargrp_id_find(*(index + seg), unit_size))
					++cnt;
				else
					break;
			}
			ioreq_pgiomap_find(req, grpid, &pgcur, &map);
			M0_ASSERT(map != NULL);
			rc = map->pi_ops->pi_dgmode_process(map,
					irfop->irf_tioreq, &index[seg - cnt],
					cnt);
			if (rc != 0)
				return M0_ERR(rc, "Parity group dgmode "
					       "process failed");
		}
	} m0_tl_endfor;
	return M0_RC(0);
}

/*
 * Used in precomputing io fop size while adding rpc bulk buffer and
 * data buffers.
 */
static inline uint32_t io_desc_size(struct m0_net_domain *ndom)
{
	return
		/* size of variables ci_nr and nbd_len */
		M0_MEMBER_SIZE(struct m0_io_indexvec, ci_nr) +
		M0_MEMBER_SIZE(struct m0_net_buf_desc, nbd_len) +
		/* size of nbd_data */
		m0_net_domain_get_max_buffer_desc_size(ndom);
}

static inline uint32_t io_seg_size(void)
{
	return sizeof(struct m0_ioseg);
}

static uint32_t io_di_size(const struct io_request *req)
{
	struct m0_file      *file;
	struct m0t1fs_sb    *sb;
	struct m0_rm_domain *rdom;

	sb = file_to_sb(req->ir_file);
	rdom = m0t1fs_rmsvc_domain_get(&sb->csb_reqh);
	file = m0_resource_to_file(file_to_fid(req->ir_file), rdom->rd_types[M0_RM_FLOCK_RT]);
	if (file->fi_di_ops->do_out_shift(file) == 0)
		return 0;
	return file->fi_di_ops->do_out_shift(file) * M0_DI_ELEMENT_SIZE;
}

static int bulk_buffer_add(struct io_req_fop	   *irfop,
			   struct m0_net_domain	   *dom,
			   struct m0_rpc_bulk_buf **rbuf,
			   uint32_t		   *delta,
			   uint32_t		    maxsize)
{
	int		    rc;
	int		    seg_nr;
	struct io_request  *req;
	struct m0_indexvec *ivec;

	M0_ENTRY("io_req_fop %p net_domain %p delta_size %d",
		 irfop, dom, *delta);
	M0_PRE(irfop  != NULL);
	M0_PRE(dom    != NULL);
	M0_PRE(rbuf   != NULL);
	M0_PRE(delta  != NULL);
	M0_PRE(maxsize > 0);

	req     = bob_of(irfop->irf_tioreq->ti_nwxfer, struct io_request,
			 ir_nwxfer, &ioreq_bobtype);
	ivec    = M0_IN(ioreq_sm_state(req), (IRS_READING, IRS_WRITING)) ||
		  (ioreq_sm_state(req) == IRS_DEGRADED_WRITING &&
		   M0_IN(req->ir_sns_state, (SRS_UNINITIALIZED, SRS_REPAIR_NOTDONE))) ?
		  &irfop->irf_tioreq->ti_ivec :
		  &irfop->irf_tioreq->ti_dgvec->dr_ivec;
	seg_nr  = min32(m0_net_domain_get_max_buffer_segments(dom),
		       SEG_NR(ivec));
	*delta += io_desc_size(dom);

	if (m0_io_fop_size_get(&irfop->irf_iofop.if_fop) + *delta < maxsize) {

		rc = m0_rpc_bulk_buf_add(&irfop->irf_iofop.if_rbulk, seg_nr,
					 dom, NULL, rbuf);
		if (rc != 0) {
			*delta -= io_desc_size(dom);
			return M0_ERR(rc, "Failed to add rpc_bulk_buffer");
		}
	} else {
		rc      = -ENOSPC;
		*delta -= io_desc_size(dom);
	}

	M0_POST(ergo(rc == 0, *rbuf != NULL));
	return M0_RC(rc);
}

static int target_ioreq_iofops_prepare(struct target_ioreq *ti,
				       enum page_attr       filter)
{
	int			        rc = 0;
	uint32_t		        buf = 0;
	/* Number of segments in one m0_rpc_bulk_buf structure. */
	uint32_t		        bbsegs;
	uint32_t		        maxsize;
	uint32_t		        delta;
	enum page_attr		        rw;
	enum page_attr                 *pattr;
	struct m0_bufvec               *bvec;
	struct io_request              *req;
	struct m0_indexvec             *ivec;
	struct io_req_fop              *irfop;
	struct m0_net_domain           *ndom;
	struct m0_rpc_bulk_buf         *rbuf;
	struct m0_pool_version_numbers  curr;
	struct m0_pool_version_numbers *cli;

	M0_ENTRY("prepare io fops for target ioreq %p filter %u, tfid "FID_F,
		 ti, filter, FID_P(&ti->ti_fid));
	M0_PRE_EX(target_ioreq_invariant(ti));
	M0_PRE(M0_IN(filter, (PA_DATA, PA_PARITY)));

	req = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
		     &ioreq_bobtype);
	M0_ASSERT(M0_IN(ioreq_sm_state(req),
		  (IRS_READING, IRS_DEGRADED_READING,
		   IRS_WRITING, IRS_DEGRADED_WRITING)));

	/*
	 * Degraded mode write IO still uses index vector and buffer vector
	 * for target_ioreq object instead of forming a degraded mode vector.
	 * This is an exception, done in order to save the hassle of going
	 * through all pages again and assigning them to degraded mode vectors.
	 */
	if (M0_IN(ioreq_sm_state(req), (IRS_READING, IRS_WRITING)) ||
	    (ioreq_sm_state(req) == IRS_DEGRADED_WRITING &&
	     M0_IN(req->ir_sns_state, (SRS_UNINITIALIZED, SRS_REPAIR_NOTDONE)))) {
		ivec  = &ti->ti_ivec;
		bvec  = &ti->ti_bufvec;
		pattr = ti->ti_pageattrs;
	} else {
		if (ti->ti_dgvec == NULL) {
			return M0_RC(0);
		}
		ivec  = &ti->ti_dgvec->dr_ivec;
		bvec  = &ti->ti_dgvec->dr_bufvec;
		pattr = ti->ti_dgvec->dr_pageattrs;
	}

	ndom	= ti->ti_session->s_conn->c_rpc_machine->rm_tm.ntm_dom;
	rw      = M0_IN(ioreq_sm_state(req),
			(IRS_WRITING, IRS_DEGRADED_WRITING)) ?
		  PA_WRITE :
		  ioreq_sm_state(req) == IRS_DEGRADED_READING ?
		                         PA_DGMODE_READ : PA_READ;
	maxsize = m0_rpc_session_get_max_item_payload_size(ti->ti_session);

	while (buf < SEG_NR(ivec)) {

		delta  = 0;
		bbsegs = 0;

		M0_LOG(M0_DEBUG, "pageattr = %u, filter = %u, rw = %u",
			pattr[buf], filter, rw);

		if (!(pattr[buf] & filter) || !(pattr[buf] & rw)) {
			++buf;
			continue;
		}

		M0_ALLOC_PTR_ADDB(irfop, &m0_addb_gmc,
		                  M0T1FS_ADDB_LOC_TI_FOP_PREP,
		                  &m0t1fs_addb_ctx);
		if (irfop == NULL)
			goto err;

		rc = io_req_fop_init(irfop, ti, filter);
		if (rc != 0) {
			m0_free(irfop);
			goto err;
		}
		++iommstats.a_io_req_fop_nr;

		m0_poolmach_current_version_get(file_to_sb(req->ir_file)->
				                csb_pool.po_mach, &curr);
		cli = (struct m0_pool_version_numbers *)
		      &(io_rw_get(&irfop->irf_iofop.if_fop)->crw_version);
		*cli = curr;

		rc = bulk_buffer_add(irfop, ndom, &rbuf, &delta, maxsize);
		if (rc != 0) {
			io_req_fop_fini(irfop);
			m0_free(irfop);
			goto err;
		}
		delta += io_seg_size();

		/*
		 * Adds io segments and io descriptor only if it fits within
		 * permitted size.
		 */
		while (buf < SEG_NR(ivec) &&
		       m0_io_fop_size_get(&irfop->irf_iofop.if_fop) + delta <
		       maxsize) {

			/*
			 * Adds a page to rpc bulk buffer only if it passes
			 * through the filter.
			 */
			if (pattr[buf] & rw && pattr[buf] & filter) {

				delta += io_seg_size() + io_di_size(req);

				rc = m0_rpc_bulk_buf_databuf_add(rbuf,
						bvec->ov_buf[buf],
						COUNT(ivec, buf),
						INDEX(ivec, buf), ndom);

				if (rc == -EMSGSIZE) {

					/*
					 * Fix the number of segments in
					 * current m0_rpc_bulk_buf structure.
					 */
					rbuf->bb_nbuf->nb_buffer.ov_vec.v_nr =
						bbsegs;
					rbuf->bb_zerovec.z_bvec.ov_vec.v_nr =
						bbsegs;
					bbsegs = 0;

					delta -= io_seg_size() - io_di_size(req);
					rc     = bulk_buffer_add(irfop, ndom,
							&rbuf, &delta, maxsize);
					if (rc == -ENOSPC)
						break;
					else if (rc != 0)
						goto fini_fop;

					/*
					 * Since current bulk buffer is full,
					 * new bulk buffer is added and
					 * existing segment is attempted to
					 * be added to new bulk buffer.
					 */
					continue;
				} else if (rc == 0)
					++bbsegs;
			}
			++buf;
		}

		if (m0_io_fop_byte_count(&irfop->irf_iofop) == 0) {
			irfop_fini(irfop);
			continue;
		}

		rbuf->bb_nbuf->nb_buffer.ov_vec.v_nr = bbsegs;
		rbuf->bb_zerovec.z_bvec.ov_vec.v_nr = bbsegs;

		io_rw_get(&irfop->irf_iofop.if_fop)->crw_fid = ti->ti_fid;

		rc = m0_io_fop_prepare(&irfop->irf_iofop.if_fop);
		if (rc != 0)
			goto fini_fop;

		M0_CNT_INC(ti->ti_nwxfer->nxr_iofop_nr);
		M0_LOG(M0_INFO, "Number of io fops = %llu",
		       ti->ti_nwxfer->nxr_iofop_nr);
		iofops_tlist_add(&ti->ti_iofops, irfop);
	}

	return M0_RC(0);
fini_fop:
	irfop_fini(irfop);
err:
	m0_tl_teardown(iofops, &ti->ti_iofops, irfop) {
		irfop_fini(irfop);
	}

	return M0_ERR(rc, "iofops_prepare failed");
}

const struct inode_operations m0t1fs_reg_inode_operations = {
	.setattr     = m0t1fs_setattr,
	.getattr     = m0t1fs_getattr,
	.setxattr    = m0t1fs_setxattr,
	.getxattr    = m0t1fs_getxattr,
	.listxattr   = m0t1fs_listxattr,
	.removexattr = m0t1fs_removexattr
};

#undef M0_TRACE_SUBSYSTEM
