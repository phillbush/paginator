#include <stddef.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

#define ICON_SIZE       16              /* preferred icon size */
#define ALLDESKTOPS     0xFFFFFFFF
#define PAGER_ACTION    ((long)(1 << 14))

enum {
	COLOR_MID,
	COLOR_LIGHT,
	COLOR_DARK,
	COLOR_LAST
};

enum {
	STYLE_ACTIVE,
	STYLE_INACTIVE,
	STYLE_URGENT,
	STYLE_LAST
};

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

unsigned long getwinprop(Window win, Atom prop, Window **wins);         /* remember to free `wins` after use */
unsigned long getatomprop(Window win, Atom prop, Atom **atoms);         /* remember to free `atoms` after use */
unsigned long getcardprop(Window win, Atom prop);
unsigned long ealloccolor(const char *s);
XID geticonprop(Window win);
int isurgent(Window win);
int hasstate(Window win, Atom atom);
void drawbackground(Window win, Picture icon, int w, int h, int style);
void drawborder(Window win, int w, int h, int style);
void drawpager(Window win, int w, int h, int nrows, int ncols);
void clientmsg(Window win, Atom atom, unsigned long d0, unsigned long d1, unsigned long d2, unsigned long d3, unsigned long d4);
void *emalloc(size_t size);
void *ecalloc(size_t nmemb, size_t size);
void xinit(int *screenw, int *screenh);
void setcolors(const char *windowcolors[STYLE_LAST][COLOR_LAST], const char *selbackground, const char *background, const char *separator, int shadowthickness);
void destroywin(Window win);
void cleanx(void);
void mapwin(Window win);
void unmapwin(Window win);
void reparentwin(Window win, Window parent, int x, int y);
void freepicture(XID pic);
void preparewin(Window win);
void getgeometry(Window win, int *cx, int *cy, int *cw, int *ch);
void moveresize(Window win, int x, int y, int w, int h);
void setbg(Window win, int w, int h, int select);
int max(int x, int y);
int nextevent(XEvent *ev);
Window createminiwindow(Window parent, int border);
Window createwindow(int w, int h, int dockapp, char *class, int argc, char *argv[]);
char *getresource(char *name, char *class);

extern Atom atoms[];
extern Window root;
