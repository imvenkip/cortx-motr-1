INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I/usr/include

ping_fop_u.h ping_fop_u.c ping_fop_k.h ping_fop_k.c: ping_fop.ff \
		$(top_builddir)/fop/rt/libc2rt.la \
		$(top_builddir)/fop/fop2c
		$(top_builddir)/fop/fop2c -ku $<

io_fop_init.c io_u.h io_u.c:
	ln -sf $(top_builddir)/stob/ut/io_fop_init.c io_fop_init.c
	ln -sf $(top_builddir)/stob/ut/io_u.h io_u.h
	ln -sf $(top_builddir)/stob/ut/io_u.c io_u.c

noinst_PROGRAMS      = rpcping

rpcping_SOURCES     = io_u.h io_u.c io_fop_init.c \
			ping_fop_u.h ping_fop_u.c ping_fop.h ping_fop.c \
			ping_fom.h ping_fom.c \
			main.c $(top_builddir)/rpc/ut/formation-ut.c

rpcping_LDADD       = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST     = ping_fop.ff
