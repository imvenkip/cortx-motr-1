mdservicedir                     = $(includedir)/colibri/mdservice
mdservice_HEADERS                = md_foms.h md_fops.h

md_fops_u.h md_fops_u.c: md_fops.ff \
                  $(top_builddir)/fop/rt/libc2rt.la \
                  $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -u $<


noinst_LTLIBRARIES               = libcolibri-mdservice.la
libcolibri_mdservice_la_SOURCES  = md_fops_u.c md_fops_u.h \
				   md_foms.c md_foms.h md_fops.c md_fops.h
INCLUDES                         = -I. -I$(top_srcdir) -I$(top_srcdir)/include

EXTRA_DIST                       = md_fops.ff

clean-local:
	cd @SRCDIR@/mdservice ; \
	rm -fr md_fops_u.[ch]
