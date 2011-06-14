#include "cob/cob.h"
#include "rpc/rpccore.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "rpc/session.h"
#include "rpc/session_int.h"

static const struct c2_update_stream_ops update_stream_ops;
static const struct c2_rpc_item_type_ops rpc_item_ops;

static int update_stream_init(struct c2_update_stream *us,
			       struct c2_rpcmachine *mach)
{
	us->us_session_id = ~0;
	us->us_slot_id    = ~0;

	us->us_ops   = &update_stream_ops;
	us->us_mach  = mach;
	us->us_state = UPDATE_STREAM_UNINITIALIZED;

	c2_mutex_init(&us->us_guard);
	return 0;
}

static void update_stream_fini(struct c2_update_stream *us)
{
	c2_mutex_fini(&us->us_guard);
}

static int rpc_init(struct c2_rpc *rpc) __attribute__((unused)); /*XXX: for now*/
static int rpc_init(struct c2_rpc *rpc)
{
	c2_list_link_init(&rpc->r_linkage);
	c2_list_init(&rpc->r_items);
	rpc->r_session = NULL;
	return 0;
}
static void rpc_fini(struct c2_rpc *rpc) __attribute__((unused)); /*XXX: for now*/
static void rpc_fini(struct c2_rpc *rpc)
{
	rpc->r_session = NULL;
	c2_list_fini(&rpc->r_items);
	c2_list_link_fini(&rpc->r_linkage);
}

/* can be exported, used c2_ prefix */
static void c2_rpc_item_fini(struct c2_rpc_item *item)
{
	item->ri_state = RPC_ITEM_FINALIZED;
	c2_chan_fini(&item->ri_chan);
}

static void c2_rpc_item_ref_fini(struct c2_ref *ref)
{
	struct c2_rpc_item *item;
	item = container_of(ref, struct c2_rpc_item, ri_ref);
	c2_rpc_item_fini(item);
}

int c2_rpc_item_init(struct c2_rpc_item *item,
		     struct c2_rpcmachine *mach)
{
	c2_ref_init(&item->ri_ref, 1, c2_rpc_item_ref_fini);
	c2_chan_init(&item->ri_chan);
	item->ri_state = RPC_ITEM_UNINITIALIZED;
	item->ri_type = NULL;
	item->ri_sender_id = SENDER_ID_INVALID;
	item->ri_session_id = SESSION_ID_INVALID;
	item->ri_slot_id = SLOT_ID_INVALID;
	item->ri_mach = mach;
	return 0;
}

bool c2_rpc_item_is_update(struct c2_rpc_item *item)
{
	return (item->ri_flags & RPC_ITEM_MUTABO) != 0;
}

int  c2_rpc_core_init(void)
{
	return 0;
}

void c2_rpc_core_fini(void)
{
}

int c2_rpc_reply_timedwait(struct c2_rpc_item *item, const c2_time_t *timeout)
{
	return 0;
}

int c2_rpc_group_timedwait(struct c2_rpc_group *group, const c2_time_t *timeout)
{
	return 0;
}

int c2_rpc_update_stream_get(struct c2_rpcmachine *machine,
			     struct c2_service_id *srvid,
			     enum c2_update_stream_flags flag,
			     const struct c2_update_stream_ops *ops,
			     struct c2_update_stream **out)
{
	int rc = -ENOMEM;

	C2_ALLOC_PTR(*out);
	if (*out == NULL)
		return rc;

	rc = update_stream_init(*out, machine);
	if (rc < 0)
		c2_free(*out);

	if (ops != NULL) {
		(*out)->us_ops = ops;
	}

	return rc;
}

void c2_rpc_update_stream_put(struct c2_update_stream *us)
{
	update_stream_fini(us);
	c2_free(us);
}

size_t c2_rpc_cache_item_count(struct c2_rpcmachine *machine,
			       enum c2_rpc_item_priority prio)
{
	return 0;
}

