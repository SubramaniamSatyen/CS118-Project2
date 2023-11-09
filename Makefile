CC=g++
CFLAGS=-Wall -Wextra -Werror

all: clean build

default: build

build: server.cpp client.cpp
	g++ -Wall -Wextra -o server server.cpp
	g++ -Wall -Wextra -o client client.cpp

clean:
	rm -f server client output.txt

zip: clean
	zip ${USERID}.zip server.cpp client.cpp Makefile