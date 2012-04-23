foldir                     = $(includedir)/colibri/fol
fol_HEADERS                = fol.h lsn.h

noinst_LTLIBRARIES         = libcolibri-fol.la
libcolibri_fol_la_SOURCES  = fol.c fol.h lsn.h
INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include

#CWD=$(PWD)
#
#fol.c fol.h fol_template.c fol_print.c: fol.t dbtypes.h
#	cd @DB_SRC@/dist ; \
#        awk -f gen_rec.awk \
#	    -v source_file=$(CWD)/fol.c \
#	    -v header_file=$(CWD)/fol.h \
#	    -v print_file=$(CWD)/fol_print.c \
#	    -v template_file=$(CWD)/fol_template.0.c < @SRCDIR@/fol/fol.t

#INCLUDES                   = -I@DB_SRC@
#EXTRA_DIST	           = fol.t

#clean-local:
#	cd @SRCDIR@/fol ; \
#	rm -fr fol.c fol.h fol_print.c fol_template.0.c
