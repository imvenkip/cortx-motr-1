noinst_LTLIBRARIES                = libcolibri-net-bulkmem.la \
                                    libcolibri-net-bulksunrpc.la
INCLUDES                          = -I. -I$(top_srcdir) -I$(top_srcdir)/include

libcolibri_net_bulkmem_la_SOURCES = mem_xprt_xo.c $(bulkmem_HEADERS)

bulkmemdir                        = $(topdir)/net/bulk_emulation
bulkmem_HEADERS                   = ../bulk_mem.h mem_xprt.h mem_xprt_pvt.h

# depend on included .c files (do not add to _SOURCES)
mem_xprt_xo.c: mem_xprt_ep.c mem_xprt_tm.c mem_xprt_msg.c mem_xprt_bulk.c

libcolibri_net_bulksunrpc_la_SOURCES = \
   sunrpc_xprt_xo.c $(bulksunrpc_HEADERS)

bulksunrpcdir                     = $(topdir)/net/bulk_emulation
bulksunrpc_HEADERS                = ../bulk_sunrpc.h sunrpc_xprt.h


