CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11

BIN_CLIENT = meu_navegador
BIN_SERVER = meu_servidor

all: $(BIN_CLIENT) $(BIN_SERVER)

$(BIN_CLIENT): client.c
	$(CC) $(CFLAGS) -o $(BIN_CLIENT) client.c

$(BIN_SERVER): server.c
	$(CC) $(CFLAGS) -o $(BIN_SERVER) server.c

clean:
	rm -f $(BIN_CLIENT) $(BIN_SERVER)

.PHONY: all clean
