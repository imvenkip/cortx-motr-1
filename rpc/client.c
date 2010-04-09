#include "lib/c2list.h"
#include "lib/cc.h"
#include "lib/refs.h"
#include "lib/memory.h"

struct c2_rwlock clients_list_lock;
static struct c2_list servers_list;

static void rpc_client_free(struct c2_ref *ref)
{
	struct rpc_client *cli;

	cli = container_of(ref, struct rpc_client, rc_ref);
	c2_free(cli);
}

struct rpc_client *rpc_client_create(const struct client_id *id)
{
	struct rpc_client *cli;

	cli = c2_alloc(sizeof *cli);
	if(!cli)
		return NULL;

	c2_list_link_init(&cli->rc_link);
	cli->rc_id = *id;
	c2_ref_init(&cli->cl_ref, 1, rpc_client_free);
	c2_rwlock_init(&cli->rc_sessions_list_lock);
	c2_list_init(&cli->rc_sessions_list);

	return cli;
}

void rpc_client_destroy(struct rpc_client *cli)
{

}

struct rpc_client *rpc_client_find(const struct client_id id)
{

}
