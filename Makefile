SHELL := /bin/bash
PYTHON := $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)
APP_ID := com.immichtv.viewer
SDK ?= $(HOME)/webos-ndk/arm-webos-linux-gnueabi_sdk-buildroot
SYSROOT := $(SDK)/arm-webos-linux-gnueabi/sysroot
ARM_CC := $(SDK)/bin/arm-webos-linux-gnueabi-gcc
COMMON := -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -Isrc -Ivendor
CORE_SOURCES := src/common.c src/read-policy.c src/config.c src/catalog.c src/navigation.c vendor/cJSON.c
SOURCES := $(CORE_SOURCES) src/api.c src/worker.c src/proxy.c src/demux.c src/media.c src/media-webos.c src/vector.c src/ui-paint.c $(wildcard src/ui/*.c src/app/*.c) src/main.c
HEADERS := $(wildcard src/*.h src/ui/*.h src/app/*.h)
HOST_FLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image libavformat libavcodec libavutil)
HOST_LIBS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image libavformat libavcodec libavutil) -lpthread -lm
ARM_FLAGS := --sysroot=$(SYSROOT) -I$(SYSROOT)/usr/include/SDL2 -Ivendor/ffmpeg -DWEBOS -tno-glibc-polyfill
ARM_LIBS := -L$(SYSROOT)/usr/lib -Lassets/lib -Wl,-rpath-link,$(SYSROOT)/usr/lib -Wl,--export-dynamic -Wl,--hash-style=both -Wl,--disable-new-dtags,-rpath,'$$ORIGIN/assets/lib' -lSDL2 -lSDL2_ttf -lSDL2_image -lplayerAPIs -lpf-1.0 -l:libavformat-plx.so.63 -l:libavcodec-plx.so.63 -l:libavutil-plx.so.61 -lstdc++ -lpthread -ldl -lm
QA ?= 0
SUFFIX := $(if $(filter 1,$(QA)),-qa,)
HOST_BIN := build/immich-lg-tv$(SUFFIX)
ARM_BIN := build/immich-lg-tv-arm$(SUFFIX)
ifeq ($(QA),1)
COMMON += -DENABLE_QA
endif

.PHONY: all host tv ipk test demo fixtures clean
all: host
build:
	mkdir -p build
host: $(HOST_BIN)
$(HOST_BIN): $(SOURCES) $(HEADERS) Makefile | build
	$(CC) $(COMMON) $(HOST_FLAGS) $(SOURCES) -o $@ $(HOST_LIBS)
tv: $(ARM_BIN)
$(ARM_BIN): $(SOURCES) $(HEADERS) vendor/starfish.c vendor/starfish.h Makefile | build
	$(ARM_CC) $(COMMON) $(ARM_FLAGS) $(SOURCES) vendor/starfish.c -o $@ $(ARM_LIBS)
	$(SDK)/bin/arm-webos-linux-gnueabi-strip $@
ipk: tv
	$(PYTHON) tools/fetch-sources.py
	$(PYTHON) tools/package.py $(ARM_BIN)
build/test-core: tests/test_core.c $(CORE_SOURCES) src/api.c src/proxy.c $(HEADERS) | build
	$(CC) $(COMMON) tests/test_core.c $(CORE_SOURCES) src/api.c src/proxy.c -o $@ -lpthread -lm
build/test-demux: tests/test_demux.c $(CORE_SOURCES) src/api.c src/demux.c $(HEADERS) | build
	$(CC) $(COMMON) $(shell pkg-config --cflags libavformat libavcodec libavutil) tests/test_demux.c $(CORE_SOURCES) src/api.c src/demux.c -o $@ $(shell pkg-config --libs libavformat libavcodec libavutil) -lpthread -lm
test: build/test-core build/test-demux
	$(MAKE) QA=1 host
	build/test-core
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
fixtures:
	$(PYTHON) tools/prepare_fixtures.py $(FIXTURE_ARGS)
demo:
	$(PYTHON) tools/mock_immich.py --host 0.0.0.0 --port 22830
clean:
	rm -f build/immich-lg-tv build/immich-lg-tv-arm build/immich-lg-tv-qa build/immich-lg-tv-arm-qa build/test-core
