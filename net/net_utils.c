#include "net/net.h"

bool nodes_is_same(struct node_id *c1, struct node_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}
