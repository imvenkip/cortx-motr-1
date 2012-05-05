noinst_LTLIBRARIES                = libcolibri-net-bulkmem.la

INCLUDES                          = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

libcolibri_net_bulkmem_la_SOURCES = mem_xprt_xo.c ../bulk_mem.h mem_xprt.h

# depend on included .c files (do not add to _SOURCES) and pvt header
mem_xprt_xo.c: mem_xprt_ep.c mem_xprt_tm.c mem_xprt_msg.c mem_xprt_bulk.c \
		mem_xprt_pvt.h

EXTRA_DIST                 = mem_xprt_ep.c mem_xprt_bulk.c mem_xprt_msg.c \
			     mem_xprt_pvt.h mem_xprt_tm.c
