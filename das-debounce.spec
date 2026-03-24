Name:           das-debounce
Version:        1.0.0
Release:        1%{?dist}
Summary:        Fix Das Keyboard 4 Professional volume knob bounce on Linux

License:        MIT
URL:            https://github.com/DigitalCyberSoft/das-debounce
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  pkgconfig(libevdev)
BuildRequires:  systemd-rpm-macros
Requires:       libevdev

%description
The Das Keyboard 4 Professional's volume knob encoder produces spurious
direction reversals at high rotation speeds. The keyboard firmware performs
no debounce, causing erratic volume changes on Linux.

das-debounce grabs the Consumer Control input device, suppresses direction
reversals within a configurable debounce window, and re-emits clean events
via uinput.

%prep
%autosetup

%build
%make_build

%install
%make_install PREFIX=%{_prefix}

%post
%systemd_post das-debounce.service
udevadm control --reload-rules 2>/dev/null || :

%preun
%systemd_preun das-debounce.service

%postun
%systemd_postun_with_restart das-debounce.service
udevadm control --reload-rules 2>/dev/null || :

%files
%license LICENSE
%{_bindir}/das-debounce
%{_unitdir}/das-debounce.service
/usr/lib/udev/rules.d/90-das-keyboard.rules
