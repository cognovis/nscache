# $Header$

ifdef INST
  NSHOME ?= $(INST)
else
  ifdef NSBUILD
    NSHOME=..
  else
    NSHOME=/usr/local/aolserver
    ifneq ( $(shell [ -f $(NSHOME)/include/Makefile.module ] && echo ok), ok)
      NSHOME = ../aolserver
    endif
  endif
endif

# Version number to use in release tags.
VER_ = $(subst .,_,$(VER))

#
# Module name
#
MOD      =  nscache.so

#
# Objects to build
#
OBJS     =  tclcache.o

#
# Header files in THIS directory (included with your module)
#
HDRS     =  

#
# Extra libraries required by your module (-L and -l go here)
#
MODLIBS  =  

#
# Compiler flags required by your module (-I for external headers goes here)
#
CFLAGS   =  

include  $(NSHOME)/include/Makefile.module

.PHONY: test dist

test:
	cd test && $(INST)/bin/nsd8x -ft nsd.tcl

release:
	@if [ "$$VER" = "" ]; then echo 1>&2 "VER must be set to version number!"; exit 1; fi
	cvs rtag -r v$(VER_) nscache

force-release:
	@if [ "$$VER" = "" ]; then echo 1>&2 "VER must be set to version number!"; exit 1; fi
	cvs rtag -F v$(VER_) nscache

dist:
	@if [ "$$VER" = "" ]; then echo 1>&2 "VER must be set to version number!"; exit 1; fi
	rm -rf work
	mkdir work
	cd work && cvs co -r v$(VER_) nscache
	perl -pi -e 's/\@VER\@/$(VER)/g' work/nscache/index.html work/nscache/tclcache.c
	mv work/nscache work/nscache-$(VER)
	( cd work && tar cvf - nscache-$(VER) ) | gzip -9 > nscache-$(VER).tar.gz
	rm -rf work

