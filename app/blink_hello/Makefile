TARGET    := blink_hello
CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra
BOARD     ?= pocket
BOARD_DIR ?= blink_hello
CROSS     ?= arm-none-linux-gnueabihf-
CROSS_FLAGS := -O2 -Wall -march=armv7-a -mfpu=neon -mfloat-abi=hard
SOURCES   := $(TARGET).c Makefile README.md

RSYNC     := $(shell command -v rsync 2>/dev/null)
RSYNC_SSH ?= ssh
ifeq ($(RSYNC),)
COPY := scp
else
COPY := rsync -a -e "$(RSYNC_SSH)"
endif

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

cross:
	$(CROSS)gcc $(CROSS_FLAGS) -o $(TARGET) $(TARGET).c

push: cross
	-chmod 755 $(TARGET)
	$(COPY) $(TARGET) $(BOARD):$(BOARD_DIR)/

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	$(COPY) $(SOURCES) $(BOARD):$(BOARD_DIR)/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make"

clean:
	rm -f $(TARGET)

.PHONY: all cross push run deploy build-remote clean
