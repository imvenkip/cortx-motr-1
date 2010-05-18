#include <string.h>
#include <errno.h>

#include "lib/memory.h"
#include "lib/cc.h"
#include "net/net_types.h"

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

int c2_rpc_op_table_init(struct c2_rpc_op_table **table)
{
	struct c2_rpc_op_table *t;

	C2_ALLOC_PTR(t);
	if (!t)
		return -ENOMEM;

	c2_rwlock_init(&t->rot_ops_lock);
	*table = t;

	return 0;
}

void c2_rpc_op_table_fini(struct c2_rpc_op_table *table)
{
	c2_rwlock_fini(&table->rot_ops_lock);

	c2_free(table);
}

int c2_rpc_op_register(struct c2_rpc_op_table *table, const struct c2_rpc_op *op)
{
	void *old;
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
	}
	table->rot_ops[table->rot_index ++] = *op;
out:
	c2_rwlock_write_unlock(&table->rot_ops_lock);
	return rc;
}

struct c2_rpc_op const *c2_rpc_op_find(struct c2_rpc_op_table *rop, int op)
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
