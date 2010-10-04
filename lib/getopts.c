/* -*- C -*- */

#define _POSIX_C_SOURCE 2 /* for getopts */
#include <unistd.h>     /* getopts */
#include <stdio.h>      /* fprintf, sscanf */
#include <stdlib.h>     /* strtoull */

/* getopts(3) interface */
extern char *optarg;
extern int   optind;
extern int   optopt;
extern int   opterr;
extern int   optreset;

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/getopts.h"

/**
   @addtogroup getopts
   @{
 */

static void usage(const char *progname, 
		  const struct c2_getopts_opt *opts, unsigned nr)
{
	int i;

	fprintf(stderr, "Unknown option '%c'\nUsage: %s options...\n\n"
		"where valid options are\n\n", optopt, progname);

	for (i = 0; i < nr; ++i) {
		const struct c2_getopts_opt *o;

		o = &opts[i];
		fprintf(stderr, "\t -%c %6.6s: %s\n", o->go_opt,
			o->go_type == GOT_VOID ? "" : 
			o->go_type == GOT_FLAG ? "" : 
			o->go_type == GOT_FORMAT ? "arg" : 
			o->go_type == GOT_NUMBER ? "number" : "string",
			o->go_desc);
	}
}

static int getnum(const char *arg, const char *desc, int64_t *out)
{
	char *end;

	*out = strtoull(arg, &end, 0);
	if (*end != 0) {
		fprintf(stderr, "Failed conversion of \"%s\" to %s\n", 
			arg, desc);
		return -EINVAL;
	} else
		return 0;
	
}

int c2_getopts(const char *progname, int argc, char * const *argv,
	       const struct c2_getopts_opt *opts, unsigned nr)
{
	char *optstring;
	int   i;
	int   scan;
	int   ch;
	int   result;

	C2_ALLOC_ARR(optstring, 2 * nr + 1);
	if (optstring == NULL)
		return -ENOMEM;

	for (scan = i = 0; i < nr; ++i) {
		/* -W is reserved by POSIX.2 and used by GNU getopts as a long
                    option escape. */
		C2_ASSERT(opts[i].go_opt != 'W');
		optstring[scan++] = opts[i].go_opt;
		if (opts[i].go_type != GOT_VOID && opts[i].go_type != GOT_FLAG)
			optstring[scan++] = ':';
		if (opts[i].go_type == GOT_FLAG)
			*opts[i].go_u.got_flag = false;
	}

	result = 0;

	/*
	 * Re-set global getopt(3) state before calling it.
	 */
	optind = 1;
	opterr = 1;

	while (result == 0 && (ch = getopt(argc, argv, optstring)) != -1) {
		for (i = 0; i < nr; ++i) {
			const struct c2_getopts_opt  *o;
			const union c2_getopts_union *u;

			o = &opts[i];
			if (ch != o->go_opt)
				continue;

			u = &o->go_u;
			switch (o->go_type) {
			case GOT_VOID:
				u->got_void();
				break;
			case GOT_NUMBER: {
				int64_t num;

				result = getnum(optarg, o->go_desc, &num);
				if (result == 0)
					u->got_number(num);
				break;
			}
			case GOT_STRING:
				u->got_string(optarg);
				break;
			case GOT_FORMAT:
				result = sscanf(optarg, u->got_fmt.f_string,
						u->got_fmt.f_out);
				result = result == 1 ? 0 : -EINVAL;
				if (result != 0) {
					fprintf(stderr, "Cannot scan \"%s\" "
						"as \"%s\" in \"%s\"\n", 
						optarg, u->got_fmt.f_string, 
						o->go_desc);
				}
				break;
			case GOT_FLAG:
				*u->got_flag = true;
				break;
			default:
				C2_IMPOSSIBLE("Wrong option type.");
			}
			break;
		}
		if (i == nr)  {
			C2_ASSERT(ch == '?');
			usage(progname, opts, nr);
			result = -EINVAL;
		}
	}

	c2_free(optstring);
	return result;
}

/** @} end of getopts group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
