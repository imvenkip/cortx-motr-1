yaml2dbdir             = $(includedir)/colibri/yaml2db
yaml2db_HEADERS        = yaml2db.h

noinst_LTLIBRARIES     = libcolibri-yaml2db.la

libcolibri_yaml2db_la_SOURCES  = yaml2db.c
libcolibri_yaml2db_la_LIBADD   =  @YAML_LIBS@

INCLUDES = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I/usr/include
