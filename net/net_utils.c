/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef __KERNEL__
#include "lib/misc.h"
#endif

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
