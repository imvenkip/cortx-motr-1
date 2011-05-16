#include "rpc/rpccore.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "rpc/session.h"
#include "net/net.h"

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
	c2_list_link_init(&us->us_linkage);
	return 0;
}

static void update_stream_fini(struct c2_update_stream *us)
{
	c2_mutex_fini(&us->us_guard);
	c2_list_link_fini(&us->us_linkage);
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
	return 0;
}

int  c2_rpc_core_init(void)
{
	return 0;
}

void c2_rpc_core_fini(void)
{
}

int c2_rpc_submit(struct c2_update_stream *us, struct c2_rpc_item *item,
		  enum c2_rpc_item_priority prio,
		  const struct c2_time *deadline)
{
	return 0;
}

int c2_rpc_cancel(struct c2_rpc_item *item)
{
	return 0;
}

int c2_rpc_group_open(struct c2_rpcmachine *machine,
		      struct c2_rpc_group **group)
{
	return 0;
}

int c2_rpc_group_close(struct c2_rpcmachine *machine, struct c2_rpc_group *group)
{
	return 0;
}

int c2_rpc_group_submit(struct c2_rpc_group *group,
			struct c2_rpc_item *item,
			struct c2_update_stream *us,
			enum c2_rpc_item_priority prio,
			const struct c2_time *deadline)
{
	return 0;
}

int c2_rpc_reply_timedwait(struct c2_rpc_item *item, const struct c2_time *timeout)
{
	return 0;
}

int c2_rpc_group_timedwait(struct c2_rpc_group *group, const struct c2_time *timeout)
{
	return 0;
}

