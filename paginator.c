#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#define PROGNAME        "Paginator"
#define ICON_SIZE       16              /* preferred icon size */
#define MAX_DESKTOPS    100             /* maximum number of desktops */
#define DEF_WIDTH       125             /* default width for the pager */
#define DEF_NCOLS       2               /* default number of columns */
#define SEPARATOR_SIZE  1               /* size of the line between minidesktops */
#define RESIZETIME      64              /* time to redraw containers during resizing */
#define PAGER_ACTION    ((long)(1 << 14))
#define SEPARATOR(x)    ((x) * SEPARATOR_SIZE)

/* colors */
enum {
	COLOR_BACKGROUND,
	COLOR_BORDER,
	COLOR_LAST
};

/* decoration style */
enum {
	STYLE_ACTIVE,
	STYLE_INACTIVE,
	STYLE_LAST
};

/* atoms */
enum {
	WM_DELETE_WINDOW,
	_NET_ACTIVE_WINDOW,
	_NET_CLIENT_LIST_STACKING,
	_NET_CURRENT_DESKTOP,
	_NET_WM_DESKTOP,
	_NET_DESKTOP_LAYOUT,
	_NET_SHOWING_DESKTOP,
	_NET_MOVERESIZE_WINDOW,
	_NET_NUMBER_OF_DESKTOPS,
	_NET_WM_ICON,
	_NET_WM_STATE,
	_NET_WM_STATE_HIDDEN,

	ATOM_LAST,
};

/* orientation */
enum Orientation {
	_NET_WM_ORIENTATION_HORZ = 0,
	_NET_WM_ORIENTATION_VERT = 1,
};

/* starting corner */
enum StartingCorner {
	_NET_WM_TOPLEFT     = 0,
	_NET_WM_TOPRIGHT    = 1,
	_NET_WM_BOTTOMRIGHT = 2,
	_NET_WM_BOTTOMLEFT  = 3,
};

/* draw context */
struct DC {
	GC gc;
	unsigned long windowcolors[STYLE_LAST][COLOR_LAST];
	unsigned long desktopselbg;
	unsigned long desktopbg;
	unsigned long separator;
};

/* mini-desktop geometry */
struct Desktop {
	Window miniwin;
	int x, y, w, h;
};

/* client info */
struct Client {
	Window clientwin;
	Window miniwin;
	Picture icon;
	Pixmap pix;
	size_t desk;
	int cx, cy, cw, ch;
	int x, y, w, h;
	int ishidden;
	int ismapped;
};

/* the pager */
struct Pager {
	struct Desktop **desktops;
	struct Client **clients;
	struct Client *active;
	Window win;
	Pixmap pix;
	size_t nclients;
	size_t ndesktops;
	size_t currdesktop;
	int cellw, cellh;
	int w, h;
	int showingdesk;
};

/* configuration structure */
struct Config {
	int nrows, ncols;
	int x, y;
	int userplaced;
	int xnegative, ynegative;
	enum Orientation orient;
	enum StartingCorner corner;
	const char *windowcolors[STYLE_LAST][COLOR_LAST];
	const char *desktopselbg;
	const char *desktopbg;
	const char *separator;
};

/* global variables */
static XSetWindowAttributes miniswa = {
	.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask
};
static int (*xerrorxlib)(Display *, XErrorEvent *);
static struct Pager pager = {0};
static struct DC dc;
static Atom atoms[ATOM_LAST];
static Display *dpy;
static Visual *visual;
static Window root;
static Colormap colormap;
static unsigned int depth;
static int screen;
static int screenw, screenh;
static int running = 1;
static int wflag = 0;                   /* whether to start in withdrawn mode */
static int iflag = 0;                   /* whether to draw icons */

#include "config.h"

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: paginator [-iw] [-c corner] [-g geometry] [-l layout] [-o orientation]\n");
	exit(1);
}

/* get maximum */
static int
max(int x, int y)
{
	return x > y ? x : y;
}

/* call malloc checking for error */
static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

/* call calloc checking for errors */
static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

/* get color from color string */
static unsigned long
ealloccolor(const char *s)
{
	XColor color;

	if(!XAllocNamedColor(dpy, colormap, s, &color, &color)) {
		warnx("could not allocate color: %s", s);
		return BlackPixel(dpy, screen);
	}
	return color.pixel;
}

/* error handler */
static int
xerror(Display *dpy, XErrorEvent *e)
{
	if (e->error_code == BadWindow || e->error_code == BadDrawable ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadMatch) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadValue))
		return 0;
	return xerrorxlib(dpy, e);
}

