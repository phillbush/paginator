#include <err.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/xpm.h>
#include <X11/extensions/Xrender.h>

#include "x.xpm"

#define APP_CLASS       "Paginator"
#define APP_NAME        "paginator"
#define MAX_VALUE       32767   /* 2^15-1 */
#define ICON_SIZE       16
#define ALLDESKTOPS     0xFFFFFFFF
#define PAGER_ACTION    2
#define FLAG(f, b)      (((f) & (b)) == (b))

#define ATOMS                            \
	X(WM_DELETE_WINDOW)              \
	X(_NET_ACTIVE_WINDOW)            \
	X(_NET_CLIENT_LIST_STACKING)     \
	X(_NET_CURRENT_DESKTOP)          \
	X(_NET_WM_DESKTOP)               \
	X(_NET_DESKTOP_LAYOUT)           \
	X(_NET_SHOWING_DESKTOP)          \
	X(_NET_MOVERESIZE_WINDOW)        \
	X(_NET_NUMBER_OF_DESKTOPS)       \
	X(_NET_WM_ICON)                  \
	X(_NET_WM_STATE)                 \
	X(_NET_WM_STATE_HIDDEN)          \
	X(_NET_WM_STATE_STICKY)          \
	X(_NET_WM_STATE_DEMANDS_ATTENTION)

#define NCOLORS         17      /* number of color resources */
#define RESOURCES                                                                       \
	/* ENUM             CLASS                NAME                         DEFAULT */\
	/* color resources MUST be listed first; values are RGB channels              */\
	X(RES_ACTIVE_BG,    "Background",        "activeBackground",          0x000000 )\
	X(RES_ACTIVE_BOR,   "BorderColor",       "activeBorderColor",         0x000000 )\
	X(RES_ACTIVE_TOP,   "TopShadowColor",    "activeTopShadowColor",      0xB6B6B6 )\
	X(RES_ACTIVE_BOT,   "BottomShadowColor", "activeBottomShadowColor",   0x616161 )\
	X(RES_URGENT_BG,    "Background",        "urgentBackground",          0xFC6161 )\
	X(RES_URGENT_BOR,   "BorderColor",       "urgentBorderColor",         0x000000 )\
	X(RES_URGENT_TOP,   "TopShadowColor",    "urgentTopShadowColor",      0xF7D9D9 )\
	X(RES_URGENT_BOT,   "BottomShadowColor", "urgentBottomShadowColor",   0x9B1D1D )\
	X(RES_INACTIVE_BG,  "Background",        "inactiveBackground",        0xAAAAAA )\
	X(RES_INACTIVE_BOR, "BorderColor",       "inactiveBorderColor",       0x000000 )\
	X(RES_INACTIVE_TOP, "TopShadowColor",    "inactiveTopShadowColor",    0xFFFFFF )\
	X(RES_INACTIVE_BOT, "BottomShadowColor", "inactiveBottomShadowColor", 0x555555 )\
	X(RES_DESK_BG,      "Background",        "desktopBackground",         0x505075 )\
	X(RES_DESK_BOR,     "SeparatorColor",    "separatorColor",            0x000000 )\
	X(RES_DESK_TOP,     "TopShadowColor",    "frameTopShadowColor",       0x000000 )\
	X(RES_DESK_BOT,     "BottomShadowColor", "frameBottomShadowColor",    0xFFFFFF )\
	X(RES_DESK_FG,      "Background",        "currentDesktopBackground",  0xAAAAAA )\
	/* width resources MUST be listed next; value is width in pixels              */\
	X(RES_FRAME,        "ShadowThickness",   "frameShadowThickness",      1        )\
	X(RES_BORDER,       "BorderWidth",       "borderWidth",               1        )\
	X(RES_SHADOW,       "ShadowThickness",   "shadowThickness",           1        )\
	X(RES_SEPARATOR,    "SeparatorWidth",    "separatorWidth",            1        )\
	/* geometry resources; values are width and height in pixels                  */\
	X(RES_GEOMETRY,     "Geometry",          "geometry",                  58       )\

#define MOUSEEVENTMASK  (ButtonReleaseMask | PointerMotionMask)

enum Atom {
#define X(atom) atom,
	ATOMS
	NATOMS
#undef  X
};

enum Resource {
#define X(resource, class, name, value) resource,
	RESOURCES
	NRESOURCES
#undef  X
};

enum Width {
	FRAME_WIDTH,
	BORDER_WIDTH,
	SHADOW_WIDTH,
	SEPARATOR_WIDTH,
	NBORDERS,
};

enum Colors {
	COLOR_BG     = 0,
	COLOR_BOR    = 1,
	COLOR_TOP    = 2,
	COLOR_BOT    = 3,
	COLOR_FG     = 4,       /* extra, for DESKTOP only */

	SCM_ACTIVE   = 0,
	SCM_URGENT   = 4,
	SCM_INACTIVE = 8,
	SCM_DESKTOP  = 12,
};

enum Orientation {
	_NET_WM_ORIENTATION_HORZ = 0,
	_NET_WM_ORIENTATION_VERT = 1,
};

enum StartingCorner {
	_NET_WM_TOPLEFT     = 0,
	_NET_WM_TOPRIGHT    = 1,
	_NET_WM_BOTTOMRIGHT = 2,
	_NET_WM_BOTTOMLEFT  = 3,
};

typedef unsigned long Cardinal;

typedef struct {
	Pixmap          pixmap;
	Picture         picture;
	XRenderColor    channels;
} Color;

typedef struct {
	XrmClass        class;
	XrmName         name;
} Resource;

typedef struct {
	Window miniwin;
	XRectangle geometry;
} Desktop;

typedef struct {
	/* client window */
	Window          clientwin;
	XRectangle      clientgeom;

	/* miniature windows (one for each desktop) */
	Window         *miniwins;
	XRectangle     *minigeoms;

	Picture         icon;
	Cardinal        desk;
	bool            ishidden;
	bool            isurgent;
} Client;

typedef struct {
	Display        *display;
	bool            running;

	/* root window */
	Window          root;
	XRectangle      rootgeom;

	/* pager window */
	Window          window;
	XRectangle      geometry;
	int             geomflags;
	int             borders[NBORDERS];

	/* graphics */
	unsigned int    depth;
	Colormap        colormap;
	Visual         *visual;
	XRenderPictFormat *format, *formatARGB;
	Color           colors[NCOLORS];
	Picture         icon, mask;

	/* grid */
	enum Orientation orient;
	enum StartingCorner corner;
	int             nrows, ncols;

	/* atoms and resources */
	const char     *xrm;
	Resource        application;
	Resource        resources[NRESOURCES];
	Atom            atoms[NATOMS];

	/* desktops */
	Cardinal        ndesktops;
	Cardinal        activedesktop;
	bool            showingdesk;
	Desktop        *desktops;

	/* clients */
	Cardinal        nclients;
	Client         *activeclient;
	Client        **clients;
} Pager;

