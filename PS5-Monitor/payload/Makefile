# PS5 Monitor - payload Makefile
# Product UI: Developed by Sparrow · Powered by AI
# Third-party source provenance is documented separately in THIRD_PARTY_NOTICES.md.

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
PS5_IP          ?= 192.168.0.160

CC     := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
DEPLOY := $(PS5_PAYLOAD_SDK)/bin/prospero-deploy
STRIP  := $(PS5_PAYLOAD_SDK)/bin/llvm-strip

ELF := ps5-monitor.elf
SOURCES := main.c monitor_web.c monitor_perf.c web_assets.c
HBROOT := $(PS5_PAYLOAD_SDK)/target/user/homebrew
CFLAGS := -Wall -O3 -pthread -I$(HBROOT)/include
LDFLAGS := -L$(HBROOT)/lib

all: $(ELF)

web_assets.c web_assets.h: web/index.html web/app.css web/favicon.png tools/embed_web.py
	python3 tools/embed_web.py

$(ELF): $(SOURCES) web_assets.h monitor_web.h monitor_perf.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS) -lkernel_sys -lSceSystemService -lSceUserService -lSceAppInstUtil -lSceNetCtl -lScePad -lsqlite3
	$(STRIP) $@

deploy: $(ELF)
	$(DEPLOY) http://$(PS5_IP):9021 $(ELF)

clean:
	rm -f $(ELF) web_assets.c web_assets.h

.PHONY: all clean deploy
