
# for sound, add the following libraries to the build.
# /usr/lib/x86_64-linux-gnu/libopenal.so /usr/lib/libalut.so
#
# like this:
#	g++ bump.cpp libggfonts.a -Wall -obump -lX11 -lGL -lGLU -lm -lrt \
#	/usr/lib/x86_64-linux-gnu/libopenal.so \
#	/usr/lib/libalut.so
#

all: bump

bump: bump.cpp fonts.h
	g++ bump.cpp libggfonts.a -Wall -obump -lX11 -lGL -lGLU -lm -lrt

clean:
	rm -f bump
	rm -f *.o

