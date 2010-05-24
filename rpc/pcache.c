#include "lib/cdefs.h"
#include "lib/memory.h"

#include "rpc/rpclib.h"
#include "rpc/pcache.h"

static const char pcache_db_name[] = "pcache";

static int pcache_key_enc(void *buffer, void **rec, uint32_t *size)
{
	/** XXX */
	*rec = buffer;
	*size = sizeof(struct c2_rcid);

	return 0;
}

int c2_pcache_init(struct c2_rpc_server *srv)
{
	srv->rs_cache.c_pkey_enc = pcache_key_enc;
	return 	c2_cache_init(&srv->rs_cache, srv->rs_env, "test_db1", 0);
}

void c2_pcache_fini(struct c2_rpc_server *srv)
{
	c2_cache_fini(&srv->rs_cache);
}

