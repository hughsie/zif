EXTRA_DIST =						\
	zif.sgml

if HAVE_DOCBOOK2MAN
man_MANS =						\
	zif.1
endif

if HAVE_DOCBOOK2MAN
zif.1: zif.sgml
	docbook2man $? > /dev/null
endif

clean-local :
	rm -f *~
	rm -f *.1
	rm -f manpage.*

