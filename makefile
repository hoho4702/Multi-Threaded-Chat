CC = g++
all:
	$(CC) -o server server.cc -lpthread
	$(CC) -o client client.cc -lpthread

clean:
	rm -f server client *.o