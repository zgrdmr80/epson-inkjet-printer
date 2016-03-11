# Copyright (C) Seiko Epson Corporation 2009.
SUBDIRS = src

docdir = @prefix@/doc
doc_DATA = \
	AUTHORS \
	COPYING \
	COPYING.LIB \
	COPYING.EPSON \
	NEWS \
	README

ldsohooksdir = $(sysconfdir)/ld.so.conf.d
ldsohooks_DATA = ld.so.conf.d/99-epson-inkjet-printer.conf

EXTRA_DIST = $(doc_DATA) $(ldsohooks_DATA)
