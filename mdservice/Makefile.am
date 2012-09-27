include $(top_srcdir)/Makefile.ff

mdservicedir                     = $(includedir)/colibri/mdservice
mdservice_HEADERS                = md_foms.h md_fops.h md_service.h md_fops_ff.h

MD_FOPS_SOURCES                  = md_fops_ff.h md_fops_ff.c

noinst_LTLIBRARIES               = libcolibri-mdservice.la
libcolibri_mdservice_la_SOURCES  = $(MD_FOPS_SOURCES) md_service.c md_service.h \
                                   md_foms.c md_foms.h md_fops.c md_fops.h
INCLUDES                         = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST                       = md_fops.ff

MOSTLYCLEANFILES                 = $(MD_FOPS_SOURCES)
