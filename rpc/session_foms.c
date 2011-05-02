#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "rpc/session_foms.h"
#include "rpc/session_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "net/net.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#else
#include "rpc/session_u.h"
#endif

#include "fop/fop_format_def.h"



struct c2_fom_ops c2_rpc_fom_conn_create_ops = {
	.fo_fini = &c2_rpc_fom_conn_create_fini,
	.fo_state = &c2_rpc_fom_conn_create_state
};

static struct c2_fom_type_ops c2_rpc_fom_conn_create_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_conn_create_type = {
	.ft_ops = &c2_rpc_fom_conn_create_type_ops
};

int c2_rpc_fom_conn_create_state(struct c2_fom *fom)
{
	printf("Called conn_create_state\n");
	return 0;
}
void c2_rpc_fom_conn_create_fini(struct c2_fom *fom)
{
	printf("called conn_create_fini\n");
}


