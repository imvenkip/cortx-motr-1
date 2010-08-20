/* -*- C -*- */

#include <stdio.h>   /* fprintf */
#include <stdlib.h>  /* atoi */
#include <err.h>

#include "balloc/balloc.h"

int main(int argc, char **argv)
{
	struct c2_balloc_ctxt         ctxt = {
		.bc_nr_thread = 1,
	};
	struct c2_balloc_free_req free_req = { 0 };
	unsigned long long ph;
	unsigned long long len;

	int rc;
	char *path;

	if (argc != 4)
		errx(1, "Usage: balloc path-to-db-dir physical len");

	path = argv[1];
	ph = atoll(argv[2]);
	len = atoll(argv[3]);

	ctxt.bc_home = path;
	rc = c2_balloc_init(&ctxt);
	if (rc != 0) {
		fprintf(stderr, "c2_balloc_init error: %d\n", rc);
		return rc;
	}

	free_req.bfr_logical     = 0x88;
	free_req.bfr_physical    = ph;
	free_req.bfr_len         = len;
	rc = c2_balloc_free(&ctxt, &free_req);

	printf("start=%10llu len=%10llu rc = %d\n", (unsigned long long)ph, (unsigned long long) len, rc);
	printf("start=0x%08llx len=0x%08llx rc = %d\n", (unsigned long long)ph, (unsigned long long) len, rc);

	c2_balloc_fini(&ctxt);
	return rc;
}


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
