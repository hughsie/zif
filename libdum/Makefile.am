INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(SQLITE_CFLAGS)					\
	$(PACKAGEKIT_CFLAGS)					\
	$(RPM_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	-I$(top_srcdir)/libdum					\
	-I$(top_builddir)/libdum				\
	-I.							\
	-DDUM_COMPILATION					\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/dum"\"			\
	-DEGG_VERBOSE="\"PK_VERBOSE\""				\
	-DEGG_LOGGING="\"PK_LOGGING\""				\
	-DEGG_CONSOLE="\"PK_CONSOLE\""				\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

dumgconfigdir = $(libdir)/dumgconfig
dumgconfig_DATA = 						\
	dum.pc

lib_LTLIBRARIES =						\
	libdum.la

libdum_includedir = $(includedir)/libdum

libdum_include_HEADERS =					\
	dum.h

libdum_la_SOURCES =						\
	egg-debug.c						\
	egg-debug.h						\
	egg-string.c						\
	egg-string.h						\
	dum-string.c						\
	dum-string.h						\
	dum-string-array.c					\
	dum-string-array.h					\
	dum-depend.c						\
	dum-depend.h						\
	dum-depend-array.c					\
	dum-depend-array.h					\
	dum-package.c						\
	dum-package.h						\
	dum-package-local.c					\
	dum-package-local.h					\
	dum-package-remote.c					\
	dum-package-remote.h					\
	dum-store.c						\
	dum-store.h						\
	dum-store-local.c					\
	dum-store-local.h					\
	dum-store-remote.c					\
	dum-store-remote.h					\
	dum-repo-md.c						\
	dum-repo-md.h						\
	dum-repo-md-master.c					\
	dum-repo-md-master.h					\
	dum-repo-md-filelists.c					\
	dum-repo-md-filelists.h					\
	dum-repo-md-primary.c					\
	dum-repo-md-primary.h					\
	dum-sack.c						\
	dum-sack.h						\
	dum-sack-local.c					\
	dum-sack-local.h					\
	dum-sack-remote.c					\
	dum-sack-remote.h					\
	dum-repos.c						\
	dum-repos.h						\
	dum-utils.c						\
	dum-utils.h						\
	dum-groups.c						\
	dum-groups.h						\
	dum-config.c						\
	dum-config.h						\
	dum-monitor.c						\
	dum-monitor.h						\
	dum-download.c						\
	dum-download.h

libdum_la_LIBADD =						\
	$(GLIB_LIBS)						\
	$(SQLITE_LIBS)						\
	$(PACKAGEKIT_LIBS)					\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(NULL)

libdum_la_LDFLAGS =						\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^dum_.*'

libdum_la_CFLAGS =						\
	$(WARNINGFLAGS_C)

if EGG_BUILD_TESTS
check_PROGRAMS =						\
	dum-self-test

dum_self_test_SOURCES =						\
	$(libdum_la_SOURCES)					\
	egg-test.h						\
	egg-test.c						\
	dum-self-test.c						\
	$(NULL)

dum_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(PACKAGEKIT_LIBS)					\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(SQLITE_LIBS)

dum_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = dum-self-test
endif

EXTRA_DIST =							\
	dum.pc.in						\
	dum-marshal.list

BUILT_SOURCES = 						\
	dum-marshal.c						\
	dum-marshal.h

dum-marshal.c: dum-marshal.list
	echo "#include \"dum-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=dum_marshal --body >> $@

dum-marshal.h: dum-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=dum_marshal --header > $@

CLEANFILES = $(BUILT_SOURCES)

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)

