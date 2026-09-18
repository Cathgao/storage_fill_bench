CC ?= clang
CFLAGS ?= -O3 -Wall -Wextra -march=armv8-a
TARGET = fast_fill_bench
SRC = fast_fill_bench.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

static: $(SRC)
	$(CC) $(CFLAGS) -static -s $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET) test.tmp fill_bench.csv

.PHONY: all static clean
