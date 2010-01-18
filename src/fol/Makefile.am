CWD			 = $(shell pwd)

colibri_fol.c colibri_fol_template.c colibri_fol_print.c: colibri_fol.src dbtypes.h
	cd @DB4SRC@/dist ; \
        awk -f gen_rec.awk \
	    -v source_file=$(CWD)/colibri_fol.c \
	    -v header_file=$(CWD)/colibri_fol.h \
	    -v print_file=$(CWD)/colibri_fol_print.c \
	    -v template_file=$(CWD)/colibri_fol_template.0.c < $(CWD)/colibri_fol.src

INCLUDES                = -I$(top_srcdir)/include -I @DB4SRC@ -I@DB4SRC@/build_unix
EXTRA_DIST		= colibri_fol.src
