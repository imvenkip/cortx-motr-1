#include "lib/errno.h"
#include "net/net.h"
#include "lib/memory.h"

/**
   @addtogroup net Networking.

   @{
 */

static const struct c2_addb_loc net_cli_addb = {
	.al_name = "net-cli"
};

C2_ADDB_EV_DEFINE(net_addb_conn_send, "send", 0x10, C2_ADDB_STAMP);
C2_ADDB_EV_DEFINE(net_addb_conn_call, "call", 0x11, C2_ADDB_STAMP);

#define ADDB_ADD(conn, ev, ...) \
C2_ADDB_ADD(&(conn)->nc_addb, &net_cli_addb, ev , ## __VA_ARGS__)


/**
 Get an ADDB record from domain.
 */
static
struct c2_addb_rec_header *c2_addb_record_get(struct c2_net_domain *domain)
{
	struct c2_addb_rec_header *header = NULL;
	struct c2_addb_rec_item   *item = NULL;

	c2_rwlock_write_lock(&domain->nd_lock);
	if (!c2_list_is_empty(&domain->nd_addb_items)) {
		item = container_of(domain->nd_addb_items.l_head,
			            struct c2_addb_rec_item,
				    ari_linkage);
		c2_list_del(&item->ari_linkage);
	}
	c2_rwlock_write_unlock(&domain->nd_lock);

	if (item) {
		header = item->ari_header;
		c2_list_link_fini(&item->ari_linkage);
		c2_free(item);
	}
	return header;
}


/**
   Send the request to connection and wait for reply synchronously.

   The ->sio_call() is responsible to add the addb record and free it.
 */
int c2_net_cli_call(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_call);
	call->ac_addb_rec = c2_addb_record_get(conn->nc_domain);
	return conn->nc_ops->sio_call(conn, call);
}
C2_EXPORTED(c2_net_cli_call);

/**
   Send the request to connection asynchronously and don't wait for reply.

   The ->sio_send() is responsible to add the addb record and free it.
 */
int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_send);
	call->ac_addb_rec = c2_addb_record_get(conn->nc_domain);
	return conn->nc_ops->sio_send(conn, call);
}
C2_EXPORTED(c2_net_cli_send);

int c2_service_id_init(struct c2_service_id *id, struct c2_net_domain *dom, ...)
{
	va_list varargs;
	int     result;

	id->si_domain = dom;
	va_start(varargs, dom);
	result = dom->nd_xprt->nx_ops->xo_service_id_init(id, varargs);
	va_end(varargs);
	return result;
}
C2_EXPORTED(c2_service_id_init);

void c2_service_id_fini(struct c2_service_id *id)
{
	id->si_ops->sis_fini(id);
}

/** @} end of net group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
