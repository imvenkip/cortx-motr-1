#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>

#include "stob.h"

/**
   @addtogroup stob
   @{
 */

extern int  linux_stob_module_init(void);
extern void linux_stob_module_fini(void);

int main(int argc, char **argv)
{
	linux_stob_module_init();
	sleep(600);
	return 0;
}

/** @} end group stob */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
