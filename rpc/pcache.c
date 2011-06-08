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
 * Original author: Alexey Lyashkov
 * Original creation date: 05/12/2010
 */

#include <lib/cdefs.h>
#include <lib/memory.h>

#include <rpc/rpclib.h>
#include <rpc/pcache.h>

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

