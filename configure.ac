AC_PREREQ(2.63)

m4_define([zif_major_version], [0])
m4_define([zif_minor_version], [2])
m4_define([zif_micro_version], [7])
m4_define([zif_version],
          [zif_major_version.zif_minor_version.zif_micro_version])

AC_INIT([zif],[0.2.7])
AC_CONFIG_SRCDIR(libzif)
AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_SYS_LARGEFILE

GOBJECT_INTROSPECTION_CHECK([0.9.8])

ZIF_MAJOR_VERSION=zif_major_version
ZIF_MINOR_VERSION=zif_minor_version
ZIF_MICRO_VERSION=zif_micro_version
ZIF_VERSION=zif_version
AC_SUBST(ZIF_MAJOR_VERSION)
AC_SUBST(ZIF_MINOR_VERSION)
AC_SUBST(ZIF_MICRO_VERSION)
AC_SUBST(ZIF_VERSION)

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# libtool versioning - this applies to libzif
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# increment;
# CURRENT	If the API or ABI interface has changed (reset REVISION to 0)
# REVISION	If the API and ABI remains the same, but bugs are fixed.
# AGE		If libzif can be linked into executables which can be
# 		built with previous versions of this library. Don't use.
LT_CURRENT=3
LT_REVISION=5
LT_AGE=0
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

AS_ALL_LINGUAS
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_SEARCH_LIBS([strerror],[cposix])
AC_HEADER_STDC
LT_INIT
AM_PROG_CC_C_O

# Internationalisation
IT_PROG_INTLTOOL([0.35.0])
GETTEXT_PACKAGE=Zif
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

# set up gtk-doc
GTK_DOC_CHECK(1.9)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl ---------------------------------------------------------------------------
dnl - Use strict options (default enabled for devs, disabled in releases)
dnl ---------------------------------------------------------------------------
dnl if .git directory is present, considering we are working in the repository
if test -d ".git"; then
	default_strict=yes
else
	default_strict=no
fi

dnl ---------------------------------------------------------------------------
dnl - Able to use gpgme functionality
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gpgme, AS_HELP_STRING([--enable-gpgme],[Build gpgme functionality]),
	      build_gpgme=$enableval,build_gpgme=yes)
if test x$build_gpgme = xyes; then
	AC_MSG_CHECKING([for gpgme])
	if ! test -x "/usr/bin/gpgme-config"; then
		AC_MSG_ERROR([Cannot locate gpgme])
	else
		AC_MSG_RESULT([yes])
		GPGME_CFLAGS="`\"/usr/bin/gpgme-config\" --cflags`"
		GPGME_LIBS="`\"/usr/bin/gpgme-config\" --libs`"
		AC_SUBST([GPGME_CFLAGS])
		AC_SUBST([GPGME_LIBS])
		AC_DEFINE(USE_GPGME, 1, [if we should use gpgme])
	fi
	build_gpgme=yes
else
	build_gpgme=no
fi

AC_ARG_ENABLE(strict, AS_HELP_STRING([--enable-strict],[Enable strict compilation options]), enable_strict=$enableval,
							enable_strict=$default_strict)
if test x$enable_strict != xno; then
	if test "$GCC" = "yes"; then
		WARNINGFLAGS_C="$WARNINGFLAGS_C -Werror"
	fi
fi

if test "$GCC" = "yes"; then
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wall"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-declarations"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wredundant-decls"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wpointer-arith"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wwrite-strings"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winit-self"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wreturn-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-nonliteral"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-security"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-include-dirs"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-format-attribute"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wsign-compare"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wtype-limits"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wuninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Waggregate-return"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wdeclaration-after-statement"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wshadow"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wno-strict-aliasing"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winline"
else
	WARNINGFLAGS_C=""
fi
AC_SUBST(WARNINGFLAGS_C)

dnl ---------------------------------------------------------------------------
dnl - Library dependencies
dnl ---------------------------------------------------------------------------
GLIB_REQUIRED=2.14.0
GIO_REQUIRED=2.16.1

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED gobject-2.0 gthread-2.0)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(RPM, rpm)
PKG_CHECK_MODULES(SOUP, libsoup-2.4)