/* parse command-line options */
static void
getoptions(int argc, char **argv)
{
	int ch;
	int status;
	char *s, *endp;

	while ((ch = getopt(argc, argv, "c:g:il:o:w")) != -1) {
		switch (ch) {
		case 'c':
			if (strpbrk(optarg, "Bb") != NULL) {
				if (strpbrk(optarg, "Rr") != NULL) {
					config.corner = _NET_WM_BOTTOMRIGHT;
				} else {
					config.corner = _NET_WM_BOTTOMLEFT;
				}
			} else if (strpbrk(optarg, "Rr") != NULL) {
				config.corner = _NET_WM_TOPRIGHT;
			}
			break;
		case 'g':
			status = XParseGeometry(optarg, &config.x, &config.y, &pager.w, &pager.h);
			if (status & (XValue | YValue))
				config.userplaced = 1;
			if (status & XNegative)
				config.xnegative = 1;
			if (status & YNegative)
				config.ynegative = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'l':
			s = optarg;
			config.ncols = strtol(s, &endp, 10);
			if (config.ncols < 0 || config.ncols > MAX_DESKTOPS || endp == s)
				errx(1, "improper layout argument: %s", optarg);
			s = endp;
			if (*s != 'x' && *s != 'X')
				errx(1, "improper layout argument: %s", optarg);
			s++;
			config.nrows = strtol(s, &endp, 10);
			if (config.nrows < 0 || config.nrows > MAX_DESKTOPS || endp == s || *endp != '\0')
				errx(1, "improper layout argument: %s", optarg);
			break;
		case 'o':
			if (strpbrk(optarg, "Vv") != NULL)
				config.orient = _NET_WM_ORIENTATION_VERT;
			break;
		case 'w':
			wflag = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0) {
		usage();
	}
}

/* get window property from root window; remember to XFree(wins) later */
static unsigned long
getwinprop(Window win, Atom prop, Window **wins)
{
	unsigned char *list;
	unsigned long len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */

	list = NULL;
	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_WINDOW, &da, &di, &len, &dl, &list) != Success || list == NULL) {
		*wins = NULL;
		return 0;
	}
	*wins = (Window *)list;
	return len;
}

/* get atom property from window */
static unsigned long
getcardprop(Window win, Atom prop)
{
	int di;
	unsigned long card;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da;

	card = 0;
	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_CARDINAL, &da, &di, &dl, &dl, &p) == Success && p) {
		card = *(unsigned long *)p;
		XFree(p);
	}
	return card;
}

/* get atom property from given window */
static unsigned long
getatomprop(Window win, Atom prop, Atom **atoms)
{
	unsigned char *list;
	unsigned long len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */

	list = NULL;
	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_ATOM,
		               &da, &di, &len, &dl, &list) != Success || list == NULL) {
		*atoms = NULL;
		return 0;
	}
	*atoms = (Atom *)list;
	return len;
}

static uint32_t
prealpha(uint32_t p)
{
	uint8_t a = p >> 24u;
	uint32_t rb = (a * (p & 0xFF00FFu)) >> 8u;
	uint32_t g = (a * (p & 0x00FF00u)) >> 8u;
	return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
}

static Pixmap
geticccmicon(Window win, int *iconw, int *iconh, int *d)
{
	XWMHints *wmhints;
	GC gc;
	Pixmap pix = None;
	Window dw;
	int x, y, b;

	if ((wmhints = XGetWMHints(dpy, win)) == NULL)
		return None;
	if (!(wmhints->flags & IconPixmapHint))
		goto done;
	pix = wmhints->icon_pixmap;
	XGetGeometry(dpy, wmhints->icon_pixmap, &dw, &x, &y, iconw, iconh, &b, d);
	pix = XCreatePixmap(dpy, root, *iconw, *iconh, *d);
	gc = XCreateGC(dpy, pix, 0, NULL);
	XSetForeground(dpy, gc, 1);
	XFillRectangle(dpy, pix, gc, 0, 0, *iconw, *iconh);
	XCopyArea(dpy, wmhints->icon_pixmap, pix, gc, 0, 0, *iconw, *iconh, 0, 0);
done:
	XFree(wmhints);
	return pix;
}

