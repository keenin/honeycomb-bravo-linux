# Build the HoneycombBravoLights X-Plane plugin (Linux, lin_x64).
# Needs the X-Plane SDK headers: make XPSDK=/path/to/SDK
XPSDK   ?= SDK
CC      ?= gcc
CFLAGS  ?= -O2 -g0 -Wall -Wextra
CFLAGS  += -std=gnu11 -fPIC -fvisibility=hidden \
           -DLIN=1 -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=1 \
           -I$(XPSDK)/CHeaders/XPLM
LDFLAGS += -shared -Wl,-soname,HoneycombBravoLights.xpl

OUT = build/HoneycombBravoLights/lin_x64/HoneycombBravoLights.xpl

all: $(OUT)

$(OUT): src/HoneycombBravoLights.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

PLUGDIR ?= $(HOME)/X-Plane 12/Resources/plugins/HoneycombBravoLights
install: $(OUT)
	mkdir -p "$(PLUGDIR)/lin_x64"
	install -m 755 $(OUT) "$(PLUGDIR)/lin_x64/"

clean:
	rm -rf build

.PHONY: all install clean
