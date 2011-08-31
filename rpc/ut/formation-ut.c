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

#include "rpc/rpccore.h"
#include "stob/ut/io_fop.h"
#include "lib/thread.h"
#include "lib/misc.h"
#include "colibri/init.h"
#include "lib/ut.h"
#include "ioservice/io_fops.h"
#include <stdlib.h>
#include "colibri/init.h"
#include "rpc/session.h"
#include "db/db.h"
#include "cob/cob.h"
#include "addb/addb.h"
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

/* external functions. */
int c2_rpc_item_io_coalesce(struct c2_rpc_frm_item_coalesced *c_item,
		struct c2_rpc_item *b_item);
extern int io_fop_get_opcode(const struct c2_fop *fop);

/** ADDB variables and structures */
static const struct c2_addb_ctx_type c2_rpc_ut_addb_ctx_type = {
        .act_name = "rpc-ut-ctx-type"
};

static const struct c2_addb_loc c2_rpc_ut_addb_loc = {
        .al_name = "rpc-ut-loc"
};

struct c2_addb_ctx c2_rpc_ut_addb_ctx;

/* Some random deadline values for testing purpose only */
#define MIN_NONIO_DEADLINE	 0		// 0 ms
#define MAX_NONIO_DEADLINE	 10000000	// 10 ms
#define MIN_IO_DEADLINE		 10000000	// 10 ms
#define MAX_IO_DEADLINE		 100000000	// 100 ms

/* Some random priority values for testing purpose only */
#define MIN_NONIO_PRIO		 0
#define MAX_NONIO_PRIO		 5
#define MIN_IO_PRIO		 6
#define MAX_IO_PRIO		 10

/* Array of groups */
#define MAX_GRPS		 16
struct c2_rpc_group		*rgroup[MAX_GRPS];

uint64_t			 c2_rpc_max_message_size;
uint64_t			 c2_rpc_max_fragments_size;
uint64_t			 c2_rpc_max_rpcs_in_flight;

#define nthreads		 256
struct c2_thread		 form_ut_threads[nthreads];
uint64_t			 thread_no = 0;

#define nfops			 256
struct c2_fop			*form_fops[nfops];

uint64_t			 nwrite_iovecs = 0;
struct c2_fop_io_vec	       **form_write_iovecs = NULL;

#define nfiles			 64
struct c2_fop_file_fid		*form_fids = NULL;

#define	io_size			 8192
#define nsegs			 8
/* At the moment, we are sending only 3 different types of FOPs,
   namely - file create, file read and file write. */
#define nopcodes		 2

struct c2_net_end_point		 ep;



/* Function pointer to different FOP creation methods. */
typedef struct c2_fop * (*fopFuncPtr)(void);

struct c2_fop *form_create_write_fop();
struct c2_fop *form_create_read_fop();

/* Array of function pointers. */
fopFuncPtr form_fop_table[nopcodes] = {
	&form_create_write_fop, &form_create_read_fop
};

extern struct c2_rpc_item_type c2_rpc_item_type_readv;
extern struct c2_rpc_item_type c2_rpc_item_type_writev;
extern struct c2_rpc_item_type c2_rpc_item_type_create;

#define niopatterns		 8
#define pattern_length		 4
char				 file_data_patterns[niopatterns][pattern_length];

#define ndatafids		 8
struct c2_fop_file_fid		 fid_data[ndatafids];

uint64_t			*file_offsets = NULL;

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

#define nslots			 8
struct c2_rpc_slot		*slots[nslots];
struct c2_rpc_session		 session;

struct c2_rpcmachine		 rpcmachine;
struct c2_cob_domain		 cob_domain;
struct c2_dbenv			 db;
char				 db_name[] = "rpc_form_db";
struct c2_rpc_conn		 conn;

#define BOUNDED			 1
#define UNBOUNDED		 2
#define MIN_SLOTS		 0
#define MAX_SLOTS		 nslots

/** Function to init the slot */
void c2_rpc_frm_slot_init(struct c2_rpc_slot *slot,
		struct c2_rpc_session *session, int slot_id)
{
	C2_PRE(session != NULL);
	C2_PRE(slot != NULL);

	slot->sl_session =  session;
	slot->sl_slot_id = slot_id;
	c2_list_link_init(&slot->sl_link);
	c2_list_init(&slot->sl_item_list);
	slot->sl_last_sent = NULL;
	slot->sl_last_persistent = NULL;
	c2_list_init(&slot->sl_ready_list);
	c2_mutex_init(&slot->sl_mutex);
}

