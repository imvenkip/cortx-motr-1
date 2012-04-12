/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/19/2010
 */

#ifndef __COLIBRI_LIB_GETOPTS_H__
#define __COLIBRI_LIB_GETOPTS_H__

#include "lib/cdefs.h"   /* ARRAY_SIZE */
#include "lib/types.h"

/**
   @defgroup getopts C2 getopt(3) version from heaven.

   c2_getopts() is a higher-level analogue of a standard getopt(3) function. The
   interface is designed to avoid proliferation of nearly identical code
   fragments, typical for getopt(3) usage (switches nested in loops) and to hide
   global state exposed by getopt(3).

   c2_getopts() interface is especially convenient when used together with
   anonymous functions (see LAMBDA() macro in lib/thread.h).

   c2_getopts() is implemented on top of getopt(3).

   @note the final 's' in c2_getopts() is to avoid file-name clashes with
   standard library headers.

   @see lib/ut/getopts.c for usage examples.

   @{
 */

/**
   Types of options supported by c2_getopts().
 */
enum c2_getopts_opt_type {
	/**
	    An option without an argument.

	    When this option is encountered, its call-back
	    c2_getopts_opt::go_u::got_void() is executed for its side-effects.
	 */
	GOT_VOID,
	/**
	   An option with a numerical argument. The argument is expected in the
	   format that strtoull(..., 0) can parse. When this option is
	   encountered, its call-back c2_getopts_opt::go_u::got_number() is
	   invoked with the parsed argument as its sole parameter.
	 */
	GOT_NUMBER,
	/**
	   An option with a string argument. When this option is encountered,
	   its call-back c2_getopts_opt::go_u::got_strint() is invoked with the
	   string argument as its sole parameter.
	 */
	GOT_STRING,
	/**
	   An options with an argument with a format that can be parsed by
	   scanf(3). The argument string is parsed by a call to sscanf(3) with a
	   caller-supplied format string and caller-supplied address to store
	   the result at. No call-back is invoked. The caller is expected to
	   analyse the contents of the address after c2_getopts() returns.
	 */
	GOT_FORMAT,
	/**
	   An option without an argument, serving as a binary flag. When this
	   option is encountered, a user supplied boolean stored at
	   c2_getopts_opt::go_u::got_flag is set to true. If the option wasn't
	   encountered, the flag is set to false. No call-back is invoked. The
	   user is expected to inspect the flag after c2_getopts() returns.
	 */
	GOT_FLAG,
	/** An option without an argument.

	    When this option encountered, program usage is printed to STDERR and
	    program terminates immediately with exit(3).
	 */
	GOT_HELP
};

/**
   A description of an option, recognized by c2_getopts().

   Callers are not supposed to construct these explicitly. C2_*ARG() macros,
   defined below, are used instead.

   @see C2_VOIDARG
   @see C2_NUMBERARG
   @see C2_STRINGARG
   @see C2_FORMATARG
   @see C2_FLAGARG
 */
struct c2_getopts_opt {
	enum c2_getopts_opt_type go_type;
	/** Option character. */
	char                     go_opt;
	/** Human-readable option description. Used in error messages. */
	const char              *go_desc;
	/** Option-type specific data. */
	union c2_getopts_union {
		/** Call-back invoked for a GOT_VOID option. */
		void   (*got_void)(void);
		/** Call-back invoked for a GOT_NUMBER option. */
		void   (*got_number)(int64_t num);
		/** Call-back invoked for a GOT_STRING option. */
		void   (*got_string)(const char *string);
		struct {
			/** Format string for a GOT_FORMAT option. */
			const char *f_string;
			/** Address to store parsed argument for a GOT_FORMAT
			    option. */
			void       *f_out;
		}        got_fmt;
		/** Address of a boolean flag for a GOT_FLAG option. */
		bool    *got_flag;
	} go_u;
};

/**
   Parses command line stored in argv[] array with argc elements according to a
   traditional UNIX/POSIX fashion.

   Recognized options are supplied in opts[] array with nr elements.

   When a parsing error occurs (unrecognized option, invalid argument format,
   etc.), a error message is printed on stderr, followed by a usage summary. The
   summary enumerates all the recognized options, their argument requirements
   and human-readable descriptions. Caller-supplied progname is used as a prefix
   of error messages.

   @note -W option is reserved by POSIX.2. GNU getopt() uses -W as a long option
   escape. Do not use it.
 */
int c2_getopts(const char *progname, int argc, char * const *argv,
	       const struct c2_getopts_opt *opts, unsigned nr);

/**
   A wrapper around c2_getopts(), calculating the size of options array.
 */
#define C2_GETOPTS(progname, argc, argv, ...)				\
	c2_getopts((progname), (argc), (argv),				\
		(const struct c2_getopts_opt []){ __VA_ARGS__ },	\
	   ARRAY_SIZE(((const struct c2_getopts_opt []){ __VA_ARGS__ })))

/**
   Defines a GOT_VOID option, with a given description and a call-back.
 */
#define C2_VOIDARG(ch, desc, func) {		\
	.go_type = GOT_VOID,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_void = (func) }	\
}

/**
   Defines a GOT_NUMBER option, with a given description and a call-back.
 */
#define C2_NUMBERARG(ch, desc, func) {		\
	.go_type = GOT_NUMBER,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_number = (func) }	\
}

/**
   Defines a GOT_STRING option, with a given description and a call-back.
 */
#define C2_STRINGARG(ch, desc, func) {		\
	.go_type = GOT_STRING,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_string = (func) }	\
}

/**
   Defines a GOT_FORMAT option, with a given description, argument format and
   address.
 */
#define C2_FORMATARG(ch, desc, fmt, ptr) {			\
	.go_type = GOT_FORMAT,					\
	.go_opt  = (ch),					\
	.go_desc = (desc),					\
	.go_u    = {						\
		.got_fmt = {					\
			.f_string = (fmt), .f_out = (ptr)	\
		}						\
	}							\
}

/**
   Defines a GOT_FLAG option, with a given description and a flag address.
 */
#define C2_FLAGARG(ch, desc, ptr) {		\
	.go_type = GOT_FLAG,			\
	.go_opt  = (ch),			\
	.go_desc = (desc),			\
	.go_u    = { .got_flag = (ptr) }	\
}

/**
   Defines a GOT_HELP option.
 */
#define C2_HELPARG(ch) {			\
	.go_type = GOT_HELP,			\
	.go_opt  = (ch),			\
	.go_desc = "display this help and exit",\
	.go_u    = { .got_void = NULL }		\
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
