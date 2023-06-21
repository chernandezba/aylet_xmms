CC= gcc
LD=ld
CFLAGS=-Wall -ggdb
LDFLAGS=-shared -lc

INCGLIB=`glib-config glib --cflags`
LIBGLIB=`glib-config glib --libs`

INCGTK=`gtk-config gtk --cflags`
LIBGTK=`gtk-config gtk --libs`

PROGRAMS=libaylet_xmms.so

all: $(PROGRAMS)


libaylet_xmms.so: libaylet_xmms.c
	$(CC) $(INCGLIB) $(INCGTK) $(CFLAGS) -o libaylet_xmms.so $(LDFLAGS) $(LIBGLIB) $(LIBGTK) libaylet_xmms.c

install: libaylet_xmms.so
	cp -f $(PROGRAMS) /usr/lib/xmms/Input

clean:
	rm -f $(PROGRAMS)
	rm -f core
	rm -f *.o
	rm -f *.so
