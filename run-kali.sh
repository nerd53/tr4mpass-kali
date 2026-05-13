#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DEPS=1
RUN_TESTS=1
RUN_APP=1
APP_ARGS=()

usage() {
    cat <<'USAGE'
Usage: ./run-kali.sh [options] [-- tr4mpass args...]

Options:
  --no-install    Skip apt dependency installation
  --no-tests      Skip make test
  --build-only    Install dependencies and build, but do not run tr4mpass
  -h, --help      Show this help

Examples:
  ./run-kali.sh -- -v
  ./run-kali.sh --no-tests -- --force-path-a -v
USAGE
}

while (($#)); do
    case "$1" in
        --no-install)
            INSTALL_DEPS=0
            shift
            ;;
        --no-tests)
            RUN_TESTS=0
            shift
            ;;
        --build-only)
            RUN_APP=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            APP_ARGS=("$@")
            break
            ;;
        *)
            APP_ARGS+=("$1")
            shift
            ;;
    esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "[!] This script is intended for Kali/Linux." >&2
fi

if (( INSTALL_DEPS )); then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "[!] apt-get not found. Re-run with --no-install and install deps manually." >&2
        exit 1
    fi

    SUDO=()
    if [[ "${EUID}" -ne 0 ]]; then
        SUDO=(sudo)
    fi

    "${SUDO[@]}" apt-get update
    "${SUDO[@]}" apt-get install -y \
        build-essential \
        clang \
        git \
        make \
        pkg-config \
        usbmuxd \
        libcurl4-openssl-dev \
        libimobiledevice-dev \
        libirecovery-dev \
        libplist-dev \
        libssh2-1-dev \
        libssl-dev \
        libusb-1.0-0-dev
fi

cd "$ROOT_DIR"

make

if (( RUN_TESTS )); then
    make test
fi

if (( RUN_APP )); then
    if [[ "${EUID}" -eq 0 ]]; then
        exec ./tr4mpass "${APP_ARGS[@]}"
    else
        exec sudo -E ./tr4mpass "${APP_ARGS[@]}"
    fi
fi