/** Function to fini the slot */
void c2_rpc_frm_slot_fini(struct c2_rpc_slot *slot)
{
	C2_PRE(slot != NULL);

	if(c2_list_is_empty(&slot->sl_ready_list)){
		c2_list_fini(&slot->sl_ready_list);
	}
	c2_mutex_fini(&slot->sl_mutex);
	c2_free(slot);
}

/**
  Function to init required values in connection
*/
void c2_rpc_frm_conn_init(struct c2_rpc_conn *conn,
		struct c2_rpcmachine *rpc_mc)
{
	C2_PRE(rpc_mc != NULL);
	C2_PRE(conn != NULL);

	conn->c_rpcmachine = rpc_mc;
	c2_mutex_init(&conn->c_mutex);
	c2_list_link_init(&conn->c_link);
	c2_list_init(&conn->c_sessions);
	//c2_chan_init(&conn->c_chan);
}

/**
  Function to init required values in session
*/
void c2_rpc_frm_session_init(struct c2_rpc_session *session,
		struct c2_rpc_conn *conn)
{
	C2_PRE(session != NULL);
	C2_PRE(conn != NULL);

	session->s_conn = conn;
	c2_mutex_init(&session->s_mutex);
	c2_list_init(&session->s_unbound_items);
	c2_list_link_init(&session->s_link);
}

/**
  Init of all required data structures
 */
int c2_rpc_frm_ut_init()
{
	struct c2_cob_domain_id cob_dom_id = { 42 };
	int			result = 0;
	int			i = 0;

	c2_addb_ctx_init(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_ctx_type,
			&c2_addb_global_ctx);
	c2_addb_choose_default_level(AEL_WARN);

	result = c2_init();
	C2_ASSERT(result == 0);

	result = io_fop_init();
	C2_ASSERT(result == 0);

	/* Init the db */
	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	/* Init the cob domain */
	result = c2_cob_domain_init(&cob_domain, &db, &cob_dom_id);
	C2_ASSERT(result == 0);

	/* Init the rpcmachine */
	//c2_rpcmachine_init(&rpcmachine, &cob_domain, NULL);

	/* Init the connection structure */
	c2_rpc_frm_conn_init(&conn, &rpcmachine);

	/* Init the sessions structure */
	c2_rpc_frm_session_init(&session, &conn);

	/* Init the slots */
	for (i=0; i < nslots; i++)
	{
		slots[i] = c2_alloc(sizeof(struct c2_rpc_slot));
		c2_rpc_frm_slot_init(slots[i], &session, i);
		//c2_list_add(&rpcmachine.cr_ready_slots, &slots[i]->sl_link);
	}

	/* Init the rpc formation component */
	result = c2_rpc_frm_init(&rpcmachine.cr_formation);
	return 0;
}

/**
  Init of all required data structures
 */

void c2_rpc_frm_ut_fini()
{
	int	i = 0;

	/* Fini the rpc formation component */
	c2_rpc_frm_fini(&rpcmachine.cr_formation);

	/* Fini the slots */
	for(i = 0; i < nslots; i++)
	{
		c2_rpc_frm_slot_fini(slots[i]);
	}

	/* Fini the rpcmachine */
	//c2_rpcmachine_fini(&rpcmachine);

	/* Fini cob domain */
	c2_cob_domain_fini(&cob_domain);

	/* Fini dbenv */
	//c2_dbenv_fini(&db);
	c2_addb_ctx_fini(&c2_rpc_ut_addb_ctx);
}
/**
  Alloc and initialize the global array of groups used for UT
 */
