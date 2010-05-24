#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>    /* memset */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */
#include <errno.h>

#include "lib/assert.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/sunrpc/sunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"

#include "io_fop.h"

/**
   @addtogroup stob
   @{
 */

static struct c2_stob_domain *dom;

static bool create_handler(const struct c2_rpc_op *op, void *arg, void **ret)
{
	struct c2_stob_io_create_fop     *in = arg;
	struct c2_stob_io_create_rep_fop *ex;
	struct c2_stob_id                 id;
	struct c2_stob                   *obj;
	bool result;

	C2_ALLOC_PTR(ex);
	C2_ASSERT(ex != NULL);

	id.si_seq = in->sic_object.f_d1;
	id.si_id  = in->sic_object.f_d2;
	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	result = c2_stob_create(obj);
	C2_ASSERT(result == 0);
	ex->sicr_rc = 0;
	*ret = ex;
	return true;
}


static const struct c2_rpc_op create_op = {
	.ro_op          = SIF_CREAT,
	.ro_arg_size    = sizeof(struct c2_stob_io_create_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_create_fop,
	.ro_result_size = sizeof(struct c2_stob_io_create_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_create_rep_fop,
	.ro_handler     = create_handler
};

/**
   Simple server for unit-test purposes. 
 */
int main(int argc, char **argv)
{
	int result;

	struct c2_service_id    sid = { .si_uuid = "UUURHG" };
	struct c2_rpc_op_table *ops;
	struct c2_service       service;
	struct c2_net_domain    ndom;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	result = linux_stob_module_init();
	C2_ASSERT(result == 0);
	
	result = mkdir("./__s", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	result = mkdir("./__s/o", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  "./__s", &dom);
	C2_ASSERT(result == 0);

	memset(&service, 0, sizeof service);

	result = c2_net_init();
	C2_ASSERT(result == 0);

	result = c2_net_xprt_init(&c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, "127.0.0.1", PORT);
	C2_ASSERT(result == 0);

	c2_rpc_op_table_init(&ops);
	C2_ASSERT(ops != NULL);

	result = c2_rpc_op_register(ops, &create_op);
	C2_ASSERT(result == 0);

	result = c2_service_start(&service, &sid, ops);
	C2_ASSERT(result >= 0);

	while (1)
		sleep(1);

	c2_service_stop(&service);
	c2_rpc_op_table_fini(ops);

	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_user_sunrpc_xprt);
	c2_net_fini();

	dom->sd_ops->sdo_fini(dom);
	linux_stob_module_fini();

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
