TARGET  := blink_hello
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
BOARD   ?= pocket
BOARD_DIR ?= blink_hello
CROSS   ?= D:/arm-none-linux-gnu-15.3.rel2/bin/arm-none-linux-gnueabihf-
CROSS_FLAGS := -O2 -Wall -march=armv7-a -mfpu=neon -mfloat-abi=hard

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

cross:
	$(CROSS)gcc $(CROSS_FLAGS) -o $(TARGET) $(TARGET).c

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp $(TARGET).c Makefile README.md $(BOARD):$(BOARD_DIR)/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make"

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all cross deploy build-remote run clean
