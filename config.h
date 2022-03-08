struct Config config = {
	/* number of columns and rows, if 0 they are computed from the number of desktops */
	.ncols = 2,
	.nrows = 0,

	/* desktop orientation */
	.orient = _NET_WM_ORIENTATION_HORZ,

	/* starting corner on the desktop grid */
	.corner = _NET_WM_TOPLEFT,

	/* colors */
	.windowcolors[STYLE_ACTIVE][COLOR_BACKGROUND]   = "#729FCF",
	.windowcolors[STYLE_ACTIVE][COLOR_BORDER]       = "#204A87",
	.windowcolors[STYLE_INACTIVE][COLOR_BACKGROUND] = "#555753",
	.windowcolors[STYLE_INACTIVE][COLOR_BORDER]     = "#888A85",
	.desktopselbg    = "#2E3436",
	.desktopbg       = "#121212",
	.separator       = "#888A85",
};