static Pixmap
getewmhicon(Window win, int *iconw, int *iconh, int *d)
{
	XImage *img;
	GC gc;
	Pixmap pix = None;
	Atom da;
	size_t i, size;
	uint32_t *data32;
	unsigned long *q, *p, *end, *data = NULL;
	unsigned long len, dl;
	int diff, mindiff = INT_MAX;
	int w, h;
	int format;
	char *datachr = NULL;

	(void)d;
	if (XGetWindowProperty(dpy, win, atoms[_NET_WM_ICON], 0L, UINT32_MAX, False, AnyPropertyType, &da, &format, &len, &dl, (unsigned char **)&q) != Success)
		return None;
	if (q == NULL)
		return None;
	if (len == 0 || format != 32)
		goto done;
	for (p = q, end = p + len; p < end; p += size) {
		w = *p++;
		h = *p++;
		size = w * h;
		if (w < 1 || h < 1 || p + size > end)
			break;
		diff = max(w, h) - ICON_SIZE;
		if (diff >= 0 && diff < mindiff) {
			*iconw = w;
			*iconh = h;
			data = p;
			if (diff == 0) {
				break;
			}
		}
	}
	if (data == NULL)
		goto done;
	size = *iconw * *iconh;
	data32 = (uint32_t *)data;
	for (i = 0; i < size; ++i)
		data32[i] = prealpha(data[i]);
	datachr = emalloc(size * sizeof(*data));
	(void)memcpy(datachr, data32, size * sizeof(*data));
	if ((img = XCreateImage(dpy, visual, 32, ZPixmap, 0, datachr, *iconw, *iconh, 32, 0)) == NULL)
		goto done;
	XInitImage(img);
	pix = XCreatePixmap(dpy, root, *iconw, *iconh, 32);
	gc = XCreateGC(dpy, pix, 0, NULL);
	XPutImage(dpy, pix, gc, img, 0, 0, 0, 0, *iconw, *iconh);
	XFreeGC(dpy, gc);
	XDestroyImage(img);
done:
	XFree(q);
	return pix;
}

static Picture
geticonprop(Window win)
{
	Pixmap pix;
	Picture pic = None;
	XRenderPictFormat xpf;
	XTransform xf;
	int iconw, iconh;
	int icccm = 0;
	int d;

	if ((pix = getewmhicon(win, &iconw, &iconh, &d)) == None) {
		pix = geticccmicon(win, &iconw, &iconh, &d);
		icccm = 1;
	}
	if (pix == None)
		return None;
	xpf.depth = d;
	xpf.type = PictTypeDirect;
	if (icccm)
		pic = XRenderCreatePicture(dpy, pix, XRenderFindFormat(dpy, PictFormatType | PictFormatDepth, &xpf, 0), 0, NULL);
	else
		pic = XRenderCreatePicture(dpy, pix, XRenderFindStandardFormat(dpy, PictStandardARGB32), 0, NULL);
	XFreePixmap(dpy, pix);
	if (max(iconw, iconh) != ICON_SIZE) {
		XRenderSetPictureFilter(dpy, pic, FilterBilinear, NULL, 0);
		xf.matrix[0][0] = (iconw << 16u) / ICON_SIZE; xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
		xf.matrix[1][0] = 0; xf.matrix[1][1] = (iconh << 16u) / ICON_SIZE; xf.matrix[1][2] = 0;
		xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
		XRenderSetPictureTransform(dpy, pic, &xf);
	}
	return pic;
}

/* return non-zero if window is hidden */
static int
ishidden(Window win)
{
	Atom *as;
	unsigned long natoms, i;
	int retval;

	retval = 0;
	if ((natoms = getatomprop(win, atoms[_NET_WM_STATE], &as)) != 0) {
		for (i = 0; i < natoms; i++) {
			if (as[i] == atoms[_NET_WM_STATE_HIDDEN]) {
				retval = 1;
				break;
			}
		}
		XFree(as);
	}
	return retval;
}

/* send client message to root window */
static void
clientmsg(Window win, Atom atom, unsigned long d0, unsigned long d1, unsigned long d2, unsigned long d3, unsigned long d4)
{
	XEvent ev;
	long mask = SubstructureRedirectMask | SubstructureNotifyMask;

	ev.xclient.type = ClientMessage;
	ev.xclient.serial = 0;
	ev.xclient.send_event = True;
	ev.xclient.message_type = atom;
	ev.xclient.window = win;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = d0;
	ev.xclient.data.l[1] = d1;
	ev.xclient.data.l[2] = d2;
	ev.xclient.data.l[3] = d3;
	ev.xclient.data.l[4] = d4;
	if (!XSendEvent(dpy, root, False, mask, &ev)) {
		errx(1, "could not send event");
	}
}

/* free desktops and the desktop array */
static void
cleandesktops(void)
{
	size_t i;

	for (i = 0; i < pager.ndesktops; i++) {
		if (pager.desktops[i]->miniwin != None)
			XDestroyWindow(dpy, pager.desktops[i]->miniwin);
		free(pager.desktops[i]);
	}
	free(pager.desktops);
}

/* destroy client's mini-windows and free it */
static void
cleanclient(struct Client *cp)
{
	if (cp == NULL)
		return;
	if (cp->miniwin != None)
		XDestroyWindow(dpy, cp->miniwin);
	if (cp->icon != None)
		XRenderFreePicture(dpy, cp->icon);
	if (cp->pix != None)
		XFreePixmap(dpy, cp->pix);
	free(cp);
}

