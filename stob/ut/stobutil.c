#include <stdio.h>

#include "colibri/init.h"
#include "lib/getopts.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/thread.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "db/db.h"
#include "stob/stob.h"
#include "stob/ad.h"
#include "dtm/dtm.h"

enum {
	MAX_STR_ARG_LEN = 256,
};

enum stob_type {
	UNINITIALISED_STOB_TYPE = 0,
	AD_STOB_TYPE,
	LINUX_STOB_TYPE,
	INVALID_STOB_TYPE,
};

struct cmd_line_args {
	/** Create a stob */
	bool               cla_create;

	bool               cla_verbose;

	enum stob_type     cla_stob_type;

	/** Path to database */
	char               cla_db_path[MAX_STR_ARG_LEN + 1];

	/** Name of domain. */
	char               cla_dom_path[MAX_STR_ARG_LEN + 1];

	/** stob id: hi:lo */
	struct c2_stob_id  cla_stob_id;
};

static int cmd_line_args_process(struct cmd_line_args *clargs,
				 int                   argc,
				 char                 *argv[]);

static bool cmd_line_args_are_valid(const struct cmd_line_args *clargs);

static void cmd_line_args_print(const struct cmd_line_args *clargs);

static void usage(void);

static int stob_domain_locate_or_create(enum stob_type          stob_type,
					const char             *dom_path,
					const char             *db_path,
					struct c2_stob_domain **out);

static void stob_domain_fini(struct c2_stob_domain *stob_domain);

static int stob_create(struct c2_stob_domain    *dom,
		       const struct c2_stob_id  *stob_id,
		       struct c2_stob          **out);

int main(int argc, char *argv[])
{
	struct c2_stob_domain *stob_domain;
	struct c2_stob        *stob;
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

	rc = c2_init();
	if (rc != 0) {
		fprintf(stderr, "Colibri initailisation failed\n");
		return rc;
	}

	rc = stob_domain_locate_or_create(clargs.cla_stob_type,
					  clargs.cla_dom_path,
					  clargs.cla_db_path,
					  &stob_domain);
	if (rc != 0)
		goto out;

	rc = stob_create(stob_domain, &clargs.cla_stob_id, &stob);
	if (stob != NULL)
		c2_stob_put(stob);

	stob_domain_fini(stob_domain);

out:
	c2_fini();
	return rc;
}

