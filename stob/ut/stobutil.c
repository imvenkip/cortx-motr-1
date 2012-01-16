#include "colibri/init.h"
#include "lib/trace.h"
#include "lib/getopts.h"
#include "lib/misc.h"
#include "lib/thread.h"
#include "lib/errno.h"
#include "stob/stob.h"

enum {
	MAX_STR_ARG_LEN = 256,
};

enum stob_dom_type {
	STOB_DOM_NOT_ENTERED = 0,
	STOB_DOM_AD,
	STOB_DOM_LINUX,
	STOB_DOM_INVALID,
};

struct cmd_line_args {
	/** Create a stob */
	bool               cla_create;

	bool               cla_verbose;

	enum stob_dom_type cla_stob_dom_type;

	/** Path to database */
	char               cla_db_path[MAX_STR_ARG_LEN + 1];

	/** Name of domain. */
	char               cla_dom_path[MAX_STR_ARG_LEN + 1];

	/** stob id: hi:lo */
	struct c2_stob_id  cla_stob_id;
};

static int cmd_line_args_process(struct cmd_line_args *clargs,
				 int                   argc,
				 char                 *argv[])
{
	int rc;

	C2_SET0(clargs);
	rc = C2_GETOPTS("stobutil", argc, argv,

		C2_FLAGARG('c', "Create stob", &clargs->cla_create),

		C2_FLAGARG('v', "verbose", &clargs->cla_verbose),

		C2_STRINGARG('t', "Stob domain type [AD/linux]",
			LAMBDA(void, (const char *str)
			{
				clargs->cla_stob_dom_type =
				   strcmp(str, "AD") == 0    ? STOB_DOM_AD :
				   strcmp(str, "linux") == 0 ? STOB_DOM_LINUX :
							     STOB_DOM_INVALID;
			})),

		C2_STRINGARG('d', "db path",
			LAMBDA(void, (const char *str)
			{
				strncpy(clargs->cla_db_path, str,
						MAX_STR_ARG_LEN);
				clargs->cla_db_path[MAX_STR_ARG_LEN] = '\0';
			})),

		C2_STRINGARG('p', "Domain path",
			LAMBDA(void, (const char *str)
			{
				strncpy(clargs->cla_dom_path, str,
						MAX_STR_ARG_LEN);
				clargs->cla_dom_path[MAX_STR_ARG_LEN] = '\0';
			})),

		C2_STRINGARG('s', "Stob id: hi:lo",
			LAMBDA(void, (const char *str)
			{
				sscanf(str, "%lu:%lu",
					&clargs->cla_stob_id.si_bits.u_hi,
					&clargs->cla_stob_id.si_bits.u_lo);
			}))

	); /* End C2_GETOPTS() */
	return rc;
}

bool cmd_line_args_are_valid(const struct cmd_line_args *clargs)
{
	bool rc = true;

	bool string_is_empty(const char *str)
	{
		return *str == '\0';
	}
	/*
	 * Currently only supported operation is "create stob".
	 * Hence -c flag is must.
	 */
	if (!clargs->cla_create) {
		 fprintf(stderr, "Error: Missing -c option\n");
		 rc = false;
	}

	switch (clargs->cla_stob_dom_type) {

		case STOB_DOM_NOT_ENTERED:
			fprintf(stderr, "Error: Missing -t option\n");
			rc = false;
			break;

		case STOB_DOM_INVALID:
			fprintf(stderr, "Error: Domain type must be either "
					"\"AD\" or \"linux\"\n");
			rc = false;
			break;

		default:
			break;
	}

	/* if domain type is "Ad" then is db path entered ? */
	if (clargs->cla_stob_dom_type == STOB_DOM_AD &&
		string_is_empty(clargs->cla_db_path)) {
		fprintf(stderr, "Error: db path is must if domain type is "
				"\"AD\"\n");
		rc = false;
	}

	/* Is domain path entered ? */
	if (string_is_empty(clargs->cla_dom_path)) {
		fprintf(stderr, "Error: Missing -p option\n");
		rc = false;
	}

	/* Is stob id entered ? */
	if (clargs->cla_stob_id.si_bits.u_hi == 0 &&
	    clargs->cla_stob_id.si_bits.u_lo == 0) {
		fprintf(stderr, "Error: Missing -s option\n");
		rc = false;
	}

	return rc;
}

void cmd_line_args_print(const struct cmd_line_args *clargs)
{
	fprintf(stderr, "Create: %s\n"
			"Domain type: %s\n"
			"db path: %s\n"
			"domain path: %s\n"
			"stob id: %lu:%lu\n"
			"verbose: %s\n",
			clargs->cla_create ? "true" : "false",
			clargs->cla_stob_dom_type == STOB_DOM_AD ? "AD"
								 : "linux",
			clargs->cla_db_path,
			clargs->cla_dom_path,
			(unsigned long)clargs->cla_stob_id.si_bits.u_hi,
			(unsigned long)clargs->cla_stob_id.si_bits.u_lo,
			clargs->cla_verbose ? "true" : "false");
}

static void usage(void)
{
	fprintf(stderr, "\nUsage:\n");
	fprintf(stderr, "stobutil -c -t [AD -d /db/path |linux] "
			"-p /stob/dom/path -s hi:lo\n");
	fprintf(stderr, "\n         -c       : Create stob"
			"\n         -t string: Stob domain type [AD/linux]"
			"\n         -d string: db path"
			"\n         -p string: Domain path"
			"\n         -s string: Stob id: hi:lo\n");

}

static int stob_domain_locate_or_create(enum stob_dom_type      dom_type,
					const char             *dom_path,
					const char             *db_path,
					struct c2_stob_domain **out)
{

}

int main(int argc, char *argv[])
{
	struct c2_stob_domain *stob_domain;
	struct cmd_line_args   clargs;
	int                    rc;

	rc = cmd_line_args_process(&clargs, argc, argv);
	if (rc != 0)
		return -EINVAL;

	if (!cmd_line_args_are_valid(&clargs)) {
		usage();
		return -EINVAL;
	}

	if (clargs.cla_verbose)
		cmd_line_args_print(&clargs);

	rc = stob_domain_locate_or_create(clargs.cla_stob_dom_type,
					  clargs.cla_dom_path,
					  clargs.cla_db_path,
					  &stob_domain);
	return 0;
}
