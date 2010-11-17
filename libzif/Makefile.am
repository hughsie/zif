INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(SQLITE_CFLAGS)					\
	$(RPM_CFLAGS)						\
	$(ZLIB_CFLAGS)						\
	$(BZLIB_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(ARCHIVE_CFLAGS)					\
	-I$(top_srcdir)/libzif					\
	-I$(top_builddir)/libzif				\
	-I.							\
	-DZIF_COMPILATION					\
	-DG_LOG_DOMAIN=\"Zif\"					\
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
	zif-category.h						\
	zif-changeset.h						\
	zif-config.h						\
	zif-depend.h						\
	zif-download.h						\
	zif-groups.h						\
	zif-lock.h						\
	zif-manifest.h						\
	zif-md.h						\
	zif-md-metalink.h					\
	zif-md-mirrorlist.h					\
	zif-monitor.h						\
	zif-package.h						\
	zif-package-array.h					\
	zif-package-local.h					\
	zif-package-remote.h					\
	zif-release.h						\
	zif-repos.h						\
	zif-state.h						\
	zif-store-array.h					\
	zif-store.h						\
	zif-store-local.h					\
	zif-store-remote.h					\
	zif-string.h						\
	zif-transaction.h					\
	zif-update.h						\
	zif-update-info.h					\
	zif-upgrade.h						\
	zif-utils.h						\
	zif-version.h

libzif_la_SOURCES =						\
	zif-array.c						\
	zif-array.h						\
	zif-category.c						\
	zif-category.h						\
	zif-changeset.c						\
	zif-changeset.h						\
	zif-config.c						\
	zif-config.h						\
	zif-delta.c						\
	zif-delta.h						\
	zif-depend.c						\
	zif-depend.h						\
	zif-download.c						\
	zif-download.h						\
	zif-groups.c						\
	zif-groups.h						\
	zif-legal.c						\
	zif-legal.h						\
	zif-lock.c						\
	zif-lock.h						\
	zif-manifest.c						\
	zif-manifest.h						\
	zif-marshal.c						\
	zif-marshal.h						\
	zif-md.c						\
	zif-md-comps.c						\
	zif-md-comps.h						\
	zif-md-delta.c						\
	zif-md-delta.h						\
	zif-md-filelists-sql.c					\
	zif-md-filelists-sql.h					\
	zif-md-filelists-xml.c					\
	zif-md-filelists-xml.h					\
	zif-md.h						\
	zif-md-metalink.c					\
	zif-md-metalink.h					\
	zif-md-mirrorlist.c					\
	zif-md-mirrorlist.h					\
	zif-md-other-sql.c					\
	zif-md-other-sql.h					\
	zif-md-primary-sql.c					\
	zif-md-primary-sql.h					\
	zif-md-primary-xml.c					\
	zif-md-primary-xml.h					\
	zif-md-updateinfo.c					\
	zif-md-updateinfo.h					\
	zif-media.c						\
	zif-media.h						\
	zif-monitor.c						\
	zif-monitor.h						\
	zif-package.c						\
	zif-package.h						\
	zif-package-array.c					\
	zif-package-array.h					\
	zif-package-local.c					\
	zif-package-local.h					\
	zif-package-meta.c					\
	zif-package-meta.h					\
	zif-package-remote.c					\
	zif-package-remote.h					\
	zif-release.c						\
	zif-release.h						\
	zif-repos.c						\
	zif-repos.h						\
	zif-state.c						\
	zif-state.h						\
	zif-store-array.c					\
	zif-store-array.h					\
	zif-store.c						\
	zif-store.h						\
	zif-store-local.c					\
	zif-store-local.h					\
	zif-store-meta.c					\
	zif-store-meta.h					\
	zif-store-remote.c					\
	zif-store-remote.h					\
	zif-string.c						\
	zif-string.h						\
	zif-transaction.c					\
	zif-transaction.h					\
	zif-update.c						\
	zif-update.h						\
	zif-update-info.c					\
	zif-update-info.h					\
	zif-upgrade.c						\
	zif-upgrade.h						\
	zif-utils.c						\
	zif-utils.h						\
	zif-version.h

zif-marshal.c: zif-marshal.list
	echo "#include \"zif-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=zif_marshal --body >> $@

zif-marshal.h: zif-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=zif_marshal --header > $@

BUILT_SOURCES = 						\
	zif-marshal.c						\
	zif-marshal.h

CLEANFILES = $(BUILT_SOURCES)

libzif_la_LIBADD =						\
	$(GLIB_LIBS)						\
	$(SQLITE_LIBS)						\
	$(ZLIB_LIBS)						\
	$(BZLIB_LIBS)						\
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

if ZIF_BUILD_TESTS
check_PROGRAMS =						\
	zif-self-test

zif_self_test_SOURCES =						\
	zif-self-test.c

zif_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(ZLIB_LIBS)						\
	$(BZLIB_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(lib_LTLIBRARIES)					\
	$(SQLITE_LIBS)

zif_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS = zif-self-test
endif

EXTRA_DIST =							\
	zif-version.h.in					\
	zif-marshal.list					\
	zif.pc.in

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)

