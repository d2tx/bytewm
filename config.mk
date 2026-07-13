# bytewm build config
VERSION = 0.1

X11LIB = /usr/X11R6/lib
X11INC = /usr/X11R6/include

X11LIBS = -lX11
X11INCS = -I$(X11INC)

CFLAGS = -std=c99 -pedantic -Wall -g -O0 \
	$(X11INCS) \
	-DVERSION=\"$(VERSION)\"
LDFLAGS = $(X11LIBS) -rdynamic

CC = cc
PREFIX = /usr/local
