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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 05/24/2011
 */

#include "rpc/formation.h"
#include "stob/ut/io_fop.h"
#include "lib/thread.h"
#include "lib/misc.h"
#include "colibri/init.h"
#include "lib/ut.h"
#include <stdlib.h>
#include "colibri/init.h"
#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

/*
   *** Current scenario ***
   1. The rpc core component is not ready completely. So this UT tries to
      simulate the things which are not available.
   2. The RPC Formation component

   *** Requirements for UT of formation component. ***
   1. Simulate the end user access pattern.[C2_RPC_FORM_ACCESS_PATERN]
   2. Simulate minimalistic meta data traffic.[C2_RPC_FORM_MD_TRAFFIC]
   3. Simulate sufficient IO traffic to stress all corner cases.
      Ensure multiple requests on same files, multiple IO requests
      on same/different files in same groups so as to stress all
      corner cases. [C2_RPC_FORM_IO_TRAFFIC]
   4. Handle all memory management.[C2_RPC_FORM_MEM_MGMT]
   5. Write maximum asserts to check behavior of component.[C2_RPC_FORM_ASSERTS]
   6. Display statistics on rpc formation.[C2_RPC_FORM_STATS]

   *** A concise design for UT of RPC Formation. ***
   1. Consider a flat hierarchy of a number of files.
   2. These files will be created first.
   3. A certain set of files will be picked up for IO.
   4. All files will be put in some group and there will be sufficient
      multiple IO requests on same files.
   5. Create FOPs for these requests (metadata/IO), assign
 */

/* Some random deadline values for testing purpose only */
#define MIN_NONIO_DEADLINE	0 		// 0 ms
//#define MAX_NONIO_DEADLINE	1		// 1 ns
#define MAX_NONIO_DEADLINE	10000000	// 10 ms
#define MIN_IO_DEADLINE		10000000  	// 10 ms
#define MAX_IO_DEADLINE		100000000 	// 100 ms

/* Some random priority values for testing purpose only */
#define MIN_NONIO_PRIO		0
#define MAX_NONIO_PRIO		5
#define MIN_IO_PRIO		6
#define MAX_IO_PRIO 		10

/* Array of groups */
#define MAX_GRPS		16
struct c2_rpc_group		*rgroup[MAX_GRPS];

uint64_t			 c2_rpc_max_message_size;
uint64_t			 c2_rpc_max_fragments_size;
uint64_t			 c2_rpc_max_rpcs_in_flight;

#define nthreads 		256
struct c2_thread		 form_ut_threads[nthreads];
uint64_t			thread_no = 0;

#define nfops 			256
struct c2_fop			*form_fops[nfops];

uint64_t			 nwrite_iovecs = 0;
struct c2_fop_io_vec		**form_write_iovecs = NULL;

#define nfiles 			64
struct c2_fop_file_fid		*form_fids = NULL;
uint64_t			*file_offsets = NULL;
#define	io_size 		8192
#define nsegs 			8
/* At the moment, we are sending only 3 different types of FOPs,
   namely - file create, file read and file write. */
#define nopcodes 3

struct c2_net_end_point		ep;


/* Function pointer to different FOP creation methods. */
typedef struct c2_fop * (*fopFuncPtr)(void);

struct c2_fop *form_create_write_fop();
struct c2_fop *form_create_read_fop();
struct c2_fop *form_create_file_create_fop();

/* Array of function pointers. */
fopFuncPtr form_fop_table[nopcodes] = {
	&form_create_write_fop, &form_create_read_fop,
	&form_create_file_create_fop
};

#define niopatterns 		8
#define pattern_length 		4
char				file_data_patterns[niopatterns][pattern_length];

/* Defining functions here to avoid linking errors. */
int create_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

int read_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

int write_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

int quit_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

/**
  Alloc and initialize the global array of groups used for UT
 */
int c2_rpc_form_groups_alloc(void)
{
	int		i = 0;
	printf("Inside c2_rpc_form_groups_alloc \n");

	for(i = 0; i < MAX_GRPS; i++) {
		rgroup[i] = c2_alloc(sizeof(struct c2_rpc_group));
		if(rgroup[i] == NULL) {
			return -1;
		}
		c2_list_init(&rgroup[i]->rg_items);
		c2_mutex_init(&rgroup[i]->rg_guard);
		rgroup[i]->rg_expected = 0;
		rgroup[i]->nr_residual = 0;
	}
	return 0;
}

