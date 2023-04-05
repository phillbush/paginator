#include <sys/queue.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "x.h"

#define PROGNAME        "Taskinator"
#define RESOURCE        "taskinator."
#define DEF_WIDTH       63
#define DEF_HEIGHT      24
#define DEF_PADDING     2

TAILQ_HEAD(Queue, Client);
/* client info */
struct Client {
	TAILQ_ENTRY(Client) entries;
	Window clientwin;
	Window miniwin;
	Picture icon;
	Pixmap pix;
	unsigned long desktop;
	int w, h;
	int ishidden;
	int isurgent;
	int exists;
	char *title;
};

/* the pager */
struct Pager {
	Atom list;
	struct Queue clientq;
	struct Client *active;
	Window win;
	unsigned long currdesktop;
	int w, h;
};

/* whether we're running */
static int running = 1;

/* command-line flags */
static int dflag = 0;                   /* whether to show windows from all desktops */
static int hflag = 0;                   /* whether to show only hidden windows */
static int iflag = 0;                   /* whether to draw icons */
static int vflag = 0;                   /* whether to use vertical orientation */
static int wflag = 0;                   /* whether to start in withdrawn mode */

/* global pager variable */
static struct Pager pager = {
	.active = NULL,
	.win = None,
	.w = DEF_WIDTH,
	.h = DEF_HEIGHT,
};

/* configuration structure */
struct Config {
	int shadowthickness;                    /* thickness of the miniwindow shadows */

	/* color names */
	const char *background;
	const char *windowcolors[STYLE_LAST][COLOR_LAST];

	const char *font;
};

/* default configuration */
static struct Config config = {
	/* size of 3D shadow effect; must be a small number */
	.shadowthickness = 2,

	/* colors */
	.windowcolors = {
		[STYLE_ACTIVE]   = {"#3465A4", "#729FCF", "#204A87"},
		[STYLE_INACTIVE] = {"#555753", "#888A85", "#2E3436"},
		[STYLE_URGENT]   = {"#CC0000", "#EF2929", "#A40000"},
	},
	.background = "#121212",

	/* font */
	.font = "monospace:pixelsize=11",
};

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: paginator [-dhivw] [-a atom] [-g geometry]\n");
	exit(1);
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	long n;
	char *s;

	if ((s = getresource(RESOURCE "activeBackground", "*")) != NULL)
		config.windowcolors[STYLE_ACTIVE][COLOR_MID] = s;
	if ((s = getresource(RESOURCE "activeTopShadowColor", "*")) != NULL)
		config.windowcolors[STYLE_ACTIVE][COLOR_LIGHT] = s;
	if ((s = getresource(RESOURCE "activeBottomShadowColor", "*")) != NULL)
		config.windowcolors[STYLE_ACTIVE][COLOR_DARK] = s;

	if ((s = getresource(RESOURCE "inactiveBackground", "*")) != NULL)
		config.windowcolors[STYLE_INACTIVE][COLOR_MID] = s;
	if ((s = getresource(RESOURCE "inactiveTopShadowColor", "*")) != NULL)
		config.windowcolors[STYLE_INACTIVE][COLOR_LIGHT] = s;
	if ((s = getresource(RESOURCE "inactiveBottomShadowColor", "*")) != NULL)
		config.windowcolors[STYLE_INACTIVE][COLOR_DARK] = s;

	if ((s = getresource(RESOURCE "urgentBackground", "*")) != NULL)
		config.windowcolors[STYLE_URGENT][COLOR_MID] = s;
	if ((s = getresource(RESOURCE "urgentTopShadowColor", "*")) != NULL)
		config.windowcolors[STYLE_URGENT][COLOR_LIGHT] = s;
	if ((s = getresource(RESOURCE "urgentBottomShadowColor", "*")) != NULL)
		config.windowcolors[STYLE_URGENT][COLOR_DARK] = s;

	if ((s = getresource(RESOURCE "background", "*")) != NULL)
		config.background = s;

	if ((s = getresource(RESOURCE "shadowThickness", "*")) != NULL)
		if ((n = strtol(s, NULL, 10)) > 0 && n < 100)
			config.shadowthickness = n;
}

