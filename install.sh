#!/bin/bash
set -euo pipefail

# das-debounce installer
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/DigitalCyberSoft/das-debounce/main/install.sh | sudo bash
#   curl -fsSL ... | sudo bash -s -- --uninstall

REPO="https://raw.githubusercontent.com/DigitalCyberSoft/das-debounce/main"
BINDIR="/usr/local/bin"
UNITDIR="/usr/lib/systemd/system"
UDEVDIR="/usr/lib/udev/rules.d"

# --- helpers ---

die() { echo "error: $*" >&2; exit 1; }

check_root() {
    [ "$(id -u)" -eq 0 ] || die "must be run as root (use sudo)"
}

detect_pkg_manager() {
    if command -v dnf >/dev/null 2>&1; then
        echo "dnf"
    elif command -v apt-get >/dev/null 2>&1; then
        echo "apt"
    elif command -v pacman >/dev/null 2>&1; then
        echo "pacman"
    else
        echo "unknown"
    fi
}

install_build_deps() {
    local pm
    pm=$(detect_pkg_manager)
    echo ":: installing build dependencies ($pm)"
    case "$pm" in
        dnf)    dnf install -y gcc libevdev-devel ;;
        apt)    apt-get update -qq && apt-get install -y gcc libevdev-dev pkg-config ;;
        pacman) pacman -S --noconfirm --needed gcc libevdev ;;
        *)      die "unknown package manager -- install gcc and libevdev-dev manually, then run 'make && sudo make install'" ;;
    esac
}

# --- uninstall ---

do_uninstall() {
    echo ":: stopping service"
    systemctl disable --now das-debounce 2>/dev/null || true

    echo ":: removing files"
    rm -f "$BINDIR/das-debounce"
    rm -f "$UNITDIR/das-debounce.service"
    rm -f "$UDEVDIR/90-das-keyboard.rules"

    systemctl daemon-reload
    udevadm control --reload-rules 2>/dev/null || true

    echo ":: das-debounce uninstalled"
    exit 0
}

# --- install ---

do_install() {
    install_build_deps

    TMPDIR_CLEANUP=$(mktemp -d)
    trap 'rm -rf "${TMPDIR_CLEANUP:-}"' EXIT INT TERM
    echo ":: downloading source"
    curl -fsSL "$REPO/das-debounce.c" -o "$TMPDIR_CLEANUP/das-debounce.c"

    echo ":: compiling"
    gcc -Wall -Wextra -O2 -o "$TMPDIR_CLEANUP/das-debounce" "$TMPDIR_CLEANUP/das-debounce.c" \
        $(pkg-config --cflags --libs libevdev)

    echo ":: installing binary"
    install -Dm755 "$TMPDIR_CLEANUP/das-debounce" "$BINDIR/das-debounce"

    echo ":: installing systemd service"
    cat > "$UNITDIR/das-debounce.service" <<'UNIT'
[Unit]
Description=Das Keyboard volume knob debounce
After=systemd-udevd.service

[Service]
Type=simple
ExecStart=/usr/local/bin/das-debounce
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
UNIT

    echo ":: installing udev rule"
    cat > "$UDEVDIR/90-das-keyboard.rules" <<'UDEV'
ACTION=="add", SUBSYSTEM=="input", ATTRS{idVendor}=="24f0", ATTRS{idProduct}=="204a", TAG+="systemd", ENV{SYSTEMD_WANTS}="das-debounce.service"
UDEV

    echo ":: enabling service"
    systemctl daemon-reload
    udevadm control --reload-rules 2>/dev/null || true
    systemctl enable --now das-debounce

    echo ":: das-debounce installed and running"
    systemctl status das-debounce --no-pager || true
}

# --- main ---

check_root

if [ "${1:-}" = "--uninstall" ]; then
    do_uninstall
else
    do_install
fi
