# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib

# includes and libs
INCS += -I${LOCALINC} -I${X11INC}
LIBS += -L${LOCALLIB} -L${X11LIB} -lX11 -lXinerama -lXrender -lXpm

# files
PROGS = paginator taskinator
OBJS = paginator.o taskinator.o x.o
SRCS = paginator.o taskinator.o x.o x.h

all: ${PROGS}

paginator: paginator.o x.o
	${CC} -o $@ paginator.o x.o ${LIBS} ${LDFLAGS}

taskinator: taskinator.o x.o
	${CC} -o $@ taskinator.o x.o ${LIBS} ${LDFLAGS}

paginator.o: x.o
taskinator.o: x.o

.c.o:
	${CC} ${INCS} ${CFLAGS} ${CPPFLAGS} -c $<

clean:
	-rm -f ${OBJS} ${PROGS} ${PROGS:=.core}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 755 paginator ${DESTDIR}${PREFIX}/bin/paginator
	install -m 755 taskinator ${DESTDIR}${PREFIX}/bin/taskinator
	install -m 644 paginator.1 ${DESTDIR}${MANPREFIX}/man1/paginator.1
	install -m 644 taskinator.1 ${DESTDIR}${MANPREFIX}/man1/taskinator.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/paginator
	rm -f ${DESTDIR}${PREFIX}/bin/taskinator
	rm -f ${DESTDIR}${MANPREFIX}/man1/paginator.1
	rm -f ${DESTDIR}${MANPREFIX}/man1/taskinator.1

.PHONY: all install uninstall clean
