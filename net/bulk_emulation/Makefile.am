noinst_LTLIBRARIES                = libcolibri-net-bulkmem.la \
                                    libcolibri-net-bulksunrpc.la
INCLUDES                          = -I. -I$(top_srcdir) -I$(top_srcdir)/include

libcolibri_net_bulkmem_la_SOURCES = mem_xprt_xo.c ../bulk_mem.h mem_xprt.h

# depend on included .c files (do not add to _SOURCES) and pvt header
mem_xprt_xo.c: mem_xprt_ep.c mem_xprt_tm.c mem_xprt_msg.c mem_xprt_bulk.c \
		mem_xprt_pvt.h

libcolibri_net_bulksunrpc_la_SOURCES = \
   sunrpc_xprt_xo.c ../bulk_sunrpc.h sunrpc_xprt.h $(bulksunrpc_U_SOURCES)

bulksunrpc_U_SOURCES              = sunrpc_io_u.h sunrpc_io_u.c

# depend on included .c files (do not add to _SOURCES) and pvt header
sunrpc_xprt_xo.c: sunrpc_xprt_bulk.c sunrpc_xprt_ep.c sunrpc_xprt_msg.c \
		sunrpc_xprt_tm.c sunrpc_xprt_pvt.h

EXTRA_DIST                 = sunrpc_xprt_bulk.c sunrpc_xprt_ep.c sunrpc_xprt_msg.c \
                             sunrpc_xprt_tm.c sunrpc_xprt_pvt.h mem_xprt_ep.c \
                             mem_xprt_bulk.c mem_xprt_msg.c mem_xprt_pvt.h \
                             mem_xprt_tm.c ksunrpc_server.c sunrpc_io.ff

MOSTLYCLEANFILES           = $(bulksunrpc_U_SOURCES)

sunrpc_xprt_pvt.h: $(bulksunrpc_U_SOURCES)
$(bulksunrpc_U_SOURCES): sunrpc_io.ff \
    $(top_builddir)/fop/rt/libc2rt.la $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

clean-local:
	cd @SRCDIR@/net/bulk_emulation ; \
	rm -fr sunrpc_io_[uk].[hc]
