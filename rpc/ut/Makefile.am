INCLUDES          = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include -iquote ../

noinst_LTLIBRARIES = librpc-ut.la

librpc_ut_la_SOURCES = rpclib_ut.c \
			  ../it/ping_fop.c \
			  ../it/ping_fop_u.c \
			  ../it/ping_fom.c \
			  service.c \
			  formation2.c

librpc_ut_la_LIBADD = $(top_builddir)/colibri/libcolibri.la

clean-local:
	cd @SRCDIR@/rpc/ut ;
