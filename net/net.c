#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <colibri/colibri.h>
#include "net.h"

extern int  user_sunrpc_init(void);
extern void user_sunrpc_fini(void);

int c2_net_init()
{
	return user_sunrpc_init();
}

void c2_net_fini()
{
	user_sunrpc_fini();
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