/* free clients and client array */
static void
cleanclients(void)
{
	size_t i;

	for (i = 0; i < pager.nclients; i++) {
		cleanclient(pager.clients[i]);
		pager.clients[i] = NULL;
	}
	free(pager.clients);
}

/* destroy drawing context */
static void
cleandc(void)
{
	int i;

	for (i = 0; i < STYLE_LAST; i++)
		XFreeColors(dpy, colormap, dc.windowcolors[i], COLOR_LAST, 0);
	XFreeColors(dpy, colormap, &dc.desktopselbg, 1, 0);
	XFreeColors(dpy, colormap, &dc.desktopbg, 1, 0);
	XFreeColors(dpy, colormap, &dc.separator, 1, 0);
	XFreeGC(dpy, dc.gc);
}

/* set pager size */
static void
setpagersize(int w, int h)
{
	pager.w = w;
	pager.h = h;
}

/* update number of desktops */
static void
setndesktops(void)
{
	size_t i;

	cleandesktops();
	pager.ndesktops = getcardprop(root, atoms[_NET_NUMBER_OF_DESKTOPS]);
	pager.desktops = ecalloc(pager.ndesktops, sizeof(*pager.desktops));
	if (pager.ndesktops < 1 || pager.ndesktops > MAX_DESKTOPS)
		errx(1, "could not get number of desktops");
	for (i = 0; i < pager.ndesktops; i++) {
		pager.desktops[i] = emalloc(sizeof(*pager.desktops[i]));
		pager.desktops[i]->miniwin = XCreateWindow(
			dpy, pager.win, 0, 0, 1, 1, 0,
			CopyFromParent, CopyFromParent, CopyFromParent,
			CWEventMask, &miniswa
		);
	}
}

/* return from client list the client with given client window */
static struct Client *
getclient(Window win)
{
	size_t i;

	for (i = 0; i < pager.nclients; i++)
		if (pager.clients[i] != NULL && pager.clients[i]->clientwin == win)
			return pager.clients[i];
	return NULL;
}

/* return from client list the client with given client window; and delete it from client list */
static struct Client *
getdelclient(Window win)
{
	struct Client *cp;
	size_t i;

	for (i = 0; i < pager.nclients; i++) {
		if (pager.clients[i] != NULL && pager.clients[i]->clientwin == win) {
			cp = pager.clients[i];
			pager.clients[i] = NULL;
			return cp;
		}
	}
	return NULL;
}

/* set hidden state of given client */
static void
sethiddenstate(struct Client *cp)
{
	if (cp == NULL)
		return;
	cp->ishidden = ishidden(cp->clientwin);
}

/* set client's desktop number */
static void
setdesktop(struct Client *cp)
{
	if (cp == NULL)
		return;
	cp->desk = getcardprop(cp->clientwin, atoms[_NET_WM_DESKTOP]);
}

/* update list of clients; return nonzero if list of clients has changed */
static int
setclients(void)
{
	struct Client **clients;
	struct Client *oldcp;
	Window *wins;
	Window dw;
	unsigned int du, b;
	size_t nclients;
	size_t i;
	int x, y;
	int ret;

	ret = 0;
	nclients = getwinprop(root, atoms[_NET_CLIENT_LIST_STACKING], &wins);
	clients = ecalloc(nclients, sizeof(*clients));
	for (i = 0; i < nclients; i++) {
		oldcp = pager.nclients > 0 ? pager.clients[i] : NULL;
		clients[i] = getdelclient(wins[i]);
		if (oldcp == NULL || clients[i] == NULL || clients[i] != oldcp)
			ret = 1;
		if (clients[i] == NULL) {
			clients[i] = emalloc(sizeof(*clients[i]));
			*clients[i] = (struct Client) {
				.pix = None,
				.ishidden = 0,
				.ismapped = 0,
			};
			clients[i]->clientwin = wins[i];
			XSelectInput(dpy, clients[i]->clientwin, StructureNotifyMask | PropertyChangeMask);
			clients[i]->miniwin = XCreateWindow(
				dpy, pager.win, 0, 0, 1, 1, 1,
				CopyFromParent, CopyFromParent, CopyFromParent,
				CWEventMask, &miniswa
			);
			clients[i]->icon = iflag ? geticonprop(clients[i]->clientwin) : None;
		}
		if (XGetGeometry(dpy, wins[i], &dw, &x, &y, &clients[i]->cw, &clients[i]->ch, &b, &du) &&
		    XTranslateCoordinates(dpy, wins[i], root, x, y, &clients[i]->cx, &clients[i]->cy, &dw)) {
			sethiddenstate(clients[i]);
			setdesktop(clients[i]);
		} else {
			cleanclient(clients[i]);
			clients[i] = NULL;
		}
	}
	cleanclients();
	pager.clients = clients;
	pager.nclients = nclients;
	XFree(wins);
	return ret;
}

