Name:           vted
Version:        @VERSION@
Release:        1
Summary:        A small, fast terminal text editor
License:        MIT
URL:            https://github.com/E-Vex/vted
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  make

%description
vted is a small, fast terminal text editor for Linux: one C11 binary,
no dependencies beyond the C standard library and POSIX. No curses, no
autotools, no cmake - just make.

It has UTF-8 aware editing, incremental search, undo and redo,
auto-indent, go-to-line, and atomic saves that never lose the original
file.

%prep
%setup -q

%build
make CFLAGS="%{optflags} -std=c11 -I."

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix}

%files
%license LICENSE
%doc README.md
%{_bindir}/vted
%{_mandir}/man1/vted.1*

%changelog
* @DATE@ E-Vex <raiedaleve@gmail.com> - @VERSION@-1
- Update to @VERSION@; see the release notes at
  https://github.com/E-Vex/vted/releases/tag/v@VERSION@
