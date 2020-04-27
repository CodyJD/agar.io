all: lab2client lab2server

lab2server: lab2server.cpp
	g++ lab2server.cpp -Wall -o lab2server

lab2client: lab2client.cpp
	g++ lab2client.cpp -Wall -pthread -o lab2client

clean:
	rm -f lab2client lab2server