/**
  Deallocate the global array of groups used in UT
 */
int c2_rpc_form_groups_free(void)
{
	int			 i = 0;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;

	printf("Inside c2_rpc_form_groups_free \n");

	for(i = 0; i < MAX_GRPS; i++) {
	        if (!c2_list_is_empty(&rgroup[i]->rg_items)) {
			c2_list_for_each_entry_safe(&rgroup[i]->rg_items,
					item, item_next, struct c2_rpc_item ,
					ri_group_linkage)
					c2_list_del(&item->ri_group_linkage);
		}
		c2_list_fini(&rgroup[i]->rg_items);
		c2_mutex_fini(&rgroup[i]->rg_guard);
		c2_free(rgroup[i]);
	}
	return 0;
}

/**
  Alloc and initialize the items cache
 */
int c2_rpc_form_item_cache_init(void)
{
	printf("Inside c2_rpc_form_item_cache_init \n");

	items_cache = c2_alloc(sizeof(struct c2_rpc_form_items_cache));
	if(items_cache == NULL){
		return -1;
	}
	c2_mutex_init(&items_cache->ic_mutex);
	c2_list_init(&items_cache->ic_cache_list);
	return 0;
}

/**
  Deallocate the items cache
 */
void c2_rpc_form_item_cache_fini(void)
{
	struct c2_rpc_item *item;
	struct c2_rpc_item *item_next;

	printf("Inside c2_rpc_form_item_cache_fini \n");
	c2_mutex_fini(&items_cache->ic_mutex);
	if (!c2_list_is_empty(&items_cache->ic_cache_list)) {
		c2_list_for_each_entry_safe(&items_cache->ic_cache_list,
				item, item_next, struct c2_rpc_item ,
				ri_linkage)
			c2_list_del(&item->ri_linkage);
	}
	c2_list_fini(&items_cache->ic_cache_list);
	c2_free(items_cache);
}

/**
  Invoke Reply received callback for all the items in the cache
*/
void c2_rpc_form_invoke_reply_received(void)
{
        struct c2_rpc_item *item;
        struct c2_rpc_item *item_next;

        printf("Inside c2_rpc_form_invoke_reply_received \n");
        if (!c2_list_is_empty(&items_cache->ic_cache_list)) {
                c2_list_for_each_entry_safe(&items_cache->ic_cache_list,
                                item, item_next, struct c2_rpc_item ,
                                ri_linkage)
			/* Sending same item as req and reply for testing purpose
			   Formation code is anyways going to pick up req item */
			if(item->ri_state == RPC_ITEM_SENT) {
				printf("Calling callback for reply received\n");
				c2_rpc_form_extevt_rpcitem_reply_received(item, item);
			}
        }
}

/**
  Invoke item removed callback for all the items in the cache which 
  are in submitted state
*/
void c2_rpc_form_invoke_item_removed(void)
{
        struct c2_rpc_item *item;
        struct c2_rpc_item *item_next;

        printf("Inside c2_rpc_form_invoke_item_removed \n");
        if (!c2_list_is_empty(&items_cache->ic_cache_list)) {
                c2_list_for_each_entry_safe(&items_cache->ic_cache_list,
                                item, item_next, struct c2_rpc_item ,
                                ri_linkage)
			if(item->ri_state == RPC_ITEM_SUBMITTED) {
				printf("Calling callback for item removed\n");
				c2_rpc_form_extevt_rpcitem_deleted_from_cache(item);
			}
        }
}

/**
  Assign a group to a given RPC item
 */
int c2_rpc_form_item_assign_to_group(struct c2_rpc_group *grp,
		struct c2_rpc_item *item, int grpno)
{
	struct c2_rpc_item	*rpc_item = NULL;
	struct c2_rpc_item	*rpc_item_next = NULL;
	bool			 item_inserted = false;

	printf("Inside c2_rpc_form_item_assign_to_group \n");
	printf("Inside c2_rpc_form_item_assign_to_group, grpid = %d \n", grpno);
	C2_PRE(item !=NULL);

