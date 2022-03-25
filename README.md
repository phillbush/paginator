# Paginator

<p align="center">
  <img src="https://user-images.githubusercontent.com/63266536/160118895-c40ffd1d-f485-43d1-bea8-7a5ef6537e0c.png", title="demo"/>
</p>

Paginator is a desktop pager for EWMH-compliant X11 window managers.
Paginator provides a GUI interface displaying the current configuration
of all desktops, allowing the user to change the current desktop or the
current active window with the mouse.

Paginator comes with the following features:

* Option to show icons (-i).
* Option to dock the pager as a dockapp (-w).
* Options to control the layout of the pager (-c, -g, -l, -o).
* Several X Resources to control the colors.
* Motif-like 3D shaped mini-windows.

## Files

The files are:
* `./README.md`:   This file.
* `./Makefile`:    The makefile.
* `./config.h`:    The hardcoded default configuration for paginatorj.
* `./paginator.1`: The manual file (man page) for paginator.
* `./paginator.c`: The source code of paginator.


## Installation

First, read the Makefile and set the proper environment variables to
match your local setup.

In order to build paginator you need the Xlib header files (and some X
extensions: Xrender and Xinerama).  The default configuration for
paginator is specified in the file `config.h`, you can edit it, but most
configuration can be changed at runtime via command-line options or X
resources.  Enter the following command to build paginator.  This
command creates the binary file ./paginator.

	make

By default, paginator is installed into the `/usr/local` prefix.  Enter
the following command to install paginator (if necessary as root).  This
command installs the binary file `./paginator` into the `${PREFIX}/bin/`
directory, and the manual file `./paginator.1` into `${MANPREFIX}/man1/`
directory.

	make install


## TODO

* Own a manager selection, as specified by EWMH.
* Set the `_NET_DESKTOP_LAYOUT` property, as specified by EWMH.
