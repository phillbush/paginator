# Paginator

<p align="center">
  <img src="./demo.png", title="demo"/>
</p>

Paginator is a desktop pager for EWMH-compliant X11 window managers.
Paginator provides a graphical interface displaying the current
configuration of all desktops, allowing the user to change the current
desktop or the current active window with the mouse.

## Options
Paginator understand the following command-line options.

* `-geometry geometry`: Specify the initial size for Paginator.
* `-name name`:         Specify a resource/instance name for Paginator.
* `-xrm resources`:     Specify X resources for Paginator.

## Customization
Paginator can be customized by setting the following X resources.

* `Paginator.client.activeBackground`
* `Paginator.client.activeBorderColor`
* `Paginator.client.activeTopShadowColor`
* `Paginator.client.activeBottomShadowColor`
* `Paginator.client.inactiveBackground`
* `Paginator.client.inactiveBorderColor`
* `Paginator.client.inactiveTopShadowColor`
* `Paginator.client.inactiveBottomShadowColor`
* `Paginator.client.urgentBackground`
* `Paginator.client.urgentBorderColor`
* `Paginator.client.urgentTopShadowColor`
* `Paginator.client.urgentBottomShadowColor`
* `Paginator.client.borderWidth`
* `Paginator.client.shadowThickness`
* `Paginator.pager.borderWidth`
* `Paginator.pager.shadowThickness`
* `Paginator.pager.background`
* `Paginator.pager.foreground`
* `Paginator.geometry`

## Installation
Run `make all` to build, and `make install` to install the binary and
the manual into `${PREFIX}` (`/usr/local`).

## Usage
Run `paginator` with a number of rows and columns:

```
$ paginator 2 3
```

This creates the following pager:

```
+-------+-------+-------+
|       |       |       |
|   1   |   2   |   3   |
|       |       |       |
+-------+-------+-------+
|       |       |       |
|   4   |   5   |   6   |
|       |       |       |
+-------+-------+-------+
```

## License
The code and manual are under the MIT/X license.
The icons are in CC0/Public Domain.
See `./LICENSE` for more information.

## Epilogue
**Read the manual.**
