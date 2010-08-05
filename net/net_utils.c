/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/svc.h>

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/rwlock.h"
#include "net/net.h"

/**
   @addtogroup net Networking.
 */

bool c2_services_are_same(const struct c2_service_id *c1,
			  const struct c2_service_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}

/** @} end of net group */
