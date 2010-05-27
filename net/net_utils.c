#include <string.h>
#include <errno.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/svc.h>

#include "lib/memory.h"
#include "lib/rwlock.h"
#include "net/net.h"

bool c2_services_are_same(const struct c2_service_id *c1,
			  const struct c2_service_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}


struct c2_rpc_op_table {
	/**
	 number of operations in table
	 */
	int			rot_index;
	/**
	 number an allocated entries
	*/
	int			rot_maxindex;
	/**
	 protect add vs find
	 */
	struct c2_rwlock	rot_ops_lock;
	/**
	 array of rpc operations
	 */
	struct c2_rpc_op	*rot_ops;
};

/**
  nullproc handler.

  This will always exist in every operation table.
*/
static bool null_handler(const struct c2_rpc_op *op, void *arg, void **ret)
{
	printf("got NULLPROC: ping\n");
	if (ret)
		*ret = NULL;
	return true;
}

static const struct c2_rpc_op null_op = {
        .ro_op          = NULLPROC,
        .ro_arg_size    = 0,
        .ro_xdr_arg     = (c2_xdrproc_t)xdr_void,
        .ro_result_size = 0,
        .ro_xdr_result  = (c2_xdrproc_t)xdr_void,
        .ro_handler     = null_handler
};


int c2_rpc_op_register(struct c2_rpc_op_table *table, 
		       const struct c2_rpc_op *op)
{
	struct c2_rpc_op *old;
	int rc = 0;

	c2_rwlock_write_lock(&table->rot_ops_lock);
	if (table->rot_index == table->rot_maxindex) { 
		old = table->rot_ops;
		C2_ALLOC_ARR(table->rot_ops, table->rot_maxindex + 8);
		if (table->rot_ops == NULL) {
			table->rot_ops = old;
			rc = -ENOMEM;
			goto out;
		}
		table->rot_maxindex += 8;
		c2_free(old);
	}
	table->rot_ops[table->rot_index ++] = *op;
out:
	c2_rwlock_write_unlock(&table->rot_ops_lock);
	return rc;
}

const struct c2_rpc_op *c2_rpc_op_find(struct c2_rpc_op_table *rop, uint64_t op)
{
	int i;

	if (rop == NULL)
		return NULL;

	c2_rwlock_read_lock(&rop->rot_ops_lock);
	for(i = 0; i <= rop->rot_maxindex; i++) {
		if (rop->rot_ops[i].ro_op == op) {
			c2_rwlock_read_unlock(&rop->rot_ops_lock);
			return &rop->rot_ops[i];
		}
	}
	c2_rwlock_read_unlock(&rop->rot_ops_lock);

	return NULL;
}

int c2_rpc_op_table_init(struct c2_rpc_op_table **table)
{
	struct c2_rpc_op_table *t;
	int result;

	C2_ALLOC_PTR(t);
	if (!t)
		return -ENOMEM;

	c2_rwlock_init(&t->rot_ops_lock);
	*table = t;

	/* add nullproc */
	result = c2_rpc_op_register(t, &null_op);
	C2_ASSERT(result == 0);
	return 0;
}

void c2_rpc_op_table_fini(struct c2_rpc_op_table *table)
{
	c2_rwlock_fini(&table->rot_ops_lock);
	c2_free(table->rot_ops);
	c2_free(table);
}
