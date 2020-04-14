
# for sound, add the following libraries to the build.
# /usr/lib/x86_64-linux-gnu/libopenal.so /usr/lib/libalut.so
#
# like this:
#	g++ bump.cpp libggfonts.a -Wall -obump -lX11 -lGL -lGLU -lm -lrt \
#	/usr/lib/x86_64-linux-gnu/libopenal.so \
#	/usr/lib/libalut.so
#

all: bump lab2server lab2client

bump: bump.cpp fonts.h
	g++ bump.cpp libggfonts.a -Wall -obump -lX11 -lGL -lGLU -lm -lrt

lab2client: lab2client.cpp
	g++ lab2client.cpp -Wall -pthread -o lab2client

lab2server: lab2server.cpp
	g++ lab2server.cpp -Wall -o lab2server
clean:
	rm -f bump lab2client lab2server
	rm -f *.o
