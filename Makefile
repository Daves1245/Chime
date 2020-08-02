# Compiler Variables
CC=gcc
FLAGS=-Wall
INC=-Isrc/
LIB_FLAGS=-lpthread -lm
DIR_GUARD=@mkdir -p $(@D)

# Build Configurations
CFG=release
ifeq ($(CFG), debug)
	FLAGS += -g -DDEBUG
endif
ifneq ($(CFG),debug)
ifneq ($(CFG),release)
	@echo "Invalid configuration " $(CFG) "."
	@echo "Choices are 'release', 'debug'."
	@exit 1
endif
endif

# Sources
CLIENT_SOURCES=src/message.c src/client.c
SERVER_SOURCES=src/message.c src/server.c

# Main targets
.PHONY: all clean

all: bin/$(CFG)/chime 

clean:
	rm -rf bin/$(CFG)/*

clean_all:
	rm -rf bin/*

# Compile
bin/$(CFG)/chime:
	$(DIR_GUARD)
	$(CC) $(FLAGS) $(CLIENT_SOURCES) -o bin/$(CFG)/client $(LIB_FLAGS)
	$(CC) $(FLAGS) $(SERVER_SOURCES) -o bin/$(CFG)/server $(LIB_FLAGS)