/* set current desktop */
static void
setcurrdesktop(void)
{
	pager.currdesktop = getcardprop(root, atoms[_NET_CURRENT_DESKTOP]);
	if (pager.currdesktop < 0) {
		errx(1, "could not get current desktop");
	}
}

/* update showing desktop state */
static void
setshowingdesk(void)
{
	pager.showingdesk = getcardprop(root, atoms[_NET_SHOWING_DESKTOP]);
}

/* set mini-desktops geometry */
static void
setdeskgeom(void)
{
	int x, y, w, h;
	size_t i, xi, yi;

	w = pager.w - config.ncols;
	h = pager.h - config.nrows;
	for (i = 0; i < pager.ndesktops; i++) {
		xi = yi = i;
		if (config.orient == _NET_WM_ORIENTATION_HORZ)
			yi /= config.ncols;
		else
			xi /= config.nrows;
		if (config.corner == _NET_WM_TOPRIGHT) {
			x = config.ncols - 1 - xi % config.ncols;
			y = yi % config.nrows;
		} else if (config.corner == _NET_WM_BOTTOMLEFT) {
			x = xi % config.ncols;
			y = config.nrows - 1 - yi % config.nrows;
		} else if (config.corner == _NET_WM_BOTTOMRIGHT) {
			x = config.ncols - 1 - xi % config.ncols;
			y = config.nrows - 1 - yi % config.nrows;
		} else {
			x = xi % config.ncols;
			y = yi % config.nrows;
		}
		pager.desktops[i]->x = w * x / config.ncols + x;
		pager.desktops[i]->y = h * y / config.nrows + y;
		pager.desktops[i]->w = max(1, w * (x + 1) / config.ncols - w * x / config.ncols);
		pager.desktops[i]->h = max(1, h * (y + 1) / config.nrows - h * y / config.nrows);
		XMoveResizeWindow(
			dpy, pager.desktops[i]->miniwin,
			pager.desktops[i]->x, pager.desktops[i]->y,
			pager.desktops[i]->w, pager.desktops[i]->h
		);
	}
}

/* set active window */
static void
setactive(void)
{
	Window *wins;

	pager.active = NULL;
	if (getwinprop(root, atoms[_NET_ACTIVE_WINDOW], &wins) > 0) {
		pager.active = getclient(*wins);
		XFree(wins);
	}

}

/* map desktop miniwindows */
static void
mapdesktops(void)
{
	size_t i;
	unsigned long color;

	for (i = 0; i < pager.ndesktops; i++) {
		color = (i == pager.currdesktop) ? dc.desktopselbg : dc.desktopbg;
		XSetWindowBackground(dpy, pager.desktops[i]->miniwin, color);
		XSetForeground(dpy, dc.gc, color);
		XFillRectangle(dpy, pager.desktops[i]->miniwin, dc.gc, 0, 0, pager.desktops[i]->w, pager.desktops[i]->h);
		XMapWindow(dpy, pager.desktops[i]->miniwin);
	}
}

/* unmap client miniwindow */
static void
unmapclient(struct Client *cp)
{
	if (cp->ismapped) {
		XUnmapWindow(dpy, cp->miniwin);
		cp->ismapped = 0;
	}
}

/* redraw client miniwindow */
static void
drawclient(struct Client *cp)
{
	Picture pic;
	int style;

	if (cp == NULL)
		return;
	style = (cp == pager.active) ? STYLE_ACTIVE : STYLE_INACTIVE;
	XSetWindowBorder(dpy, cp->miniwin, dc.windowcolors[style][COLOR_BORDER]);
	if (cp->pix != None)
		XFreePixmap(dpy, cp->pix);
	cp->pix = XCreatePixmap(dpy, cp->miniwin, cp->w, cp->h, depth);
	XSetForeground(dpy, dc.gc, dc.windowcolors[style][COLOR_BACKGROUND]);
	XFillRectangle(dpy, cp->pix, dc.gc, 0, 0, cp->w, cp->h);
	if (cp->icon != None) {
		pic = XRenderCreatePicture(dpy, cp->pix, XRenderFindVisualFormat(dpy, visual), 0, NULL);
		XRenderComposite(dpy, PictOpOver, cp->icon, None, pic, 0, 0, 0, 0, (cp->w - ICON_SIZE) / 2, (cp->h - ICON_SIZE) / 2, cp->w, cp->h);
	}
	XCopyArea(dpy, cp->pix, cp->miniwin, dc.gc, 0, 0, cp->w, cp->h, 0, 0);
	XSetWindowBackgroundPixmap(dpy, cp->miniwin, cp->pix);
}

