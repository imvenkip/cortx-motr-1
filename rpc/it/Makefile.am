INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS      = rpcping
io_fop_init.c io_u.h io_u.c:
	ln -sf $(top_builddir)/stob/ut/io_fop_init.c io_fop_init.c
	ln -sf $(top_builddir)/stob/ut/io_u.h io_u.h
	ln -sf $(top_builddir)/stob/ut/io_u.c io_u.c

rpcping_SOURCES     = io_u.h io_u.c io_fop_init.c \
		      main.c $(top_builddir)/net/bulk_emulation/st/ping.c \
		      $(top_builddir)/rpc/ut/formation-ut-infra.c
rpcping_LDADD       = $(top_builddir)/colibri/libcolibri.la

