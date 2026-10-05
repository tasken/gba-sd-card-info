SHELL := /bin/sh
.DELETE_ON_ERROR:

CROSS ?= $(if $(DEVKITARM),$(DEVKITARM)/bin/arm-none-eabi-,arm-none-eabi-)
GBAFIX ?= $(if $(DEVKITPRO),$(DEVKITPRO)/tools/bin/gbafix,gbafix)
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
HOST_CC ?= cc
PYTHON ?= python3
BUILD_COMMIT ?= $(shell if revision=$$(git rev-parse --short=8 HEAD 2>/dev/null); then \
	printf '%s' "$$revision"; git diff --quiet HEAD -- || printf '%s' '-dirty'; \
	fi)
BUILD_TAG ?= $(shell git describe --tags --exact-match HEAD 2>/dev/null)
export BUILD_COMMIT BUILD_TAG

CFLAGS := -mcpu=arm7tdmi -mthumb -mthumb-interwork -Os \
	-std=c11 -ffreestanding -fno-builtin -fno-common \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-Wall -Wextra -Werror -MMD -MP -Isrc
ASFLAGS := -mcpu=arm7tdmi -mthumb-interwork
LDFLAGS := -mcpu=arm7tdmi -mthumb -mthumb-interwork -nostdlib \
	-Wl,-T,gba.ld,-Map,build/gba-sd-card-info.map,--orphan-handling=error,-z,noexecstack
SOURCES := main platform registers names probe data storage report_format report_save ui font runtime
OBJECTS := $(addprefix build/,$(addsuffix .o,$(SOURCES))) \
	build/startup.o build/supercard_io.o build/ff.o \
	build/sfw_driver.o build/sfw_io.o build/sfw_crc.o build/sfw_backend.o
SFW_DEFINES := -DSUPERCHIS_IO -D__GBA__ \
	-Dsend_empty_clocks=sfw_send_empty_clocks \
	-Dwait_sdcard_idle=sfw_wait_sdcard_idle \
	-Dreceive_sdcard_response=sfw_receive_sdcard_response \
	-Dsend_sdcard_commandbuf=sfw_send_sdcard_commandbuf
SFW_CFLAGS := $(filter-out -std=c11,$(CFLAGS)) -std=gnu11 \
	-Wno-old-style-declaration
HOST_FLAGS := -std=c11 -Wall -Wextra -Werror -Isrc -g

.PHONY: all test check clean FORCE
all: build/gba-sd-card-info.gba

build:
	mkdir -p build

build/build_info.h: FORCE tools/build_info.py | build
	$(PYTHON) -I tools/build_info.py

build/report_format.o build/ui.o: build/build_info.h
build/report_format.o build/ui.o: CFLAGS += -include build/build_info.h

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

build/ff.o: src/fatfs/ff.c src/fatfs/ff.h src/fatfs/ffconf.h src/fatfs/diskio.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sfw_driver.o: src/superfw/supercard_driver.c | build
	$(CC) $(SFW_CFLAGS) $(SFW_DEFINES) -c $< -o $@

build/sfw_crc.o: src/superfw/crc.c | build
	$(CC) $(SFW_CFLAGS) $(SFW_DEFINES) -c $< -o $@

build/sfw_io.o: src/superfw/supercard_io.S | build
	$(CC) $(ASFLAGS) $(SFW_DEFINES) -c $< -o $@

build/sfw_backend.o: src/sfw_backend.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/gba-sd-card-info.elf: $(OBJECTS) gba.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -lgcc -o $@

build/gba-sd-card-info.gba: build/gba-sd-card-info.elf tools/check_rom.py
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -tGBASDCARDINF -cSCIE -m00 -r0
	$(PYTHON) tools/check_rom.py $@ $<

build/test_sd: tests/test_sd.c src/registers.c src/sd.h | build
	$(HOST_CC) $(HOST_FLAGS) tests/test_sd.c src/registers.c -o $@

build/test_probe: tests/test_probe.c src/probe.c src/data.c src/registers.c src/names.c src/sd.h src/transport.h | build
	$(HOST_CC) $(HOST_FLAGS) tests/test_probe.c src/probe.c src/data.c src/registers.c src/names.c -o $@

build/test_report: tests/test_report.c src/report_format.c src/report_save.c src/registers.c src/names.c src/fatfs/ff.c src/fatfs/ff.h src/fatfs/ffconf.h src/report.h src/sd.h | build
	$(HOST_CC) $(HOST_FLAGS) tests/test_report.c src/report_format.c src/report_save.c src/registers.c src/names.c src/fatfs/ff.c -o $@

build/test_data: tests/test_data.c src/data.c src/registers.c src/sd.h src/transport.h | build
	$(HOST_CC) $(HOST_FLAGS) tests/test_data.c src/data.c src/registers.c -o $@

build/test_storage: tests/test_storage.c src/storage.c src/registers.c src/report.h src/sd.h src/sfw_backend.h | build
	$(HOST_CC) $(HOST_FLAGS) tests/test_storage.c src/storage.c src/registers.c -o $@

test: build/test_sd build/test_probe build/test_report build/test_data build/test_storage
	./build/test_sd
	./build/test_probe
	./build/test_report
	./build/test_data
	./build/test_storage
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

check: test all

clean:
	$(PYTHON) tools/clean.py

-include $(OBJECTS:.o=.d)
