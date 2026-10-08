#!/bin/sh
# Same as `make`, for machines without make. Usage: XPSDK=/path/to/SDK ./build.sh
set -e
cd "$(dirname "$0")"
XPSDK=${XPSDK:-SDK}
mkdir -p build/HoneycombBravoLights/lin_x64
gcc -O2 -Wall -Wextra -std=gnu11 -fPIC -fvisibility=hidden \
    -DLIN=1 -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=1 \
    -I"$XPSDK/CHeaders/XPLM" src/HoneycombBravoLights.c -shared \
    -Wl,-soname,HoneycombBravoLights.xpl \
    -o build/HoneycombBravoLights/lin_x64/HoneycombBravoLights.xpl
echo "built build/HoneycombBravoLights/lin_x64/HoneycombBravoLights.xpl"
