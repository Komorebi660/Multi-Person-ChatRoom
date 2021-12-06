CC=g++

C_FLAGS = -pthread -Wall

make: server client

server:
	$(CC) $(C_FLAGS) Server.cpp -o server.out
client:
	$(CC) $(C_FLAGS) Client.cpp -o client.out

cl:
	rm *.out