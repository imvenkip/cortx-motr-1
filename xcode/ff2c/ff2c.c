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
 * Original creation date: 02-Jan-2012
 */

/**
   @addtogroup xcode

   <b>ff2c</b>

   ff2c is a simple translator, taking as an input a set of descriptions of
   desired serialized representations and producing a set of C declarations of
   types having given serialized representations. In addition, definitions of
   xcode data (c2_xcode_type and c2_xcode_field) for generated types is
   produced.

   Serialized representation descriptions are given in a very simple language
   with the following grammar:

   @verbatim
ff             ::= statement-list
statement-list ::= statement | statement ';' statement-list
statement      ::= require | declaration
require        ::= 'require' '"' pathname '"'
declaration    ::= type identifier
type           ::= atomic | compound | opaque | identifier
atomic         ::= 'void' | 'u8' | 'u32' | 'u64'
compound       ::= kind '{' field-list '}'
opaque         ::= '*' identifier
kind           ::= 'record' | 'union' | 'sequence'
field-list     ::= field | field ';' field-list
field          ::= declaration tag escape
tag            ::= empty | ':' expression
escape         ::= empty | '[' identifier ']'
   @endverbatim

The language is case-sensitive. Tokens are separated by whitespace and C-style
comments.

The meaning of language constructs is explained in the following example (which
uses Pascal-style comments instead of valid C-style comments due to technical
restrictions):
@code

(* "require" statement introduces a dependency on other source file. For each
   "require", an #include directive is produced, which includes corresponding
   header file, "lib/vec.h" in this case *)
require "lib/vec";
require "fop/fop";

(* Following in this file are type declaration statements, which all have form
   "type-declaration type-name". *)

(* define "fid" as a RECORD type, having two U64 fields. Produced C declaration
   is:

struct fid {
	uint64_t f_container;
	uint64_t f_offset;
}; *)
record {
	u64 f_container;
	u64 f_offset
} fid;

(* define "optfid" as a UNION containing a byte discriminator field (o_flag),
   followed either by a fid (provided o_flag's value is 1) or a U32 value
   o_short (provided o_flag's value is 3). Produced C declaration is:

struct optfid {
	uint8_t o_flag;
	union {
		struct fid o_fid;
		uint32_t o_short;
	} u;
}; *)
union {
	u8  o_flag;
	fid o_fid   :1;
	u32 o_short :3
} optfid;

(* define optfidarray as a counted array of optfid instances. Produced C
   declaration is:

struct optfidarray {
	uint64_t ofa_nr;
	struct optfid *ofa_data;
}; *)
sequence {
	u64    ofa_nr;
	optfid ofa_data
} optfidarray;

(* define fixarray as a fixed-size array of optfids. Array size is NR, which
   must be defined in one of "require"-d files. Produced C declaration is:

struct fixarray {
	c2_void_t fa_none;
	struct optfid *fa_data;
}; *)
sequence {
	void   fa_none :NR;
	optfid fa_data
} fixarray;

(* demonstrate declaration of a more complex structure. Produced C declaration
   is:

struct package {
	struct fid p_fid;
	struct c2_cred *p_cred;
	struct package_p_name {
		uint32_t s_nr;
		struct p_name_s_el {
			uint8_t e_flag;
			struct fixarray e_payload;
			struct s_el_e_datum {
				uint64_t d_0;
				uint64_t d_1;
			} e_datum;
		} s_el;
	} p_name;
}; *)
record {
	fid      p_fid;

        (* "p_cred" is opaque field. It is represented as a pointer to struct
           c2_cred. The actual type of pointed object is returned by
           c2_package_cred_get() function. *)
       *c2_cred  p_cred [c2_package_cred_get];

       (* field's type can be defined in-place. ff2c generates a name of the
          form "parent_type"_"field_name" for such anonymous type. *)
	sequence {
		u32 s_nr;
		record {
			u8       e_flag;
			fixarray e_payload;
			record {
				u64 d_0;
				u64 d_1
			} e_datum
		} s_el
	} p_name
} package
@endcode

   @{
 */

#include <err.h>
#include <unistd.h>                           /* getopt, close, open */
#include <sys/mman.h>                         /* mmap, munmap */
#include <sys/types.h>
#include <sys/stat.h>                         /* stat */
#include <fcntl.h>                            /* O_RDONLY */
#include <libgen.h>                           /* dirname */
#include <string.h>                           /* basename, strdup, strlen */
#include <stdlib.h>                           /* malloc, free */
#include <ctype.h>                            /* toupper */
#include <stdio.h>                            /* fopen, fclose */

#include "xcode/ff2c/lex.h"
#include "xcode/ff2c/parser.h"
#include "xcode/ff2c/sem.h"
#include "xcode/ff2c/gen.h"

int main(int argc, char **argv)
{
	int          fd;
	int          optch;
	int          result;
	const char  *path;
	void        *addr;
	struct stat  buf;
	char        *scratch;
	char        *ch;
	char        *bname;
	char        *gname;
	char        *out_h;
	char        *out_c;
	size_t       len;
	FILE        *c;
	FILE        *h;

	struct ff2c_context   ctx;
	struct ff2c_term     *t;
	struct ff2c_ff        ff;
	struct ff2c_gen_opt   opt;

	while ((optch = getopt(argc, argv, "")) != -1) {
	}

	path = argv[optind];
	fd = open(path, O_RDONLY);
	if (fd == -1)
		err(1, "cannot open \"%s\"", path);
	result = fstat(fd, &buf);
	if (result == -1)
		err(1, "cannot fstat \"%s\"", path);
	addr = mmap(NULL, buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		err(1, "cannot mmap \"%s\"", path);

	scratch = fmt("%s", path);
	len = strlen(scratch);
	if (len > 3 && strcmp(scratch + len - 3, ".ff") == 0)
		*(scratch + len - 3) = 0;

	out_h = fmt("%s.h", scratch);
	out_c = fmt("%s.c", scratch);

	bname = basename(scratch);
	gname = fmt("__COLIBRI_%s_%s_H__", basename(dirname(scratch)), bname);

	for (ch = gname; *ch != 0; ch++) {
		*ch = toupper(*ch);
		if (strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", *ch) == NULL)
 			*ch = '_';
	}

	opt.go_basename  = bname;
	opt.go_guardname = gname;

	c = fopen(out_c, "w");
	if (c == NULL)
		err(1, "cannot open \"%s\" for writing", out_c);

	h = fopen(out_h, "w");
	if (h == NULL)
		err(1, "cannot open \"%s\" for writing", out_h);

	memset(&ctx, 0, sizeof ctx);
	memset(&ff, 0, sizeof ff);

	ff2c_context_init(&ctx, addr, buf.st_size);
	result = ff2c_parse(&ctx, &t);
	if (result != 0)
		err(2, "cannot parse");

	ff2c_sem_init(&ff, t);

	opt.go_out = h;
	ff2c_h_gen(&ff, &opt);
	opt.go_out = c;
	ff2c_c_gen(&ff, &opt);

	ff2c_sem_fini(&ff);
	ff2c_term_fini(t);
	ff2c_context_fini(&ctx);

	fclose(h);
	fclose(c);

	free(out_c);
	free(out_h);
	free(gname);
	free(scratch);
	result = munmap(addr, buf.st_size);
	if (result == -1)
		warn("cannot munmap");
	result = close(fd);
	if (result == -1)
		warn("cannot close");
	return EXIT_SUCCESS;
}

/** @} end of xcode group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
