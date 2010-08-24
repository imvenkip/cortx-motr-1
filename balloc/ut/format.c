/* -*- C -*- */

#include <stdio.h>   /* fprintf */
#include <sys/stat.h> /* mkdir */
#include <err.h>

#include "balloc/balloc.h"

int main(int argc, char **argv)
{
	struct c2_balloc_format_req   format_req = { 0 };

	int rc;
	char *path;

	if (argc != 2)
		errx(1, "Usage: balloc path-to-db-dir");

	path = argv[1];
	rc = mkdir(path, 0700);
	if (rc != 0)
		err(1, "mkdir(\"%s\")", path);

	format_req.bfr_db_home = path;
	format_req.bfr_totalsize = 4096ULL * 1024 * 1024 * 1; //=40GB
	format_req.bfr_blocksize = 4096;
	format_req.bfr_groupsize = 4096 * 8; //=128MB = ext4 group size
	format_req.bfr_reserved_groups = 2;

	rc = c2_balloc_format(&format_req);
	if (rc == 0) {
		printf("Successfully formatted!\n");
	} else
		err(1, "format error (\"%s\")", path);

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
