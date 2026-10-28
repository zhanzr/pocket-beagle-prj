TARGETS  := coremark-arm coremark-thumb
BOARD    ?= pocket
BOARD_DIR ?= coremark
CROSS    ?= arm-none-linux-gnueabihf-
ITERATIONS ?= 40000
SOURCES  := core_list_join.c core_main.c core_matrix.c core_state.c \
            core_util.c linux/core_portme.c linux/core_portme.h

FLAGS := -O3 -funroll-loops -flto \
         -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard \
         -I. -Ilinux \
         -DFLAGS_STR=\"\" \
         -DSEED_METHOD=SEED_VOLATILE -DMEM_METHOD=MEM_MALLOC \
         -DPERFORMANCE_RUN=1 -DITERATIONS=$(ITERATIONS)

RSYNC     := $(shell command -v rsync 2>/dev/null)
RSYNC_SSH ?= ssh
ifeq ($(RSYNC),)
COPY := scp
else
COPY := rsync -a -e "$(RSYNC_SSH)"
endif

all: $(TARGETS)

coremark-arm: $(SOURCES)
	$(CROSS)gcc $(FLAGS) -marm $(SOURCES) -o $@

coremark-thumb: $(SOURCES)
	$(CROSS)gcc $(FLAGS) -mthumb $(SOURCES) -o $@

push: all
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	$(COPY) $(TARGETS) $(BOARD):$(BOARD_DIR)/
	ssh $(BOARD) "chmod 755 $(BOARD_DIR)/$(TARGETS)"

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./coremark-arm && ./coremark-thumb"

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/linux"
	$(COPY) core_list_join.c core_main.c core_matrix.c core_state.c \
	        core_util.c coremark.h Makefile README.md $(BOARD):$(BOARD_DIR)/
	$(COPY) linux/core_portme.c linux/core_portme.h \
	        $(BOARD):$(BOARD_DIR)/linux/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make CROSS="

size: $(TARGETS)
	$(CROSS)size $(TARGETS)

clean:
	rm -f $(TARGETS)

.PHONY: all push run deploy build-remote size clean
