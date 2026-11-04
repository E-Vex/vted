# vted @VERSION@ — static Linux build

This is a fully static build of vted, the small, fast terminal text
editor. It has no dependencies at all and runs on any Linux
distribution: glibc or musl, Debian or Arch or Alpine or Gentoo, as
long as the machine is x86_64.

Install system wide (binary goes to /usr/local/bin, manual to
/usr/local/share/man/man1):

    sudo ./install.sh

Install for your user only:

    PREFIX="$HOME/.local" ./install.sh

Or skip installing and run it where it stands:

    ./vted

Manual install, if you prefer:

    install -m 755 vted ~/.local/bin/vted
    install -m 644 vted.1 ~/.local/share/man/man1/vted.1

The manual page is included: `man ./vted.1` reads it straight from
this directory.

Source, documentation and other packages:
https://github.com/E-Vex/vted