	item->ri_group = grp;
	c2_mutex_lock(&grp->rg_guard);
	grp->rg_expected++;
	grp->rg_grpid = grpno;
	/* Insert by sorted priority in groups list */
	c2_list_for_each_entry_safe(&grp->rg_items,
			rpc_item, rpc_item_next,
			struct c2_rpc_item, ri_group_linkage){
		if (item->ri_prio <= rpc_item->ri_prio) {
			c2_list_add_before(&rpc_item->ri_group_linkage,
					&item->ri_group_linkage);
			item_inserted = true;
			break;
		}

	}
	if(!item_inserted) {
		c2_list_add_after(&rpc_item->ri_group_linkage,
				&item->ri_group_linkage);
	}
	c2_mutex_unlock(&grp->rg_guard);
	return 0;
}

/**
  Assign a deadline to a given RPC item
 */
int c2_rpc_form_item_assign_deadline(struct c2_rpc_item *item,
		c2_time_t deadline)
{
	printf("Inside c2_rpc_form_item_assign_deadline \n");
	C2_PRE(item !=NULL);

	item->ri_deadline = deadline;
	return 0;
}

/**
  Assign a priority to a given RPC item
 */
int c2_rpc_form_item_assign_prio(struct c2_rpc_item *item, const int prio)
{
	printf("Inside c2_rpc_form_item_assign_prio \n");
	C2_PRE(item !=NULL);

	item->ri_prio = prio;
	return 0;
}

/**
  Insert an rpc item to the global items cache such that it is sorted
  according to timeout
 */
void c2_rpc_form_item_add_to_cache(struct c2_rpc_item *item)
{
	struct c2_rpc_item	*rpc_item;
	struct c2_rpc_item	*rpc_item_next;
	bool			 item_inserted = false;

	printf("Inside c2_rpc_form_add_rpc_to_cache \n");
	C2_PRE(item != NULL);

	c2_mutex_lock(&items_cache->ic_mutex);
	c2_list_for_each_entry_safe(&items_cache->ic_cache_list,
			rpc_item, rpc_item_next,
			struct c2_rpc_item, ri_linkage){
		if (item->ri_deadline <= rpc_item->ri_deadline) {
			c2_list_add_before(&rpc_item->ri_linkage,
					&item->ri_linkage);
			item_inserted = true;
			break;
		}
	}
	if(!item_inserted) {
		c2_list_add_after(&rpc_item->ri_linkage, &item->ri_linkage);
	}
	item->ri_state = RPC_ITEM_SUBMITTED;
	c2_mutex_unlock(&items_cache->ic_mutex);
	c2_rpc_form_extevt_rpcitem_added_in_cache(item);
}

/**
   Add rpc items from an rpc group.
 */
int c2_rpc_form_rpcgroup_add_to_cache(struct c2_rpc_group *group)
{
	int				 res = 0;
	struct c2_rpc_item		*item = NULL;

	C2_PRE(group != NULL);
	/* Item addition is done on a new thread every time. */
	c2_list_for_each_entry(&group->rg_items, item, struct c2_rpc_item,
			ri_group_linkage) {
		C2_ASSERT(thread_no < nfops);
		res = C2_THREAD_INIT(&form_ut_threads[thread_no],
				struct c2_rpc_item*,
				NULL, &c2_rpc_form_item_add_to_cache,
				     item, "form_ut_%p", item);
		C2_ASSERT(res == 0);
		thread_no++;
	}
	return 0;
}

/**
  Populate the rpc item parameters specific to IO FOPs
 */
int c2_rpc_form_item_io_populate_param(struct c2_rpc_item *item)
{
	int		prio;
	c2_time_t	deadline;

	printf("Inside c2_rpc_form_item_io_populate_param \n");
	C2_PRE(item != NULL);

	prio = rand() % MAX_IO_PRIO + MIN_IO_PRIO;
	c2_rpc_form_item_assign_prio(item, prio);
	deadline = rand() % (MAX_IO_DEADLINE-1) + MIN_IO_DEADLINE;
	c2_rpc_form_item_assign_deadline(item, deadline);

	return 0;
}

/**
  Populate the rpc item parameters specific to Non-IO FOPs
 */
int c2_rpc_form_item_nonio_populate_param(struct c2_rpc_item *item)
{
	int		prio;
	c2_time_t	deadline;

	printf("Inside c2_rpc_form_item_nonio_populate_param \n");
	C2_PRE(item != NULL);

	prio = rand() % MAX_NONIO_PRIO + MIN_NONIO_PRIO;
	c2_rpc_form_item_assign_prio(item, prio);
	deadline = rand() % MAX_NONIO_DEADLINE + MIN_NONIO_DEADLINE;
	c2_rpc_form_item_assign_deadline(item, deadline);

	item->ri_group = NULL;

	return 0;
}

