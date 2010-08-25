#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/net.h"

/**
   @addtogroup net Networking.
 */

extern int  usunrpc_init(void);
extern void usunrpc_fini(void);

int c2_net_init()
{
	return usunrpc_init();
}

void c2_net_fini()
{
	usunrpc_fini();
}

int c2_net_xprt_init(struct c2_net_xprt *xprt)
{
	return 0;
}

void c2_net_xprt_fini(struct c2_net_xprt *xprt)
{
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
