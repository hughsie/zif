-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(SQLITE_CFLAGS)					\
	$(RPM_CFLAGS)						\
	$(ZLIB_CFLAGS)						\
	$(BZLIB_CFLAGS)						\
	$(LZMA_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(ARCHIVE_CFLAGS)					\
	$(GPGME_CFLAGS)						\
	-I$(top_srcdir)/libzif					\
	-I$(top_builddir)/libzif				\
	-I.							\
	-DZIF_COMPILATION					\
	-DG_LOG_DOMAIN=\"Zif\"					\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DTOP_SRCDIR=\""$(top_srcdir)"\" 			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DSBINDIR=\""$(sbindir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 						\
	zif.pc

lib_LTLIBRARIES =						\
	libzif.la

libzif_includedir = $(includedir)/libzif

libzif_include_HEADERS =					\
	zif.h							\
	zif-private.h						\
	zif-category.h						\
	zif-changeset.h						\
	zif-config.h						\
	zif-db.h						\
	zif-delta.h						\
	zif-depend.h						\
	zif-download.h						\
	zif-groups.h						\
	zif-history.h						\
	zif-lock.h						\
	zif-manifest.h						\
	zif-monitor.h						\
	zif-object-array.h					\
	zif-package-array.h					\
	zif-package.h						\
	zif-package-local.h					\
	zif-package-private.h					\
	zif-package-remote.h					\
	zif-package-rhn.h					\
	zif-release.h						\
	zif-repos.h						\
	zif-state.h						\
	zif-state-private.h					\
	zif-store-array.h					\
	zif-store-directory.h					\
	zif-store.h						\
	zif-store-local.h					\
	zif-store-meta.h					\
	zif-store-remote.h					\
	zif-store-rhn.h						\
	zif-string.h						\
	zif-transaction.h					\
	zif-update.h						\
	zif-update-info.h					\
	zif-upgrade.h						\
	zif-utils.h						\
	zif-version.h

libzif_la_SOURCES =						\
	zif-category.c						\
	zif-category.h						\
	zif-category-private.h					\
	zif-changeset.c						\
	zif-changeset.h						\
	zif-changeset-private.h					\
	zif-config.c						\
	zif-config.h						\
	zif-db.c						\
	zif-db.h						\
	zif-delta.c						\
	zif-delta.h						\
	zif-delta-private.h					\
	zif-depend.c						\
	zif-depend.h						\
	zif-depend-private.h					\
	zif-download.c						\
	zif-download.h						\
	zif-download-private.h					\
	zif-groups.c						\
	zif-groups.h						\
	zif-history.c						\
	zif-history.h						\
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
	zif-object-array.c					\
	zif-object-array.h					\
	zif-package-array.c					\
	zif-package-array.h					\
	zif-package-array-private.h				\
	zif-package.c						\
	zif-package.h						\
	zif-package-local.c					\
	zif-package-local.h					\
	zif-package-meta.c					\
	zif-package-meta.h					\
	zif-package-private.h					\
	zif-package-remote.c					\
	zif-package-remote.h					\
	zif-package-rhn.c					\
	zif-package-rhn.h					\
	zif-release.c						\
	zif-release.h						\
	zif-repos.c						\
	zif-repos.h						\
	zif-state.c						\
	zif-state.h						\
	zif-state-private.h					\
	zif-store-array.c					\
	zif-store-array.h					\
	zif-store.c						\
	zif-store-directory.c					\
	zif-store-directory.h					\
	zif-store.h						\
	zif-store-local.c					\
	zif-store-local.h					\
	zif-store-meta.c					\
	zif-store-meta.h					\
	zif-store-remote.c					\
	zif-store-remote.h					\
	zif-store-remote-private.h				\
	zif-store-rhn.c						\
	zif-store-rhn.h						\
	zif-string.c						\
	zif-string.h						\
	zif-transaction.c					\
	zif-transaction.h					\
	zif-transaction-private.h				\
	zif-update.c						\
	zif-update.h						\
	zif-update-info.c					\
	zif-update-info.h					\
	zif-update-info-private.h				\
	zif-update-private.h					\
	zif-upgrade.c						\
	zif-upgrade.h						\
	zif-upgrade-private.h					\
	zif-utils.c						\
	zif-utils.h						\
	zif-utils-private.h					\
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
	$(LZMA_LIBS)						\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(GPGME_LIBS)						\
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
	$(LZMA_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(lib_LTLIBRARIES)					\
	$(SQLITE_LIBS)

zif_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS = zif-self-test

# check-tool: Run tests under $(TOOL)
check-tool:
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) check \
	TESTS_ENVIRONMENT='$(TESTS_ENVIRONMENT) $(top_builddir)/libtool --mode=execute \
	env $(TOOL)'

# Check tests under valgrind.  Saves log to log-valgrind.txt
VALGRIND_FLAGS = \
	--tool=memcheck --suppressions=$(srcdir)/.valgrind-suppressions \
	--track-origins=yes \
	--leak-check=yes
	$(EXTRA_VALGRIND_FLAGS)
CLEANFILES +=  log-valgrind.txt
valgrind_verbose = $(valgrind_verbose_$(V))
valgrind_verbose_ = $(valgrind_verbose_$(AM_DEFAULT_VERBOSITY))
valgrind_verbose_0 = | \
	grep '\(^[^=]\|ERROR SUMMARY\|definitely lost\|indirectly lost\)' | grep -v ': 0'
# TODO: The following check does not fail if valgrind finds error.  It should.
check-valgrind:
	$(AM_V_at)$(MAKE) $(AM_MAKEFLGS) check-tool TOOL="valgrind $(VALGRIND_FLAGS)" \
	2>&1 | tee log-valgrind.txt $(valgrind_verbose)

.PHONY: check-tool check-valgrind

endif

if HAVE_INTROSPECTION
introspection_sources =						\
	$(libzif_la_SOURCES)					\
	$(libzifbase_include_HEADERS)

Zif-1.0.gir: libzif.la
Zif_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
Zif_1_0_gir_CFLAGS = $(INCLUDES)
Zif_1_0_gir_SCANNERFLAGS = --identifier-prefix=Zif \
				--symbol-prefix=zif_ \
				--warn-all \
				--add-include-path=$(srcdir) \
				--c-include="zif-private.h"
Zif_1_0_gir_EXPORT_PACKAGES = zif
Zif_1_0_gir_LIBS = libzif.la
Zif_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += Zif-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

install-data-hook:
	if test -w $(DESTDIR)$(prefix)/; then \
		mkdir -p $(DESTDIR)$(localstatedir)/lib/zif; \
		touch $(DESTDIR)$(localstatedir)/lib/zif/history.db; \
	fi

EXTRA_DIST =							\
	zif-version.h.in					\
	zif-marshal.list					\
	zif.pc.in

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)


-include $(top_srcdir)/git.mk
