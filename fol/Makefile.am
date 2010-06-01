lib_LTLIBRARIES    = libcolibri-fol.la
CWD=$(PWD)

fol.c fol.h fol_template.c fol_print.c: fol.t dbtypes.h
	cd @DBSRC@/dist ; \
        awk -f gen_rec.awk \
	    -v source_file=$(CWD)/fol.c \
	    -v header_file=$(CWD)/fol.h \
	    -v print_file=$(CWD)/fol_print.c \
	    -v template_file=$(CWD)/fol_template.0.c < @SRCDIR@/fol/fol.t

libcolibri_fol_la_SOURCES  = fol.c fol_template.c fol_print.c fol.h dbtypes.h
libcolibri_fol_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_fol_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@ @DBLIBS@ @PTHREAD_LIBS@

INCLUDES           = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@ -I@DBSRC@/build_unix

EXTRA_DIST	   = fol.t

clean-local:
	cd @SRCDIR@/fol ; \
	rm -fr fol.c fol.h fol_print.c fol_template.0.c
