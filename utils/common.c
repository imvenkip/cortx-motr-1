/* -*- C -*- */

#include <stdio.h>    /* setbuf */
#include <stdlib.h>   /* system */
#include <sys/stat.h> /* mkdir */
#include <unistd.h>   /* chdir */
#include <errno.h>

#include "colibri/init.h"
#include "lib/assert.h"

int unit_start(const char *sandbox)
{
	int result;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	result = c2_init();
	if (result == 0) {
		result = mkdir(sandbox, 0700);
		if (result == 0 || errno == EEXIST)
			result = chdir(sandbox);
		if (result != 0)
			result = -errno;
	}
	return result;
}

void unit_end(const char *sandbox)
{
	char *cmd;
	int   rc;

	c2_fini();

	rc = chdir("..");
	C2_ASSERT(rc == 0);

	rc = asprintf(&cmd, "rm -fr \"%s\"", sandbox);
	C2_ASSERT(rc > 0);

	rc = system(cmd);
	C2_ASSERT(rc == 0);

	free(cmd);
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