static int cmd_line_args_process(struct cmd_line_args *clargs,
				 int                   argc,
				 char                 *argv[])
{
	int rc;

	C2_SET0(clargs);
	rc = C2_GETOPTS("stobutil", argc, argv,

		C2_FLAGARG('c', "Create stob", &clargs->cla_create),

		C2_FLAGARG('v', "verbose", &clargs->cla_verbose),

		C2_STRINGARG('t', "Stob type [AD/linux]",
			LAMBDA(void, (const char *str)
			{
				clargs->cla_stob_type =
				strcasecmp(str, "AD") == 0    ? AD_STOB_TYPE :
				strcasecmp(str, "linux") == 0 ? LINUX_STOB_TYPE:
							      INVALID_STOB_TYPE;
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

static bool cmd_line_args_are_valid(const struct cmd_line_args *clargs)
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

	switch (clargs->cla_stob_type) {

		case UNINITIALISED_STOB_TYPE:
			fprintf(stderr, "Error: Missing -t option\n");
			rc = false;
			break;

		case INVALID_STOB_TYPE:
			fprintf(stderr, "Error: Stob type must be either "
					"\"AD\" or \"linux\"\n");
			rc = false;
			break;

		default:
			break;
	}

	/* if stob type is "Ad" then is db path entered ? */
	if (clargs->cla_stob_type == AD_STOB_TYPE &&
		string_is_empty(clargs->cla_db_path)) {
		fprintf(stderr, "Error: db path is must if stob type is "
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

static void cmd_line_args_print(const struct cmd_line_args *clargs)
{
	fprintf(stderr, "Create: %s\n"
			"Stob type: %s\n"
			"db path: %s\n"
			"domain path: %s\n"
			"stob id: %lu:%lu\n"
			"verbose: %s\n",
			clargs->cla_create ? "true" : "false",
			clargs->cla_stob_type == AD_STOB_TYPE ? "AD"
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
			"\n         -t string: Stob type [AD/linux]"
			"\n         -d string: db path"
			"\n         -p string: Domain path"
			"\n         -s string: Stob id: hi:lo\n");

}

/**
   XXX IMPORTANT: This structure definition is copied from
       colibri/colibri_setup.c

   It is assumed that, stobutil.c has very short lifetime. Hence it is
   better to copy the structure definition than to modify colibri_setup.c
   for stobutil.

   Internal structure which encapsulates stob type and
   stob domain references for linux and ad stobs respectively.
 */
struct cs_reqh_stobs {
        /**
           Type of storage domain to be initialise (e.g. Linux or AD)
         */
        const char            *stype;
        /**
           Backend storage object id
         */
        struct c2_stob_id      stob_id;
        /**
           Linux storage domain type.
         */
        struct c2_stob_domain *linuxstob;
        /**
           Allocation data storage domain type.
         */
        struct c2_stob_domain *adstob;
};

/*
 * Defined in colibri/colibri_setup.c
 */
int cs_storage_init(const char *stob_type, const char *stob_path,
                    struct cs_reqh_stobs *stob, struct c2_dbenv *db);

void cs_storage_fini(struct cs_reqh_stobs *stob);

static struct cs_reqh_stobs reqh_stobs;
static struct c2_dbenv      dbenv;

static int stob_domain_locate_or_create(enum stob_type          stob_type,
					const char             *dom_path,
					const char             *db_path,
					struct c2_stob_domain **out)
{
	/* dbenv is not required for linux stob domain. */
	struct c2_dbenv *dbenvp = NULL;
	char            *str_stob_type;
	int              rc;

	/*
	 * XXX It is important to note that, mechanism to create stob domain
	 * in this routine MUST be same as that of provided by
	 * colibri_setup.c:cs_storage_init(). Otherwise, ioservice will not
	 * be able to access the stob-domain.
	 * Rather than re-implementating same thing, better to call very
	 * cs_storage_init() here. We need to prepare input as required
	 * by cs_storage_init().
	 */
	C2_ASSERT(stob_type == AD_STOB_TYPE || stob_type == LINUX_STOB_TYPE);

	*out = NULL;

	if (stob_type == AD_STOB_TYPE) {
		rc = c2_dbenv_init(&dbenv, db_path, 0);
		if (rc != 0) {
			fprintf(stderr, "Failed to init dbenv\n");
			return rc;
		}
		dbenvp = &dbenv;
	}

	str_stob_type = (stob_type == AD_STOB_TYPE) ? "AD" : "Linux";

	rc = cs_storage_init(str_stob_type, dom_path, &reqh_stobs, dbenvp);

	if (rc == 0)
		*out = (stob_type == AD_STOB_TYPE) ? reqh_stobs.adstob
						   : reqh_stobs.linuxstob;

	return rc;
}

static void stob_domain_fini(struct c2_stob_domain *stob_domain)
{
	cs_storage_fini(&reqh_stobs);
}

static int stob_create(struct c2_stob_domain    *dom,
		       const struct c2_stob_id  *stob_id,
		       struct c2_stob          **out)
{
	struct c2_stob *stob;
	struct c2_dtx  *dtx;
	int             rc;

	*out = NULL;
	dtx  = NULL;
	if (dom->sd_type == &ad_stob_type) {
		C2_ALLOC_PTR(dtx);
		rc = dom->sd_ops->sdo_tx_make(dom, dtx);
		if (rc != 0) {
			C2_TRACE("Unable to create transaction context\n");
			return rc;
		}
	}

	rc = c2_stob_find(dom, stob_id, &stob);
	if (rc != 0)
		return rc;

	/*
	 * Here, stob != NULL and c2_stob_find() has taken
	 * reference on stob.
	 * Must call c2_stob_put() on stob, after this point.
	 */
	rc = c2_stob_locate(stob, dtx);
	if (rc == 0) {
		fprintf(stderr, "Stob already exists\n");
		*out = stob;
		return rc;
	}

	rc = c2_stob_create(stob, dtx);
	if (rc == 0) {
		fprintf(stderr, "Stob created successfuly\n");
		if (dtx != NULL)
			rc = c2_db_tx_commit(&dtx->tx_dbtx);
		*out = stob;
	} else {
		fprintf(stderr, "Stob creation failed [%d]\n", rc);
		if (dtx != NULL)
			rc = c2_db_tx_abort(&dtx->tx_dbtx);
		c2_stob_put(stob);
	}

	return rc;
}
