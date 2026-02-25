# Makefile for Group Chat Application

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -pthread

# Target executables
SERVER = server
CLIENT = client

# Source files
SERVER_SRC = GroupChatServer.c
CLIENT_SRC = GroupChatClient.c

# Default port for testing
PORT = 8080

# Default target: build both server and client
all: $(SERVER) $(CLIENT)

# Build server
$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC) $(LDFLAGS)

# Build client
$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC) $(LDFLAGS)

# Run server (use: make run-server PORT=8080)
run-server: $(SERVER)
	./$(SERVER) $(PORT)

# Run client (use: make run-client PORT=8080)
run-client: $(CLIENT)
	./$(CLIENT) localhost $(PORT)

# Clean build artifacts
clean:
	rm -f $(SERVER) $(CLIENT) *.o
	rm -rf *.dSYM

# Test: compile and check for errors (doesn't run)
test: all
	@echo "✓ Server and client compiled successfully!"
	@echo "To test:"
	@echo "  Terminal 1: make run-server"
	@echo "  Terminal 2: make run-client"
	@echo "  Terminal 3: make run-client"

# Help message
help:
	@echo "Available targets:"
	@echo "  make              - Build both server and client"
	@echo "  make server       - Build server only"
	@echo "  make client       - Build client only"
	@echo "  make run-server   - Build and run server on port $(PORT)"
	@echo "  make run-client   - Build and run client connecting to localhost:$(PORT)"
	@echo "  make clean        - Remove compiled binaries"
	@echo "  make test         - Compile and show test instructions"
	@echo ""
	@echo "Examples:"
	@echo "  make run-server PORT=9000"
	@echo "  make run-client PORT=9000"

.PHONY: all clean run-server run-client test help
