#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "x.h"

#define PROGNAME        "Paginator"
#define RESOURCE        "paginator."
#define MAX_DESKTOPS    100             /* maximum number of desktops */
#define DEF_WIDTH       125             /* default width for the pager */
#define DEF_NCOLS       1               /* default number of columns */
#define SEPARATOR_SIZE  1               /* size of the line between minidesktops */

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
	int screenw, screenh;
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

/* default configuration */
static struct Config config = {
	/* number of columns and rows, if 0 they are computed from the number of desktops */
	.ncols = 2,
	.nrows = 0,

	/* size of 3D shadow effect; must be a small number */
	.shadowthickness = 2,

	/* desktop orientation */
	.orient = _NET_WM_ORIENTATION_HORZ,

	/* starting corner on the desktop grid */
	.corner = _NET_WM_TOPLEFT,

	/* colors */
	.windowcolors = {
		[STYLE_ACTIVE]   = {"#3465A4", "#729FCF", "#204A87"},
		[STYLE_INACTIVE] = {"#555753", "#888A85", "#2E3436"},
		[STYLE_URGENT]   = {"#CC0000", "#EF2929", "#A40000"},
	},
	.desktopselbg    = "#BABDB6",
	.desktopbg       = "#121212",
	.separator       = "#888A85",
};

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: paginator [-iw] [-c corner] [-g geometry] [-l layout] [-o orientation]\n");
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
		config.desktopbg = s;
	if ((s = getresource(RESOURCE "selbackground", "*")) != NULL)
		config.desktopselbg = s;
	if ((s = getresource(RESOURCE "separator", "*")) != NULL)
		config.separator = s;

	if ((s = getresource(RESOURCE "numColumns", "*")) != NULL)
		if ((n = strtol(s, NULL, 10)) > 0 && n < 100)
			config.ncols = n;
	if ((s = getresource(RESOURCE "numRows", "*")) != NULL)
		if ((n = strtol(s, NULL, 10)) > 0 && n < 100)
			config.nrows = n;
	if ((s = getresource(RESOURCE "shadowThickness", "*")) != NULL)
		if ((n = strtol(s, NULL, 10)) > 0 && n < 100)
			config.shadowthickness = n;

	if ((s = getresource(RESOURCE "orientation", "*")) != NULL) {
		if (s[0] == 'h' || s[0] == 'H') {
			config.orient = _NET_WM_ORIENTATION_HORZ;
		} else if (s[0] == 'v' || s[0] == 'V') {
			config.orient = _NET_WM_ORIENTATION_VERT;
		}
	}
	if ((s = getresource(RESOURCE "startingCorner", "*")) != NULL) {
		if (strcasecmp(s, "TOPLEFT") == 0) {
			config.corner = _NET_WM_TOPLEFT;
		} else if (strcasecmp(s, "TOPRIGHT") == 0) {
			config.corner = _NET_WM_TOPRIGHT;
		} else if (strcasecmp(s, "TOPRIGHT") == 0) {
			config.corner = _NET_WM_BOTTOMLEFT;
		} else if (strcasecmp(s, "TOPRIGHT") == 0) {
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

/* free desktops and the desktop array */
static void
cleandesktops(void)
{
	size_t i;

	for (i = 0; i < pager.ndesktops; i++) {
		if (pager.desktops[i]->miniwin != None)
			destroywin(pager.desktops[i]->miniwin);
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
			destroywin(cp->miniwins[i]);
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
		freepicture(cp->icon);
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
	destroywin(pager.win);
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
		moveresize(
			pager.desktops[i]->miniwin,
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
		unmapwin(cp->miniwins[i]);
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
			reparentwin(cp->miniwins[i], pager.desktops[i]->miniwin, cp->x, cp->y);
		}
	} else if (cp->nminiwins == 1) {
		reparentwin(cp->miniwins[0], pager.desktops[cp->desk]->miniwin, cp->x, cp->y);
	}
}

/* map desktop miniwindows */
static void
mapdesktops(void)
{
	size_t i;

	for (i = 0; i < pager.ndesktops; i++) {
		setbg(pager.desktops[i]->miniwin, (i == pager.currdesktop));
		mapwin(pager.desktops[i]->miniwin);
	}
}

/* set size of client's miniwindow and map client's miniwindow */
static void
configureclient(struct Desktop *dp, struct Client *cp, size_t i)
{
	if (cp == NULL)
		return;

	cp->x = cp->cx * dp->w / pager.screenw;
	cp->y = cp->cy * dp->h / pager.screenh;
	cp->w = max(1, cp->cw * dp->w / pager.screenw - 2 * config.shadowthickness);
	cp->h = max(1, cp->ch * dp->h / pager.screenh - 2 * config.shadowthickness);
	drawclient(cp);
	moveresize(cp->miniwins[i], cp->x, cp->y, cp->w, cp->h);
	if (cp->ismapped || cp->nmappedwins != cp->nminiwins) {
		mapwin(cp->miniwins[i]);
	}
}

/* get size of client's window and call routine to map client's miniwindow */
static void
mapclient(struct Client *cp)
{
	size_t i;

	if (cp == NULL)
		return;
	if (pager.showingdesk || cp->ishidden || cp->desk < 0 || (cp->desk >= pager.ndesktops && cp->desk != ALLDESKTOPS)) {
		unmapclient(cp);
		return;
	}
	getgeometry(cp->clientwin, &cp->cx, &cp->cy, &cp->cw, &cp->ch);
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
	drawpager(pager.win, pager.w, pager.h, pager.nrows, pager.ncols);
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
		pager.desktops[i]->miniwin = createminiwindow(pager.win, 0);
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
				cp->miniwins[i] = createminiwindow( pager.win, config.shadowthickness);
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
			preparewin(clients[i]->clientwin);
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

/* initialize colors */
static void
initcolors(void)
{
}

/* create prompt window */
static void
initpager(int argc, char *argv[])
{
	/* create pager window */
	pager.win = createwindow(
		1, 1,
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
	/* get initial number of desktops */
	setndesktops();

	/* compute initial pager size */
	if (pager.w <= 0 && pager.h <= 0)
		pager.w = DEF_WIDTH;
	if (pager.w > 0 && pager.h <= 0)
		pager.h = (SEPARATOR_SIZE * pager.nrows) + (pager.nrows * (pager.w - (SEPARATOR_SIZE * pager.ncols)) * pager.screenh) / (pager.ncols * pager.screenw);
	if (pager.w <= 0 && pager.h > 0)
		pager.w = (SEPARATOR_SIZE * pager.ncols) + (pager.ncols * (pager.h - (SEPARATOR_SIZE * pager.nrows)) * pager.screenw) / (pager.nrows * pager.screenh);

	/* compute user-defined pager position */
	if (config.xnegative)
		config.x += pager.screenw - pager.w;
	if (config.ynegative)
		config.y += pager.screenh - pager.h;

	/* commit pager window geometry */
	moveresize(pager.win, config.x, config.y, pager.w, pager.h);

	/* get initial client list */
	setdeskgeom();
	drawpager(pager.win, pager.w, pager.h, pager.nrows, pager.ncols);
	setcurrdesktop();
	setclients();
	setactive();
	mapdesktops();
	mapclients();

	/* map window */
	mapwin(pager.win);
}

/* if win is a mini-client-window, focus its client; if it's a mini-desk-window, change to its desktop */
static void
focus(Window win, unsigned int button)
{
	size_t i, j;

	if (ev->button != Button1 && ev->button != Button3)
		return;
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
				if (ev->button == Button1)
					clientmsg(pager.clients[i]->clientwin, atoms[_NET_ACTIVE_WINDOW], 2, CurrentTime, 0, 0, 0);
				else
					clientmsg(None, atoms[_NET_CURRENT_DESKTOP], i, CurrentTime, 0, 0, 0);
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
	focus(ev->window, ev->button);
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
	struct Client *c;

	ev = &e->xconfigure;
	if (ev->window == root) {
		/* screen size changed (eg' a new monitor was plugged-in) */
		pager.screenw = ev->width;
		pager.screenh = ev->height;
		mapdrawall();
	} else {
		if (ev->window == pager.win && setpagersize(ev->width, ev->height)) {
			/* the pager window may have been resized */
			mapdrawall();
		}
		if ((c = getclient(ev->window)) != NULL) {
			/* a client window window may have been moved or resized */
			mapclient(c);
		}
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

	xinit(&pager.screenw, &pager.screenh);
	getresources();
	getoptions(argc, argv);
	setcolors(config.windowcolors, config.desktopselbg, config.desktopbg, config.separator, config.shadowthickness);
	initcolors();
	initpager(argc, argv);
	initclients();
	while (running && nextevent(&ev))
		if (xevents[ev.type] != NULL)
			(*xevents[ev.type])(&ev);
	cleandesktops();
	cleanclients();
	cleanpager();
	cleanx();
	return 0;
}
