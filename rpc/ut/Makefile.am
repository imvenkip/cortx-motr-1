INCLUDES             = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include -iquote ../

noinst_LTLIBRARIES   = librpc-ut.la

ONWIRE_FOPS          = onwire_fops.h onwire_fops.c

$(ONWIRE_FOPS) : onwire_fops.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

librpc_ut_la_SOURCES = rpclib_ut.c                 \
                       ../it/ping_fop.c            \
                       ../it/ping_fop_xc.c         \
                       ../it/ping_fom.c            \
                       service.c                   \
                       onwire_fops.h onwire_fops.c \
                       onwire_ut.c                 \
                       formation2.c

librpc_ut_la_LIBADD = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST          = onwire_fops.ff

MOSTLYCLEANFILES    = $(ONWIRE_FOPS)
