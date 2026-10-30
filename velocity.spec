Name:           velocity
Version:        0.2.0
Release:        1%{?dist}
Summary:        A speedtest app for Linux
License:        GPL-3.0-or-later
URL:            https://github.com/vani-tty1/velocity
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  meson >= 1.7.0
BuildRequires:  ninja-build gcc gettext pkgconfig(gtk4) pkgconfig(libadwaita-1) >= 1.8
BuildRequires:  pkgconfig(epoxy) pkgconfig(libsoup-3.0) pkgconfig(json-glib-1.0)
BuildRequires:  blueprint-compiler desktop-file-utils appstream glib2-devel
Requires:       gtk4 libadwaita >= 1.8
Requires:       libsoup3 json-glib

%description
A speedtest app using LibreSpeed

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%check
%meson_test

%files
%license COPYING
%doc README.md
%{_bindir}/velocity
%{_datadir}/applications/io.github.vani_tty1.velocity.desktop
%{_datadir}/dbus-1/services/io.github.vani_tty1.velocity.service
%{_datadir}/glib-2.0/schemas/io.github.vani_tty1.velocity.gschema.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.vani_tty1.velocity.svg
%{_datadir}/icons/hicolor/symbolic/apps/io.github.vani_tty1.velocity-symbolic.svg
%{_datadir}/metainfo/io.github.vani_tty1.velocity.metainfo.xml

%changelog
* Fri Oct 29 2026 Giovanni Rafanan <giovannirafanan609@gmail.com> - 0.2.0-1
- New Results page
- Results are now exportable via a JSON file
- Better auto-selection of test servers
- New server picker to choose preferred test server
