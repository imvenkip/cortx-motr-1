reqhdir                    = $(includedir)/colibri/reqh
reqh_HEADERS               = reqh.h

noinst_LTLIBRARIES         = libcolibri-reqh.la

REQH_FOPS                  = reqh_fops_xc.h reqh_fops_xc.c

$(REQH_FOPS) : reqh_fops_xc.ff \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

libcolibri_reqh_la_SOURCES = $(REQH_FOPS)                   \
			     reqh.c reqh.h reqh_fops.c      \
			     reqh_generic.c reqh_service.c  \
			     reqh_service.h

INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST                 = reqh_fops_xc.ff

MOSTLYCLEANFILES           = $(REQH_FOPS)