/* parse command-line options */
static void
getoptions(int argc, char **argv)
{
	int ch;
	int x, y;

	while ((ch = getopt(argc, argv, "a:dg:hivw")) != -1) {
		switch (ch) {
		case 'a':
			pager.list = XInternAtom(dpy, optarg, True);
			if (pager.list == None)
				errx(1, "atom does not exist: %s", optarg);
			break;
		case 'd': dflag = 1; break;
		case 'g':
			XParseGeometry(optarg, &x, &y, &pager.w, &pager.h);
			break;
		case 'h': hflag = 1; break;
		case 'i': iflag = 1; break;
		case 'v': vflag = 1; break;
		case 'w': wflag = 1; break;
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

/* destroy client's mini-window and free it */
static void
cleanclient(struct Client *cp)
{
	if (cp->miniwin != None)
		destroywin(cp->miniwin);
	if (cp->icon != None)
		freepicture(cp->icon);
	if (pager.active == cp)
		pager.active = NULL;
	TAILQ_REMOVE(&pager.clientq, cp, entries);
}

/* free clients and client array */
static void
cleanclients(void)
{
	struct Client *cp;

	TAILQ_FOREACH(cp, &pager.clientq, entries) {
		cleanclient(cp);
	}
}

/* free pager window and pixmap */
static void
cleanpager(void)
{
	destroywin(pager.win);
}

/* redraw client miniwindow */
static void
drawclient(struct Client *cp)
{
	int style;

	if (cp == NULL)
		return;
	if (cp == pager.active)
		style = STYLE_ACTIVE;
	else if (cp->isurgent)
		style = STYLE_URGENT;
	else
		style = STYLE_INACTIVE;
	drawborder(cp->miniwin, cp->w, cp->h, style);
	drawbackground(cp->miniwin, cp->icon, cp->w, cp->h, style);
}

/* remap all client miniwindows */
static void
mapclients(void)
{
	struct Client *cp;
	size_t n, i;
	int part, pos;

	pos = 0;
	if (vflag) {
		TAILQ_FOREACH(cp, &pager.clientq, entries) {
			if ((hflag && !cp->ishidden) || (cp->desktop != ALLDESKTOPS && !dflag && cp->desktop != pager.currdesktop)) {
				unmapwin(cp->miniwin);
			} else {
				cp->w = max(1, pager.w - 2 * config.shadowthickness);;
				cp->h = DEF_HEIGHT;
				drawclient(cp);
				moveresize(cp->miniwin, 0, pos, cp->w, cp->h);
				mapwin(cp->miniwin);
				pos += DEF_HEIGHT + 2 * config.shadowthickness;
			}
		}
	} else {
		n = i = 0;
		TAILQ_FOREACH(cp, &pager.clientq, entries) {
			if ((hflag && !cp->ishidden) || (cp->desktop != ALLDESKTOPS && !dflag && cp->desktop != pager.currdesktop)) {
				unmapwin(cp->miniwin);
			} else {
				n++;
			}
		}
		if (n == 0)
			return;
		part = (pager.w - (n * 2 * config.shadowthickness)) / n;
		TAILQ_FOREACH(cp, &pager.clientq, entries) {
			if ((!hflag || !cp->ishidden) && (cp->desktop == ALLDESKTOPS || dflag || cp->desktop == pager.currdesktop)) {
				cp->w = max(1, ((i + 1) * part) - (i * part));
				cp->h = DEF_HEIGHT;
				drawclient(cp);
				moveresize(cp->miniwin, pos, 0, cp->w, cp->h);
				mapwin(cp->miniwin);
				pos += cp->w + 2 * config.shadowthickness;
			}
		}
	}
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

/* return from client list the client with given client window */
static struct Client *
getclient(Window win)
{
	struct Client *cp;

	TAILQ_FOREACH(cp, &pager.clientq, entries)
		if (cp->clientwin == win)
			return cp;
	return NULL;
}

/* set hidden state of given client; then map or unmap it */
static void
sethidden(struct Client *cp)
{
	if (cp == NULL)
		return;
	cp->ishidden = hasstate(cp->clientwin, atoms[_NET_WM_STATE_HIDDEN]);
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
static void
setdesktop(struct Client *cp)
{
	cp->desktop = getcardprop(cp->clientwin, atoms[_NET_WM_DESKTOP]);
	if (hasstate(cp->clientwin, atoms[_NET_WM_STATE_STICKY]))
		cp->desktop = ALLDESKTOPS;
}

/* update list of clients; remap mini-client-windows if list has changed */
static void
setclients(void)
{
	struct Client *cp, *tmp;
	Window *wins;
	size_t nclients;
	size_t i;

	nclients = getwinprop(root, pager.list, &wins);
	TAILQ_FOREACH(cp, &pager.clientq, entries)
		cp->exists = 0;
	for (i = 0; i < nclients; i++) {
		cp = getclient(wins[i]);
		if (cp == NULL) {
			cp = emalloc(sizeof(*cp));
			*cp = (struct Client) {
				.ishidden = 0,
				.isurgent = 0,
				.w = 1,
				.h = 1,
			};
			cp->miniwin = createminiwindow(pager.win, config.shadowthickness);
			cp->clientwin = wins[i];
			preparewin(cp->clientwin);
			cp->icon = iflag ? geticonprop(cp->clientwin) : None;
			TAILQ_INSERT_TAIL(&pager.clientq, cp, entries);
		}
		sethidden(cp);
		setdesktop(cp);
		cp->exists = 1;
	}
	tmp = NULL;
	for (cp = TAILQ_FIRST(&pager.clientq); cp != NULL; cp = tmp) {
		tmp = TAILQ_NEXT(cp, entries);
		if (!cp->exists) {
			cleanclient(cp);
		}
	}
	XFree(wins);
	mapclients();
}

/* set current desktop */
static void
setcurrdesktop(void)
{
	unsigned long prevdesktop;

	prevdesktop = pager.currdesktop;
	pager.currdesktop = getcardprop(root, atoms[_NET_CURRENT_DESKTOP]);
	if (!dflag && prevdesktop != pager.currdesktop) {
		mapclients();
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

/* create prompt window */
static void
initpager(int argc, char *argv[])
{
	TAILQ_INIT(&pager.clientq);

	/* create pager window */
	pager.win = createwindow(
		pager.w, pager.h,
		wflag,
		PROGNAME,
		argc,
		argv
	);
}

/* set initial list of desktops and clients */
static void
initclients(void)
{
	/* get initial client list */
	setcurrdesktop();
	setclients();
	setactive();
	mapclients();

	/* map window */
	mapwin(pager.win);
}

/* if win is a mini-client-window, focus its client; if it's a mini-desk-window, change to its desktop */
static void
focus(Window win)
{
	struct Client *cp;

	TAILQ_FOREACH(cp, &pager.clientq, entries) {
		if (cp == NULL)
			continue;
		if (win == cp->miniwin) {
			clientmsg(cp->clientwin, atoms[_NET_ACTIVE_WINDOW], 2, CurrentTime, 0, 0, 0);
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

/* act upon configuration change (paginator watches clients' configuration changes) */
static void
xeventconfigurenotify(XEvent *e)
{
	XConfigureEvent *ev;

	ev = &e->xconfigure;
	if (ev->window == pager.win && setpagersize(ev->width, ev->height)) {
		/* the pager window may have been resized */
		mapclients();
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
	if (ev->state != PropertyNewValue)
		return;
	if (ev->atom == pager.list) {
		/* the list of windows was reset */
		setclients();
	} else if (ev->atom == atoms[_NET_ACTIVE_WINDOW]) {
		/* the active window value was reset */
		setactive();
	} else if (ev->atom == atoms[_NET_CURRENT_DESKTOP]) {
		/* the current desktop value was reset */
		setcurrdesktop();
	} else if (ev->atom == atoms[_NET_WM_STATE]) {
		/* the list of states of a window (which may or may not include a relevant state) was reset */
		if ((cp = getclient(ev->window)) == NULL)
			return;
		sethidden(cp);
		setdesktop(cp);
		seturgency(cp);
		mapclients();
	} else if (ev->atom == atoms[_NET_WM_DESKTOP]) {
		/* the desktop of a window was reset */
		if ((cp = getclient(ev->window)) == NULL)
			return;
		setdesktop(cp);
		mapclients();
	} else if (ev->atom == XA_WM_HINTS) {
		/* the urgency state of a window was reset */
		if ((cp = getclient(ev->window)) == NULL)
			return;
		seturgency(cp);
	} else if (ev->atom == atoms[_NET_WM_ICON] && iflag) {
		if ((cp = getclient(ev->window)) == NULL)
			return;
		freepicture(cp->icon);
		cp->icon = geticonprop(cp->clientwin);
		drawclient(cp);
	}
}

static void
xeventexpose(XEvent *e)
{
	(void)e;
}

/* paginator: a X11 desktop pager */
int
main(int argc, char *argv[])
{
	XEvent ev;
	void (*xevents[LASTEvent])(XEvent *) = {
		[ButtonRelease]         = xeventbuttonrelease,
		[Expose]                = xeventexpose,
		[ConfigureNotify]       = xeventconfigurenotify,
		[ClientMessage]         = xeventclientmessage,
		[PropertyNotify]        = xeventpropertynotify,
	};
	xinit(NULL, NULL);
	pager.list = atoms[_NET_CLIENT_LIST_STACKING];
	getresources();
	getoptions(argc, argv);
	setcolors(config.windowcolors, NULL, config.background, NULL, config.shadowthickness);
	initpager(argc, argv);
	initclients();
	while (running && nextevent(&ev))
		if (xevents[ev.type] != NULL)
			(*xevents[ev.type])(&ev);
	cleanclients();
	cleanpager();
	cleanx();
	return 0;
}