/**
  Populate the rpc item parameters based on the FOP type
 */
int c2_rpc_form_item_populate_param(struct c2_rpc_item *item)
{
	bool 		io_req = false;
	int		res = 0;

	printf("Inside c2_rpc_form_item_populate_param \n");
	C2_PRE(item != NULL);

	io_req = c2_rpc_item_is_io_req(item);
	if(io_req) {
		res = c2_rpc_form_item_io_populate_param(item);
		C2_ASSERT(res==0);
	}
	else {
		res = c2_rpc_form_item_nonio_populate_param(item);
		C2_ASSERT(res==0);
	}
	item->ri_endp = ep;
	c2_list_link_init(&item->ri_unformed_linkage);
	c2_list_link_init(&item->ri_group_linkage);
	c2_list_link_init(&item->ri_linkage);
	c2_list_link_init(&item->ri_rpcobject_linkage);
	return 0;
}


void form_ut_thread_init(int a)
{
#ifndef __KERNEL__
	//printf("Thread id %d initialized.\n", form_ut_threads[a].t_h.h_id);
#endif
	/* Call its function. */
}

struct c2_fop_file_fid *form_get_fid(int i)
{
	struct c2_fop_file_fid	*fid = NULL;
	C2_ASSERT(i < nfiles);
	fid = &form_fids[i];
	return fid;
}

struct c2_rpc_group *form_get_rpcgroup(int i)
{
	C2_ASSERT(i < MAX_GRPS);
	return rgroup[i];
}

struct c2_fop *form_create_file_create_fop()
{
	int 				 i = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_file_create	*create_fop = NULL;
	struct c2_fop_file_fid		*fid = NULL;

	fop = c2_fop_alloc(&c2_fop_file_create_fopt, NULL);
	if (fop == NULL) {
		printf("Failed to allocate struct c2_fop.\n");
		return NULL;
	}
	create_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	fid = form_get_fid(i);
	create_fop->fcr_fid = *fid;
	return fop;
}

void form_fini_fop(struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	c2_fop_free(fop);
}

struct c2_fop_io_vec *form_get_new_iovec(int i)
{
	struct c2_fop_io_vec		*iovec = NULL;
	uint64_t			 offset = 0;
	uint64_t			 seg_size = 0;
	int				 a = 0;
	int				 j = 0;
	int				 k = 0;
	bool				 status = true;

	C2_ASSERT(i < nfiles);
	C2_ALLOC_PTR(iovec);
	if (iovec == NULL) {
		printf("Failed to allocate memory for struct c2_fop_io_vec.\n");
		status = false;
		goto last;
	}
	iovec->iov_count = nsegs;
	C2_ALLOC_ARR(iovec->iov_seg, iovec->iov_count);
	if (iovec->iov_seg == NULL) {
		printf("Failed to allocate memory for struct \
				c2_fop_io_seg.\n");
		status = false;
		goto last;
	}
	seg_size = io_size / nsegs;
	for (offset = file_offsets[i], a = 0; a < nsegs; a++) {
		iovec->iov_seg[a].f_offset = offset;
		iovec->iov_seg[a].f_buf.f_count = seg_size;
		C2_ALLOC_ARR(iovec->iov_seg[a].f_buf.f_buf, seg_size);
		if (iovec->iov_seg[a].f_buf.f_buf == NULL) {
			printf("Failed to allocate memory for char array.\n");
			status = false;
			goto last;
		}
		k = (rand()) % niopatterns;
		for (j = 0; j < (seg_size / pattern_length); j+=pattern_length) {
			memcpy(&iovec->iov_seg[a].f_buf.f_buf[j],
					file_data_patterns[k], pattern_length);
		}
		offset += seg_size;
	}
last:
	if (status == false) {
		for (j = 0; j < nsegs; j++) {
			c2_free(iovec->iov_seg[j].f_buf.f_buf);
		}
		c2_free(iovec->iov_seg);
		c2_free(iovec);
		iovec = NULL;
	}
	else {
		file_offsets[i] = iovec->iov_seg[a].f_offset +
			iovec->iov_seg[a].f_buf.f_count;
		form_write_iovecs[nwrite_iovecs] = iovec;
		nwrite_iovecs++;
	}
	return iovec;
}

