CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -g -Iinclude
LDFLAGS = -pthread -lrt
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)
BIN     = campuscompute

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

src/%.o: src/%.c include/common.h
	$(CC) $(CFLAGS) -c $< -o $@

# Thread-sanitizer build of the logger (optional check)
tsan:
	$(CC) $(CFLAGS) -fsanitize=thread $(SRC) -o $(BIN)_tsan $(LDFLAGS)

test: all
	./tests/run_tests.sh

clean:
	rm -f $(OBJ) $(BIN) $(BIN)_tsan logs/campus.log

.PHONY: all clean test tsan
