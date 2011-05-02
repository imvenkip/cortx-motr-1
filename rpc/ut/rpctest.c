#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <rpc/xdr.h>
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/bitstring.h" 
#include "lib/misc.h"
#include "db/db.h" 
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "net/usunrpc/usunrpc.h"
#include "net/net.h"
#include "rpc/session_fops.h"

int main(void)
{
	struct c2_fop		*fop;
	struct c2_fom		*fom = NULL;
	printf("Program start\n");
	c2_init();

	fop = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
	C2_ASSERT(fop != NULL);
	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	fom->fo_ops->fo_state(fom);
	C2_ASSERT(fom != NULL);
	
	c2_fini();
	printf("program end\n");
	return 0;
}
