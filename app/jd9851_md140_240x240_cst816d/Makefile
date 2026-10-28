TARGET    := jd9851_demo
CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra
BOARD     ?= pocket
BOARD_DIR ?= jd9851_md140_240x240_cst816d
CROSS     ?= arm-none-linux-gnueabihf-
SOURCES   := main.c lcd.c touch.c lcd_fonts.c lcd_font_1608.c lcd.h \
             lcd_fonts.h lcd_font_1608.h touch.h Makefile README.md

RSYNC     := $(shell command -v rsync 2>/dev/null)
RSYNC_SSH ?= ssh
ifeq ($(RSYNC),)
COPY := scp
else
COPY := rsync -a -e "$(RSYNC_SSH)"
endif

all: $(TARGET)

$(TARGET): main.c lcd.c touch.c lcd_fonts.c lcd_font_1608.c lcd.h \
           lcd_fonts.h lcd_font_1608.h touch.h
	$(CC) $(CFLAGS) -o $@ main.c lcd.c touch.c lcd_fonts.c lcd_font_1608.c

cross:
	$(CROSS)gcc $(CFLAGS) -o $(TARGET) main.c lcd.c touch.c lcd_fonts.c \
	        lcd_font_1608.c

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