void form_write_iovec_fini()
{
	int			i = 0;
	int			j = 0;
	struct c2_fop_io_vec	*iovec = NULL;

	C2_PRE(form_write_iovecs != NULL);
	for (j = 0; j < nwrite_iovecs; j++) {
		iovec = form_write_iovecs[j];
		for (i = 0; i < iovec->iov_count; i++) {
			c2_free(iovec->iov_seg[i].f_buf.f_buf);
			iovec->iov_seg[i].f_buf.f_buf = NULL;
		}
		c2_free(iovec->iov_seg);
		c2_free(iovec);
	}
}

struct c2_fop *form_create_write_fop()
{
	int				 i = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_file_fid		*fid = NULL;
	struct c2_fop_io_vec		*iovec = NULL;

	fop = c2_fop_alloc(&c2_fop_cob_writev_fopt, NULL);
	if (fop == NULL) {
		printf("Failed to allocate struct c2_fop.\n");
		return NULL;
	}
	write_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	fid = form_get_fid(i);
	write_fop->fwr_fid = *fid;
	iovec = form_get_new_iovec(i);
	write_fop->fwr_iovec = *iovec;
	return fop;
}

struct c2_fop *form_create_read_fop()
{
	int				 i = 0;
	int				 j = 0;
	int				 k = 0;
	int				 seg_size = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	struct c2_fop_file_fid		*fid = NULL;

	fop = c2_fop_alloc(&c2_fop_cob_readv_fopt, NULL);
	if (fop == NULL) {
		printf("Failed to allocate struct c2_fop.\n");
		return NULL;
	}
	read_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	fid = form_get_fid(i);
	read_fop->frd_fid = *fid;
	read_fop->frd_ioseg.fs_count = nsegs;
	C2_ALLOC_ARR(read_fop->frd_ioseg.fs_segs, nsegs);
	if (read_fop->frd_ioseg.fs_segs == NULL) {
		printf("Failed to allocate memory for IO segment\n");
		c2_fop_free(fop);
		fop = NULL;
	}
	else {
		seg_size = io_size / nsegs;
		for (j = file_offsets[i], k = 0; k < nsegs; k++) {
			read_fop->frd_ioseg.fs_segs[k].f_offset = j;
			read_fop->frd_ioseg.fs_segs[k].f_count = seg_size;
			j += seg_size;
		}
		file_offsets[i] = read_fop->frd_ioseg.fs_segs[k].f_offset +
			read_fop->frd_ioseg.fs_segs[k].f_count;
	}
	return fop;
}

void form_fini_read_fop(struct c2_fop *fop)
{
	struct c2_fop_cob_readv			*read_fop = NULL;

	C2_PRE(fop != NULL);
	read_fop = c2_fop_data(fop);
	c2_free(read_fop->frd_ioseg.fs_segs);
	c2_fop_free(fop);
}

void form_fini_fops()
{
	int			opcode = 0;
	int			i = 0;

	C2_PRE(form_fops != NULL);
	for (i = 0; i < nfops; i++) {
		if (form_fops[i]) {
			opcode = c2_rpc_item_io_get_opcode(
					&form_fops[i]->f_item);
			switch (opcode) {
				case C2_RPC_FORM_IO_READ:
					form_fini_read_fop(form_fops[i]);
					break;

				case C2_RPC_FORM_IO_WRITE:
					form_fini_fop(form_fops[i]);
					break;

				default:
					form_fini_fop(form_fops[i]);
			};
		}
	}
}

void init_file_io_patterns()
{
	strcpy(file_data_patterns[0], "a1b2");
	strcpy(file_data_patterns[1], "b1c2");
	strcpy(file_data_patterns[2], "c1d2");
	strcpy(file_data_patterns[3], "d1e2");
	strcpy(file_data_patterns[4], "e1f2");
	strcpy(file_data_patterns[5], "f1g2");
	strcpy(file_data_patterns[6], "g1h2");
	strcpy(file_data_patterns[7], "h1i2");
}

struct c2_fop *form_get_new_fop()
{
	struct c2_fop		*fop = NULL;
	fopFuncPtr		funcPtr;
	int			 i = 0;

	i = (rand()) % nopcodes;
	funcPtr = form_fop_table[i];
	fop = funcPtr();
	return fop;
}

