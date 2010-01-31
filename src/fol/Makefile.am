lib_LTLIBRARIES    = libfol.la

fol.c fol.h fol_template.c fol_print.c: fol.t dbtypes.h
	cd @DB4SRC@/dist ; \
        awk -f gen_rec.awk \
	    -v source_file=@SRCDIR@/src/fol/fol.c \
	    -v header_file=@SRCDIR@/src/fol/fol.h \
	    -v print_file=@SRCDIR@/src/fol/fol_print.c \
	    -v template_file=@SRCDIR@/src/fol/fol_template.0.c < @SRCDIR@/src/fol/fol.t

libfol_la_SOURCES  = fol.c fol_template.c fol_print.c fol.h dbtypes.h
libfol_la_LDFLAGS  = -release @LT_RELEASE@
libfol_la_LIBADD   = $(top_builddir)/src/lib/libc2.la @LIBS@ @DB4LIBS@

INCLUDES           = -I. -I$(top_srcdir)/include -I @DB4SRC@ -I@DB4SRC@/build_unix

EXTRA_DIST	   = fol.t

clean-local:
	cd @SRCDIR@/src/fol ; \
	rm -fr fol.c fol.h fol_print.c fol_template.0.c
