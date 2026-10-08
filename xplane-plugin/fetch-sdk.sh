#!/bin/sh
# Download the official X-Plane SDK into ./SDK.
# The SDK is not part of this repository (see .gitignore).
#
# Usage:
#   ./fetch-sdk.sh         # SDK 4.1.1, the release this plugin was written against
#   ./fetch-sdk.sh 430     # another release, e.g. 4.3.0
#
# Zips are published at:
#   https://developer.x-plane.com/sdk/plugin-sdk-downloads/
set -eu
cd "$(dirname "$0")"

if [ -f SDK/CHeaders/XPLM/XPLMPlugin.h ]; then
	echo "SDK already present at $(pwd)/SDK"
	exit 0
fi

ver=${1:-411}
case "$ver" in
	4.*) ver=$(printf '%s' "$ver" | tr -d '.') ;;
esac
case "$ver" in
	''|*[!0-9]*)
		echo "usage: $0 [version]   e.g. 411 or 430" >&2
		exit 1
		;;
esac

url="https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK${ver}.zip"
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

echo "Downloading $url"
curl -fL --retry 3 -o "$tmp" "$url"
rm -rf SDK
unzip -q "$tmp" -d .
test -f SDK/CHeaders/XPLM/XPLMPlugin.h
echo "Installed X-Plane SDK ${ver} into $(pwd)/SDK"