/* remap client miniwindow into its desktop miniwindow */
static void
reparentclient(struct Client *cp)
{
	struct Desktop *dp;

	if (cp == NULL || cp->desk < 0 || cp->desk >= pager.ndesktops)
		return;
	dp = pager.desktops[cp->desk];
	XReparentWindow(dpy, cp->miniwin, dp->miniwin, cp->x, cp->y);
}

/* remap single client miniwindow */
static void
mapclient(struct Client *cp)
{
	struct Desktop *dp;
	Window dw;
	unsigned int du, b;
	int x, y;

	if (cp == NULL)
		return;
	if (cp->ishidden || cp->desk < 0 || cp->desk >= pager.ndesktops) {
		unmapclient(cp);
		return;
	}
	XGetGeometry(dpy, cp->clientwin, &dw, &x, &y, &cp->cw, &cp->ch, &b, &du);
	XTranslateCoordinates(dpy, cp->clientwin, root, x, y, &cp->cx, &cp->cy, &dw);
	dp = pager.desktops[cp->desk];
	cp->x = cp->cx * dp->w / screenw;
	cp->y = cp->cy * dp->h / screenh;
	cp->w = cp->cw * dp->w / screenw;
	cp->h = cp->ch * dp->h / screenh;
	drawclient(cp);
	XMoveResizeWindow(dpy, cp->miniwin, cp->x, cp->y, cp->w, cp->h);
	if (!cp->ismapped) {
		XMapWindow(dpy, cp->miniwin);
		cp->ismapped = 1;
	}
	reparentclient(cp);
}

/* remap all client miniwindows */
static void
mapclients(void)
{
	struct Client *cp;
	size_t i;

	for (i = 0; i < pager.nclients; i++) {
		cp = pager.clients[i];
		if (cp == NULL)
			continue;
		if (pager.showingdesk) {
			unmapclient(cp);
		} else {
			mapclient(cp);
		}
	}
}

/* draw pager pixmap */
static void
drawpager(void)
{
	int x, y;
	int w, h;
	int i;

	/* draw pager */
	if (pager.pix != None)
		XFreePixmap(dpy, pager.pix);
	pager.pix = XCreatePixmap(dpy, pager.win, pager.w, pager.h, depth);
	XSetForeground(dpy, dc.gc, dc.desktopbg);
	XFillRectangle(dpy, pager.pix, dc.gc, 0, 0, pager.w, pager.h);
	XSetForeground(dpy, dc.gc, dc.separator);
	w = pager.w - config.ncols;
	h = pager.h - config.nrows;
	for (i = 1; i < config.ncols; i++) {
		x = w * i / config.ncols + i - 1;
		XDrawLine(dpy, pager.pix, dc.gc, x, 0, x, pager.h);
	}
	for (i = 1; i < config.nrows; i++) {
		y = h * i / config.nrows + i - 1;
		XDrawLine(dpy, pager.pix, dc.gc, 0, y, pager.w, y);
	}
	XCopyArea(dpy, pager.pix, pager.win, dc.gc, 0, 0, pager.w, pager.h, 0, 0);
}

/* intern atoms */
static void
initatoms(void)
{
	char *atomnames[ATOM_LAST] = {
		[WM_DELETE_WINDOW]              = "WM_DELETE_WINDOW",
		[_NET_ACTIVE_WINDOW]            = "_NET_ACTIVE_WINDOW",
		[_NET_CLIENT_LIST_STACKING]     = "_NET_CLIENT_LIST_STACKING",
		[_NET_CURRENT_DESKTOP]          = "_NET_CURRENT_DESKTOP",
		[_NET_WM_DESKTOP]               = "_NET_WM_DESKTOP",
		[_NET_DESKTOP_LAYOUT]           = "_NET_DESKTOP_LAYOUT",
		[_NET_SHOWING_DESKTOP]          = "_NET_SHOWING_DESKTOP",
		[_NET_MOVERESIZE_WINDOW]        = "_NET_MOVERESIZE_WINDOW",
		[_NET_NUMBER_OF_DESKTOPS]       = "_NET_NUMBER_OF_DESKTOPS",
		[_NET_WM_ICON]                  = "_NET_WM_ICON",
		[_NET_WM_STATE]                 = "_NET_WM_STATE",
		[_NET_WM_STATE_HIDDEN]          = "_NET_WM_STATE_HIDDEN",
	};

	XInternAtoms(dpy, atomnames, ATOM_LAST, False, atoms);
}

