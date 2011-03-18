#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/net.h"

/**
   @addtogroup net Networking.
 */


int c2_net_init()
{
	return 0;
}

void c2_net_fini()
{
}

int c2_net_xprt_init(struct c2_net_xprt *xprt)
{
	return 0;
}
C2_EXPORTED(c2_net_xprt_init);

void c2_net_xprt_fini(struct c2_net_xprt *xprt)
{
}
C2_EXPORTED(c2_net_xprt_fini);

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
