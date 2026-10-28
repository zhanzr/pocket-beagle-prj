TARGET    := ssd1317_demo
CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra
BOARD     ?= pocket
BOARD_DIR ?= ssd1317_md066_64x48_iic
CROSS     ?= arm-none-linux-gnueabihf-
SOURCES   := main.c oled.c oledfont.c oled.h Makefile README.md

RSYNC     := $(shell command -v rsync 2>/dev/null)
RSYNC_SSH ?= ssh
ifeq ($(RSYNC),)
COPY := scp
else
COPY := rsync -a -e "$(RSYNC_SSH)"
endif

all: $(TARGET)

$(TARGET): main.c oled.c oledfont.c oled.h
	$(CC) $(CFLAGS) -o $@ main.c oled.c oledfont.c

cross:
	$(CROSS)gcc $(CFLAGS) -o $(TARGET) main.c oled.c oledfont.c

push: cross
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	$(COPY) $(TARGET) $(BOARD):$(BOARD_DIR)/
	ssh $(BOARD) "chmod 755 $(BOARD_DIR)/$(TARGET)"

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	$(COPY) $(SOURCES) $(BOARD):$(BOARD_DIR)/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make"

size: $(TARGET)
	$(CROSS)size $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all cross push run deploy build-remote size clean