int c2_rpc_frm_groups_alloc(void)
{
	int		i = 0;

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
int c2_rpc_frm_groups_free(void)
{
	int			 i = 0;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;

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
  Assign a group to a given RPC item
 */
int c2_rpc_frm_item_assign_to_group(struct c2_rpc_group *grp,
		struct c2_rpc_item *item, int grpno)
{
	struct c2_rpc_item	*rpc_item = NULL;
	struct c2_rpc_item	*rpc_item_next = NULL;
	bool			 item_inserted = false;

	C2_PRE(item !=NULL);

	item->ri_group = grp;
	c2_mutex_lock(&grp->rg_guard);
	grp->rg_expected++;
	grp->rg_grpid = grpno;
	/* Insert by sorted priority in groups list
	   Assumption- Lower the value of priority,
	   higher is the actual priority  */
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
int c2_rpc_frm_item_assign_deadline(struct c2_rpc_item *item,
		c2_time_t deadline)
{
	C2_PRE(item !=NULL);

	item->ri_deadline = deadline;
	return 0;
}

/**
  Assign a priority to a given RPC item
 */
int c2_rpc_frm_item_assign_prio(struct c2_rpc_item *item, const int prio)
{
	C2_PRE(item !=NULL);

	item->ri_prio = prio;
	return 0;
}

void c2_rpc_frm_item_add_to_rpcmachine(struct c2_rpc_item *item)
{
	int		state = 0;
	int		slot_no = 0;
	int		res = 0;
	/* Randomly select the state of the item to be BOUNDED or UNBOUNDED */
	state = rand() % UNBOUNDED + BOUNDED;

	if (state == BOUNDED){
		/* Find a random slot and add to its free list */
		slot_no = rand() % MAX_SLOTS + MIN_SLOTS;
		/* Since this slot is accessed by formation code by
		   referencing rpc_item->ri_slot_refs[0].sr_slot,
		   modify item->ri_slot_refs[0].sr_slot to point to
		   current slot and call the event on formation module. */
		item->ri_slot_refs[0].sr_slot = slots[slot_no];
		item->ri_state = RPC_ITEM_SUBMITTED;
		res = c2_rpc_frm_item_ready(item);
		if (res != 0) {
			printf("Event RPC ITEM READY returned failure.\n");
		}
	}
	else if (state == UNBOUNDED) {
		/* Call the event on formation module. */
		item->ri_slot_refs[0].sr_slot = NULL;
		item->ri_state = RPC_ITEM_SUBMITTED;
		res = c2_rpc_frm_ubitem_added(item);
		if (res != 0) {
			printf("Event UNBOUND ITEM ADDED returned failure.\n");
		}
	}
}

/**
   Add rpc items from an rpc group.
 */
int c2_rpc_frm_rpcgroup_add_to_rpcmachine(struct c2_rpc_group *group)
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
				NULL, &c2_rpc_frm_item_add_to_rpcmachine,
				     item, "form_ut_%p", item);
		C2_ASSERT(res == 0);
		thread_no++;
	}
	return 0;
}

/**
  Populate the rpc item parameters specific to IO FOPs
 */
int c2_rpc_frm_item_io_populate_param(struct c2_rpc_item *item)
{
	int		prio;
	c2_time_t	deadline;

	C2_PRE(item != NULL);

	prio = rand() % MAX_IO_PRIO + MIN_IO_PRIO;
	c2_rpc_frm_item_assign_prio(item, prio);
	deadline = rand() % (MAX_IO_DEADLINE-1) + MIN_IO_DEADLINE;
	c2_rpc_frm_item_assign_deadline(item, deadline);

	return 0;
}

/**
  Populate the rpc item parameters specific to Non-IO FOPs
 */
int c2_rpc_frm_item_nonio_populate_param(struct c2_rpc_item *item)
{
	int		prio;
	c2_time_t	deadline;

	C2_PRE(item != NULL);

	prio = rand() % MAX_NONIO_PRIO + MIN_NONIO_PRIO;
	c2_rpc_frm_item_assign_prio(item, prio);
	deadline = rand() % MAX_NONIO_DEADLINE + MIN_NONIO_DEADLINE;
	c2_rpc_frm_item_assign_deadline(item, deadline);

	item->ri_group = NULL;

	return 0;
}

/**
  Populate the rpc item parameters based on the FOP type
 */
int c2_rpc_frm_item_populate_param(struct c2_rpc_item *item)
{
	bool		 io_req = false;
	int		 res = 0;

	C2_PRE(item != NULL);

	/* Associate an rpc item with its type. */
	c2_rpc_item_attach(item);

	io_req = item->ri_type->rit_ops->rito_io_coalesce;
	if (io_req) {
		res = c2_rpc_frm_item_io_populate_param(item);
		C2_ASSERT(res==0);
	}
	else {
		res = c2_rpc_frm_item_nonio_populate_param(item);
		C2_ASSERT(res==0);
	}
	//item->ri_endp = NULL;
	item->ri_mach = &rpcmachine;
	item->ri_session = &session;
	c2_list_link_init(&item->ri_unformed_linkage);
	c2_list_link_init(&item->ri_group_linkage);
	c2_list_link_init(&item->ri_linkage);
	c2_list_link_init(&item->ri_rpcobject_linkage);
	c2_list_link_init(&item->ri_unbound_link);
	c2_list_link_init(&item->ri_slot_refs[0].sr_ready_link);
	item->ri_reply = NULL;
	c2_chan_init(&item->ri_chan);

	return 0;
}

