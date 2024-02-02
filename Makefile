# Compiler Variables
CC=gcc
FLAGS=-Wall
INC=-Isrc/ -Isrc/utils/
LIB_FLAGS=-lpthread -lm
DIR_GUARD=@mkdir -p $(@D)

# Build Configurations
CFG=release
ifeq ($(CFG), debug)
	FLAGS += -g -DDEBUG -DWNO_ERROR -O0
endif
ifneq ($(CFG),debug)
ifneq ($(CFG),release)
	@echo "Invalid configuration " $(CFG) "."
	@echo "Choices are 'release', 'debug'."
	@exit 1
endif
endif

# Sources
SOURCES=src/utils/functions.c src/message.c src/logging.c src/transmitmsg.c src/connection.c src/ftransfer/transmitfile.c src/client.c src/server.c src/main.c

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
	$(CC) $(FLAGS) $(SOURCES) -o bin/$(CFG)/chime $(LIB_FLAGS) $(INC)