/* initialize colors */
static void
initcolors(void)
{
	int i, j;

	for (i = 0; i < STYLE_LAST; i++)
		for (j = 0; j < COLOR_LAST; j++)
			dc.windowcolors[i][j] = ealloccolor(config.windowcolors[i][j]);
	dc.desktopselbg = ealloccolor(config.desktopselbg);
	dc.desktopbg = ealloccolor(config.desktopbg);
	dc.separator = ealloccolor(config.separator);
}

/* select events from root window */
static void
initroot(void)
{
	XChangeWindowAttributes(dpy, root, CWEventMask,
		&(XSetWindowAttributes){
			.event_mask = StructureNotifyMask | PropertyChangeMask,
		}
	);
}

/* compute prompt geometry and create prompt window */
static void
initpager(int argc, char *argv[])
{
	XWMHints *wmhints;
	XSizeHints *shints;
	XClassHint *chint;

	/* zero pager fields */
	pager.active = NULL;
	pager.pix = None;

	/* create pager window */
	pager.win = XCreateWindow(
		dpy, root, 0, 0, 1, 1, 0,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWEventMask | CWBackPixel,
		&(XSetWindowAttributes){
			.event_mask = ExposureMask | StructureNotifyMask,
			.background_pixel = dc.desktopbg,
		}
	);

	/* get initial number of desktops */
	setndesktops();
	setcurrdesktop();

	/* compute pager layout */
	if (config.ncols <= 0 && config.nrows <= 0)
		config.ncols = DEF_NCOLS;
	if (config.ncols > 0 && config.nrows <= 0)
		config.nrows = (pager.ndesktops + (pager.ndesktops % config.ncols)) / config.ncols;
	if (config.ncols <= 0 && config.nrows > 0)
		config.ncols = (pager.ndesktops + (pager.ndesktops % config.nrows)) / config.nrows;

	/* compute pager size */
	if (pager.w <= 0 && pager.h <= 0)
		pager.w = DEF_WIDTH;
	if (pager.w > 0 && pager.h <= 0)
		pager.h = SEPARATOR(config.nrows) + (config.nrows * (pager.w - SEPARATOR(config.ncols)) * screenh) / (config.ncols * screenw);
	if (pager.w <= 0 && pager.h > 0)
		pager.w = SEPARATOR(config.ncols) + (config.ncols * (pager.h - SEPARATOR(config.nrows)) * screenw) / (config.nrows * screenh);

	/* compute user-defined pager position */
	if (config.xnegative)
		config.x += screenw - pager.w;
	if (config.ynegative)
		config.y += screenh - pager.h;

	/* commit pager window geometry */
	XMoveResizeWindow(dpy, pager.win, config.x, config.y, pager.w, pager.h);

	/* create graphic contexts */
	dc.gc = XCreateGC(dpy, pager.win, GCFillStyle | GCLineStyle,
		&(XGCValues){
			.fill_style = FillSolid,
			.line_style = LineOnOffDash,
		}
	);

	/* set window hints */
	wmhints = NULL;
	if ((chint = XAllocClassHint()) == NULL)
		errx(1, "XAllocClassHint");
	chint->res_name = *argv;
	chint->res_class = PROGNAME;
	if ((shints = XAllocSizeHints()) == NULL)
		errx(1, "XAllocSizeHints");
	shints->flags = PMaxSize | PMinSize;
	if (config.userplaced)
		shints->flags |= USPosition;
	shints->min_width = shints->max_width = pager.w;
	shints->min_height = shints->max_height = pager.h;
	if (wflag) {
		if ((wmhints = XAllocWMHints()) == NULL)
			errx(1, "XAllocWMHints");
		wmhints->flags = IconWindowHint | StateHint;
		wmhints->initial_state = WithdrawnState;
		wmhints->icon_window = pager.win;
	}
	XmbSetWMProperties(dpy, pager.win, PROGNAME, PROGNAME, argv, argc, shints, wmhints, chint);
	XFree(chint);
	XFree(shints);
	XFree(wmhints);

	/* set WM protocols */
	XSetWMProtocols(dpy, pager.win, &atoms[WM_DELETE_WINDOW], 1);

	/* set window attributes */
	//XSetWindowBackground(dpy, pager.win, dc.desktopbg);

	/* get initial client list */
	setdeskgeom();
	setclients();
	setactive();
	drawpager();
	mapdesktops();
	mapclients();

	/* map window */
	XMapWindow(dpy, pager.win);
}

