# Paginator

![demo](./demo.png)

(Paginator is at the top right on the image.)

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

* `Paginator.background`:
  Color of a desktop miniature.
* `Paginator.foreground`:
  Color of the current desktop miniature.
* `Paginator.separatorColor`:
  Color of the separator between desktop miniatures.
* `Paginator.topShadowColor`:
  Color of the light shadow around Paginator.
* `Paginator.bottomShadowColor`:
  Color of the heavy shadow around Paginator.
* `Paginator.activeBackground`:
  Color of the miniature of the active window.
* `Paginator.activeBorderColor`:
  Color of the border of the miniature of the active window.
* `Paginator.activeTopShadowColor`:
  Color of the light shadow of the miniature of the active window.
* `Paginator.activeBottomShadowColor`:
  Color of the heavy shadow of the miniature of the active window.
* `Paginator.inactiveBackground`:
  Color of the miniature of a regular window.
* `Paginator.inactiveBorderColor`:
  Color of the border of the miniature of a regular window.
* `Paginator.inactiveTopShadowColor`:
  Color of the light shadow of the miniature of a regular window.
* `Paginator.inactiveBottomShadowColor`:
  Color of the heavy shadow of the miniature of a regular window.
* `Paginator.urgentBackground`:
  Color of the miniature of an urgent window.
* `Paginator.urgentBorderColor`:
  Color of the border of the miniature of an urgent window.
* `Paginator.urgentTopShadowColor`:
  Color of the light shadow of the miniature of an urgent window.
* `Paginator.urgentBottomShadowColor`:
  Color of the heavy shadow of the miniature of an urgent window.
* `Paginator.borderWidth`:
  Width in pixels of the border around the miniatures of windows.
* `Paginator.separatorWidth`:
  Width in pixels of the separator between desktop miniatures.
* `Paginator.shadowThickness`:
  Width in pixels of the 3D shadows.
* `Paginator.geometry`:
  Initial geometry of paginator.

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
See `./LICENSE` for more information.

## Epilogue
**Read the manual.**
