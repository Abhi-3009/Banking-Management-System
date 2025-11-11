CC = gcc
CFLAGS = -Wall -Iinclude -std=c11

SRC = src
OBJ = obj

OBJS = $(OBJ)/admin.o \
       $(OBJ)/customer.o \
       $(OBJ)/employee.o \
       $(OBJ)/loan.o \
       $(OBJ)/manager.o \
       $(OBJ)/utils.o \
       $(OBJ)/locks.o \
       $(OBJ)/sessions.o

all: server client

server: server.c $(OBJS)
	$(CC) $(CFLAGS) -o server server.c $(OBJS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c

$(OBJ)/%.o: $(SRC)/%.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)/*.o server client