size_t c2_rpc_rpc_count(struct c2_rpcmachine *machine)
{
	return 0;
}

void c2_rpc_avg_rpc_item_time(struct c2_rpcmachine *machine,
			      c2_time_t *time)
{
}

size_t c2_rpc_bytes_per_sec(struct c2_rpcmachine *machine)
{
	return 0;
}

static int rpc_proc_ctl_init(struct c2_rpc_processing_ctl *ctl)
{
	return 0;
}

static void rpc_proc_ctl_fini(struct c2_rpc_processing_ctl *ctl)
{
}

static int rpc_proc_init(struct c2_rpc_processing *proc)
{
	int rc;
	rc = rpc_proc_ctl_init(&proc->crp_ctl);
	if (rc < 0)
		return rc;

	c2_list_init(&proc->crp_formation_lists);
	c2_list_init(&proc->crp_form);

	return rc;
}

static void rpc_proc_fini(struct c2_rpc_processing *proc)
{
	c2_list_fini(&proc->crp_form);
	c2_list_fini(&proc->crp_formation_lists);
	rpc_proc_ctl_fini(&proc->crp_ctl);
}

static int rpc_stat_init(struct c2_rpc_statistics *stat)
{
	return 0;
}

static void rpc_stat_fini(struct c2_rpc_statistics *stat)
{
}

int  c2_rpcmachine_init(struct c2_rpcmachine	*machine,
			struct c2_cob_domain	*dom,
			struct c2_fol		*fol)
{
	int rc;

	rc = rpc_proc_init(&machine->cr_processing);
	if (rc < 0)
		return rc;

	rc = rpc_stat_init(&machine->cr_statistics);
	if (rc < 0) {
		rpc_proc_fini(&machine->cr_processing);
	}

	c2_list_init(&machine->cr_rpc_conn_list);
	c2_list_init(&machine->cr_ready_slots);
	c2_mutex_init(&machine->cr_session_mutex);

	rc = c2_rpc_reply_cache_init(&machine->cr_rcache, dom, fol);

	return rc;
}

void c2_rpcmachine_fini(struct c2_rpcmachine *machine)
{
	rpc_stat_fini(&machine->cr_statistics);
	rpc_proc_fini(&machine->cr_processing);	
	//c2_list_fini(&machine->cr_rpc_conn_list);
	c2_list_fini(&machine->cr_ready_slots);
	c2_mutex_fini(&machine->cr_session_mutex);
	c2_rpc_reply_cache_fini(&machine->cr_rcache);
}

/** simple vector of RPC-item operations */
static void rpc_item_op_sent(struct c2_rpc_item *item)
{
	DBG("item: xid: %lu, SENT\n", item->ri_verno.vn_vc);
}

static void rpc_item_op_added(struct c2_rpc *rpc, struct c2_rpc_item *item)
{
	DBG("item: xid: %lu, ADDED\n", item->ri_verno.vn_vc);
}

static void rpc_item_op_replied(struct c2_rpc_item *item, int rc)
{
	DBG("item: xid: %lu, REPLIED\n", item->ri_verno.vn_vc);
}

static const struct c2_rpc_item_type_ops rpc_item_ops = {
	.rio_sent    = rpc_item_op_sent,
	.rio_added   = rpc_item_op_added,
	.rio_replied = rpc_item_op_replied
};

/** simple vector of update stream operations */
void us_timeout(struct c2_update_stream *us)
{
	DBG("us: ssid: %lu, slotid: %lu, TIMEOUT\n", us->us_session_id, us->us_slot_id);
}
void us_recovery_complete(struct c2_update_stream *us)
{
	DBG("us: ssid: %lu, slotid: %lu, RECOVERED\n", us->us_session_id, us->us_slot_id);
}

static const struct c2_update_stream_ops update_stream_ops = {
	.uso_timeout           = us_timeout,
	.uso_recovery_complete = us_recovery_complete
};


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
