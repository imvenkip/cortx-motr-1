include $(top_srcdir)/Makefile.xc

addbdir                     = $(includedir)/mero/addb
addb_HEADERS                = addb.h addb_macros.h addb_pvt.h addb_wire.h addb_wire_xc.h

noinst_LTLIBRARIES          = libmero-addb.la
libmero_addb_la_SOURCES  = addb.c addb_wire_xc.c

INCLUDES                    = -iquote . -iquote $(top_srcdir)/include

EXTRA_DIST                  = addb_ct.c addb_ctxobj.c addb_macros.h addb_pvt.h \
                              addb_rpcsink.c addb_wire.h \
                              addbmc.dia addbmc.png

addb.c: addb_wire_xc.h

clean-local: clean-xc
