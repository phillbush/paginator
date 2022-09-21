# Paginator & Taskinator

<p align="center">
  <img src="https://user-images.githubusercontent.com/63266536/160118895-c40ffd1d-f485-43d1-bea8-7a5ef6537e0c.png", title="demo"/>
</p>

Paginator is a desktop pager for EWMH-compliant X11 window managers.
Paginator provides a graphical interface displaying the current
configuration of all desktops, allowing the user to change the current
desktop or the current active window with the mouse.

Taskinator is a dockable taskbar for EWMH-compliant X11 window managers.
Taskinator provides a graphical interface displaying buttons for each
window, allowing the user to change the current active window with the
mouse.

Paginator and Taskinator comes with the following features:

* Option to show icons (`-i`).
* Option to dock the pager as a dockapp (`-w`).
* (For paginator) options to control the layout of the pager (`-c`, `-g`, `-l`, `-o`).
* (For taskinator) options to control which windows are listed (`-d` and `-h`).
* Several X Resources to control the colors.
* Motif-like 3D shaped mini-windows.

## Files

The files are:
* `./README.md`:    This file.
* `./Makefile`:     The makefile.
* `./paginator.1`:  The manual file (man page) for paginator.
* `./paginator.c`:  The source code of paginator.
* `./taskinator.1`: The manual file (man page) for taskinator.
* `./taskinator.c`: The source code of taskinator.
* `./x.c`:          Shared source code.
* `./x.h`:          Interface for shared source code.


## Installation

First, read the Makefile and set the proper environment variables to
match your local setup.

In order to build paginator and taskinator you need the Xlib header
files (and some X extensions: Xrender and Xinerama).  Enter the
following command to build them.  This command creates the binary files
`./paginator` and `./taskinator`.

	make

By default, paginator and taskinator are installed into the `/usr/local`
prefix.  Enter the following command to install them (if necessary as
root).  This command installs the binary files `./paginator` and
`./taskinator` into the `${PREFIX}/bin/` directory, and the manual files
`./paginator.1` and `./taskinator.1` into the `${MANPREFIX}/man1/`
directory.

	make install


## TODO

* Own a manager selection, as specified by EWMH.
* Set the `_NET_DESKTOP_LAYOUT` property, as specified by EWMH.
* Implement a *bar mode* in addition to the *dockapp mode*.
