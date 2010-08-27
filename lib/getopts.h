/* -*- C -*- */

#ifndef __COLIBRI_LIB_GETOPTS_H__
#define __COLIBRI_LIB_GETOPTS_H__

#include "lib/cdefs.h"   /* ARRAY_SIZE */
#include "lib/types.h"

/**
   @defgroup getopts C2 getopts(3) version from heaven.
   @{
 */

enum c2_getopts_opt_type {
	GOT_VOID,
	GOT_NUMBER,
	GOT_STRING
};

struct c2_getopts_opt {
	enum c2_getopts_opt_type go_type;
	char                    go_opt;
	const char             *go_desc;
	union {
		void (*got_void)(void);
		void (*got_number)(int64_t num);
		void (*got_string)(const char *string);
	} go_u;
};

int c2_getopts(const char *progname, int argc, char * const argv[],
	      const struct c2_getopts_opt *opts, unsigned nr);

#define C2_GETOPTS(progname, argc, argv, ...)				\
	c2_getopts((progname), (argc), (argv),				\
		(const struct c2_getopts_opt []){ __VA_ARGS__ },	\
	   ARRAY_SIZE(((const struct c2_getopts_opt []){ __VA_ARGS__ })))

#define C2_VOIDARG(ch, desc, func) {		\
	.go_type = GOT_VOID,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_void = (func) }	\
}

#define C2_NUMBERARG(ch, desc, func) {		\
	.go_type = GOT_NUMBER,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_number = (func) }	\
}

#define C2_STRINGARG(ch, desc, func) {		\
	.go_type = GOT_STRING,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_string = (func) }	\
}

/** @} end of getopts group */

/* __COLIBRI_LIB_GETOPTS_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
