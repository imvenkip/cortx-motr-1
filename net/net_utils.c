#include "net/net.h"

bool nodes_is_same(struct node_id *c1, struct node_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}

struct c2_rpc_op *find_ops(struct c2_rpc_op_table *rop, int op)
{
	int i;

	if (op < 0 || op >= rop->rot_numops)
		return NULL;

	for(i = 0; i < nops; i++)
		if (rop->rot_ops[i].ro_op == op)
			return &rop->rot_ops[i];
	return NULL;
}