static void
usage(void)
{
	(void)fprintf(
		stderr,
		"usage: paginator nrows ncols [border border]\n"
	);
	exit(EXIT_FAILURE);
}

static int
xerror(Display *display, XErrorEvent *event)
{
	char msg[128], number[128], req[128];

	if (event->error_code == BadWindow)
		return 0;
	XGetErrorText(display, event->error_code, msg, sizeof(msg));
	(void)snprintf(number, sizeof(number), "%d", event->request_code);
	XGetErrorDatabaseText(
		display,
		"XRequest",
		number,
		"<unknown>",
		req,
		sizeof(req)
	);
	errx(EXIT_FAILURE, "%s (0x%08lX): %s", req, event->resourceid, msg);
	return 0;               /* unreachable */
}

static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

static void
preparewin(Pager *pager, Window window)
{
	XSelectInput(pager->display, window, StructureNotifyMask | PropertyChangeMask);
}

static int
max(int x, int y)
{
	return x > y ? x : y;
}

static char *
gettextprop(Display *display, Window window, Atom prop, Bool delete)
{
	char *text;
	unsigned char *p;
	unsigned long len;
	unsigned long dl;               /* dummy variable */
	int format, status;
	Atom type;

	text = NULL;
	status = XGetWindowProperty(
		display,
		window,
		prop,
		0L,
		0x1FFFFFFF,
		delete,
		AnyPropertyType,
		&type, &format,
		&len, &dl,
		&p
	);
	if (status != Success || len == 0 || p == NULL) {
		goto done;
	}
	if ((text = emalloc(len + 1)) == NULL) {
		goto done;
	}
	memcpy(text, p, len);
	text[len] = '\0';
done:
	XFree(p);
	return text;
}

static Cardinal
getcardprop(Pager *pager, Window window, Atom prop)
{
	int di, status;
	Cardinal card;
	Cardinal dl;
	unsigned char *p = NULL;
	Atom da;

	card = 0;
	status = XGetWindowProperty(
		pager->display,
		window,
		prop,
		0L, 1024,
		False, XA_CARDINAL,
		&da, &di, &dl, &dl, &p
	);
	if (status == Success && p != NULL)
		card = *(Cardinal *)p;
	XFree(p);
	return card;
}

static Cardinal
getatomprop(Pager *pager, Window window, Atom prop, Atom **atoms)
{
	unsigned char *list;
	Cardinal len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */
	int status;

	list = NULL;
	status = XGetWindowProperty(
		pager->display,
		window,
		prop, 0L, 1024, False, XA_ATOM,
		&da, &di, &len, &dl, &list
	);
	if (status != Success || list == NULL) {
		*atoms = NULL;
		return 0;
	}
	*atoms = (Atom *)list;
	return len;
}

static void
clientmsg(Pager *pager, Window win, Atom atom, Cardinal cards[5])
{
	XEvent ev;
	long mask = SubstructureRedirectMask | SubstructureNotifyMask;

	ev.xclient.display = pager->display;
	ev.xclient.type = ClientMessage;
	ev.xclient.serial = 0;
	ev.xclient.send_event = True;
	ev.xclient.message_type = atom;
	ev.xclient.window = win;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = cards[0];
	ev.xclient.data.l[1] = cards[1];
	ev.xclient.data.l[2] = cards[2];
	ev.xclient.data.l[3] = cards[3];
	ev.xclient.data.l[4] = cards[4];
	if (!XSendEvent(pager->display, pager->root, False, mask, &ev)) {
		errx(1, "could not send event");
	}
}

static uint32_t
prealpha(uint32_t p)
{
	uint8_t a = p >> 24u;
	uint32_t rb = (a * (p & 0xFF00FFu)) >> 8u;
	uint32_t g = (a * (p & 0x00FF00u)) >> 8u;
	return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
}

static Cardinal
getwinprop(Pager *pager, Window window, Atom prop, Window **wins)
{
	unsigned char *list;
	Cardinal len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */
	int status;

	list = NULL;
	status = XGetWindowProperty(
		pager->display,
		window,
		prop,
		0L, 1024, False, XA_WINDOW,
		&da, &di, &len, &dl, &list
	);
	if (status != Success || list == NULL) {
		*wins = NULL;
		return 0;
	}
	*wins = (Window *)list;
	return len;
}

static Pixmap
getewmhicon(Pager *pager, Window win, int *iconw, int *iconh, int *d)
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
	if (XGetWindowProperty(pager->display, win, pager->atoms[_NET_WM_ICON], 0L, UINT32_MAX, False, AnyPropertyType, &da, &format, &len, &dl, (unsigned char **)&q) != Success)
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
	if ((img = XCreateImage(pager->display, pager->visual, 32, ZPixmap, 0, datachr, *iconw, *iconh, 32, 0)) == NULL) {
		free(datachr);
		goto done;
	}
	XInitImage(img);
	pix = XCreatePixmap(pager->display, pager->root, *iconw, *iconh, 32);
	gc = XCreateGC(pager->display, pix, 0, NULL);
	XPutImage(pager->display, pix, gc, img, 0, 0, 0, 0, *iconw, *iconh);
	XFreeGC(pager->display, gc);
	XDestroyImage(img);
done:
	XFree(q);
	return pix;
}

static Picture
geticonprop(Pager *pager, Window win)
{
	Pixmap pix;
	Picture pic = None;
	XTransform xf;
	int iconw, iconh;
	int d;

	if ((pix = getewmhicon(pager, win, &iconw, &iconh, &d)) == None)
		return None;
	if (pix == None)
		return None;
	pic = XRenderCreatePicture(pager->display, pix, pager->formatARGB, 0, NULL);
	XFreePixmap(pager->display, pix);
	if (pic == None)
		return None;
	if (max(iconw, iconh) != ICON_SIZE) {
		XRenderSetPictureFilter(pager->display, pic, FilterBilinear, NULL, 0);
		xf.matrix[0][0] = (iconw << 16u) / ICON_SIZE; xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
		xf.matrix[1][0] = 0; xf.matrix[1][1] = (iconh << 16u) / ICON_SIZE; xf.matrix[1][2] = 0;
		xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
		XRenderSetPictureTransform(pager->display, pic, &xf);
	}
	return pic;
}

