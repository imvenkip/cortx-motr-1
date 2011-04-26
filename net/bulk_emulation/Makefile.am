noinst_LTLIBRARIES                = libcolibri-net-bulkmem.la \
                                    libcolibri-net-bulksunrpc.la
INCLUDES                          = -I. -I$(top_srcdir) -I$(top_srcdir)/include

libcolibri_net_bulkmem_la_SOURCES = mem_xprt_xo.c $(bulkmem_HEADERS)

bulkmemdir                        = $(topdir)/net/bulk_emulation
bulkmem_HEADERS                   = ../bulk_mem.h mem_xprt.h

# depend on included .c files (do not add to _SOURCES) and pvt header
mem_xprt_xo.c: mem_xprt_ep.c mem_xprt_tm.c mem_xprt_msg.c mem_xprt_bulk.c \
		mem_xprt_pvt.h

libcolibri_net_bulksunrpc_la_SOURCES = \
   sunrpc_xprt_xo.c $(bulksunrpc_HEADERS) $(bulksunrpc_U_SOURCES)

bulksunrpcdir                     = $(topdir)/net/bulk_emulation
bulksunrpc_HEADERS                = ../bulk_sunrpc.h sunrpc_xprt.h
bulksunrpc_U_SOURCES              = sunrpc_io_u.h sunrpc_io_u.c
bulksunrpc_K_sources              = sunrpc_io_k.h sunrpc_io_k.c

# depend on included .c files (do not add to _SOURCES) and pvt header
sunrpc_xprt_xo.c: sunrpc_xprt_bulk.c sunrpc_xprt_ep.c sunrpc_xprt_msg.c \
		sunrpc_xprt_pvt.h

MOSTLYCLEANFILES           = $(bulksunrpc_U_SOURCES) $(bulksunrpc_K_sources)

$(bulksunrpc_U_SOURCES) $(bulksunrpc_K_sources): sunrpc_io.ff \
    $(top_builddir)/fop/libc2rt.la $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<
