INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(SQLITE_CFLAGS)					\
	$(PACKAGEKIT_CFLAGS)					\
	$(RPM_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(ARCHIVE_CFLAGS)					\
	-I$(top_srcdir)/libzif					\
	-I$(top_builddir)/libzif				\
	-I.							\
	-DZIF_COMPILATION					\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/zif"\"			\
	-DEGG_VERBOSE="\"PK_VERBOSE\""				\
	-DEGG_LOGGING="\"PK_LOGGING\""				\
	-DEGG_CONSOLE="\"PK_CONSOLE\""				\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 						\
	zif.pc

lib_LTLIBRARIES =						\
	libzif.la

libzif_includedir = $(includedir)/libzif

libzif_include_HEADERS =					\
	zif.h							\
	zif-completion.h					\
	zif-string.h						\
	zif-string-array.h					\
	zif-depend.h						\
	zif-depend-array.h					\
	zif-package.h						\
	zif-package-local.h					\
	zif-package-remote.h					\
	zif-store.h						\
	zif-store-local.h					\
	zif-store-remote.h					\
	zif-repo-md.h						\
	zif-repo-md-filelists.h					\
	zif-repo-md-primary.h					\
	zif-sack.h						\
	zif-repos.h						\
	zif-utils.h						\
	zif-groups.h						\
	zif-config.h						\
	zif-monitor.h						\
	zif-download.h

libzif_la_SOURCES =						\
	egg-debug.c						\
	egg-debug.h						\
	egg-string.c						\
	egg-string.h						\
	zif-completion.c					\
	zif-completion.h					\
	zif-string.c						\
	zif-string.h						\
	zif-string-array.c					\
	zif-string-array.h					\
	zif-depend.c						\
	zif-depend.h						\
	zif-depend-array.c					\
	zif-depend-array.h					\
	zif-package.c						\
	zif-package.h						\
	zif-package-local.c					\
	zif-package-local.h					\
	zif-package-remote.c					\
	zif-package-remote.h					\
	zif-store.c						\
	zif-store.h						\
	zif-store-local.c					\
	zif-store-local.h					\
	zif-store-remote.c					\
	zif-store-remote.h					\
	zif-repo-md.c						\
	zif-repo-md.h						\
	zif-repo-md-filelists.c					\
	zif-repo-md-filelists.h					\
	zif-repo-md-primary.c					\
	zif-repo-md-primary.h					\
	zif-sack.c						\
	zif-sack.h						\
	zif-repos.c						\
	zif-repos.h						\
	zif-utils.c						\
	zif-utils.h						\
	zif-groups.c						\
	zif-groups.h						\
	zif-config.c						\
	zif-config.h						\
	zif-monitor.c						\
	zif-monitor.h						\
	zif-download.c						\
	zif-download.h

libzif_la_LIBADD =						\
	$(GLIB_LIBS)						\
	$(SQLITE_LIBS)						\
	$(PACKAGEKIT_LIBS)					\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(NULL)

libzif_la_LDFLAGS =						\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^zif_.*'

libzif_la_CFLAGS =						\
	$(WARNINGFLAGS_C)

if EGG_BUILD_TESTS
check_PROGRAMS =						\
	zif-self-test

zif_self_test_SOURCES =						\
	$(libzif_la_SOURCES)					\
	egg-test.h						\
	egg-test.c						\
	zif-self-test.c						\
	$(NULL)

zif_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(PACKAGEKIT_LIBS)					\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(SQLITE_LIBS)

zif_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = zif-self-test
endif

EXTRA_DIST =							\
	zif.pc.in						\
	zif-marshal.list

BUILT_SOURCES = 						\
	zif-marshal.c						\
	zif-marshal.h

zif-marshal.c: zif-marshal.list
	echo "#include \"zif-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=zif_marshal --body >> $@

zif-marshal.h: zif-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=zif_marshal --header > $@

CLEANFILES = $(BUILT_SOURCES)

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)

