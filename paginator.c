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
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#define PROGNAME        "Paginator"
#define ICON_SIZE       16              /* preferred icon size */
#define MAX_DESKTOPS    100             /* maximum number of desktops */
#define DEF_WIDTH       125             /* default width for the pager */
#define DEF_NCOLS       1               /* default number of columns */
#define SEPARATOR_SIZE  1               /* size of the line between minidesktops */
#define ALLDESKTOPS     0xFFFFFFFF
#define PAGER_ACTION    ((long)(1 << 14))

/* colors */
enum {
	COLOR_MID,
	COLOR_LIGHT,
	COLOR_DARK,
	COLOR_LAST
};

/* decoration style */
enum {
	STYLE_ACTIVE,
	STYLE_INACTIVE,
	STYLE_URGENT,
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
	_NET_WM_STATE_STICKY,
	_NET_WM_STATE_DEMANDS_ATTENTION,

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
	Window *miniwins;
	Picture icon;
	size_t nminiwins;
	size_t nmappedwins;
	unsigned long desk;
	int cx, cy, cw, ch;
	int x, y, w, h;
	int ishidden;
	int ismapped;
	int isurgent;
};

/* the pager */
struct Pager {
	struct Desktop **desktops;
	struct Client **clients;
	struct Client *active;
	Window win;
	size_t nclients;
	unsigned long ndesktops;
	unsigned long currdesktop;
	int nrows, ncols;
	int cellw, cellh;
	int w, h;
	int showingdesk;
};

/* configuration structure */
struct Config {
	int nrows, ncols;                       /* number of rows and columns in the pager grid */
	int x, y;                               /* user-defined pager placement */
	int xnegative, ynegative;               /* whether user-defined x and y placement are negative */
	int userplaced;                         /* whether user defined pager placement */
	int shadowthickness;                    /* thickness of the miniwindow shadows */
	enum Orientation orient;                /* desktop orientation */
	enum StartingCorner corner;             /* desktop initial corner */

	/* color names */
	const char *desktopselbg;
	const char *desktopbg;
	const char *separator;
	const char *windowcolors[STYLE_LAST][COLOR_LAST];
};

/* X stuff */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static struct DC dc;
static Atom atoms[ATOM_LAST];
static Display *dpy;
static Visual *visual;
static XrmDatabase xdb = NULL;
static Window root;
static Colormap colormap;
static unsigned int depth;
static int screen;
static int screenw, screenh;
static char *xrm = NULL;

/* window attributes for miniwindows */
static XSetWindowAttributes miniswa = {
	.event_mask = ButtonPressMask | ButtonReleaseMask
};

/* whether we're running */
static int running = 1;

/* command-line flags */
static int wflag = 0;                   /* whether to start in withdrawn mode */
static int iflag = 0;                   /* whether to draw icons */