/**
  Get fid from global pool of fids
 */
struct c2_fop_file_fid *form_get_fid(int i)
{
	struct c2_fop_file_fid	*fid = NULL;
	C2_ASSERT(i < nfiles);
	fid = &form_fids[i];
	return fid;
}

/**
  Free the fop
 */
void form_fini_fop(struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	c2_fop_free(fop);
}

/**
  Get new iovector for a fiven fid
 */
struct c2_fop_io_vec *form_get_new_iovec(struct c2_fop_file_fid *fid)
{
	int				 i = 0;
	struct c2_fop_io_vec		*iovec = NULL;
	uint64_t			 offset = 0;
	uint64_t			 seg_size = 0;
	int				 a = 0;
	int				 j = 0;
	int				 k = 0;
	bool				 status = true;

	C2_ASSERT(i < nfiles);
	for (a = 0; a < ndatafids; a++) {
		if ((fid_data[a].f_seq == fid->f_seq) &&
				(fid_data[a].f_oid == fid->f_oid)) {
			i = a;
			break;
		}
	}
	C2_ALLOC_PTR(iovec);
	if (iovec == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc, c2_addb_oom);
		status = false;
		goto last;
	}
	iovec->iv_count = nsegs;
	C2_ALLOC_ARR(iovec->iv_segs, iovec->iv_count);
	if (iovec->iv_segs == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc, c2_addb_oom);
		status = false;
		goto last;
	}
	seg_size = io_size / nsegs;
	for (offset = file_offsets[i], a = 0; a < nsegs; a++) {
		iovec->iv_segs[a].is_offset = offset;
		iovec->iv_segs[a].is_buf.ib_count = seg_size;
		C2_ALLOC_ARR(iovec->iv_segs[a].is_buf.ib_buf, seg_size);
		if (iovec->iv_segs[a].is_buf.ib_buf == NULL) {
			C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc,
					c2_addb_oom);
			status = false;
			goto last;
		}
		k = (rand()) % niopatterns;
		for (j = 0; j < (seg_size / pattern_length); j+=pattern_length) {
			memcpy(&iovec->iv_segs[a].is_buf.ib_buf[j],
					file_data_patterns[k], pattern_length);
		}
		offset += seg_size;
	}
last:
	if (status == false) {
		for (j = 0; j < nsegs; j++) {
			c2_free(iovec->iv_segs[j].is_buf.ib_buf);
		}
		c2_free(iovec->iv_segs);
		c2_free(iovec);
		iovec = NULL;
	}
	else {
		file_offsets[i] = iovec->iv_segs[a-1].is_offset +
			iovec->iv_segs[a-1].is_buf.ib_count;
		form_write_iovecs[nwrite_iovecs] = iovec;
		nwrite_iovecs++;
		for (a = 0; a < nsegs; ++a) {
			printf("Input Fid - seq = %lu, oid = %lu: Write segment %d: offset = %lu, count = %lu\n",
					fid->f_seq, fid->f_oid, a,
					iovec->iv_segs[a].is_offset,
					iovec->iv_segs[a].is_buf.ib_count);
		}
	}
	return iovec;
}

/**
  Deallocate the iovec
 */
void form_write_iovec_fini()
{
	int			i = 0;
	int			j = 0;
	struct c2_fop_io_vec	*iovec = NULL;

	C2_PRE(form_write_iovecs != NULL);
	for (j = 0; j < nwrite_iovecs; j++) {
		iovec = form_write_iovecs[j];
		for (i = 0; i < iovec->iv_count; i++) {
			c2_free(iovec->iv_segs[i].is_buf.ib_buf);
			iovec->iv_segs[i].is_buf.ib_buf = NULL;
		}
		c2_free(iovec->iv_segs);
		c2_free(iovec);
	}
}

