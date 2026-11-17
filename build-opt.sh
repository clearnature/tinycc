#!/bin/sh
# Build this TinyCC tree and install it into /opt/tinycc.
#
#   ./build-opt.sh                 # configure (if needed) + build
#   ./build-opt.sh --test          # ... + run the test suites
#   ./build-opt.sh --test --install  # ... + install into the prefix
#   ./build-opt.sh --clean         # remove build products
#
# The install prefix lives in the generated config.mak.  If config.mak is
# missing or points elsewhere, this script re-runs configure with
# --prefix=/opt/tinycc, because install paths (bindir, libdir, tccdir) are
# baked in there and cannot be changed from config-extra.mak.

set -e

TOP=$(cd "$(dirname "$0")" && pwd)
PREFIX=/opt/tinycc
CC=${CC:-gcc}
JOBS=$(nproc 2>/dev/null || echo 4)
DO_TEST=no
DO_INSTALL=no

while test $# -gt 0; do
    case $1 in
    --test)    DO_TEST=yes ;;
    --install) DO_INSTALL=yes ;;
    --clean)   cd "$TOP" && make distclean; exit 0 ;;
    --jobs=*)  JOBS=${1#--jobs=} ;;
    --prefix=*) PREFIX=${1#--prefix=} ;;
    *) echo "usage: $0 [--test] [--install] [--clean] [--jobs=N] [--prefix=DIR]" >&2
       exit 2 ;;
    esac
    shift
done

cd "$TOP"

if ! grep -qxF "prefix=$PREFIX" config.mak 2>/dev/null; then
    echo "== configure --prefix=$PREFIX"
    ./configure --prefix="$PREFIX" --cc="$CC"
fi

echo "== make -j$JOBS"
make -j"$JOBS"

if test "$DO_TEST" = yes; then
    echo "== make test"
    make test
fi

if test "$DO_INSTALL" = yes; then
    if test "$(id -u)" = 0; then
        echo "== make install"
        make install
    else
        echo "== make install (root via sudo/pkexec)"
        if command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
            sudo make -C "$TOP" install
        else
            pkexec /usr/bin/make -C "$TOP" install
        fi
    fi
    echo "== $PREFIX/bin/tcc -v"
    "$PREFIX/bin/tcc" -v
fi
