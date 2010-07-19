/* -*- C -*- */

#include "lib/cdefs.h"

#include "lib/thread.h"
#include "stob/stob.h"
#include "net/net.h"
#include "rpc/rpclib.h"
#include "fop/fop.h"
#include "addb/addb.h"
#include "lib/ut.h"

#include "colibri/init.h"

extern int  c2_memory_init(void);
extern void c2_memory_fini(void);

/** @addtogroup init @{ */

struct init_fini_call {
	int  (*ifc_init)(void);
	void (*ifc_fini)(void);
};

struct init_fini_call subsystem[] = {
	{ &c2_memory_init,  &c2_memory_fini },
	{ &c2_uts_init,     &c2_uts_fini },
	{ &c2_threads_init, &c2_threads_fini },
	{ &c2_addb_init,    &c2_addb_fini },
	{ &c2_stobs_init,   &c2_stobs_fini },
	{ &c2_net_init,     &c2_net_fini },
/*	{ &c2_rpclib_init,  &c2_rpclib_fini }, */
	{ &c2_fops_init,    &c2_fops_fini }
};

static void fini_nr(int i)
{
	while (--i >= 0) {
		if (subsystem[i].ifc_fini != NULL)
			subsystem[i].ifc_fini();
	}
}

int c2_init(void)
{
	int i;
	int result;

	for (result = i = 0; i < ARRAY_SIZE(subsystem); ++i) {
		result = subsystem[i].ifc_init();
		if (result != 0) {
			fini_nr(i);
			break;
		}
	}
	return result;
}

void c2_fini()
{
	fini_nr(ARRAY_SIZE(subsystem));
}

/** @} end of init group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