int main(int argc, char **argv)
{
	int		result = 0;
	int		i = 0;
	int		j = 0;
	struct c2_fop	*fop = NULL;

	result = c2_init();
	C2_ASSERT(result == 0);

	result = io_fop_init();
	C2_ASSERT(result == 0);

	result = c2_rpc_form_init();

	/*
	 1. Initialize the thresholds like max_message_size, max_fragements
	    and max_rpcs_in_flight.*/

	/* Lustre limits the rpc size(actually the number of pages in rpc)
	   by the MTU(Max Transferrable Unit) of LNET which is defined
	   to be 1M. !! Not sure of this is right !! */
	//c2_rpc_max_message_size = 1024*1024;
	c2_rpc_max_message_size = 1024 * 100;
	/* We start with a default value of 8. The max value in Lustre, is
	   limited to 32. */
	c2_rpc_max_rpcs_in_flight = 8;
	c2_rpc_max_fragments_size = 16;

	c2_rpc_form_set_thresholds(c2_rpc_max_message_size,
			c2_rpc_max_rpcs_in_flight, c2_rpc_max_fragments_size);

	result = c2_rpc_form_item_cache_init();
	C2_ASSERT(result == 0);

	/* 3. Create a number of meta-data and IO FOPs. For IO, decide the
	    number of files to operate upon. Decide how to assign items to
	    rpc groups and have multiple IO requests within or across groups.*/
	/* Init the fid structures and rpc groups. */
	C2_ALLOC_ARR(form_fids, nfiles);
	if (form_fids == NULL) {
		printf("Failed to allocate memory for array of struct \
				c2_fop_file_fid.\n");
		return -1;
	}

	C2_ALLOC_ARR(file_offsets, nfiles);
	if (file_offsets == NULL) {
		printf("Failed to allocate memory for array of uint64_t.\n");
		return -1;
	}

	for (i = 0; i < niopatterns; i++) {
	}
	init_file_io_patterns();

	C2_ALLOC_ARR(form_write_iovecs, nfops);

	result = c2_rpc_form_groups_alloc();
	C2_ASSERT(result == 0);

	/* For every group, create a fop in a random manner
	   and populate its constituent rpc item.
	   Wait till all items in the group are populated.
	   And submit the whole group at once.*/
	for (i = 0; i < MAX_GRPS; i++) {
		for (j = 0; j < nfops/MAX_GRPS; j++) {
			fop = form_get_new_fop();
			C2_ASSERT(fop != NULL);
			result = c2_rpc_form_item_populate_param(&fop->f_item);
			C2_ASSERT(result == 0);
			result = c2_rpc_form_item_assign_to_group(rgroup[i],
					&fop->f_item, i);
		}
		result = c2_rpc_form_rpcgroup_add_to_cache(rgroup[i]);
		C2_ASSERT(result == 0);
	}

	/* 4. Populate the associated rpc items.
	 5. Assign priority and timeout for all rpc items. The thumb rule
	    for this is - meta data FOPs should have higher priority and
	    shorted timeout while IO FOPs can have lower priority than
	    meta data FOPs and relatively larger timeouts.
	 6. Assign a thread each time from the thread pool to do the
	    rpc submission. This will give ample opportunity to test the
	    formation algorithm in multi threaded environment.
	 7. Simulate necessary behavior of grouping component.
	 8. This will trigger execution of formation algorithm.
	 9. Grab output produced by formation algorithm and analyze the
	    statistics.
	 */

	/* Joining all threads will take care of releasing all references
	   to rpc items(and fops inherently) and then formation component
	   can be "fini"ed. */
	for (i = 0; i < thread_no; i++) {
		c2_thread_join(&form_ut_threads[i]);
		c2_thread_fini(&form_ut_threads[i]);
	}
	printf("No. of items in items cache before invoking reply recd:%lu\n",
			c2_list_length(&items_cache->ic_cache_list)); 
	//c2_rpc_form_invoke_reply_received();
	printf("No. of items in items cache after invoking reply recd:%lu\n", 
			c2_list_length(&items_cache->ic_cache_list)); 
	//c2_rpc_form_invoke_item_removed();
	c2_rpc_form_fini();
	c2_free(form_fids);
	form_fids = NULL;
	form_write_iovec_fini();
	c2_free(form_write_iovecs);
	form_fini_fops();
	c2_rpc_form_item_cache_fini();
	c2_rpc_form_groups_free();
	return 0;
}

