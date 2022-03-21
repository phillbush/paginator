# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib

# includes and libs
INCS += -I${LOCALINC} -I${X11INC}
LIBS += -L${LOCALLIB} -L${X11LIB} -lX11 -lXinerama -lXrender

# flags
CFLAGS += -g -O0 -Wall -Wextra ${INCS} ${CPPFLAGS}
LDFLAGS += ${LIBS}

# files
PROG = paginator
SRCS = ${PROG:=.c}
OBJS = ${SRCS:.c=.o}

all: ${PROG}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS}

${OBJS}: config.h

.c.o:
	${CC} ${CFLAGS} -c $<

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 644 ${PROG}.1 ${DESTDIR}${MANPREFIX}/man1/${PROG}.1

uninstall:
	rm -f ${DESTDIR}/${PREFIX}/bin/${PROG}
	rm -f ${DESTDIR}/${MANPREFIX}/man1/${PROG}.1

clean:
	-rm ${OBJS} ${PROG}

.PHONY: all install uninstall clean
