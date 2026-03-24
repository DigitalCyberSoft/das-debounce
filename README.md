# das-debounce

Fix the Das Keyboard 4 Professional volume knob on Linux.

## The Problem

The volume knob's rotary encoder produces spurious direction reversals at high rotation speeds, alternating between volume-up and volume-down every 8-16ms. The keyboard firmware performs no debounce, causing erratic volume changes.

Additionally, Linux/X11 enables key auto-repeat on volume keycodes by default, generating phantom events between knob detents at lower speeds.

## How It Works

`das-debounce` runs as a systemd service that:

1. Grabs exclusive access to the Das Keyboard's Consumer Control input device
2. Suppresses direction reversals arriving within a debounce window (default 100ms)
3. Re-emits clean events via a virtual uinput device

The exclusive grab also eliminates the X11 auto-repeat issue since the desktop only sees the virtual device.

## Install from Source

### Fedora

```sh
sudo dnf install gcc libevdev-devel
make
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now das-debounce
sudo udevadm control --reload-rules
```

### Fedora (RPM)

```sh
sudo dnf install gcc libevdev-devel rpm-build
rpmbuild -bb das-debounce.spec --define "_sourcedir $(pwd)"
sudo dnf install ~/rpmbuild/RPMS/x86_64/das-debounce-*.rpm
```

### Debian/Ubuntu

```sh
sudo apt install gcc libevdev-dev pkg-config
make
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now das-debounce
sudo udevadm control --reload-rules
```

### Arch

```sh
sudo pacman -S gcc libevdev
make
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now das-debounce
sudo udevadm control --reload-rules
```

## Uninstall

```sh
sudo systemctl disable --now das-debounce
sudo make uninstall
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
```

## Configuration

The debounce window can be adjusted by editing the service file:

```sh
sudo systemctl edit das-debounce
```

```ini
[Service]
ExecStart=
ExecStart=/usr/local/bin/das-debounce 150
```

Valid range: 10-500ms. Default: 100ms.

## Device Support

Currently targets the Das Keyboard 4 Professional (vendor `24F0`, product `204A`). Other Das Keyboard models using the same USB IDs should also work.

## License

MIT
