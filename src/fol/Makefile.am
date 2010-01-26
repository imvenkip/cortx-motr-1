lib_LTLIBRARIES    = libfol.la

colibri_fol.c colibri_fol.h colibri_fol_template.c colibri_fol_print.c: colibri_fol.src dbtypes.h
	cd @DB4SRC@/dist ; \
        awk -f gen_rec.awk \
	    -v source_file=@SRCDIR@/fol/colibri_fol.c \
	    -v header_file=@SRCDIR@/fol/colibri_fol.h \
	    -v print_file=@SRCDIR@/fol/colibri_fol_print.c \
	    -v template_file=@SRCDIR@/fol/colibri_fol_template.0.c < @SRCDIR@/fol/colibri_fol.src

libfol_la_SOURCES  = colibri_fol.c colibri_fol_template.c colibri_fol_print.c \
		     colibri_fol.h dbtypes.h

libfol_la_CFLAGS   = @CFLAGS@
libfol_la_LIBADD   = @DB4LIBS@

INCLUDES            = -I$(top_srcdir)/include -I @DB4SRC@ -I@DB4SRC@/build_unix
