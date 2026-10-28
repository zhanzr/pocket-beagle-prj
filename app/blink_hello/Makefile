TARGET    := blink_hello
CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra
BOARD     ?= pocket
BOARD_DIR ?= blink_hello
CROSS     ?= arm-none-linux-gnueabihf-
CROSS_FLAGS := -O2 -Wall -march=armv7-a -mfpu=neon -mfloat-abi=hard

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

cross:
	$(CROSS)gcc $(CROSS_FLAGS) -o $(TARGET) $(TARGET).c

push: cross
	scp $(TARGET) $(BOARD):$(BOARD_DIR)/

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp $(TARGET).c Makefile README.md $(BOARD):$(BOARD_DIR)/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make"

clean:
	rm -f $(TARGET)

.PHONY: all cross push run deploy build-remote clean
