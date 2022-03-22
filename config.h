struct Config config = {
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
	},
	.desktopselbg    = "#BABDB6",
	.desktopbg       = "#121212",
	.separator       = "#888A85",
};
