# das-debounce

Fix the Das Keyboard 4 Professional volume knob on Linux.

## The Problem

The Das Keyboard 4 Professional's volume knob produces erratic, inconsistent volume changes on Linux. Turning the knob fast causes the volume to jump around or barely move at all.

There are two root causes:

### 1. X11 auto-repeat on volume keycodes

The knob sends `XF86AudioRaiseVolume` (keycode 123) and `XF86AudioLowerVolume` (keycode 122) as standard key events. X11 enables auto-repeat on these keycodes by default. Each knob detent fires a key press, then X11's repeat mechanism generates phantom events at ~25/sec before the release arrives 8ms later. At normal rotation speeds this causes volume to jump in unpredictable steps.

You can fix this in isolation with `xset -r 122; xset -r 123`, but this only solves part of the problem and must be re-applied every login.

### 2. Encoder bounce at high rotation speed

At fast rotation speeds, the rotary encoder produces spurious direction reversals, alternating between volume-up and volume-down every 8-16ms. The keyboard firmware performs no debounce. Raw `evtest` captures show the encoder rapidly toggling direction even when spinning in only one direction:

```
424.548  KEY_VOLUMEUP
424.564  KEY_VOLUMEDOWN   <-- bounce
424.580  KEY_VOLUMEUP     <-- bounce
424.596  KEY_VOLUMEDOWN   <-- bounce
424.612  KEY_VOLUMEDOWN   <-- settles
```

The opposing events cancel each other out, so the volume goes nowhere during fast turns. There is no X11 or desktop-level fix for this. The bad data comes straight from the hardware.

### What doesn't work

- **Compressed air / contact cleaner** -- this is not a dirty encoder problem. Brand new units exhibit the same behavior. The encoder simply lacks firmware debounce.
- **`xset -r 122; xset -r 123`** -- disabling auto-repeat fixes the low-speed stutter but does nothing about the high-speed direction bounce.
- **Changing desktop volume step size** -- doesn't help when the events themselves alternate direction.
- **Different USB port / hub** -- the bounce originates in the keyboard's encoder, not the USB transport.

## How It Works

`das-debounce` runs as a systemd service that:

1. Grabs exclusive access to the Das Keyboard's Consumer Control input device
2. Suppresses direction reversals arriving within a debounce window (default 100ms)
3. Re-emits clean events via a virtual uinput device

Same-direction events always pass through immediately. Only direction changes within the debounce window are suppressed. The exclusive grab also eliminates the X11 auto-repeat issue since the desktop only sees the virtual device.

## Quick Install

```sh
curl -fsSL https://raw.githubusercontent.com/DigitalCyberSoft/das-debounce/main/install.sh | sudo bash
```

Requires root for installing the binary, systemd service, and udev rule.

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

Or if installed via `install.sh`:

```sh
curl -fsSL https://raw.githubusercontent.com/DigitalCyberSoft/das-debounce/main/install.sh | sudo bash -s -- --uninstall
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
