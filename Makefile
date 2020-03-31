CFLAGS=-Wall -Wextra -Werror -std=c11 -g -I. -L.

run: demo
	./demo

demo: demo.c libpg3.a
	$(CC) $(CFLAGS) -odemo *.c -lSDL2 -lm -lpg3

libpg3.a:	pg.c pg.h
	$(CC) $(CFLAGS) -O2 -ffast-math -c pg.c
	ar crs libpg3.a pg.o

clean:
	rm *.o libpg3.a demo

install: lipg3.a
	install pg.h /usr/include
	install libpg3.a /usr/lib

uninstall:
	-rm /usr/include/pg.h
	-rm /usr/lib/libpg3.a
