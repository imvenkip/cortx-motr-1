rm_u.h rm_u.c: rm.ff \
                  $(top_builddir)/fop/libc2rt.la \
		  $(top_builddir)/fop/fop2c
		  $(top_builddir)/fop/fop2c -uk $<

noinst_LTLIBRARIES       = libcolibri-rm.la
libcolibri_rm_la_SOURCES = rm_u.h rm_u.c rm_fop_init.c rm_fop.h rm.h rm.c
INCLUDES                 = -I. -I$(top_srcdir) -I$(top_srcdir)/include

EXTRA_DIST       = rm.ff

clean-local:
	rm -fr rm_[uk].[hc]