/* global pager variable */
static struct Pager pager = {
	.desktops = NULL,
	.clients = NULL,
	.active = NULL,
	.win = None,
	.nclients = 0,
	.ndesktops = 0,
	.currdesktop = 0,
	.showingdesk = 0,
};

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
	    (e->request_code == X_FreePixmap && e->error_code == BadPixmap) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadMatch) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadValue))
		return 0;
	return xerrorxlib(dpy, e);
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	long n;
	char *type;
	XrmValue xval;

	if (xrm == NULL || xdb == NULL)
		return;

	if (XrmGetResource(xdb, "paginator.activeBackground", "*", &type, &xval) == True)
		config.windowcolors[STYLE_ACTIVE][COLOR_MID] = xval.addr;
	if (XrmGetResource(xdb, "paginator.activeTopShadowColor", "*", &type, &xval) == True)
		config.windowcolors[STYLE_ACTIVE][COLOR_LIGHT] = xval.addr;
	if (XrmGetResource(xdb, "paginator.activeBottomShadowColor", "*", &type, &xval) == True)
		config.windowcolors[STYLE_ACTIVE][COLOR_DARK] = xval.addr;

	if (XrmGetResource(xdb, "paginator.inactiveBackground", "*", &type, &xval) == True)
		config.windowcolors[STYLE_INACTIVE][COLOR_MID] = xval.addr;
	if (XrmGetResource(xdb, "paginator.inactiveTopShadowColor", "*", &type, &xval) == True)
		config.windowcolors[STYLE_INACTIVE][COLOR_LIGHT] = xval.addr;
	if (XrmGetResource(xdb, "paginator.inactiveBottomShadowColor", "*", &type, &xval) == True)
		config.windowcolors[STYLE_INACTIVE][COLOR_DARK] = xval.addr;

	if (XrmGetResource(xdb, "paginator.urgentBackground", "*", &type, &xval) == True)
		config.windowcolors[STYLE_URGENT][COLOR_MID] = xval.addr;
	if (XrmGetResource(xdb, "paginator.urgentTopShadowColor", "*", &type, &xval) == True)
		config.windowcolors[STYLE_URGENT][COLOR_LIGHT] = xval.addr;
	if (XrmGetResource(xdb, "paginator.urgentBottomShadowColor", "*", &type, &xval) == True)
		config.windowcolors[STYLE_URGENT][COLOR_DARK] = xval.addr;

	if (XrmGetResource(xdb, "paginator.background", "*", &type, &xval) == True)
		config.desktopbg = xval.addr;
	if (XrmGetResource(xdb, "paginator.selbackground", "*", &type, &xval) == True)
		config.desktopselbg = xval.addr;
	if (XrmGetResource(xdb, "paginator.separator", "*", &type, &xval) == True)
		config.separator = xval.addr;

	if (XrmGetResource(xdb, "paginator.numColumns", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.ncols = n;
	if (XrmGetResource(xdb, "paginator.numRows", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.nrows = n;
	if (XrmGetResource(xdb, "paginator.shadowThickness", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.shadowthickness = n;

	if (XrmGetResource(xdb, "paginator.orientation", "*", &type, &xval) == True) {
		if (xval.addr[0] == 'h' || xval.addr[0] == 'H') {
			config.orient = _NET_WM_ORIENTATION_HORZ;
		} else if (xval.addr[0] == 'v' || xval.addr[0] == 'V') {
			config.orient = _NET_WM_ORIENTATION_VERT;
		}
	}
	if (XrmGetResource(xdb, "paginator.startingCorner", "*", &type, &xval) == True) {
		if (strcasecmp(xval.addr, "TOPLEFT") == 0) {
			config.corner = _NET_WM_TOPLEFT;
		} else if (strcasecmp(xval.addr, "TOPRIGHT") == 0) {
			config.corner = _NET_WM_TOPRIGHT;
		} else if (strcasecmp(xval.addr, "TOPRIGHT") == 0) {
			config.corner = _NET_WM_BOTTOMLEFT;
		} else if (strcasecmp(xval.addr, "TOPRIGHT") == 0) {
			config.corner = _NET_WM_BOTTOMRIGHT;
		}
	}
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
	if (!XGetGeometry(dpy, wmhints->icon_pixmap, &dw, &x, &y, iconw, iconh, &b, d))
		goto done;
	if (*iconw < 1 || *iconh < 1)
		goto done;
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
	*iconw = *iconh = 0;
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

/* check whether window is urgent */
static int
isurgent(Window win)
{
	XWMHints *wmh;
	int ret;

	ret = 0;
	if ((wmh = XGetWMHints(dpy, win)) != NULL) {
		ret = wmh->flags & XUrgencyHint;
		XFree(wmh);
	}
	return ret;
}

/* return non-zero if window is hidden */
static int
hasstate(Window win, Atom atom)
{
	Atom *as;
	unsigned long natoms, i;
	int retval;

	retval = 0;
	if ((natoms = getatomprop(win, atoms[_NET_WM_STATE], &as)) != 0) {
		for (i = 0; i < natoms; i++) {
			if (as[i] == atom) {
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
	pager.desktops = NULL;
}

/* destroy client's mini-windows */
static void
destroyminiwindows(struct Client *cp)
{
	size_t i;

	if (cp == NULL)
		return;
	for (i = 0; i < cp->nminiwins; i++) {
		if (cp->miniwins[i] != None) {
			XDestroyWindow(dpy, cp->miniwins[i]);
			cp->miniwins[i] = None;
		}
	}
	free(cp->miniwins);
}

/* destroy client's mini-windows and free it */
static void
cleanclient(struct Client *cp)
{
	if (cp == NULL)
		return;
	destroyminiwindows(cp);
	if (cp->icon != None)
		XRenderFreePicture(dpy, cp->icon);
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
	pager.clients = NULL;
}

/* free pager window and pixmap */
static void
cleanpager(void)
{
	XDestroyWindow(dpy, pager.win);
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

/* draw client miniwindow borders */
static void
drawborder(Window win, int w, int h, int style)
{
	XGCValues val;
	XRectangle *recs;
	Pixmap pix;
	int i;

	if (win == None || w <= 0 || h <= 0)
		return;

	w += config.shadowthickness * 2;
	h += config.shadowthickness * 2;
	if ((pix = XCreatePixmap(dpy, win, w, h, depth)) == None)
		return;
	recs = ecalloc(config.shadowthickness * 2 + 1, sizeof(*recs));

	/* draw dark shadow */
	XSetForeground(dpy, dc.gc, dc.windowcolors[style][COLOR_DARK]);
	XFillRectangle(dpy, pix, dc.gc, 0, 0, w, h);

	/* draw light shadow */
	for(i = 0; i < config.shadowthickness; i++) {
		recs[i * 2]     = (XRectangle){
			.x = w - config.shadowthickness + i,
			.y = 0,
			.width = 1,
			.height = h - (i * 2 + 1),
		};
		recs[i * 2 + 1] = (XRectangle){
			.x = 0,
			.y = h - config.shadowthickness + i,
			.width = w - (i * 2 + 1),
			.height = 1,
		};
	}
	recs[config.shadowthickness * 2] = (XRectangle){
		.x = w - config.shadowthickness,
		.y = h - config.shadowthickness,
		.width = config.shadowthickness,
		.height = config.shadowthickness,
	};
	val.foreground = dc.windowcolors[style][COLOR_LIGHT];
	XChangeGC(dpy, dc.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, dc.gc, recs, config.shadowthickness * 2 + 1);

	/* commit pixmap into window borders */
	XSetWindowBorderPixmap(dpy, win, pix);

	XFreePixmap(dpy, pix);
	free(recs);
}

/* redraw client miniwindow background */
static void
drawbackground(Window win, Picture icon, int w, int h, int style)
{
	Picture pic;
	Pixmap pix;

	if (win == None || w <= 0 || h <= 0)
		return;
	if ((pix = XCreatePixmap(dpy, win, w, h, depth)) == None)
		return;
	XSetForeground(dpy, dc.gc, dc.windowcolors[style][COLOR_MID]);
	XFillRectangle(dpy, pix, dc.gc, 0, 0, w, h);
	if (icon != None) {
		pic = XRenderCreatePicture(dpy, pix, XRenderFindVisualFormat(dpy, visual), 0, NULL);
		XRenderComposite(dpy, PictOpOver, icon, None, pic, 0, 0, 0, 0, (w - ICON_SIZE) / 2, (h - ICON_SIZE) / 2, w, h);
	}
	XCopyArea(dpy, pix, win, dc.gc, 0, 0, w, h, 0, 0);
	XSetWindowBackgroundPixmap(dpy, win, pix);
	XFreePixmap(dpy, pix);
}

/* redraw client miniwindow */
static void
drawclient(struct Client *cp)
{
	size_t i;
	int style;

	if (cp == NULL)
		return;
	if (cp == pager.active)
		style = STYLE_ACTIVE;
	else if (cp->isurgent)
		style = STYLE_URGENT;
	else
		style = STYLE_INACTIVE;
	for (i = 0; i < cp->nminiwins; i++) {
		drawborder(cp->miniwins[i], cp->w, cp->h, style);
		drawbackground(cp->miniwins[i], cp->icon, cp->w, cp->h, style);
	}
}

/* set mini-desktops geometry */
static void
setdeskgeom(void)
{
	int x, y, w, h;
	size_t i, xi, yi;

	w = max(1, pager.w - pager.ncols);
	h = max(1, pager.h - pager.nrows);
	for (i = 0; i < pager.ndesktops; i++) {
		xi = yi = i;
		if (config.orient == _NET_WM_ORIENTATION_HORZ)
			yi /= pager.ncols;
		else
			xi /= pager.nrows;
		if (config.corner == _NET_WM_TOPRIGHT) {
			x = pager.ncols - 1 - xi % pager.ncols;
			y = yi % pager.nrows;
		} else if (config.corner == _NET_WM_BOTTOMLEFT) {
			x = xi % pager.ncols;
			y = pager.nrows - 1 - yi % pager.nrows;
		} else if (config.corner == _NET_WM_BOTTOMRIGHT) {
			x = pager.ncols - 1 - xi % pager.ncols;
			y = pager.nrows - 1 - yi % pager.nrows;
		} else {
			x = xi % pager.ncols;
			y = yi % pager.nrows;
		}
		pager.desktops[i]->x = w * x / pager.ncols + x;
		pager.desktops[i]->y = h * y / pager.nrows + y;
		pager.desktops[i]->w = max(1, w * (x + 1) / pager.ncols - w * x / pager.ncols);
		pager.desktops[i]->h = max(1, h * (y + 1) / pager.nrows - h * y / pager.nrows);
		XMoveResizeWindow(
			dpy, pager.desktops[i]->miniwin,
			pager.desktops[i]->x, pager.desktops[i]->y,
			pager.desktops[i]->w, pager.desktops[i]->h
		);
	}
}

/* unmap client miniwindow */
static void
unmapclient(struct Client *cp)
{
	size_t i;

	if (cp == NULL || !cp->ismapped)
		return;
	for (i = 0; i < cp->nminiwins; i++)
		XUnmapWindow(dpy, cp->miniwins[i]);
	cp->ismapped = 0;
}

/* remap client miniwindow into its desktop miniwindow */
static void
reparentclient(struct Client *cp)
{
	size_t i;

	if (cp == NULL || cp->desk < 0 || (cp->desk >= pager.ndesktops && cp->desk != ALLDESKTOPS))
		return;
	if (cp->desk == ALLDESKTOPS) {
		for (i = 0; i < cp->nminiwins; i++) {
			XReparentWindow(dpy, cp->miniwins[i], pager.desktops[i]->miniwin, cp->x, cp->y);
		}
	} else if (cp->nminiwins == 1) {
		XReparentWindow(dpy, cp->miniwins[0], pager.desktops[cp->desk]->miniwin, cp->x, cp->y);
	}
}

/* draw pager pixmap */
static void
drawpager(void)
{
	Pixmap pix;
	int x, y;
	int w, h;
	int i;

	if (pager.w == 0 || pager.h == 0 ||
	    (pix = XCreatePixmap(dpy, pager.win, pager.w, pager.h, depth)) == None)
		return;
	XSetForeground(dpy, dc.gc, dc.desktopbg);
	XFillRectangle(dpy, pix, dc.gc, 0, 0, pager.w, pager.h);
	XSetForeground(dpy, dc.gc, dc.separator);
	w = pager.w - pager.ncols;
	h = pager.h - pager.nrows;
	for (i = 1; i < pager.ncols; i++) {
		x = w * i / pager.ncols + i - 1;
		XDrawLine(dpy, pix, dc.gc, x, 0, x, pager.h);
	}
	for (i = 1; i < pager.nrows; i++) {
		y = h * i / pager.nrows + i - 1;
		XDrawLine(dpy, pix, dc.gc, 0, y, pager.w, y);
	}
	XCopyArea(dpy, pix, pager.win, dc.gc, 0, 0, pager.w, pager.h, 0, 0);
	XSetWindowBackgroundPixmap(dpy, pager.win, pix);
	XFreePixmap(dpy, pix);
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

/* set size of client's miniwindow and map client's miniwindow */
static void
configureclient(struct Desktop *dp, struct Client *cp, size_t i)
{
	if (cp == NULL)
		return;

	cp->x = cp->cx * dp->w / screenw;
	cp->y = cp->cy * dp->h / screenh;
	cp->w = max(1, cp->cw * dp->w / screenw - 2 * config.shadowthickness);
	cp->h = max(1, cp->ch * dp->h / screenh - 2 * config.shadowthickness);
	drawclient(cp);
	XMoveResizeWindow(dpy, cp->miniwins[i], cp->x, cp->y, cp->w, cp->h);
	if (cp->ismapped || cp->nmappedwins != cp->nminiwins) {
		XMapWindow(dpy, cp->miniwins[i]);
	}
}

/* get size of client's window and call routine to map client's miniwindow */
static void
mapclient(struct Client *cp)
{
	Window dw;
	size_t i;
	unsigned int du, b;
	int x, y;

	if (cp == NULL)
		return;
	if (pager.showingdesk || cp->ishidden || cp->desk < 0 || (cp->desk >= pager.ndesktops && cp->desk != ALLDESKTOPS)) {
		unmapclient(cp);
		return;
	}
	XGetGeometry(dpy, cp->clientwin, &dw, &x, &y, &cp->cw, &cp->ch, &b, &du);
	XTranslateCoordinates(dpy, cp->clientwin, root, x, y, &cp->cx, &cp->cy, &dw);
	if (cp->desk == ALLDESKTOPS) {
		for (i = 0; i < cp->nminiwins; i++) {
			configureclient(pager.desktops[i], cp, i);
		}
	} else if (cp->nminiwins == 1) {
		configureclient(pager.desktops[cp->desk], cp, 0);
	}
	cp->nmappedwins = cp->nminiwins;
	cp->ismapped = 1;
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
			reparentclient(cp);
		}
	}
}

/* get desktop geometry; redraw pager and remap all mini windows */
static void
mapdrawall(void)
{
	setdeskgeom();
	drawpager();
	mapdesktops();
	mapclients();
}

/* set pager size */
static int
setpagersize(int w, int h)
{
	int ret;

	ret = (pager.w != w || pager.h != h);
	pager.w = w;
	pager.h = h;
	return ret;
}

/* update number of desktops */
static void
setndesktops(void)
{
	size_t i;
	unsigned long prevndesktops;

	prevndesktops = pager.ndesktops;
	cleandesktops();
	cleanclients();
	pager.ndesktops = getcardprop(root, atoms[_NET_NUMBER_OF_DESKTOPS]);
	pager.nrows = max(1, config.nrows);
	pager.ncols = max(1, config.ncols);
	pager.nrows = max(1, (pager.ndesktops + (pager.ndesktops % pager.ncols)) / pager.ncols);
	pager.ncols = max(1, (pager.ndesktops + (pager.ndesktops % pager.nrows)) / pager.nrows);
	pager.desktops = ecalloc(pager.ndesktops, sizeof(*pager.desktops));
	for (i = 0; i < pager.ndesktops; i++) {
		pager.desktops[i] = emalloc(sizeof(*pager.desktops[i]));
		pager.desktops[i]->miniwin = XCreateWindow(
			dpy, pager.win, 0, 0, 1, 1, 0,
			CopyFromParent, CopyFromParent, CopyFromParent,
			CWEventMask, &miniswa
		);
	}
	if (prevndesktops == 0 || prevndesktops != pager.ndesktops) {
		mapdrawall();
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

/* set hidden state of given client; then map or unmap it */
static void
sethiddenandmap(struct Client *cp, int remap)
{
	int previshidden;

	if (cp == NULL)
		return;
	previshidden = cp->ishidden;
	cp->ishidden = hasstate(cp->clientwin, atoms[_NET_WM_STATE_HIDDEN]);
	if (remap && previshidden != cp->ishidden) {
		mapclient(cp);
	}
}

/* set client's urgency */
static void
seturgency(struct Client *cp)
{
	int prevurgency;

	if (cp == NULL)
		return;
	prevurgency = cp->isurgent;
	cp->isurgent = 0;
	if (hasstate(cp->clientwin, atoms[_NET_WM_STATE_DEMANDS_ATTENTION]) || isurgent(cp->clientwin))
		cp->isurgent = 1;
	if (prevurgency != cp->isurgent) {
		drawclient(cp);
	}
}

/* set client's desktop number; return non-zero if it has changed */
static int
setdesktop(struct Client *cp)
{
	unsigned long prevdesk;
	size_t i;

	if (cp == NULL)
		return 0;
	prevdesk = cp->desk;
	cp->desk = getcardprop(cp->clientwin, atoms[_NET_WM_DESKTOP]);
	if (hasstate(cp->clientwin, atoms[_NET_WM_STATE_STICKY]))
		cp->desk = ALLDESKTOPS;
	if (cp->nminiwins == 0 || prevdesk != cp->desk) {
		if (cp->nminiwins == 0 || prevdesk == ALLDESKTOPS || cp->desk == ALLDESKTOPS) {
			destroyminiwindows(cp);
			cp->nminiwins = (cp->desk == ALLDESKTOPS) ? pager.ndesktops : 1;
			cp->miniwins = ecalloc(cp->nminiwins, sizeof(*cp->miniwins));
			for (i = 0; i < cp->nminiwins; i++) {
				cp->miniwins[i] = XCreateWindow(
					dpy, pager.win, 0, 0, 1, 1, config.shadowthickness,
					CopyFromParent, CopyFromParent, CopyFromParent,
					CWEventMask, &miniswa
				);
			}
		}
		reparentclient(cp);
		return 1;
	}
	return 0;
}

/* update list of clients; remap mini-client-windows if list has changed */
static void
setclients(void)
{
	struct Client **clients;
	struct Client *oldcp;
	Window *wins;
	size_t nclients;
	size_t i;
	int changed;

	changed = 0;
	nclients = getwinprop(root, atoms[_NET_CLIENT_LIST_STACKING], &wins);
	clients = ecalloc(nclients, sizeof(*clients));
	for (i = 0; i < nclients; i++) {
		oldcp = pager.nclients > 0 ? pager.clients[i] : NULL;
		clients[i] = getdelclient(wins[i]);
		if (oldcp == NULL || clients[i] == NULL || clients[i] != oldcp)
			changed = 1;
		if (clients[i] == NULL) {
			clients[i] = emalloc(sizeof(*clients[i]));
			*clients[i] = (struct Client) {
				.ishidden = 0,
				.ismapped = 0,
				.isurgent = 0,
				.nminiwins = 0,
				.nmappedwins = 0,
				.miniwins = NULL,
			};
			clients[i]->clientwin = wins[i];
			XSelectInput(dpy, clients[i]->clientwin, StructureNotifyMask | PropertyChangeMask);
			clients[i]->icon = iflag ? geticonprop(clients[i]->clientwin) : None;
		}
		sethiddenandmap(clients[i], 0);
		if (setdesktop(clients[i])) {
			changed = 1;
		}
	}
	cleanclients();
	pager.clients = clients;
	pager.nclients = nclients;
	XFree(wins);
	if (changed) {
		mapclients();
	}
}

/* update showing desktop state */
static void
setshowingdesk(void)
{
	int prevshowingdesk;

	prevshowingdesk = pager.showingdesk;
	pager.showingdesk = getcardprop(root, atoms[_NET_SHOWING_DESKTOP]);
	if (prevshowingdesk != pager.showingdesk) {
		mapclients();
	}
}

/* set current desktop */
static void
setcurrdesktop(void)
{
	unsigned long prevdesktop;

	prevdesktop = pager.currdesktop;
	pager.currdesktop = getcardprop(root, atoms[_NET_CURRENT_DESKTOP]);
	if (prevdesktop != pager.currdesktop) {
		mapdesktops();
	}
}

/* set active window */
static void
setactive(void)
{
	struct Client *prevactive;
	Window *wins;

	prevactive = pager.active;
	pager.active = NULL;
	if (getwinprop(root, atoms[_NET_ACTIVE_WINDOW], &wins) > 0) {
		pager.active = getclient(*wins);
		XFree(wins);
	}
	if (prevactive != pager.active) {
		drawclient(prevactive);
		drawclient(pager.active);
	}
}

/* intern atoms */
static void
initatoms(void)
{
	char *atomnames[ATOM_LAST] = {
		[WM_DELETE_WINDOW]                = "WM_DELETE_WINDOW",
		[_NET_ACTIVE_WINDOW]              = "_NET_ACTIVE_WINDOW",
		[_NET_CLIENT_LIST_STACKING]       = "_NET_CLIENT_LIST_STACKING",
		[_NET_CURRENT_DESKTOP]            = "_NET_CURRENT_DESKTOP",
		[_NET_WM_DESKTOP]                 = "_NET_WM_DESKTOP",
		[_NET_DESKTOP_LAYOUT]             = "_NET_DESKTOP_LAYOUT",
		[_NET_SHOWING_DESKTOP]            = "_NET_SHOWING_DESKTOP",
		[_NET_MOVERESIZE_WINDOW]          = "_NET_MOVERESIZE_WINDOW",
		[_NET_NUMBER_OF_DESKTOPS]         = "_NET_NUMBER_OF_DESKTOPS",
		[_NET_WM_ICON]                    = "_NET_WM_ICON",
		[_NET_WM_STATE]                   = "_NET_WM_STATE",
		[_NET_WM_STATE_HIDDEN]            = "_NET_WM_STATE_HIDDEN",
		[_NET_WM_STATE_STICKY]            = "_NET_WM_STATE_STICKY",
		[_NET_WM_STATE_DEMANDS_ATTENTION] = "_NET_WM_STATE_DEMANDS_ATTENTION",
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

/* create prompt window */
static void
initpager(int argc, char *argv[])
{
	XWMHints *wmhints;
	XSizeHints *shints;
	XClassHint *chint;

	/* create pager window */
	pager.win = XCreateWindow(
		dpy, root, 0, 0, 1, 1, 0,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWEventMask | CWBackPixel,
		&(XSetWindowAttributes){
			.event_mask = StructureNotifyMask,
			.background_pixel = dc.desktopbg,
		}
	);

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
	shints->flags = (config.userplaced) ? USPosition : 0;
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
}

/* set initial list of desktops and clients */
static void
initclients(void)
{
	/* get initial number of desktops */
	setndesktops();

	/* compute initial pager size */
	if (pager.w <= 0 && pager.h <= 0)
		pager.w = DEF_WIDTH;
	if (pager.w > 0 && pager.h <= 0)
		pager.h = (SEPARATOR_SIZE * pager.nrows) + (pager.nrows * (pager.w - (SEPARATOR_SIZE * pager.ncols)) * screenh) / (pager.ncols * screenw);
	if (pager.w <= 0 && pager.h > 0)
		pager.w = (SEPARATOR_SIZE * pager.ncols) + (pager.ncols * (pager.h - (SEPARATOR_SIZE * pager.nrows)) * screenw) / (pager.nrows * screenh);

	/* compute user-defined pager position */
	if (config.xnegative)
		config.x += screenw - pager.w;
	if (config.ynegative)
		config.y += screenh - pager.h;

	/* commit pager window geometry */
	XMoveResizeWindow(dpy, pager.win, config.x, config.y, pager.w, pager.h);

	/* get initial client list */
	setdeskgeom();
	drawpager();
	setcurrdesktop();
	setclients();
	setactive();
	mapdesktops();
	mapclients();

	/* map window */
	XMapWindow(dpy, pager.win);
}

/* if win is a mini-client-window, focus its client; if it's a mini-desk-window, change to its desktop */
static void
focus(Window win)
{
	size_t i, j;

	for (i = 0; i < pager.ndesktops; i++) {
		if (win == pager.desktops[i]->miniwin) {
			clientmsg(None, atoms[_NET_CURRENT_DESKTOP], i, CurrentTime, 0, 0, 0);
			return;
		}
	}
	for (i = 0; i < pager.nclients; i++) {
		if (pager.clients[i] == NULL)
			continue;
		for (j = 0; j < pager.clients[i]->nminiwins; j++) {
			if (win == pager.clients[i]->miniwins[j]) {
				clientmsg(pager.clients[i]->clientwin, atoms[_NET_ACTIVE_WINDOW], 2, CurrentTime, 0, 0, 0);
				return;
			}
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

	ev = &e->xconfigure;
	if (ev->window == root) {
		/* screen size changed (eg' a new monitor was plugged-in) */
		screenw = ev->width;
		screenh = ev->height;
		mapdrawall();
	} else {
		if (ev->window == pager.win && setpagersize(ev->width, ev->height)) {
			/* the pager window may have been resized */
			mapdrawall();
		}

		/* a client window window was resized */
		mapclient(getclient(ev->window));
	}
}

/* act upon property change (paginator watches root's and clients' property changes) */
static void
xeventpropertynotify(XEvent *e)
{
	struct Client *cp;
	XPropertyEvent *ev;

	/*
	 * This routine is called when the value of a property has been
	 * reset. If a known property was detected to be the reset one,
	 * its internal value is reset (with a set*() function).
	 *
	 * Note that the value can by some reason not have changed, and
	 * be equal to the previously stored one. In that case, the set
	 * function will do nothing.
	 *
	 * But, if the value has been changed from the internal one, it
	 * will then call the proper remapping or redrawing function to
	 * remap or redraw the client and/or desktop miniwindows.
	 */
	ev = &e->xproperty;
	if (ev->atom == atoms[_NET_CLIENT_LIST_STACKING]) {
		/* the list of windows was reset */
		setclients();
	} else if (ev->atom == atoms[_NET_ACTIVE_WINDOW]) {
		/* the active window value was reset */
		setactive();
	} else if (ev->atom == atoms[_NET_CURRENT_DESKTOP]) {
		/* the current desktop value was reset */
		setcurrdesktop();
	} else if (ev->atom == atoms[_NET_SHOWING_DESKTOP]) {
		/* the value of the "showing desktop" state was reset */
		setshowingdesk();
	} else if (ev->atom == atoms[_NET_NUMBER_OF_DESKTOPS]) {
		/* the number of desktops value was reset */
		setndesktops();
		setclients();
	} else if (ev->atom == atoms[_NET_WM_STATE]) {
		/* the list of states of a window (which may or may not include a relevant state) was reset */
		cp = getclient(ev->window);
		sethiddenandmap(cp, 1);
		setdesktop(cp);
		seturgency(cp);
		mapclient(cp);
	} else if (ev->atom == atoms[_NET_WM_DESKTOP]) {
		/* the desktop of a window was reset */
		cp = getclient(ev->window);
		setdesktop(cp);
		mapclient(cp);
	} else if (ev->atom == XA_WM_HINTS) {
		/* the urgency state of a window was reset */
		cp = getclient(ev->window);
		seturgency(cp);
	}
}

/* paginator: a X11 desktop pager */
int
main(int argc, char *argv[])
{
	XEvent ev;
	void (*xevents[LASTEvent])(XEvent *) = {
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
	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);
	getresources();
	getoptions(argc, argv);
	initatoms();
	initcolors();
	initroot();
	initpager(argc, argv);
	initclients();
	while (running && !XNextEvent(dpy, &ev))
		if (xevents[ev.type] != NULL)
			(*xevents[ev.type])(&ev);
	cleandesktops();
	cleanclients();
	cleanpager();
	cleandc();
	XCloseDisplay(dpy);
	return 0;
}
