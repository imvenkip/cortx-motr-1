rm_u.h rm_u.c: rm.ff \
                  $(top_builddir)/fop/libc2rt.la \
		  $(top_builddir)/fop/fop2c
		  $(top_builddir)/fop/fop2c -uk $<

noinst_LTLIBRARIES       = libcolibri-rm.la
libcolibri_rm_la_SOURCES = rm.h rm.c rm_u.h rm_fops.h
#libcolibri_rm_la_SOURCES = rm_u.h rm_u.c rm_fops.c rm_fops.h \
#			   rm.h rm.c rm_foms.c rm_foms.h
INCLUDES                 = -I. -I$(top_srcdir) -I$(top_srcdir)/include

EXTRA_DIST       = rm.ff

clean-local:
	rm -fr rm_[uk].[hc]
