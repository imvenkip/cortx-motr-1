/* -*- C -*- */

#include "rpc/rpc_common.h"

bool clients_is_same(struct client_id *c1, struct client_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}

bool session_is_same(struct session_id *s1, struct session_id *s2)
{
	return memcmp(s1, s2, sizeof *s1) == 0;
}