static bool
hasstate(Pager *pager, Window window, Atom atom)
{
	Atom *as;
	Cardinal natoms, i;
	int retval;

	/* return non-zero if window has given state */
	retval = 0;
	if ((natoms = getatomprop(pager, window, pager->atoms[_NET_WM_STATE], &as)) != 0) {
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

static bool
isurgent(Pager *pager, Window win)
{
	XWMHints *wmh;
	bool ret;

	ret = false;
	if ((wmh = XGetWMHints(pager->display, win)) != NULL) {
		ret = wmh->flags & XUrgencyHint;
		XFree(wmh);
	}
	return ret;
}

static void
cleanclient(Pager *pager, Client *client)
{
	Cardinal i;

	if (client == NULL)
		return;         /* can be set to NULL by setclients() */
	for (i = 0; i < pager->ndesktops; i++)
		XDestroyWindow(pager->display, client->miniwins[i]);
	if (client->icon != None)
		XRenderFreePicture(pager->display, client->icon);
	free(client->miniwins);
	free(client->minigeoms);
	free(client);
}

static void
cleandesktops(Pager *pager)
{
	Cardinal i;

	for (i = 0; i < pager->ndesktops; i++) {
		XDestroyWindow(
			pager->display,
			pager->desktops[i].miniwin
		);
	}
	pager->ndesktops = 0;
	free(pager->desktops);
}

static void
cleanclients(Pager *pager)
{
	Cardinal i;

	for (i = 0; i < pager->nclients; i++)
		cleanclient(pager, pager->clients[i]);
	pager->nclients = 0;
	pager->activeclient = NULL;
	free(pager->clients);
}

static void
drawshadows(Pager *pager, Picture picture, int scheme, XRectangle *geometry)
{
	int i, w;

	if (scheme == SCM_DESKTOP)
		w = pager->borders[FRAME_WIDTH];
	else
		w = pager->borders[SHADOW_WIDTH];
	for(i = 0; i < w; i++) {
		/* draw light shadow */
		XRenderFillRectangle(
			pager->display,
			PictOpSrc,
			picture,
			&pager->colors[scheme + COLOR_TOP].channels,
			i, i,
			1, geometry->height - (i * 2 + 1)
		);
		XRenderFillRectangle(
			pager->display,
			PictOpSrc,
			picture,
			&pager->colors[scheme + COLOR_TOP].channels,
			i, i,
			geometry->width - (i * 2 + 1), 1
		);

		/* draw dark shadow */
		XRenderFillRectangle(
			pager->display,
			PictOpSrc,
			picture,
			&pager->colors[scheme + COLOR_BOT].channels,
			geometry->width - 1 - i, i,
			1, geometry->height - i * 2
		);
		XRenderFillRectangle(
			pager->display,
			PictOpSrc,
			picture,
			&pager->colors[scheme + COLOR_BOT].channels,
			i, geometry->height - 1 - i,
			geometry->width - i * 2, 1
		);
	}
}

static void
drawclient(Pager *pager, Client *cp)
{
	Pixmap pixmap;
	Picture picture, icon, mask;
	Cardinal i, scheme;

	if (cp->icon == None) {
		icon = pager->icon;
		mask = pager->mask;
	} else {
		icon = cp->icon;
		mask = None;
	}
	if (cp == pager->activeclient)
		scheme = SCM_ACTIVE;
	else if (cp->isurgent)
		scheme = SCM_URGENT;
	else
		scheme = SCM_INACTIVE;
	for (i = 0; i < pager->ndesktops; i++) {
		pixmap = XCreatePixmap(
			pager->display,
			pager->window,
			cp->minigeoms[i].width,
			cp->minigeoms[i].height,
			pager->depth
		);
		picture = XRenderCreatePicture(
			pager->display,
			pixmap,
			pager->format,
			0, NULL
		);
		XRenderFillRectangle(
			pager->display,
			PictOpSrc,
			picture,
			&pager->colors[scheme + COLOR_BG].channels,
			0, 0,
			cp->minigeoms[i].width,
			cp->minigeoms[i].height
		);
		XRenderComposite(
			pager->display,
			PictOpOver,
			icon, mask, picture,
			0, 0, 0, 0,
			(cp->minigeoms[i].width - ICON_SIZE) / 2,
			(cp->minigeoms[i].height - ICON_SIZE) / 2,
			ICON_SIZE, ICON_SIZE
		);
		drawshadows(pager, picture, scheme, &cp->minigeoms[i]);
		XSetWindowBackgroundPixmap(
			pager->display,
			cp->miniwins[i],
			pixmap
		);
		XSetWindowBorderPixmap(
			pager->display,
			cp->miniwins[i],
			pager->colors[scheme + COLOR_BOR].pixmap
		);
		XClearWindow(pager->display, cp->miniwins[i]);
		XRenderFreePicture(pager->display, picture);
		XFreePixmap(pager->display, pixmap);
	}
}

static void
drawpager(Pager *pager)
{
	Picture picture;
	Pixmap pixmap;

	pixmap = XCreatePixmap(
		pager->display,
		pager->window,
		pager->geometry.width,
		pager->geometry.height,
		pager->depth
	);
	picture = XRenderCreatePicture(
		pager->display,
		pixmap,
		pager->format,
		0, NULL
	);
	XRenderFillRectangle(
		pager->display,
		PictOpSrc,
		picture,
		&pager->colors[SCM_DESKTOP + COLOR_BOR].channels,
		0, 0,
		pager->geometry.width,
		pager->geometry.height
	);
	drawshadows(pager, picture, SCM_DESKTOP, &pager->geometry);
	XSetWindowBackgroundPixmap(
		pager->display,
		pager->window,
		pixmap
	);
	XClearWindow(pager->display, pager->window);
	XRenderFreePicture(pager->display, picture);
	XFreePixmap(pager->display, pixmap);
}

static void
drawdesktops(Pager *pager)
{
	Cardinal i;
	Pixmap pixmap;

	for (i = 0; i < pager->ndesktops; i++) {
		if (i == pager->activedesktop)
			pixmap = pager->colors[SCM_DESKTOP + COLOR_FG].pixmap;
		else
			pixmap = pager->colors[SCM_DESKTOP + COLOR_BG].pixmap;
		XSetWindowBackgroundPixmap(
			pager->display,
			pager->desktops[i].miniwin,
			pixmap
		);
		XClearWindow(pager->display, pager->desktops[i].miniwin);
	}
}

static void
setdeskgeom(Pager *pager)
{
	int x, y, w, h;
	int off, xi, yi;
	XRectangle *geometry;
	Cardinal i;

	off = pager->borders[FRAME_WIDTH];
	w = pager->geometry.width;
	w -= off * 2;
	w -= pager->borders[SEPARATOR_WIDTH] * (pager->ncols - 1);
	if (w < 1)
		w = 1;
	h = pager->geometry.height;
	h -= off * 2;
	h -= pager->borders[SEPARATOR_WIDTH] * (pager->nrows - 1);
	if (h < 1)
		h = 1;
	for (i = 0; i < pager->ndesktops; i++) {
		xi = yi = i;
		if (pager->orient == _NET_WM_ORIENTATION_HORZ)
			yi /= pager->ncols;
		else
			xi /= pager->nrows;
		if (pager->corner == _NET_WM_TOPRIGHT) {
			x = pager->ncols - pager->borders[SEPARATOR_WIDTH] - xi % pager->ncols;
			y = yi % pager->nrows;
		} else if (pager->corner == _NET_WM_BOTTOMLEFT) {
			x = xi % pager->ncols;
			y = pager->nrows - pager->borders[SEPARATOR_WIDTH] - yi % pager->nrows;
		} else if (pager->corner == _NET_WM_BOTTOMRIGHT) {
			x = pager->ncols - pager->borders[SEPARATOR_WIDTH] - xi % pager->ncols;
			y = pager->nrows - pager->borders[SEPARATOR_WIDTH] - yi % pager->nrows;
		} else {
			x = xi % pager->ncols;
			y = yi % pager->nrows;
		}
		geometry = &pager->desktops[i].geometry;
		geometry->x = w * x / pager->ncols + x;
		geometry->y = h * y / pager->nrows + y;
		geometry->width = w * (x + 1) / pager->ncols - w * x / pager->ncols;
		if (geometry->width < 1)
			geometry->width = 1;
		geometry->height = h * (y + 1) / pager->nrows - h * y / pager->nrows;
		if (geometry->height < 1)
			geometry->height = 1;
		XMoveResizeWindow(
			pager->display,
			pager->desktops[i].miniwin,
			off + geometry->x,
			off + geometry->y,
			geometry->width,
			geometry->height
		);
	}
}

static void
unmapclient(Pager *pager, Client *cp)
{
	Cardinal i;

	for (i = 0; i < pager->ndesktops; i++) {
		XUnmapWindow(pager->display, cp->miniwins[i]);
	}
}

static bool
isatdesk(Client *cp, Cardinal desk)
{
	if (cp->ishidden)
		return false;
	if (cp->desk == ALLDESKTOPS)
		return true;
	return (cp->desk == desk);
}

static void
mapclient(Pager *pager, Client *cp)
{
	Cardinal i;

	for (i = 0; i < pager->ndesktops; i++) {
		if (isatdesk(cp, i)) {
			XMapWindow(pager->display, cp->miniwins[i]);
		} else {
			XUnmapWindow(pager->display, cp->miniwins[i]);
		}
	}
}

static void
setclientgeometry(Pager *pager, Client *cp)
{
	Window dw;
	unsigned int du, b;
	int x, y;
	int cx, cy;
	unsigned int cw, ch;

	XGetGeometry(
		pager->display,
		cp->clientwin,
		&dw, &x, &y, &cw, &ch, &b, &du
	);
	XTranslateCoordinates(
		pager->display,
		cp->clientwin,
		pager->root,
		x, y, &cx, &cy, &dw
	);
	cp->clientgeom.x = cx;
	cp->clientgeom.y = cy;
	cp->clientgeom.width = cw;
	cp->clientgeom.height = ch;
}

static void
configureclient(Pager *pager, int desk, Client *cp)
{
	XRectangle *dp;

	dp = &pager->desktops[desk].geometry;
	cp->minigeoms[desk].x = cp->clientgeom.x * dp->width / pager->rootgeom.width;
	cp->minigeoms[desk].y = cp->clientgeom.y * dp->height / pager->rootgeom.height;
	cp->minigeoms[desk].width = cp->clientgeom.width * dp->width / pager->rootgeom.width;
	if (cp->minigeoms[desk].width < 1)
		cp->minigeoms[desk].width = 1;
	cp->minigeoms[desk].height = cp->clientgeom.height * dp->height / pager->rootgeom.height;
	if (cp->minigeoms[desk].height < 1)
		cp->minigeoms[desk].height = 1;
	XMoveResizeWindow(
		pager->display,
		cp->miniwins[desk],
		cp->minigeoms[desk].x,
		cp->minigeoms[desk].y,
		cp->minigeoms[desk].width,
		cp->minigeoms[desk].height
	);
}

static void
mapclients(Pager *pager)
{
	Client *cp;
	Cardinal i;

	for (i = 0; i < pager->nclients; i++) {
		cp = pager->clients[i];
		if (pager->showingdesk) {
			unmapclient(pager, cp);
		} else {
			mapclient(pager, cp);
		}
	}
}

static void
redrawall(Pager *pager)
{
	Cardinal i, j;

	setdeskgeom(pager);
	drawdesktops(pager);
	for (i = 0; i < pager->nclients; i++) {
		for (j = 0; j < pager->ndesktops; j++)
			configureclient(pager, j, pager->clients[i]);
		drawclient(pager, pager->clients[i]);
	}
}

static int
setpagersize(Pager *pager, unsigned int w, unsigned int h)
{
	int ret;

	ret = (pager->geometry.width != w || pager->geometry.height != h);
	pager->geometry.width = w;
	pager->geometry.height = h;
	return ret;
}

static Window
createminiwindow(Pager *pager, Window parent, int border)
{
	return XCreateWindow(
		pager->display,
		parent,
		0, 0, 1, 1,
		border,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWEventMask,
		&(XSetWindowAttributes){
			.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask
		}
	);
}

static void
setndesktops(Pager *pager)
{
	Cardinal i;

	cleandesktops(pager);
	cleanclients(pager);
	pager->nclients = 0;
	pager->clients = NULL;
	pager->ndesktops = getcardprop(
		pager,
		pager->root,
		pager->atoms[_NET_NUMBER_OF_DESKTOPS]
	);
	if (pager->ndesktops < 1)
		return;
	pager->desktops = ecalloc(pager->ndesktops, sizeof(*pager->desktops));
	for (i = 0; i < pager->ndesktops; i++) {
		pager->desktops[i].miniwin = createminiwindow(pager, pager->window, 0);
		XMapWindow(pager->display, pager->desktops[i].miniwin);
	}
}

static Client *
getclient(Pager *pager, Window window)
{
	Cardinal i;

	for (i = 0; i < pager->nclients; i++)
		if (pager->clients[i]->clientwin == window)
			return pager->clients[i];
	return NULL;
}

static void
sethidden(Pager *pager, Client *cp)
{
	cp->ishidden = hasstate(pager, cp->clientwin, pager->atoms[_NET_WM_STATE_HIDDEN]);
}

static void
seturgency(Pager *pager, Client *cp)
{
	cp->isurgent = 0;
	if (hasstate(pager, cp->clientwin, pager->atoms[_NET_WM_STATE_DEMANDS_ATTENTION]))
		cp->isurgent = true;
	else if (isurgent(pager, cp->clientwin))
		cp->isurgent = true;
}

static void
setdesktop(Pager *pager, Client *cp)
{
	if (hasstate(pager, cp->clientwin, pager->atoms[_NET_WM_STATE_STICKY])) {
		cp->desk = ALLDESKTOPS;
		return;
	}
	cp->desk = getcardprop(pager, cp->clientwin, pager->atoms[_NET_WM_DESKTOP]);
}

static void
raiseclients(Pager *pager)
{
	Cardinal i, j;

	for (i = 0; i < pager->nclients; i++) {
		for (j = 0; j < pager->ndesktops; j++) {
			XRaiseWindow(
				pager->display,
				pager->clients[i]->miniwins[j]
			);
		}
	}
}

static void
setclients(Pager *pager)
{
	Client **clients = NULL;
	Window *wins = NULL;
	Cardinal nclients = 0;
	Cardinal i, j;

	if (pager->ndesktops > 0) {
		nclients = getwinprop(
			pager,
			pager->root,
			pager->atoms[_NET_CLIENT_LIST_STACKING],
			&wins
		);
	}
	if (nclients > 0)
		clients = ecalloc(nclients, sizeof(*clients));
	for (i = 0; i < nclients; i++) {
		clients[i] = NULL;
		for (j = 0; j < pager->nclients; j++) {
			if (pager->clients[j] == NULL)
				continue;
			if (pager->clients[j]->clientwin != wins[i])
				continue;
			clients[i] = pager->clients[j];
			pager->clients[j] = NULL;
			break;
		}
		if (clients[i] != NULL)
			continue;
		clients[i] = emalloc(sizeof(*clients[i]));
		*clients[i] = (Client) { 0 };
		clients[i]->clientwin = wins[i];
		preparewin(pager, wins[i]);
		clients[i]->icon = geticonprop(pager, wins[i]);
		sethidden(pager, clients[i]);
		setdesktop(pager, clients[i]);
		seturgency(pager, clients[i]);
		setclientgeometry(pager, clients[i]);
		clients[i]->minigeoms = ecalloc(
			pager->ndesktops,
			sizeof(*clients[i]->minigeoms)
		);
		clients[i]->miniwins = ecalloc(
			pager->ndesktops,
			sizeof(*clients[i]->miniwins)
		);
		for (j = 0; j < pager->ndesktops; j++) {
			clients[i]->miniwins[j] = createminiwindow(
				pager,
				pager->desktops[j].miniwin,
				pager->borders[BORDER_WIDTH]
			);
			configureclient(pager, j, clients[i]);
		}
		drawclient(pager, clients[i]);
	}
	cleanclients(pager);
	pager->clients = clients;
	pager->nclients = nclients;
	XFree(wins);
	raiseclients(pager);
	mapclients(pager);
}

static void
setshowingdesk(Pager *pager)
{
	bool prevshowingdesk;

	prevshowingdesk = pager->showingdesk;
	pager->showingdesk = getcardprop(
		pager,
		pager->root,
		pager->atoms[_NET_SHOWING_DESKTOP]
	);
	if (prevshowingdesk != pager->showingdesk) {
		mapclients(pager);
	}
}

static void
setcurrdesktop(Pager *pager)
{
	Cardinal prevdesktop;

	prevdesktop = pager->activedesktop;
	pager->activedesktop = getcardprop(
		pager,
		pager->root,
		pager->atoms[_NET_CURRENT_DESKTOP]
	);
	if (prevdesktop != pager->activedesktop) {
		drawdesktops(pager);
	}
}

static void
setactive(Pager *pager)
{
	Client *prevactive;
	Window *wins;

	prevactive = pager->activeclient;
	pager->activeclient = NULL;
	if (getwinprop(pager, pager->root, pager->atoms[_NET_ACTIVE_WINDOW], &wins) > 0) {
		pager->activeclient = getclient(pager, *wins);
		XFree(wins);
	}
	if (prevactive != pager->activeclient) {
		if (prevactive != NULL)
			drawclient(pager, prevactive);
		if (pager->activeclient != NULL)
			drawclient(pager, pager->activeclient);
	}
}

static bool
between(int pos, int from, int len)
{
	return pos >= from && pos < from + len;
}

static void
mousemove(Pager *pager, Client *cp, Window win, int dx, int dy, Time time)
{
	XEvent ev;
	XRectangle *desk;
	Cardinal i, newdesk, olddesk;
	int status, newx, newy;
	Window dw;

	if (cp->desk == ALLDESKTOPS) {
		clientmsg(
			pager,
			cp->clientwin,
			pager->atoms[_NET_ACTIVE_WINDOW],
			(Cardinal[]){ 2, time, 0, 0, 0 }
		);
		return;
	}
	XTranslateCoordinates(
		pager->display,
		win,
		pager->window,
		0 - pager->borders[BORDER_WIDTH],
		0 - pager->borders[BORDER_WIDTH],
		&newx, &newy,
		&dw
	);
	olddesk = newdesk = cp->desk;
	XReparentWindow(pager->display, win, pager->window, newx, newy);
	XSync(pager->display, False);
	status = XGrabPointer(
		pager->display,
		win,
		False,
		MOUSEEVENTMASK,
		GrabModeAsync, GrabModeAsync,
		None, None,
		time
	);
	if (status != GrabSuccess)
		goto done;
	while (!XMaskEvent(pager->display, MOUSEEVENTMASK, &ev)) switch (ev.type) {
	case ButtonRelease:
		newx += ev.xbutton.x;
		newy += ev.xbutton.y;
		for (i = 0; i < pager->ndesktops; i++) {
			desk = &pager->desktops[i].geometry;
			if (!between(newx, desk->x, desk->width))
				continue;
			if (!between(newy, desk->y, desk->height))
				continue;
			newdesk = i;
			break;
		}
		XUngrabPointer(pager->display, ev.xbutton.time);
		time = ev.xbutton.time;
		goto done;
	case MotionNotify:
		newx += ev.xmotion.x - dx;
		newy += ev.xmotion.y - dy;
		XMoveWindow(pager->display, win, newx, newy);
		break;
	}
done:
	XReparentWindow(
		pager->display,
		win,
		pager->desktops[olddesk].miniwin,
		cp->minigeoms[olddesk].x,
		cp->minigeoms[olddesk].y
	);
	if (newdesk != olddesk) {
		clientmsg(
			pager,
			cp->clientwin,
			pager->atoms[_NET_WM_DESKTOP],
			(Atom[]){newdesk, PAGER_ACTION, 0, 0, 0}
		);
	} else {
		clientmsg(
			pager,
			cp->clientwin,
			pager->atoms[_NET_ACTIVE_WINDOW],
			(Atom[]){2, time, 0, 0, 0}
		);
	}
}

static void
setcolor(Pager *pager, const char *value, XRenderColor *color)
{
	XColor xcolor;

	if (!XParseColor(pager->display, pager->colormap, value, &xcolor)) {
		warnx("%s: unknown color name", value);
		return;
	}
	color->red   = (xcolor.flags & DoRed)   ? xcolor.red   : 0x0000;
	color->green = (xcolor.flags & DoGreen) ? xcolor.green : 0x0000;
	color->blue  = (xcolor.flags & DoBlue)  ? xcolor.blue  : 0x0000;
	color->alpha = 0xFFFF;
}

static void
setnumber(const char *value, int *retval)
{
	char *endp;
	long n;

	n = strtol(value, &endp, 10);
	if (*endp != '\0' || n < 0 || n >= MAX_VALUE) {
		warnx("%s: number invalid or out of range", value);
		return;
	}
	*retval = n;
}

static int
setgeometry(const char *value, XRectangle *root, XRectangle *rect)
{
	unsigned int w, h;
	int retval, flags, x, y;

	retval = 0;
	flags = XParseGeometry(value, &x, &y, &w, &h);
	if (flags & WidthValue) {
		rect->width = w;
		retval |= USSize;
	}
	if (flags & HeightValue) {
		rect->height = h;
		retval |= USSize;
	}
	if (flags & XValue) {
		if (flags & XNegative) {
			x += root->width;
			x -= rect->width;
		}
		rect->x = x;
		retval |= USPosition;
	}
	if (flags & YValue) {
		if (flags & YNegative) {
			y += root->height;
			y -= rect->height;
		}
		rect->y = y;
		retval |= USPosition;
	}
	return retval;
}

static XrmDatabase
newxdb(Pager *pager, const char *str)
{
	XrmDatabase xdb, tmp;

	tmp = NULL;
	if ((xdb = XrmGetStringDatabase(str)) == NULL)
		return NULL;
	if (pager->xrm != NULL)
		tmp = XrmGetStringDatabase(pager->xrm);
	if (tmp != NULL)
		XrmMergeDatabases(tmp, &xdb);
	return xdb;
}

static const char *
getresource(Pager *pager, XrmDatabase xdb, enum Resource res)
{
	XrmRepresentation tmp;
	XrmValue xval;
	Bool success;

	success = XrmQGetResource(
		xdb,
		(XrmName[]){
			pager->application.name,
			pager->resources[res].name,
			NULLQUARK,
		},
		(XrmClass[]){
			pager->application.class,
			pager->resources[res].class,
			NULLQUARK,
		},
		&tmp,
		&xval
	);
	return success ? xval.addr : NULL;
}

static void
loadresources(Pager *pager, const char *str)
{
	XrmDatabase xdb;
	const char *value;
	enum Resource res;

	if (str == NULL)
		return;
	if ((xdb = newxdb(pager, str)) == NULL)
		return;
	for (res = 0; res < NRESOURCES; res++) {
		if ((value = getresource(pager, xdb, res)) == NULL)
			continue;
		if (value[0] == '\0')
			continue;
		if (res < NCOLORS) {
			setcolor(pager, value, &pager->colors[res].channels);
		} else if (res < NCOLORS + NBORDERS) {
			setnumber(value, &pager->borders[res - NCOLORS]);
		} else if (res == RES_GEOMETRY) {
			pager->geomflags = setgeometry(
				value,
				&pager->rootgeom,
				&pager->geometry
			);
		}
	}
	XrmDestroyDatabase(xdb);
}

static void
fillcolors(Pager *pager)
{
	Color *color;
	size_t i;

	for (i = 0; i < NCOLORS; i++) {
		color = &pager->colors[i];
		XRenderFillRectangle(
			pager->display,
			PictOpSrc,
			color->picture,
			&color->channels,
			0, 0, 1, 1
		);
	}
}

static void
xeventbuttonpress(Pager *pager, XEvent *e)
{
	XButtonEvent *ev;
	size_t i, j;

	ev = &e->xbutton;
	if (ev->button != Button1)
		return;
	for (i = 0; i < pager->ndesktops; i++) {
		if (ev->window == pager->desktops[i].miniwin) {
			clientmsg(
				pager,
				None,
				pager->atoms[_NET_CURRENT_DESKTOP],
				(Atom[]){i, CurrentTime, 0, 0, 0}
			);
			return;
		}
	}
	for (i = 0; i < pager->nclients; i++) {
		if (pager->clients[i] == NULL)
			continue;
		for (j = 0; j < pager->ndesktops; j++) {
			if (ev->window != pager->clients[i]->miniwins[j])
				continue;
			mousemove(
				pager,
				pager->clients[i],
				ev->window,
				ev->x,
				ev->y,
				ev->time
			);
			return;
		}
	}
}

static void
xeventclientmessage(Pager *pager, XEvent *e)
{
	XClientMessageEvent *ev;

	ev = &e->xclient;
	if ((Atom)ev->data.l[0] == pager->atoms[WM_DELETE_WINDOW]) {
		pager->running = false;
	}
}

static void
xeventconfigurenotify(Pager *pager, XEvent *e)
{
	XConfigureEvent *ev;
	Client *c;
	Cardinal j;

	ev = &e->xconfigure;
	if (ev->window == pager->root) {
		/* screen size changed (eg' a new monitor was plugged-in) */
		pager->rootgeom.width = ev->width;
		pager->rootgeom.height = ev->height;
		redrawall(pager);
		return;
	}
	if (ev->window == pager->window) {
		/* the pager window may have been resized */
		setpagersize(pager, ev->width, ev->height);
		redrawall(pager);
	}
	if ((c = getclient(pager, ev->window)) != NULL) {
		/* a client window window may have been moved or resized */
		c->clientgeom.x = ev->x;
		c->clientgeom.y = ev->y;
		c->clientgeom.width = ev->width;
		c->clientgeom.height = ev->height;
		for (j = 0; j < pager->ndesktops; j++)
			configureclient(pager, j, c);
		drawclient(pager, c);
		mapclient(pager, c);
	}
}

static void
xeventpropertynotify(Pager *pager, XEvent *e)
{
	Client *cp;
	XPropertyEvent *ev;
	char *str;

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
	if (ev->state != PropertyNewValue)
		return;
	if (ev->atom == pager->atoms[_NET_CLIENT_LIST_STACKING]) {
		/* the list of windows was reset */
		setclients(pager);
		setactive(pager);
	} else if (ev->atom == pager->atoms[_NET_ACTIVE_WINDOW]) {
		/* the active window value was reset */
		setactive(pager);
	} else if (ev->atom == pager->atoms[_NET_CURRENT_DESKTOP]) {
		/* the current desktop value was reset */
		setcurrdesktop(pager);
	} else if (ev->atom == pager->atoms[_NET_SHOWING_DESKTOP]) {
		/* the value of the "showing desktop" state was reset */
		setshowingdesk(pager);
	} else if (ev->atom == pager->atoms[_NET_NUMBER_OF_DESKTOPS]) {
		/* the number of desktops value was reset */
		setndesktops(pager);
		setdeskgeom(pager);
		drawdesktops(pager);
		setclients(pager);
		setactive(pager);
	} else if (ev->atom == pager->atoms[_NET_WM_STATE]) {
		/* the list of states of a window (which may or may not include a relevant state) was reset */
		if ((cp = getclient(pager, ev->window)) == NULL)
			return;
		sethidden(pager, cp);
		setdesktop(pager, cp);
		seturgency(pager, cp);
		drawclient(pager, cp);
		mapclient(pager, cp);
	} else if (ev->atom == pager->atoms[_NET_WM_DESKTOP]) {
		/* the desktop of a window was reset */
		if ((cp = getclient(pager, ev->window)) == NULL)
			return;
		setdesktop(pager, cp);
		mapclient(pager, cp);
	} else if (ev->atom == XA_WM_HINTS) {
		/* the urgency state of a window was reset */
		if ((cp = getclient(pager, ev->window)) == NULL)
			return;
		seturgency(pager, cp);
		drawclient(pager, cp);
	} else if (ev->atom == pager->atoms[_NET_WM_ICON]) {
		if ((cp = getclient(pager, ev->window)) == NULL)
			return;
		if (cp->icon != None)
			XRenderFreePicture(pager->display, cp->icon);
		cp->icon = geticonprop(pager, cp->clientwin);
		drawclient(pager, cp);
	} else if (ev->atom == XA_RESOURCE_MANAGER) {
		if (ev->window != pager->root)
			return;
		str = gettextprop(
			pager->display,
			pager->root,
			XA_RESOURCE_MANAGER,
			False
		);
		if (str == NULL)
			return;
		loadresources(pager, str);
		free(str);
		fillcolors(pager);
		redrawall(pager);
		drawpager(pager);
	}
}

static void
clean(Pager *pager)
{
	size_t i;
	Color *color;

	cleanclients(pager);
	cleandesktops(pager);
	for (i = 0; i < NCOLORS; i++) {
		color = &pager->colors[i];
		if (color->picture != None)
			XRenderFreePicture(pager->display, color->picture);
		if (color->pixmap != None)
			XFreePixmap(pager->display, color->pixmap);
	}
	if (pager->icon != None)
		XRenderFreePicture(pager->display, pager->icon);
	if (pager->mask != None)
		XRenderFreePicture(pager->display, pager->mask);
	if (pager->window != None)
		XDestroyWindow(pager->display, pager->window);
	XCloseDisplay(pager->display);
}

#define RED(v)   ((((v) & 0xFF0000) >> 8) | (((v) & 0xFF0000) >> 16))
#define GREEN(v) ((((v) & 0x00FF00)     ) | (((v) & 0x00FF00) >> 8))
#define BLUE(v)  ((((v) & 0x0000FF) << 8) | (((v) & 0x0000FF)     ))

static void
setup(Pager *pager, int argc, char *argv[], char *name, char *geomstr)
{
	static char *atomnames[NATOMS] = {
#define X(atom) [atom] = #atom,
		ATOMS
#undef  X
	};
	static struct {
		const char *class, *name;
		unsigned int value;
	} resdefs[NRESOURCES] = {
#define X(i, c, n, v) [i] = { .class = c, .name = n, .value = v, },
		RESOURCES
#undef  X
	};
	XpmAttributes xpma;
	XRenderPictFormat *maskformat;
	Pixmap icon = None;
	Pixmap mask = None;
	Resource *resource;
	Color *color;
	size_t i;
	int success, status, screen;

	if (name == NULL)
		name = APP_NAME;

	/* connect to server */
	if ((pager->display = XOpenDisplay(NULL)) == NULL) {
		warnx("could not connect to X server");
		goto error;
	}
	(void)XSetErrorHandler(xerror);
	screen = DefaultScreen(pager->display);
	pager->root = RootWindow(pager->display, screen);
	pager->rootgeom.width = DisplayWidth(pager->display, screen);
	pager->rootgeom.height = DisplayHeight(pager->display, screen);
	(void)XSelectInput(pager->display, pager->root, PropertyChangeMask);
	preparewin(pager, pager->root);

	/* get visual format */
	pager->colormap = XDefaultColormap(pager->display, screen);
	pager->visual = DefaultVisual(pager->display, screen);
	pager->depth = DefaultDepth(pager->display, screen);
	pager->format = XRenderFindVisualFormat(pager->display, pager->visual);
	if (pager->format == NULL) {
		warnx("could not find XRender visual format");
		goto error;
	}
	maskformat = XRenderFindStandardFormat(
		pager->display,
		PictStandardA1
	);
	if (maskformat == NULL) {
		warnx("could not find XRender visual format");
		goto error;
	}
	pager->formatARGB = XRenderFindStandardFormat(
		pager->display,
		PictStandardARGB32
	);
	if (pager->formatARGB == NULL) {
		warnx("could not find XRender visual format");
		goto error;
	}

	/* intern atoms */
	success = XInternAtoms(
		pager->display,
		atomnames,
		NATOMS,
		False,
		pager->atoms
	);
	if (!success) {
		warnx("could not intern X atoms");
		goto error;
	}

	/* intern quarks and set colors */
	XrmInitialize();
	pager->application.class = XrmPermStringToQuark(APP_CLASS);
	pager->application.name = XrmPermStringToQuark(name);
	for (i = 0; i < NRESOURCES; i++) {
		resource = &pager->resources[i];
		resource->class = XrmPermStringToQuark(resdefs[i].class);
		resource->name = XrmPermStringToQuark(resdefs[i].name);
		if (i < NCOLORS) {
			pager->colors[i].channels = (XRenderColor){
				.red   = RED(resdefs[i].value),
				.green = GREEN(resdefs[i].value),
				.blue  = BLUE(resdefs[i].value),
				.alpha = 0xFFFF,
			};
		} else if (i < NCOLORS + NBORDERS) {
			pager->borders[i-NCOLORS] = resdefs[i].value;
		} else if (i == RES_GEOMETRY) {
			pager->geometry.width = resdefs[i].value;
			pager->geometry.height = resdefs[i].value;
		}
	}

	/* create window */
	pager->window = XCreateWindow(
		pager->display,
		pager->root,
		0, 0, 1, 1, 0,          /* x, y, width, height, border */
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWEventMask,
		&(XSetWindowAttributes){
			.event_mask = StructureNotifyMask,
		}
	);
	if (pager->window == None) {
		warnx("could not create window");
		goto error;
	}
	XSetWMProtocols(
		pager->display,
		pager->window,
		(Atom[]) {              /* protocols */
			pager->atoms[WM_DELETE_WINDOW],
		},
		1                       /* number of protocols */
	);

	/* create color layers */
	for (i = 0; i < NCOLORS; i++) {
		color = &pager->colors[i];
		color->pixmap = XCreatePixmap(
			pager->display,
			pager->window,
			1, 1,
			pager->depth
		);
		if (color->pixmap == None) {
			warnx("could not create pixmap");
			goto error;
		}
		color->picture = XRenderCreatePicture(
			pager->display,
			color->pixmap,
			pager->format,
			CPRepeat,
			&(XRenderPictureAttributes){
				.repeat = RepeatNormal,
			}
		);
		if (color->pixmap == None) {
			warnx("could not create picture");
			goto error;
		}
	}

	/* load X resources and fill color pictures */
	loadresources(pager, XResourceManagerString(pager->display));
	if (geomstr != NULL) {
		pager->geomflags = setgeometry(
			geomstr,
			&pager->rootgeom,
			&pager->geometry
		);
	}
	fillcolors(pager);

	/* set window size and properties */
	XMoveResizeWindow(
		pager->display,
		pager->window,
		pager->geometry.x,
		pager->geometry.y,
		pager->geometry.width,
		pager->geometry.height
	);
	XmbSetWMProperties(
		pager->display,
		pager->window,
		APP_CLASS,      /* title name */
		APP_CLASS,      /* icon name */
		argv,
		argc,
		&(XSizeHints){
			.flags = pager->geomflags,
		},
		&(XWMHints){
			.flags = IconWindowHint | StateHint | WindowGroupHint,
			.initial_state = WithdrawnState,
			.window_group = pager->window,
			.icon_window = pager->window,
		},
		&(XClassHint){
			.res_class = APP_CLASS,
			.res_name = name,
		}
	);

	/* set default icon */
	memset(&xpma, 0, sizeof(xpma));
	xpma = (XpmAttributes){
		.valuemask = XpmVisual,
		.visual = pager->visual,
	};
	status = XpmCreatePixmapFromData(
		pager->display,
		pager->window,
		x_xpm,
		&icon,
		&mask,
		&xpma
	);
	if (status != XpmSuccess) {
		warnx("could not load xpm");
		goto error;
	}
	if (!FLAG(xpma.valuemask, XpmSize | XpmVisual)) {
		warnx("could not load xpm");
		goto error;
	}
	pager->icon = XRenderCreatePicture(
		pager->display,
		icon,
		pager->format,
		0, NULL
	);
	pager->mask = XRenderCreatePicture(
		pager->display,
		mask,
		maskformat,
		0, NULL
	);
	XFreePixmap(pager->display, icon);
	XFreePixmap(pager->display, mask);

	/* get clients and desktops */
	setndesktops(pager);
	setdeskgeom(pager);
	setcurrdesktop(pager);
	setclients(pager);
	setactive(pager);
	drawdesktops(pager);
	drawpager(pager);
	mapclients(pager);

	XMapWindow(pager->display, pager->window);
	return;
error:
	if (icon != None)
		XFreePixmap(pager->display, icon);
	if (mask != None)
		XFreePixmap(pager->display, mask);
	clean(pager);
	exit(EXIT_FAILURE);
}

static void
setcorner(Pager *pager, char *grid[], char *borders[])
{
	setnumber(grid[0], &pager->nrows);
	if (pager->nrows < 1)
		errx(EXIT_FAILURE, "%s: invalid number of rows", grid[0]);
	setnumber(grid[1], &pager->ncols);
	if (pager->ncols < 1)
		errx(EXIT_FAILURE, "%s: invalid number of columns", grid[1]);
	if (borders == NULL)
		return;
	switch (borders[0][0]) {
	case 'l': case 'L':
		pager->orient = _NET_WM_ORIENTATION_HORZ;
		switch (borders[1][0]) {
		case 'l': case 'L':
		case 'r': case 'R':
			errx(EXIT_FAILURE, "%s: repeated border", borders[1]);
		case 't': case 'T':
			pager->corner = _NET_WM_TOPLEFT;
			break;
		case 'b': case 'B':
			pager->corner = _NET_WM_BOTTOMLEFT;
			break;
		default:
			errx(EXIT_FAILURE, "%s: invalid border", borders[1]);
		}
		break;
	case 'r': case 'R':
		pager->orient = _NET_WM_ORIENTATION_HORZ;
		switch (borders[1][0]) {
		case 'l': case 'L':
		case 'r': case 'R':
			errx(EXIT_FAILURE, "%s: repeated border", borders[1]);
		case 't': case 'T':
			pager->corner = _NET_WM_TOPRIGHT;
			break;
		case 'b': case 'B':
			pager->corner = _NET_WM_BOTTOMRIGHT;
			break;
		default:
			errx(EXIT_FAILURE, "%s: invalid border", borders[1]);
		}
		break;
	case 't': case 'T':
		pager->orient = _NET_WM_ORIENTATION_VERT;
		switch (borders[1][0]) {
		case 'l': case 'L':
			pager->corner = _NET_WM_TOPLEFT;
			break;
		case 'r': case 'R':
			pager->corner = _NET_WM_TOPRIGHT;
			break;
		case 't': case 'T':
		case 'b': case 'B':
			errx(EXIT_FAILURE, "%s: repeated border", borders[1]);
		default:
			errx(EXIT_FAILURE, "%s: invalid border", borders[1]);
		}
		pager->corner = _NET_WM_TOPLEFT;
		break;
	case 'b': case 'B':
		pager->orient = _NET_WM_ORIENTATION_VERT;
		switch (borders[1][0]) {
		case 'l': case 'L':
			pager->corner = _NET_WM_BOTTOMLEFT;
			break;
		case 'r': case 'R':
			pager->corner = _NET_WM_BOTTOMRIGHT;
			break;
		case 't': case 'T':
		case 'b': case 'B':
			errx(EXIT_FAILURE, "%s: repeated border", borders[1]);
		default:
			errx(EXIT_FAILURE, "%s: invalid border", borders[1]);
		}
		break;
	default:
		errx(EXIT_FAILURE, "%s: invalid border", borders[0]);
	}
}

int
main(int argc, char *argv[])
{
	Pager pager = { 0 };
	XEvent ev;
	void (*xevents[LASTEvent])(Pager *, XEvent *) = {
		[ButtonPress]           = xeventbuttonpress,
		[ConfigureNotify]       = xeventconfigurenotify,
		[ClientMessage]         = xeventclientmessage,
		[PropertyNotify]        = xeventpropertynotify,
	};
	char *name;
	char *geometry;
	int i;

	geometry = NULL;
	pager.xrm = getenv("RESOURCES_DATA");
	name = getenv("RESOURCES_NAME");
	if (name != NULL)
		;
	else if (argv[0] == NULL || argv[0][0] == '\0')
		name = NULL;
	else if ((name = strrchr(argv[0], '/')) != NULL)
		name++;
	else
		name = argv[0];
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			break;
		if (strcmp(argv[i], "-name") == 0) {
			name = argv[++i];
		} else if (strcmp(argv[i], "-xrm") == 0) {
			pager.xrm = argv[++i];
		} else if (strcmp(argv[i], "-geometry") == 0) {
			geometry = argv[++i];
		} else if (strcmp(argv[i], "--")) {
			i++;
			break;
		} else {
			usage();
		}
	}
	switch (argc - i) {
	case 4:
		setcorner(&pager, argv + i, argv + i + 2);
		break;
	case 2:
		setcorner(&pager, argv + i, NULL);
		break;
	default:
		usage();
	}

	setup(&pager, argc, argv, name, geometry);
	pager.running = true;
	while (pager.running && !XNextEvent(pager.display, &ev))
		if (ev.type < LASTEvent && xevents[ev.type] != NULL)
			(*xevents[ev.type])(&pager, &ev);
	clean(&pager);
	return EXIT_SUCCESS;
}