dnl ---------------------------------------------------------------------------
dnl - new RPM?
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS("rpm/rpmprob.h",
		 HAVE_RPMPROB_H="yes",
		 HAVE_RPMPROB_H="no")
if test "x$HAVE_RPMPROB_H" = "xyes"; then
	AC_DEFINE(HAVE_RPMPROB_H,1,[have rpmprob.h])
fi

dnl ---------------------------------------------------------------------------
dnl - libarchive
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS(archive.h,
		 HAVE_ARCHIVE_H="yes",
		 HAVE_ARCHIVE_H="no")
if test "x$HAVE_ARCHIVE_H" = "xyes"; then
	ARCHIVE_LIBS=-larchive
	AC_SUBST(ARCHIVE_LIBS)
else
	AC_MSG_ERROR([Cannot find archive.h])
fi

dnl ---------------------------------------------------------------------------
dnl - liblzma
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS(lzma.h,
		 HAVE_LZMA_H="yes",
		 HAVE_LZMA_H="no")
if test "x$HAVE_LZMA_H" = "xyes"; then
	LZMA_LIBS=-llzma
	AC_SUBST(LZMA_LIBS)
else
	AC_MSG_ERROR([Cannot find lzma.h])
fi

dnl ---------------------------------------------------------------------------
dnl - libbzip2
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS(bzlib.h,
		 HAVE_BZLIB_H="yes",
		 HAVE_BZLIB_H="no")
if test "x$HAVE_BZLIB_H" = "xyes"; then
	BZLIB_LIBS=-lbz2
	AC_SUBST(BZLIB_LIBS)
else
	AC_MSG_ERROR([Cannot find bzlib.h])
fi

dnl ---------------------------------------------------------------------------
dnl - zlib
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS(zlib.h,
		 HAVE_ZLIB_H="yes",
		 HAVE_ZLIB_H="no")
if test "x$HAVE_BZLIB_H" = "xyes"; then
	ZLIB_LIBS=-lz
	AC_SUBST(ZLIB_LIBS)
else
	AC_MSG_ERROR([Cannot find zlib.h])
fi

dnl ---------------------------------------------------------------------------
dnl - Is docbook2man available?
dnl ---------------------------------------------------------------------------
AC_PATH_PROG(DOCBOOK2MAN, docbook2man, no)
if test "$DOCBOOK2MAN" = "no" ; then
	AC_MSG_WARN([docbook2man not found, will not be able to build man documentation])
fi
AM_CONDITIONAL(HAVE_DOCBOOK2MAN, [test "$DOCBOOK2MAN" != "no"])

dnl ---------------------------------------------------------------------------
dnl - Make paths available for source files
dnl ---------------------------------------------------------------------------
AC_SUBST(SYSCONFDIR, $sysconfdir)
AC_SUBST(LIBDIR, $libdir)
AC_SUBST(LIBEXECDIR, $libexecdir)
AC_SUBST(DATADIR, $datadir)
AC_SUBST(BINDIR, $bindir)
AC_SUBST(SBINDIR, $sbindir)
AC_SUBST(LOCALSTATEDIR, $localstatedir)

dnl ---------------------------------------------------------------------------
dnl - Build self tests
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(tests, AS_HELP_STRING([--enable-tests],[enable unit test code]),
	      enable_tests=$enableval,enable_tests=yes)
AM_CONDITIONAL(ZIF_BUILD_TESTS, test x$enable_tests = xyes)

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
po/Makefile.in
etc/Makefile
libzif/Makefile
libzif/zif.pc
libzif/zif-version.h
tools/Makefile
examples/Makefile
docs/Makefile
docs/api/Makefile
docs/api/version.xml
man/Makefile
data/Makefile
data/tests/Makefile
data/tests/transactions/Makefile
])
AC_OUTPUT

dnl ==========================================================================
echo "
                    ZIF $VERSION
                  =============

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        cppflags:                  ${CPPFLAGS}
        gobject-introspection:     ${found_introspection}
        Building gpgme support:    ${build_gpgme}
        Building unit tests:       ${enable_tests}
"

