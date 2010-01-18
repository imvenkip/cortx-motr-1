SUBDIRS    = doc man include src patches tests

EXTRA_DIST = README NEWS AUTHORS COPYING ChangeLog

define all-sources
	( find ./ -name '*.[ch]' | grep -v .git )
endef

TAGS:
	$(all-sources) | etags -

tags:
	$(all-sources) | xargs -- ctags -w -o TAGS
