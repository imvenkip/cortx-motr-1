#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <colibri/colibri.h>

#include "net/net.h"

extern int  sunrpc_init(void);
extern void sunrpc_fini(void);

int c2_net_init()
{
	return sunrpc_init();
}

void c2_net_fini()
{
	sunrpc_fini();
}

int c2_net_xprt_init(struct c2_net_xprt *xprt)
{
	return 0;
}

void c2_net_xprt_fini(struct c2_net_xprt *xprt)
{
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