/**
  Create a new write fop
 */
struct c2_fop *form_create_write_fop()
{
	int				 i = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_file_fid		*fid = NULL;
	struct c2_fop_io_vec		*iovec = NULL;

	fop = c2_fop_alloc(&c2_fop_cob_writev_fopt, NULL);
	if (fop == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc,
				c2_addb_oom);
		return NULL;
	}
	write_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	fid = form_get_fid(i);
	write_fop->cw_fid = *fid;
	iovec = form_get_new_iovec(fid);
	write_fop->cw_iovec = *iovec;
	fop->f_type = &c2_fop_cob_writev_fopt;
	return fop;
}

/**
  Create a new read fop
 */
struct c2_fop *form_create_read_fop()
{
	int				 i = 0;
	int				 j = 0;
	int				 k = 0;
	int				 a = 0;
	int				 seg_size = 0;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	struct c2_fop_file_fid		*fid = NULL;

	fop = c2_fop_alloc(&c2_fop_cob_readv_fopt, NULL);
	if (fop == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc,
				c2_addb_oom);
		return NULL;
	}
	read_fop = c2_fop_data(fop);
	i = (rand()) % nfiles;
	fid = form_get_fid(i);
	read_fop->cr_fid = *fid;
	read_fop->cr_iovec.iv_count = nsegs;
	for (a = 0; a < ndatafids; a++) {
		if ((fid_data[a].f_seq == fid->f_seq) &&
				(fid_data[a].f_oid == fid->f_oid)) {
			i = a;
			break;
		}
	}
	C2_ALLOC_ARR(read_fop->cr_iovec.iv_segs, nsegs);
	if (read_fop->cr_iovec.iv_segs == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc,
				c2_addb_oom);
		c2_fop_free(fop);
		fop = NULL;
	}
	else {
		seg_size = io_size / nsegs;
		for (j = file_offsets[i], k = 0; k < nsegs; k++) {
			read_fop->cr_iovec.iv_segs[k].is_offset = j;
			read_fop->cr_iovec.iv_segs[k].is_buf.ib_count =
				seg_size;

			printf("Input Fid - seq = %lu, oid = %lu: Read segment %d: offset = %lu, count = %lu\n",
				fid->f_seq, fid->f_oid, k,
				read_fop->cr_iovec.iv_segs[k].is_offset,
				read_fop->cr_iovec.iv_segs[k].is_buf.ib_count);

			j += seg_size;
		}
		file_offsets[i] = read_fop->cr_iovec.iv_segs[k-1].is_offset +
			read_fop->cr_iovec.iv_segs[k-1].is_buf.ib_count;
	}
	fop->f_type = &c2_fop_cob_readv_fopt;
	return fop;
}

/**
  Deallocate the read fop
 */
void form_fini_read_fop(struct c2_fop *fop)
{
	struct c2_fop_cob_readv			*read_fop = NULL;

	C2_PRE(fop != NULL);
	read_fop = c2_fop_data(fop);
	c2_free(read_fop->cr_iovec.iv_segs);
	c2_fop_free(fop);
}

/**
  Deallocate the fops which are created in global array
 */
void form_fini_fops()
{
	int			 opcode = 0;
	int			 i = 0;

	C2_PRE(form_fops != NULL);
	for (i = 0; i < nfops; i++) {
		if (form_fops[i]) {
			opcode = form_fops[i]->f_type->ft_code;
			switch (opcode) {
				case C2_IO_SERVICE_READV_OPCODE:
					form_fini_read_fop(form_fops[i]);
					break;

				case C2_IO_SERVICE_WRITEV_OPCODE:
					form_fini_fop(form_fops[i]);
					break;

				default:
					form_fini_fop(form_fops[i]);
			};
		}
	}
}

/**
  Fill in hard coded data patterns in a global patterns array
 */
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

/**
  Get a random fop
 */
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

/**
  Initialize the global fid array
 */
void init_fids()
{
	fid_data[0].f_seq = 1;
	fid_data[0].f_oid = 2;
	fid_data[1].f_seq = 3;
	fid_data[1].f_oid = 4;
	fid_data[2].f_seq = 5;
	fid_data[2].f_oid = 6;
	fid_data[3].f_seq = 7;
	fid_data[3].f_oid = 8;
	fid_data[4].f_seq = 9;
	fid_data[4].f_oid = 10;
	fid_data[5].f_seq = 11;
	fid_data[5].f_oid = 12;
	fid_data[6].f_seq = 13;
	fid_data[6].f_oid = 14;
	fid_data[7].f_seq = 15;
	fid_data[7].f_oid = 16;
}

