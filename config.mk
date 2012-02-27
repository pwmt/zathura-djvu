# See LICENSE file for license and copyright information

VERSION = 0.0.1

# paths
PREFIX ?= /usr

# libs
GLIB_INC ?= $(shell pkg-config --cflags glib-2.0)
GLIB_LIB ?= $(shell pkg-config --libs glib-2.0)

DJVU_INC ?= $(shell pkg-config --cflags ddjvuapi)
DJVU_LIB ?= $(shell pkg-config --libs ddjvuapi)

GIRARA_INC ?= $(shell pkg-config --cflags girara-gtk2)
GIRARA_LIB ?= $(shell pkg-config --libs girara-gtk2)

ZATHURA_INC ?= $(shell pkg-config --cflags zathura)
PLUGINDIR ?= $(shell pkg-config --variable=plugindir zathura)
PLUGINDIR ?= ${PREFIX}/lib/zathura

INCS = ${GLIB_INC} ${DJVU_INC} ${ZATHURA_INC} ${GIRARA_INC}
LIBS = ${GIRARA_LIB} ${GLIB_LIB} ${DJVU_LIB}

# flags
CFLAGS += -std=c99 -fPIC -pedantic -Wall -Wno-format-zero-length $(INCS)

# debug
DFLAGS ?= -g

# build with cairo support?
WITH_CAIRO ?= 1

# compiler
CC ?= gcc
LD ?= ld

# set to something != 0 if you want verbose build output
VERBOSE ?= 0