static int update_stream_alloc(struct c2_rpcmachine *machine,
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

static int lookup_update_stream(struct c2_rpcmachine *machine,
				uint64_t session_id, uint64_t slot_id,
				struct c2_update_stream **out)
{
	struct c2_update_stream *us;
	struct c2_list_link	*us_pos;
	struct c2_list		*streams = &machine->cr_processing.crp_us_list;
	struct c2_mutex         *guard = &machine->cr_processing.crp_guard;

	c2_mutex_lock(guard);
	c2_list_for_each(streams, us_pos) {
		us = c2_list_entry(us_pos, struct c2_update_stream, us_linkage);
		if (us->us_session_id == session_id && us->us_slot_id == slot_id) {
			*out = us;
			c2_mutex_unlock(guard);
			return 0;
		}
	}

	*out = NULL;
	c2_mutex_unlock(guard);
	return -1;
}

static int lookup_slot(struct c2_rpcmachine *machine,
		       struct c2_rpc_session *sess,
		       enum c2_update_stream_flags flag,
		       uint64_t *slot_out)
{
	struct c2_update_stream *us;
	struct c2_list_link	*us_pos;
	struct c2_list		*streams = &machine->cr_processing.crp_us_list;
	struct c2_mutex         *guard = &machine->cr_processing.crp_guard;
	int			 i;
	struct c2_rpc_snd_slot  *slot;

	c2_mutex_lock(guard);
	c2_list_for_each(streams, us_pos) {
		us = c2_list_entry(us_pos, struct c2_update_stream, us_linkage);
		if (us->us_session_id == sess->s_session_id) {
			if (flag != C2_UPDATE_STREAM_DEDICATED_SLOT) {
				C2_ASSERT(sess->s_nr_slots > 0);
				slot = sess->s_slot_table[0]; /* just take first slot*/
				*slot_out = slot->ss_generation;
				c2_mutex_unlock(guard);
				return 0;
			} else {
				for (i = 0; i < sess->s_nr_slots; ++i) {
					slot = sess->s_slot_table[i];
					if (slot->ss_generation != us->us_slot_id) {
						*slot_out = slot->ss_generation;
						c2_mutex_unlock(guard);
						return 0;
					}
				}
			}
		}
	}
 
	c2_mutex_unlock(guard);
	return -1;
}

static int lookup_session_slot(struct c2_rpcmachine *machine,
			       struct c2_service_id *srvid,
			       enum c2_update_stream_flags flag,
			       uint64_t *session_id,
			       uint64_t *slot_id)
{
	int rc = -1;
	uint64_t slot_out;
	struct c2_rpc_conn      *conn;
	struct c2_list_link	*conn_pos;
	struct c2_list		*connections = NULL;

	struct c2_rpc_session   *sess;
	struct c2_list_link	*sess_pos;
	struct c2_list		*sessions = NULL;

	c2_list_for_each(connections, conn_pos) {
		conn = c2_list_entry(conn_pos, struct c2_rpc_conn, c_link);

		c2_mutex_lock(&conn->c_mutex);
		if (c2_services_are_same(srvid, conn->c_service_id)) {
			sessions = &conn->c_sessions;
			c2_list_for_each(sessions, sess_pos) {
				sess = c2_list_entry(sess_pos, struct c2_rpc_session, s_link);
				rc = lookup_slot(machine, sess, flag, &slot_out);

				*session_id = sess->s_session_id;
				*slot_id    = slot_out;
				c2_mutex_unlock(&conn->c_mutex);
				return rc;
			}
		}
		c2_mutex_unlock(&conn->c_mutex);
	}

	return rc;
}

int c2_rpc_update_stream_get(struct c2_rpcmachine *machine,
			     struct c2_service_id *srvid,
			     enum c2_update_stream_flags flag,
			     const struct c2_update_stream_ops *ops,
			     struct c2_update_stream **out)
{
	int		 rc;
	uint64_t	 session_id;
	uint64_t	 slot_id;
	struct c2_list	*streams = &machine->cr_processing.crp_us_list;

	rc = lookup_session_slot(machine, srvid, flag, &session_id, &slot_id);
	if (rc < 0)
		return rc;

	rc = lookup_update_stream(machine, session_id, slot_id, out);
	if (rc < 0) {
		rc = update_stream_alloc(machine, ops, out);
		if (rc < 0)
			return rc;
		(*out)->us_session_id = session_id;
		(*out)->us_slot_id = slot_id;
		c2_list_add(streams, &(*out)->us_linkage);
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
			      struct c2_time *time)
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
	c2_list_init(&proc->crp_us_list);
	c2_mutex_init(&proc->crp_guard);

	return rc;
}

static void rpc_proc_fini(struct c2_rpc_processing *proc)
{
	c2_mutex_fini(&proc->crp_guard);
	c2_list_fini(&proc->crp_us_list);
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

int  c2_rpcmachine_init(struct c2_rpcmachine *machine)
{
	int rc;

	rc = rpc_proc_init(&machine->cr_processing);
	if (rc < 0)
		return rc;

	rc = rpc_stat_init(&machine->cr_statistics);
	if (rc < 0) {
		rpc_proc_fini(&machine->cr_processing);
	}

	return 0;
}

void c2_rpcmachine_fini(struct c2_rpcmachine *machine)
{
	rpc_stat_fini(&machine->cr_statistics);
	rpc_proc_fini(&machine->cr_processing);	
}

/** simple vector of RPC-item operations */
static void rpc_item_op_sent(struct c2_rpc_item *item)
{
	DBG("item: xid: %lu, SENT\n", item->ri_xid);
}

static void rpc_item_op_added(struct c2_rpc *rpc, struct c2_rpc_item *item)
{
	DBG("item: xid: %lu, ADDED\n", item->ri_xid);
}

static void rpc_item_op_replied(struct c2_rpc_item *item, int rc)
{
	DBG("item: xid: %lu, REPLIED\n", item->ri_xid);
}

static const struct c2_rpc_item_type_ops rpc_item_ops = {
	.rio_sent    = rpc_item_op_sent,
	.rio_added   = rpc_item_op_added,
	.rio_replied = rpc_item_op_replied
};

/** simple vector of update stream operations */
static void us_timeout(struct c2_update_stream *us)
{
	DBG("us: ssid: %lu, slotid: %lu, TIMEOUT\n", us->us_session_id, us->us_slot_id);
}
static void us_recovery_complete(struct c2_update_stream *us)
{
	DBG("us: ssid: %lu, slotid: %lu, RECOVERED\n", us->us_session_id, us->us_slot_id);
}

static const struct c2_update_stream_ops update_stream_ops = {
	.uso_timeout           = us_timeout,
	.uso_recovery_complete = us_recovery_complete
};

/**
   RPC Item type ops
 */

/**
   RPC item ops function
   Function to return new rpc item embedding the given read segment, 
   by creating a new fop calling new fop op
 */
int c2_rpc_item_get_new_read_item(struct c2_rpc_item *curr_item, 
		struct c2_rpc_item *res_item, struct c2_fop_segment_seq *seg)
{
	struct c2_fop			*fop = NULL;
	struct c2_fop			*res_fop = NULL;
	int 				 res = 0;

	C2_PRE(item != NULL);

	fop = container_of(curr_item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	res = fop->f_type->ft_ops->fto_get_io_fop(fop, &res_fop, seg);
	res_item = res_fop->f_item;
	return 0;
}

/**
   RPC item ops function
   Function to return new rpc item embedding the given write vector, 
   by creating a new fop calling new fop op
 */
int c2_rpc_item_get_new_write_item(struct c2_rpc_item *curr_item, 
		struct c2_rpc_item *res_item, struct c2_fop_io_vec *vec)
{
	struct c2_fop			*fop = NULL;
	struct c2_fop			*res_fop = NULL;
	int 				 res = 0;
	
	C2_PRE(item != NULL);

	fop = container_of(curr_item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	res = fop->f_type->ft_ops->fto_get_io_fop(&res_fop, vec);
	res_item = res_fop->f_item;
	return 0;
}
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
