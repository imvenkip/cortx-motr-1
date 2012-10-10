include $(top_srcdir)/Makefile.xc

dtmdir                     = $(includedir)/colibri/dtm
dtm_HEADERS                = dtm.h verno.h

noinst_LTLIBRARIES         = libcolibri-dtm.la
libcolibri_dtm_la_SOURCES  = dtm.h dtm.c verno.h verno.c verno_xc.c verno_xc.h
INCLUDES                   = -iquote . -iquote $(top_srcdir) \
			     -iquote $(top_srcdir)/include

clean-local: clean-xc