/**
  Populate the global fid array
 */
void populate_fids()
{
	int		i = 0;
	int		j = 0;

	/* Total 64 fids to be populated. */
	/* nfiles = 64 */
	/* ndatafids = 8 */
	for (i = 0; i < nfiles; i++) {
		j = rand() % ndatafids;
		form_fids[i] = fid_data[j];
	}
}


/**
  This main function tests the formation code.
 */
/*int main(int argc, char **argv) */
int test()
{
	int			 result = 0;
	int			 i = 0;
	int			 j = 0;
	struct c2_fop		*fop = NULL;

	result = c2_rpc_frm_ut_init();
	C2_ASSERT(result == 0);

	/* Initialize the thresholds like max_message_size, max_fragements
	   and max_rpcs_in_flight.*/
	/* Lustre limits the rpc size(actually the number of pages in rpc)
	   by the MTU(Max Transferrable Unit) of LNET which is defined
	   to be 1M. !! Not sure of this is right !! */
	c2_rpc_max_message_size = 1024 * 100;
	/* Start with a default value of 8. The max value in Lustre, is
	   limited to 32. */
	c2_rpc_max_rpcs_in_flight = 8;
	c2_rpc_max_fragments_size = 16;

	c2_rpc_frm_set_thresholds(c2_rpc_max_rpcs_in_flight);

	/*Create a number of meta-data and IO FOPs. For IO, decide the
	  number of files to operate upon. Decide how to assign items to
	  rpc groups and have multiple IO requests within or across groups.*/

	/* Init the fid structures and rpc groups. */
	C2_ALLOC_ARR(form_fids, nfiles);
	if (form_fids == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc,
				c2_addb_oom);
		return -1;
	}
	init_fids();
	populate_fids();

	C2_ALLOC_ARR(file_offsets, ndatafids);
	if (file_offsets == NULL) {
		C2_ADDB_ADD(&c2_rpc_ut_addb_ctx, &c2_rpc_ut_addb_loc,
				c2_addb_oom);
		return -1;
	}

	for (i = 0; i < niopatterns; i++) {
	}
	init_file_io_patterns();

	C2_ALLOC_ARR(form_write_iovecs, nfops);

	result = c2_rpc_frm_groups_alloc();
	C2_ASSERT(result == 0);

	/* For every group, create a fop in a random manner
	   and populate its constituent rpc item.
	   Wait till all items in the group are populated.
	   And submit the whole group at once.
	   Assign priority and timeout for all rpc items. The thumb rule
	   for this is - meta data FOPs should have higher priority and
	   shorted timeout while IO FOPs can have lower priority than
	   meta data FOPs and relatively larger timeouts.
	   Assign a thread each time from the thread pool to do the
	   rpc submission. This will give ample opportunity to test the
	   formation algorithm in multi threaded environment.
	   Simulate necessary behavior of grouping component.
	   This will trigger execution of formation algorithm.
	 */
	for (i = 0; i < MAX_GRPS; i++) {
		for (j = 0; j < nfops/MAX_GRPS; j++) {
			fop = form_get_new_fop();
			C2_ASSERT(fop != NULL);
			result = c2_rpc_frm_item_populate_param(&fop->f_item);
			C2_ASSERT(result == 0);
			result = c2_rpc_frm_item_assign_to_group(rgroup[i],
					&fop->f_item, i);
		}
		result = c2_rpc_frm_rpcgroup_add_to_rpcmachine(rgroup[i]);
		C2_ASSERT(result == 0);
	}


	/* Joining all threads will take care of releasing all references
	   to rpc items(and fops inherently) and then formation component
	   can be "fini"ed. */
	for (i = 0; i < thread_no; i++) {
		c2_thread_join(&form_ut_threads[i]);
		c2_thread_fini(&form_ut_threads[i]);
	}

        /* Do the cleanup */
	c2_free(form_fids);
	form_fids = NULL;
	form_write_iovec_fini();
	c2_free(form_write_iovecs);
	form_fini_fops();
	c2_rpc_frm_groups_free();
//	c2_rpc_frm_ut_fini();
	return 0;
}

