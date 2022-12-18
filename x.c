#include <err.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xproto.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>

#include "x.h"
#include "x.xpm"

#define DEF_PADDING     2

/* draw context */
struct DC {
	GC gc;
	unsigned long windowcolors[STYLE_LAST][COLOR_LAST];
	unsigned long selbackground;
	unsigned long background;
	unsigned long separator;
	Pixmap icon, mask;
	int shadowthickness;
};

static int (*xerrorxlib)(Display *, XErrorEvent *);
static struct DC dc;
static Visual *visual;
static XrmDatabase xdb = NULL;
static Colormap colormap;
static unsigned int depth;
static int screen;
static char *xrm = NULL; Display *dpy;
Atom atoms[ATOM_LAST];
Window root;

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

static uint32_t
prealpha(uint32_t p)
{
	uint8_t a = p >> 24u;
	uint32_t rb = (a * (p & 0xFF00FFu)) >> 8u;
	uint32_t g = (a * (p & 0x00FF00u)) >> 8u;
	return (rb & 0xFF00FFu) | (g & 0x00FF00u) | (a << 24u);
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
	if ((img = XCreateImage(dpy, visual, 32, ZPixmap, 0, datachr, *iconw, *iconh, 32, 0)) == NULL) {
		free(datachr);
		goto done;
	}
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

void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

unsigned long
ealloccolor(const char *s)
{
	XColor color;

	if(!XAllocNamedColor(dpy, colormap, s, &color, &color)) {
		warnx("could not allocate color: %s", s);
		return BlackPixel(dpy, screen);
	}
	return color.pixel;
}

unsigned long
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

unsigned long
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

unsigned long
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

XID
geticonprop(Window win)
{
	Pixmap pix;
	Picture pic = None;
	XTransform xf;
	int iconw, iconh;
	int d;

	if ((pix = getewmhicon(win, &iconw, &iconh, &d)) == None)
		return None;
	if (pix == None)
		return None;
	pic = XRenderCreatePicture(dpy, pix, XRenderFindStandardFormat(dpy, PictStandardARGB32), 0, NULL);
	XFreePixmap(dpy, pix);
	if (pic == None)
		return None;
	if (max(iconw, iconh) != ICON_SIZE) {
		XRenderSetPictureFilter(dpy, pic, FilterBilinear, NULL, 0);
		xf.matrix[0][0] = (iconw << 16u) / ICON_SIZE; xf.matrix[0][1] = 0; xf.matrix[0][2] = 0;
		xf.matrix[1][0] = 0; xf.matrix[1][1] = (iconh << 16u) / ICON_SIZE; xf.matrix[1][2] = 0;
		xf.matrix[2][0] = 0; xf.matrix[2][1] = 0; xf.matrix[2][2] = 65536;
		XRenderSetPictureTransform(dpy, pic, &xf);
	}
	return pic;
}

int
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

int
hasstate(Window win, Atom atom)
{
	Atom *as;
	unsigned long natoms, i;
	int retval;

	/* return non-zero if window has given state */
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

void
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

int
max(int x, int y)
{
	return x > y ? x : y;
}

Window
createminiwindow(Window parent, int border)
{
	return XCreateWindow(
		dpy, parent,
		0, 0, 1, 1,
		border,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWEventMask,
		&(XSetWindowAttributes){
			.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask
		}
	);
}

Window
createwindow(int w, int h, int dockapp, char *class, int argc, char *argv[])
{
	XWMHints *wmhints;
	XClassHint *chint;
	Window win;

	win = XCreateWindow(
		dpy, root,
		0, 0, w, h,
		0,
		CopyFromParent, CopyFromParent, CopyFromParent,
		CWEventMask | CWBackPixel,
		&(XSetWindowAttributes){
			.event_mask = StructureNotifyMask,
			.background_pixel = dc.background,
		}
	);
	wmhints = NULL;
	if ((chint = XAllocClassHint()) == NULL)
		errx(1, "XAllocClassHint");
	chint->res_name = *argv;
	chint->res_class = class;
	if (dockapp) {
		if ((wmhints = XAllocWMHints()) == NULL)
			errx(1, "XAllocWMHints");
		wmhints->flags = IconWindowHint | StateHint;
		wmhints->initial_state = WithdrawnState;
		wmhints->icon_window = win;
	}
	XmbSetWMProperties(dpy, win, class, class, argv, argc, NULL, wmhints, chint);
	XFree(chint);
	XFree(wmhints);

	/* set WM protocols */
	XSetWMProtocols(dpy, win, &atoms[WM_DELETE_WINDOW], 1);

	return win;
}

char *
getresource(char *name, char *class)
{
	char *type;
	XrmValue xval;

	if (xrm == NULL || xdb == NULL)
		return NULL;
	if (!XrmGetResource(xdb, name, class, &type, &xval))
		return NULL;
	return xval.addr;
}

void
drawborder(Window win, int w, int h, int style)
{
	XGCValues val;
	XRectangle *recs;
	Pixmap pix;
	int i;

	if (win == None || w <= 0 || h <= 0)
		return;

	w += dc.shadowthickness * 2;
	h += dc.shadowthickness * 2;
	if ((pix = XCreatePixmap(dpy, win, w, h, depth)) == None)
		return;
	recs = ecalloc(dc.shadowthickness * 2 + 1, sizeof(*recs));

	/* draw dark shadow */
	XSetForeground(dpy, dc.gc, dc.windowcolors[style][COLOR_DARK]);
	XFillRectangle(dpy, pix, dc.gc, 0, 0, w, h);

	/* draw light shadow */
	for(i = 0; i < dc.shadowthickness; i++) {
		recs[i * 2]     = (XRectangle){
			.x = w - dc.shadowthickness + i,
			.y = 0,
			.width = 1,
			.height = h - (i * 2 + 1),
		};
		recs[i * 2 + 1] = (XRectangle){
			.x = 0,
			.y = h - dc.shadowthickness + i,
			.width = w - (i * 2 + 1),
			.height = 1,
		};
	}
	recs[dc.shadowthickness * 2] = (XRectangle){
		.x = w - dc.shadowthickness,
		.y = h - dc.shadowthickness,
		.width = dc.shadowthickness,
		.height = dc.shadowthickness,
	};
	val.foreground = dc.windowcolors[style][COLOR_LIGHT];
	XChangeGC(dpy, dc.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, dc.gc, recs, dc.shadowthickness * 2 + 1);

	/* commit pixmap into window borders */
	XSetWindowBorderPixmap(dpy, win, pix);

	XFreePixmap(dpy, pix);
	free(recs);
}

void
drawbackground(Window win, Picture icon, int w, int h, int style)
{
	XGCValues val;
	Picture pic;
	Pixmap pix;

	if (win == None || w <= 0 || h <= 0)
		return;
	if ((pix = XCreatePixmap(dpy, win, w, h, depth)) == None)
		return;
	XSetForeground(dpy, dc.gc, dc.windowcolors[style][COLOR_MID]);
	XFillRectangle(dpy, pix, dc.gc, 0, 0, w, h);
	if (icon == None) {
		val.clip_mask = dc.mask;
		val.clip_x_origin = (w - ICON_SIZE) / 2;
		val.clip_y_origin = (h - ICON_SIZE) / 2;
		XChangeGC(dpy, dc.gc, GCClipMask | GCClipXOrigin | GCClipYOrigin, &val);
		XCopyArea(dpy, dc.icon, pix, dc.gc, 0, 0, ICON_SIZE, ICON_SIZE, (w - ICON_SIZE) / 2, (h - ICON_SIZE) / 2);
		val.clip_mask = None;
		XChangeGC(dpy, dc.gc, GCClipMask | GCClipXOrigin | GCClipYOrigin, &val);
	} else {
		pic = XRenderCreatePicture(dpy, pix, XRenderFindVisualFormat(dpy, visual), 0, NULL);
		if (pic != None) {
			XRenderComposite(
				dpy,
				PictOpOver,
				icon, icon, pic,
				0, 0, 0, 0,
				(w - ICON_SIZE) / 2, (h - ICON_SIZE) / 2, w, h);
			XRenderFreePicture(dpy, pic);
		}
	}
	XCopyArea(dpy, pix, win, dc.gc, 0, 0, w, h, 0, 0);
	XSetWindowBackgroundPixmap(dpy, win, pix);
	XFreePixmap(dpy, pix);
}

void
drawpager(Window win, int pagerw, int pagerh, int nrows, int ncols)
{
	Pixmap pix;
	int x, y;
	int w, h;
	int i;

	if (pagerw == 0 || pagerh == 0 ||
	    (pix = XCreatePixmap(dpy, win, pagerw, pagerh, depth)) == None)
		return;
	XSetForeground(dpy, dc.gc, dc.background);
	XFillRectangle(dpy, pix, dc.gc, 0, 0, pagerw, pagerh);
	XSetForeground(dpy, dc.gc, dc.separator);
	w = pagerw - ncols;
	h = pagerh - nrows;
	for (i = 1; i < ncols; i++) {
		x = w * i / ncols + i - 1;
		XDrawLine(dpy, pix, dc.gc, x, 0, x, pagerh);
	}
	for (i = 1; i < nrows; i++) {
		y = h * i / nrows + i - 1;
		XDrawLine(dpy, pix, dc.gc, 0, y, pagerw, y);
	}
	XCopyArea(dpy, pix, win, dc.gc, 0, 0, pagerw, pagerh, 0, 0);
	XSetWindowBackgroundPixmap(dpy, win, pix);
	XFreePixmap(dpy, pix);
}

void
moveresize(Window win, int x, int y, int w, int h)
{
	XMoveResizeWindow(dpy, win, x, y, w, h);
}

void
mapwin(Window win)
{
	XMapWindow(dpy, win);
}

void
reparentwin(Window win, Window parent, int x, int y)
{
	XReparentWindow(dpy, win, parent, x, y);
}

void
unmapwin(Window win)
{
	XUnmapWindow(dpy, win);
}

void
destroywin(Window win)
{
	XDestroyWindow(dpy, win);
}

void
freepicture(XID pic)
{
	XRenderFreePicture(dpy, (Picture)pic);
}

void
preparewin(Window win)
{
	XSelectInput(dpy, win, StructureNotifyMask | PropertyChangeMask);
}

void
setbg(Window win, int w, int h, int select)
{
	unsigned long color;

	color = select ? dc.selbackground : dc.background;
	XSetWindowBackground(dpy, win, color);
	XSetForeground(dpy, dc.gc, color);
	XFillRectangle(dpy, win, dc.gc, 0, 0, w, h);
	mapwin(win);
}

void
getgeometry(Window win, int *cx, int *cy, int *cw, int *ch)
{
	Window dw;
	unsigned int du, b;
	int x, y;
	XGetGeometry(dpy, win, &dw, &x, &y, cw, ch, &b, &du);
	XTranslateCoordinates(dpy, win, root, x, y, cx, cy, &dw);
}

void
cleanx(void)
{
	int i;

	for (i = 0; i < STYLE_LAST; i++)
		XFreeColors(dpy, colormap, dc.windowcolors[i], COLOR_LAST, 0);
	XFreeColors(dpy, colormap, &dc.background, 1, 0);
	XFreeGC(dpy, dc.gc);
	XFreePixmap(dpy, dc.icon);
	XFreePixmap(dpy, dc.mask);
	XCloseDisplay(dpy);
}

void
setcolors(const char *windowcolors[STYLE_LAST][COLOR_LAST], const char *selbackground, const char *background, const char *separator, int shadowthickness)
{
	int i, j;

	for (i = 0; i < STYLE_LAST; i++)
		for (j = 0; j < COLOR_LAST; j++)
			dc.windowcolors[i][j] = ealloccolor(windowcolors[i][j]);
	if (selbackground)
		dc.selbackground = ealloccolor(selbackground);
	if (background)
		dc.background = ealloccolor(background);
	if (separator)
		dc.separator = ealloccolor(separator);
	dc.shadowthickness = shadowthickness;
}

void
xinit(int *screenw, int *screenh)
{
	XpmAttributes xa;

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	root = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	depth = DefaultDepth(dpy, screen);
	if (screenw != NULL)
		*screenw = DisplayWidth(dpy, screen);
	if (screenh != NULL)
		*screenh = DisplayHeight(dpy, screen);
	xerrorxlib = XSetErrorHandler(xerror);
	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);
	memset(&xa, 0, sizeof(xa));
	if (XpmCreatePixmapFromData(dpy, root, x_xpm, &dc.icon, &dc.mask, &xa) != XpmSuccess)
		errx(1, "could not load xpm");
	if (!(xa.valuemask & XpmSize))
		errx(1, "could not load xpm");
	dc.gc = XCreateGC(
		dpy,
		root,
		GCFillStyle | GCLineStyle,
		&(XGCValues){
			.fill_style = FillSolid,
			.line_style = LineOnOffDash,
		}
	);
	XChangeWindowAttributes(
		dpy,
		root,
		CWEventMask,
		&(XSetWindowAttributes){
			.event_mask = StructureNotifyMask | PropertyChangeMask,
		}
	);
	initatoms();
}

int
nextevent(XEvent *ev)
{
	(void)XNextEvent(dpy, ev);
	return 1;
}
