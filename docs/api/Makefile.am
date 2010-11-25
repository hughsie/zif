## Process this file with automake to produce Makefile.in

NULL =

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# The name of the module, e.g. 'glib'.
DOC_MODULE=zif

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
DOC_SOURCE_DIR=../../libzif

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-mktmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/libzif/zif-*.h
CFILE_GLOB=$(top_srcdir)/libzif/zif-*.c

# Header files to ignore when scanning.
IGNORE_HFILES =						\
	config.h					\
	zif-marshal.h					\
	$(NULL)

# Images to copy into HTML directory.
#HTML_IMAGES =						\
#	spec/pk-transactions-success.png		\
#	spec/pk-transactions-trusted.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =						\
	version.xml					\
	$(NULL)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
expand_content_files=

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
INCLUDES =						\
	$(GLIB_CFLAGS)					\
	-I$(top_builddir)/libzif			\
	-I$(top_srcdir)/libzif				\
	$(NULL)

ZIF_LIBS = $(top_builddir)/libzif/libzif.la

GTKDOC_LIBS =						\
	$(GLIB_LIBS)					\
	$(ZIF_LIBS)					\
	$(GPGME_LIBS)					\
	$(NULL)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
EXTRA_DIST += version.xml.in
