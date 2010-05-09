#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <colibri/colibri.h>

#include "thread.h"

int libc2_init()
{
	return c2_threads_init();
}

void libc2_fini()
{
	c2_threads_fini();
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
