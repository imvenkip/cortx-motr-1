#include <string.h>

#include "net/net_types.h"

bool c2_nodes_is_same(const struct c2_node_id *c1, const struct c2_node_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}

struct c2_rpc_op const *c2_find_op(const struct c2_rpc_op_table *rop, int op)
{
	int i;

	if (rop == NULL)
		return NULL;

	for(i = 0; i < rop->rot_numops; i++)
		if (rop->rot_ops[i].ro_op == op)
			return &rop->rot_ops[i];

	return NULL;
}
