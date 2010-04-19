#include "net/net.h"

bool nodes_is_same(struct node_id const *c1, struct node_id const *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}

struct c2_rpc_op const *find_ops(struct c2_rpc_op_table const *rop, int op)
{
	int i;

	if (rop == NULL)
		return NULL;

	for(i = 0; i < rop->rot_numops; i++)
		if (rop->rot_ops[i].ro_op == op)
			return &rop->rot_ops[i];

	return NULL;
}
