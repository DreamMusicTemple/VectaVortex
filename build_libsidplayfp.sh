#!/bin/sh
# Fetches and builds libsidplayfp 3.1.2 (GPL-2.0-or-later, release tarball so the MUS driver binaries are pre-generated) into Core/third_party/libsidplayfp-install.
set -e
cd "$(dirname "$0")/../third_party"
[ -d libsidplayfp-3.1.2 ] || { curl -L -o lsp.tgz https://github.com/libsidplayfp/libsidplayfp/releases/download/v3.1.2/libsidplayfp-3.1.2.tar.gz && mkdir libsidplayfp-3.1.2 && tar xzf lsp.tgz -C libsidplayfp-3.1.2 --strip-components=1; }
cd libsidplayfp-3.1.2 && ./configure --enable-static --disable-shared --prefix="$(cd .. && pwd)/libsidplayfp-install" && make -j4 && make install