/* if win is a mini-client-window, focus its client; if it's a mini-desk-window, change to its desktop */
static void
focus(Window win)
{
	size_t i;

	for (i = 0; i < pager.ndesktops; i++) {
		if (win == pager.desktops[i]->miniwin) {
			clientmsg(None, atoms[_NET_CURRENT_DESKTOP], i, CurrentTime, 0, 0, 0);
			return;
		}
	}
	for (i = 0; i < pager.nclients; i++) {
		if (pager.clients[i] != NULL && win == pager.clients[i]->miniwin) {
			clientmsg(pager.clients[i]->clientwin, atoms[_NET_ACTIVE_WINDOW], 2, CurrentTime, 0, 0, 0);
			return;
		}
	}
}

/* act upon mouse button presses; basically focus window and change to its desktop */
static void
xeventbuttonrelease(XEvent *e)
{
	XButtonEvent *ev;

	ev = &e->xbutton;
	if (ev->button == Button1) {
		focus(ev->window);
	}
}

/* close paginator when receiving WM_DELETE_WINDOW */
static void
xeventclientmessage(XEvent *e)
{
	XClientMessageEvent *ev;

	ev = &e->xclient;
	if ((Atom)ev->data.l[0] == atoms[WM_DELETE_WINDOW]) {
		running = 0;
	}
}

/* act upon configuration change (paginator watches clients' configuration changes) */
static void
xeventconfigurenotify(XEvent *e)
{
	XConfigureEvent *ev;
	size_t i;

	ev = &e->xconfigure;
	if (ev->window == root) {
		screenw = ev->width;
		screenh = ev->height;
	} else if (ev->window == pager.win) {
		setpagersize(ev->width, ev->height);
		setdeskgeom();
		drawpager();
		mapdesktops();
		mapclients();
	} else {
		for (i = 0; i < pager.nclients; i++) {
			if (pager.clients[i] != NULL && ev->window == pager.clients[i]->clientwin) {
				mapclient(pager.clients[i]);
				break;
			}
		}
	}
}

/* act upon property change (paginator watches root's and clients' property changes) */
static void
xeventpropertynotify(XEvent *e)
{
	struct Client *cp, *prevactive;
	XPropertyEvent *ev;

	ev = &e->xproperty;
	if (ev->atom == atoms[_NET_CLIENT_LIST_STACKING]) {
		if (setclients()) {
			mapclients();
		}
	} else if (ev->atom == atoms[_NET_ACTIVE_WINDOW]) {
		prevactive = pager.active;
		setactive();
		if (prevactive != pager.active) {
			drawclient(prevactive);
			drawclient(pager.active);
		}
	} else if (ev->atom == atoms[_NET_CURRENT_DESKTOP]) {
		setcurrdesktop();
		mapdesktops();
	} else if (ev->atom == atoms[_NET_SHOWING_DESKTOP]) {
		setshowingdesk();
		mapclients();
	} else if (ev->atom == atoms[_NET_NUMBER_OF_DESKTOPS]) {
		setndesktops();
		drawpager();
		mapdesktops();
	} else if (ev->atom == atoms[_NET_WM_STATE]) {
		if ((cp = getclient(ev->window)) != NULL) {
			sethiddenstate(cp);
			mapclient(cp);
		}
	} else if (ev->atom == atoms[_NET_WM_DESKTOP]) {
		if ((cp = getclient(ev->window)) != NULL) {
			setdesktop(cp);
			reparentclient(cp);
		}
	}
}

/* redraw the pager window when exposed */
static void
xeventexpose(XEvent *e)
{
	XExposeEvent *ev;

	ev = &e->xexpose;
	if (ev->window == pager.win) {
		XCopyArea(dpy, pager.pix, pager.win, dc.gc, 0, 0, pager.w, pager.h, 0, 0);
	}
}

/* paginator: a X11 desktop pager */
int
main(int argc, char *argv[])
{
	XEvent ev;
	void (*xevents[LASTEvent])(XEvent *) = {
		[Expose]                = xeventexpose,
		[ButtonRelease]         = xeventbuttonrelease,
		[ConfigureNotify]       = xeventconfigurenotify,
		[ClientMessage]         = xeventclientmessage,
		[PropertyNotify]        = xeventpropertynotify,
	};

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	root = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	depth = DefaultDepth(dpy, screen);
	screenw = DisplayWidth(dpy, screen);
	screenh = DisplayHeight(dpy, screen);
	xerrorxlib = XSetErrorHandler(xerror);
	getoptions(argc, argv);
	initatoms();
	initcolors();
	initroot();
	initpager(argc, argv);
	while (running && !XNextEvent(dpy, &ev))
		if (xevents[ev.type] != NULL)
			(*xevents[ev.type])(&ev);
	cleandesktops();
	cleanclients();
	cleandc();
	XCloseDisplay(dpy);
	return 0;
}
