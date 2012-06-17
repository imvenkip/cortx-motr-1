desimdir                      = $(includedir)/colibri/desim
desim_HEADERS                 = \
                                c2t1fs.h \
                                chs.h \
                                client.h \
                                cnt.h \
                                elevator.h \
                                net.h \
                                sim.h \
                                storage.h

noinst_LTLIBRARIES            = libcolibri-desim.la
libcolibri_desim_la_SOURCES   = $(desim_HEADERS) \
                                c2t1fs.c \
                                chs.c \
                                client.c \
                                cnt.c \
                                elevator.c \
                                net.c \
                                sim.c

INCLUDES                      = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

