reqhdir                     = $(includedir)/colibri/reqh
reqh_HEADERS                = reqh.h

noinst_LTLIBRARIES          = libcolibri-reqh.la

reqh_fops_u.h reqh_fops_u.c : reqh_fops.ff \
                        $(top_builddir)/fop/rt/libc2rt.la \
                        $(top_builddir)/fop/fop2c
		$(top_builddir)/fop/fop2c -u $<

libcolibri_reqh_la_SOURCES  = reqh_fops_u.c reqh_fops_u.h \
				reqh_fops.ff reqh.c reqh.h reqh_fops.c \
				reqh_generic.c reqh_service.c \
				reqh_service.h

INCLUDES                    = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST = reqh_fops.ff

clean-local:
	cd @SRCDIR@/reqh ; \
	rm -fr reqh_fops_[uk].[ch]
