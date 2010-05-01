#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <colibri/colibri.h>

#include "net/net.h"
#include "net/net_types.h"
#include "net/net_internal.h"

int net_init()
{
	c2_net_conn_init();

	return 0;
}

void net_fini()
{
	c2_net_conn_fini();

	return;
}
