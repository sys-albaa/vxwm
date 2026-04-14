/* See LICENSE file for copyright and license details.
This wm is forked from dwm 6.7 (but keeps up with all dwm's updates), thanks suckless for their incredible work on dwm!
Infinite tags module is heavily inspired from 5element which is inspired from the hevel wayland compositor.

vxwm 2.2 // by wh1tepearl

I just realised that i haven't commenting the entire code, sure i can perfectly read it but for the people that want to fork vxwm/make something with vxwm's code it is a pain in the ass.
From this moment, i'll try to comment the code and also make it more readable.

*/

// Modules configuration is in modules.h
// Config is in config.h

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "modules.h"
#include "drw.h"
#include "util.h"

/*
 * vxwm.c is the main window manager implementation for vxwm.
 * It handles X11 setup, event processing, layout management,
 * window rules, systray handling, and user interaction.
 */

#if INFINITE_TAGS && !WINDOWMAP
    #undef WINDOWMAP
    #define WINDOWMAP 1
#endif

#if ENHANCED_TOGGLE_FLOATING && !FLOATING_LAYOUT_FLOATS_WINDOWS
  #undef FLOATING_LAYOUT_FLOATS_WINDOWS
  #define FLOATING_LAYOUT_FLOATS_WINDOWS 1
#endif

/* macros */
/* Common helper macros used throughout the window manager.
 * These simplify event masks, window dimensions, tag masks,
 * and visibility calculations.
 */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define SYSTEM_TRAY_REQUEST_DOCK    0
/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10
#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_DEACTIVATE    2
#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR
#if !WINDOWMAP
  #if !PDWM_LIKE_TAGS_ANIMATION
    #if !SLOWER_TAGS_ANIMATION
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);  // Vanilla
    #else
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, WIDTH(c) * -1, c->y);  // Slower vanilla
    #endif
  #else
    #if !SLOWER_TAGS_ANIMATION
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, c->mon->wx + c->mon->ww / 2, -(HEIGHT(c) * 3) / 2);  // pdwm vanilla
    #else
      #define SHOWHIDEPROFILE XMoveWindow(dpy, c->win, c->mon->wx + c->mon->ww / 2, -(HEIGHT(c)));  // Slower pdwm
    #endif
  #endif
#else
  #define SHOWHIDEPROFILE 		if (c->ismapped) { \
			                          window_unmap(dpy, c->win, root, 1); \
			                          c->ismapped = 0; \
		                          } 
#endif

/* This is purely for a bit less sucky config */
#if !XRDB
#define MAYBE_CONST const
#else
#define MAYBE_CONST
#endif

/* enums */
/* Enumerations used for cursors, color schemes, atoms, click targets,
 * and X11 protocol state management.
 */
enum { CurNormal, CurResize, CurMove,
#if BETTER_RESIZE && BR_CHANGE_CURSOR
       CurNW, CurNE, CurSW, CurSE,  // corner cursors
       CurN, CurS, CurE, CurW,       // edge cursors
#endif
       CurLast }; /* cursor */

enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
#if !EWMH_TAGS
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
#else //EWMH_TAGS 
       NetWMWindowTypeDialog, NetClientList, NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop, NetDesktopNum, NetLast }; /* EWMH atoms */
#endif
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

/* Argument type used by key bindings and button actions.
 * It can carry an integer, unsigned integer, float, or pointer value.
 */
typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

/* Forward declarations for Monitor and Client so that structs can refer to each other. */
typedef struct Monitor Monitor;
typedef struct Client Client;
/* Client structure stores information about a managed window.
 * It tracks geometry, size hints, window state, tags, and linkage
 * to the current monitor and client list.
 */
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
#if INFINITE_TAGS
  int saved_cx, saved_cy;
  int saved_cw, saved_ch;
  int was_on_canvas;
#endif
#if WINDOWMAP
  int ismapped;
#endif
#if ENHANCED_TOGGLE_FLOATING
  int sfx, sfy, sfw, sfh;
  #if RESTORE_SIZE_AND_POS_ETF
    int wasmanuallyedited;
  #endif
#endif 
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

#if INFINITE_TAGS
typedef struct {
    int cx, cy;
    int saved_cx, saved_cy;
} CanvasOffset;
#endif

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
#if GAPS
  int gappx;            /* gaps between windows */
#endif
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt[2];
#if INFINITE_TAGS
  CanvasOffset *canvas;
#endif
#if EXTERNAL_BARS
  int strut_top, strut_bottom, strut_left, strut_right;
#endif
};

/* Rule structure defines automatic rules for new windows.
 * Rules can match class, instance, title, tags, floating state, and monitor.
 */
typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
} Rule;

typedef struct Systray   Systray;
struct Systray {
	Window win;
	Client *icons;
};

/* function declarations */
/* Function prototypes for all internal event handlers, helpers,
 * and window management operations used in vxwm.
 */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static Monitor *systraytomon(Monitor *m);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

#include "modules/vxwm_includes.h"

#include "config.h"

/* variables */
/* Global state for the window manager, including display handles,
 * monitors, selected client, colors, cursors, and atoms.
 */
static Systray *systray = NULL;
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */

#if BAR_PADDING
static int vp;               /* vertical padding for bar */
static int sp;               /* side padding for bar */
#endif

static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[ResizeRequest] = resizerequest,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "modules/vxwm_includes.c"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
/* Apply window rules from config to a newly managed client.
 * This sets initial tags, floating state, and monitor assignment.
 */
void
applyrules(Client *c)
{
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

/* Apply size hints and constraints for a client window.
 * This enforces minimum/maximum sizes, aspect ratios, increments,
 * and keeps windows within monitor bounds.
 */
int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

/* Arrange windows on a monitor or all monitors.
 * This shows/hides clients and triggers the layout algorithm.
 */
/* Arrange one monitor or all monitors by showing/hiding clients
 * and then applying the current layout to each monitor.
 */
void
arrange(Monitor *m)
{
#if WINDOWMAP
	XGrabServer(dpy);
#endif
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
#if WINDOWMAP
	XUngrabServer(dpy);
	XSync(dpy, False);
#endif
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

/* Run the layout function for a specific monitor.
 * This updates the layout symbol and calls the arrange callback.
 */
void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol - 1);
  m->ltsymbol[sizeof m->ltsymbol - 1] = '\0';
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

/* Attach a client to the head of the monitor's client list. */
/* Insert client at the beginning of the monitor's client list. */
void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

/* Attach a client to the head of the stacked client list.
 * The stack is used for focus order and stacking operations.
 */
/* Insert client at the beginning of the stacked client list.
 * This list defines focus traversal and stacking order.
 */
void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

/* Handle mouse button presses on the root window, status bar,
 * tag bar, and client windows.
 */
/* Handle pointer button events from the bar, tags, status text,
 * and client windows. This determines click targets and executes
 * configured button actions.
 */
void
buttonpress(XEvent *e)
{
#if !OCCUPIED_TAGS_DECORATION
	unsigned int i, x, click;
#else
  unsigned int i, x, click, occ;
#endif
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
#if !OCCUPIED_TAGS_DECORATION
		i = x = 0;
#else 
		i = x = occ = 0;
		/* Bitmask of occupied tags */
		for (c = m->clients; c; c = c->next)
			occ |= c->tags;
#endif
		do
#if !OCCUPIED_TAGS_DECORATION
			x += TEXTW(tags[i]);
#else 
			x += TEXTW(occ & 1 << i ? occupiedtags[i] : tags[i]);
#endif
		while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < x + TEXTW(selmon->ltsymbol))
			click = ClkLtSymbol;
		else if (ev->x > selmon->ww - (int)TEXTW(stext) - getsystraywidth())
			click = ClkStatusText;
		else
			click = ClkWinTitle;
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

/* Check whether another window manager is already running.
 * If a different WM has grabbed the root window, vxwm exits.
 */
/* Detect whether another window manager has already grabbed
 * the root window. If so, vxwm aborts startup.
 */
void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

/* Cleanup all resources before exiting vxwm.
 * This removes windows, frees memory, and restores input focus.
 */
/* Clean up all resources before exit: unmanage windows, destroy bars,
 * free memory and restore the input focus to the X server.
 */
void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);

	if (showsystray) {
		XUnmapWindow(dpy, systray->win);
		XDestroyWindow(dpy, systray->win);
		free(systray);
	}

	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		drw_scm_free(drw, scheme[i], 3);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

/* Cleanup a single monitor, remove its bar, and free its state. */
/* Remove and free a monitor object when it is no longer needed.
 * This also destroys the monitor's bar window.
 */
void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
#if INFINITE_TAGS
  free(mon->canvas);
#endif
  free(mon);

}

/* Handle client messages sent by windows or the X server.
 * This includes fullscreen state and active window urgency notifications.
 */
/* Handle ClientMessage events from X11 windows.
 * This covers fullscreen requests, active window hints, and systray docking.
 */
void
clientmessage(XEvent *e)
{
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
		if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
			if (!(c = (Client *)calloc(1, sizeof(Client))))
				die("fatal: could not malloc() %u bytes\n", sizeof(Client));
			if (!(c->win = cme->data.l[2])) {
				free(c);
				return;
			}
			c->mon = selmon;
			c->next = systray->icons;
			systray->icons = c;
			if (!XGetWindowAttributes(dpy, c->win, &wa)) {
				wa.width = bh;
				wa.height = bh;
				wa.border_width = 0;
			}
			c->x = c->oldx = c->y = c->oldy = 0;
			c->w = c->oldw = wa.width;
			c->h = c->oldh = wa.height;
			c->oldbw = wa.border_width;
			c->bw = 0;
			c->isfloating = True;
			c->tags = 1;
			updatesizehints(c);
			updatesystrayicongeom(c, wa.width, wa.height);
			XAddToSaveSet(dpy, c->win);
			XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
			XReparentWindow(dpy, c->win, systray->win, 0, 0);
			swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
			XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0, systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0, systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0, systray->win, XEMBED_EMBEDDED_VERSION);
			XSync(dpy, False);
			resizebarwin(selmon);
			updatesystray();
			setclientstate(c, NormalState);
		}
		return;
	}

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
}

/* Send a synthetic ConfigureNotify event to a client.
 * This keeps client window state in sync with X11.
 */
/* Send a synthetic ConfigureNotify event to the managed client.
 * This keeps the window's window manager hints synchronized.
 */
void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

/* Respond to ConfigureNotify events from the root window.
 * This updates monitor geometry and adjusts bars and fullscreen clients.
 */
void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
#if !BAR_PADDING
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
#else
        XMoveResizeWindow(dpy, m->barwin, m->wx + sp, m->by + vp, m->ww -  2 * sp, bh);
#endif
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

/* Handle requests from windows to change their configuration.
 * For managed clients, this may update floating geometry or pass
 * unhandled requests through to XConfigureWindow.
 */
void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

/* Create a new monitor structure with default layout and tag state.
 * This is called during startup and when monitor geometry changes.
 */
Monitor *
createmon(void)
{
	Monitor *m;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
#if GAPS
  m->gappx = gappx;
#endif
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
#if INFINITE_TAGS
  m->canvas = ecalloc(LENGTH(tags), sizeof(CanvasOffset));
  unsigned int i;
  for (i = 0; i < LENGTH(tags); i++) {
      m->canvas[i].cx = 0;
      m->canvas[i].cy = 0;
  }
#endif
	return m;
}

/* Handle DestroyNotify events for managed windows and systray icons.
 * When a client is destroyed, it is removed from management.
 */
void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
	else if ((c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		resizebarwin(selmon);
		updatesystray();
	}
#if EXTERNAL_BARS
  externalbars_unregister(ev->window);
#endif
}

/* Remove a client from the monitor's client list. */
void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

/* Remove a client from the stacked focus list.
 * If the removed client was selected, select the next visible client.
 */
void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

/* Return the monitor in the given direction relative to the selected monitor.
 * This is used for monitor switching and moving clients between screens.
 */
Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

/* Draw the status bar for a single monitor.
 * This includes tags, layout symbol, window title, and status text.
 */
void
drawbar(Monitor *m)
{
	int x, w, tw = 0, stw = 0;
	int boxs = drw->fonts->h / 9;
	int boxw = drw->fonts->h / 6 + 2;
	unsigned int i, occ = 0, urg = 0;
#if OCCUPIED_TAGS_DECORATION
  const char *tagtext;	
#endif
  Client *c;
	if (!m->showbar)
		return;

	if (showsystray && m == systraytomon(m) && !systrayonleft)
		stw = getsystraywidth();

	/* draw status first so it can be overdrawn by tags later */
	if (m == selmon) { /* status is only drawn on selected monitor */
		drw_setscheme(drw, scheme[SchemeNorm]);
		tw = TEXTW(stext) - lrpad / 2 + 2; /* 2px extra right padding */
		drw_text(drw, m->ww - tw - stw, 0, tw, bh, lrpad / 2 - 2, stext, 0);
	}

	resizebarwin(m);
	for (c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
#if !OCCUPIED_TAGS_DECORATION
		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
		if (occ & 1 << i)
			drw_rect(drw, x + boxs, boxs, boxw, boxw,
				m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
				urg & 1 << i);
#else
		tagtext = occ & 1 << i ? occupiedtags[i] : tags[i];
		w = TEXTW(tagtext);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tagtext, urg & 1 << i);
#endif
		x += w;
	}

	w = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeNorm]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

#if INFINITE_TAGS && IT_SHOW_COORDINATES_IN_BAR

  #if COORDINATES_DIVISOR <= 0
    #undef COORDINATES_DIVISOR
    #define COORDINATES_DIVISOR 1
  #endif

  if (selmon->lt[selmon->sellt]->arrange == NULL) {
    int tagidx = getcurrenttag(m);
    char coords[64];
    snprintf(coords, sizeof(coords), "[x%d y%d]", 
      m->canvas[tagidx].cx / COORDINATES_DIVISOR,
      m->canvas[tagidx].cy / COORDINATES_DIVISOR);
    w = TEXTW(coords);
    drw_setscheme(drw, scheme[SchemeNorm]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, coords, 0);
    x += w;
  }

#endif

#if BAR_PADDING
if ((w = m->ww - tw - stw - x - 2 * sp) > bh) {
#else
if ((w = m->ww - tw - stw - x) > bh) {
#endif
		if (m->sel) {
#if !ALT_CENTER_OF_BAR_COLOR
			drw_setscheme(drw, scheme[m == selmon ? SchemeSel : SchemeNorm]);
#else
      drw_setscheme(drw, scheme[m == selmon ? SchemeNorm : SchemeNorm]);
#endif
#if !BAR_PADDING
			drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
#else
      drw_text(drw, x, 0, w - 2 * sp, bh, lrpad / 2, m->sel->name, 0);
#endif
			if (m->sel->isfloating)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
#if !BAR_PADDING
			drw_rect(drw, x, 0, w, bh, 1, 1);
#else
      drw_rect(drw, x, 0, w - 2 * sp, bh, 1, 1);
#endif
		}
	}
#if BAR_PADDING
drw_map(drw, m->barwin, 0, 0, m->ww - stw - 2 * sp, bh);
#else
drw_map(drw, m->barwin, 0, 0, m->ww - stw, bh);
#endif
}

/* Redraw the bars for all monitors. */
void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

/* Handle EnterNotify events to switch focus when the pointer
 * enters a new window or monitor.
 */
void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

/* Handle expose events for bar windows and redraw when needed.
 */
void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window))) {
		drawbar(m);
		if (m == selmon)
			updatesystray();
	}
}

/* Change keyboard focus to the given client.
 * This updates window borders, focus order, and input focus.
 */
void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
/* Handle focus-in events for clients that need extra focus handling.
 * This avoids losing focus on some broken X11 clients.
 */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

/* Move focus to the next monitor in the specified direction. */
void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

/* Move focus through the tiled client stack.
 * Positive arg->i goes forward, negative goes backward.
 */
void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return;
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i))
					c = i;
	}
	if (c) {
		focus(c);
#if INFINITE_TAGS
    centerwindow(NULL);
#endif
#if WARP_TO_CLIENT && WARP_TO_CENTER_OF_WINDOW_AFFECTED_BY_FOCUSSTACK
    warptoclient(c);
#endif   
		restack(selmon);
	}
}

/* Read an Atom property from a client window. */
Atom
getatomprop(Client *c, Atom prop)
{
	int format;
	unsigned long nitems, dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &format, &nitems, &dl, &p) == Success && p) {
		if (nitems > 0 && format == 32)
			atom = *(long *)p;
		XFree(p);
	}
	return atom;
}

/* Calculate the total width of the system tray area.
 * This includes icon width and spacing.
 */
unsigned int
getsystraywidth()
{
	unsigned int w = 0;
	Client *i;
	if(showsystray)
		for(i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
	return w ? w + systrayspacing : 0;
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

/* Query a window's WM_STATE property. */
long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, &p) != Success)
		return -1;
	if (n != 0 && format == 32)
		result = *(long *)p;
	XFree(p);
	return result;
}

/* Retrieve a text property from a window and normalize it to UTF-8.
 * Returns 1 on success and 0 on failure.
 */
int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

/* Grab the configured mouse buttons for a client window.
 * This allows mouse actions to be routed to vxwm.
 */
void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

/* Grab all configured key combinations on the root window.
 * Num lock and caps lock modifiers are handled automatically.
 */
void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

#if WARP_TO_CLIENT && WARP_TO_CENTER_OF_WINDOW_AFFECTED_BY_INCNMASTER
//  LARP_TO_CLIENT
void
incnmaster(const Arg *arg)
{
	Client *c;
	unsigned int n;
	
	Client *moved = NULL;
	
	for (n = 0, c = nexttiled(selmon->clients); c; c = nexttiled(c->next), n++);
	
	if (arg->i > 0) {
		moved = nexttiled(selmon->clients);
		for (n = 0; moved && n < selmon->nmaster; n++)
			moved = nexttiled(moved->next);
	} else if (arg->i < 0 && selmon->nmaster > 0) {
		moved = nexttiled(selmon->clients);
		for (n = 1; moved && n < selmon->nmaster; n++)
			moved = nexttiled(moved->next);
	}
	
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
	
	if (moved)
		warptoclient(moved);
}

#else

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

#endif

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

/* Handle key press events and dispatch them to configured key bindings. */
void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

/* Kill the currently selected client window.
 * Attempts WM_DELETE_WINDOW first, then forces the client to close.
 */
void
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;

	if (!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

/* Manage a new window: create a Client object, apply rules,
 * set up event handling, and add it to monitor lists.
 */
void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
#if WINDOWMAP
  c->ismapped = 0;
#endif
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;
#if ENHANCED_TOGGLE_FLOATING
  c->sfx = c->x;
  c->sfy = c->y;
  c->sfw = c->w;
  c->sfh = c->h;
#endif
	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
	}
#if FLOATING_LAYOUT_FLOATS_WINDOWS
  if (selmon->lt[selmon->sellt]->arrange == NULL)
    c->isfloating = 1;
#endif
	if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
#if CENTER_NEW_FLOATING_WINDOWS && !NEW_WINDOWS_APPEAR_UNDER_CURSOR
  c->x = c->mon->wx + (c->mon->ww - WIDTH(c)) / 2;
  c->y = c->mon->wy + (c->mon->wh - HEIGHT(c)) / 2;
#endif
#if NEW_FLOATING_WINDOWS_APPEAR_UNDER_CURSOR
  int mx, my, di;
  unsigned int dui;
  Window dw;
  XQueryPointer(dpy, root, &dw, &dw, &mx, &my, &di, &di, &dui);
    
  c->x = mx - c->w / 2;
  c->y = my - c->h / 2;
#endif
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	#if EWMH_TAGS
	updatewmdesktop(c);
	#endif
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	focus(NULL);
#if WARP_TO_CLIENT && WARP_TO_CENTER_OF_NEW_WINDOW 
  warptoclient(c);
#endif
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

/* Handle MapRequest events for new windows and systray icons.
 * Maps windows that are not override-redirect and not already managed.
 */
void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;
	Client *i;

	if ((i = wintosystrayicon(ev->window))) {
    sendevent(i->win, netatom[Xembed], StructureNotifyMask,
              CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
    resizebarwin(selmon);
    updatesystray();
    return;
}

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
#if EXTERNAL_BARS
	if (externalbars_hasstrut(ev->window)) {
		externalbars_register(ev->window);
		XMapWindow(dpy, ev->window);
		XSelectInput(dpy, ev->window, PropertyChangeMask|StructureNotifyMask);
		return;
	}
#endif
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

/* Monocle layout: make each visible client fullscreen in the monitor area. */
void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

/* Track pointer motion on the root window and switch focus to
 * the monitor under the cursor.
 */
void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

/* Handle mouse-based moving of the selected window.
 * The window is moved with pointer motion and snapped to monitor edges.
 */
void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
#if LOCK_MOVE_RESIZE_REFRESH_RATE
	Time lasttime = 0;
#endif //LOCK_MOVE_RESIZE_REFRESH_RATE
	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
#if LOCK_MOVE_RESIZE_REFRESH_RATE
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;
#endif //LOCK_MOVE_RESIZE_REFRESH_RATE

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
#if !MOVE_IN_TILED
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
#endif
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
#if MOVE_IN_TILED
			else if (selmon->lt[selmon->sellt]->arrange || !c->isfloating) {
				if ((m = recttomon(ev.xmotion.x_root, ev.xmotion.y_root, 1, 1)) != selmon) {
					sendmon(c, m);
					selmon = m;
					focus(NULL);
				}

				Client *cc = c->mon->clients;
				while (1) {
					if (cc == 0) break;
					if(
					 cc != c && !cc->isfloating && ISVISIBLE(cc) &&
					 ev.xmotion.x_root > cc->x &&
					 ev.xmotion.x_root < cc->x + cc->w &&
					 ev.xmotion.y_root > cc->y &&
					 ev.xmotion.y_root < cc->y + cc->h ) {
						break;
					}

					cc = cc->next;
				}
#if !INFINITE_TAGS
				if (cc) {
					Client *cl1, *cl2, ocl1;

					if (!selmon->lt[selmon->sellt]->arrange) return;

					cl1 = c;
					cl2 = cc;
					ocl1 = *cl1;
					strcpy(cl1->name, cl2->name);
					cl1->win = cl2->win;
					cl1->x = cl2->x;
					cl1->y = cl2->y;
					cl1->w = cl2->w;
					cl1->h = cl2->h;

					cl2->win = ocl1.win;
					strcpy(cl2->name, ocl1.name);
					cl2->x = ocl1.x;
					cl2->y = ocl1.y;
					cl2->w = ocl1.w;
					cl2->h = ocl1.h;

					selmon->sel = cl2;

					c = cc;
					focus(c);

					arrange(cl1->mon);
				}
#else // TODO: make this an option in modules.h
				if (cc) {
				  Client *cl1, *cl2, ocl1;

				  if (!selmon->lt[selmon->sellt]->arrange) return;

				  cl1 = c;
				  cl2 = cc;
				  ocl1 = *cl1;

				  strcpy(cl1->name, cl2->name);
				  cl1->win = cl2->win;
				  cl1->x = cl2->x;
				  cl1->y = cl2->y;
				  cl1->w = cl2->w;
				  cl1->h = cl2->h;

				  cl2->win = ocl1.win;
				  strcpy(cl2->name, ocl1.name);
				  cl2->x = ocl1.x;
				  cl2->y = ocl1.y;
				  cl2->w = ocl1.w;
				  cl2->h = ocl1.h;

				  int tmp_cx = cl1->saved_cx;
				  int tmp_cy = cl1->saved_cy;
				  int tmp_cw = cl1->saved_cw;
				  int tmp_ch = cl1->saved_ch;
				  int tmp_was = cl1->was_on_canvas;

				  cl1->saved_cx = cl2->saved_cx;
				  cl1->saved_cy = cl2->saved_cy;
				  cl1->saved_cw = cl2->saved_cw;
				  cl1->saved_ch = cl2->saved_ch;
				  cl1->was_on_canvas = cl2->was_on_canvas;

				  cl2->saved_cx = tmp_cx;
				  cl2->saved_cy = tmp_cy;
				  cl2->saved_cw = tmp_cw;
				  cl2->saved_ch = tmp_ch;
				  cl2->was_on_canvas = tmp_was;

				  selmon->sel = cl2;
				  c = cc;
				  focus(c);

				  arrange(cl1->mon);
		    }
#endif
			}
#endif
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
#if ENHANCED_TOGGLE_FLOATING && RESTORE_SIZE_AND_POS_ETF
  c->wasmanuallyedited = 1;
  if (c->isfloating) {
    c->sfx = c->x;
    c->sfy = c->y;
    c->sfw = c->w;
    c->sfh = c->h;
  }
#endif
}

/* Return the next tiled client starting from c.
 * Floating or invisible clients are skipped.
 */
Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

/* Promote a client to the front of the client list and focus it. */
void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

/* Handle property change notifications on clients and the root window.
 * This includes title updates, window hints, and systray icon state.
 */
void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((c = wintosystrayicon(ev->window))) {
		if (ev->atom == XA_WM_NORMAL_HINTS) {
			updatesizehints(c);
			updatesystrayicongeom(c, c->w, c->h);
		}
		else
			updatesystrayiconstate(c, ev);
		resizebarwin(selmon);
		updatesystray();
	}

#if EXTERNAL_BARS
  if (ev->atom == XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False) ||
      ev->atom == XInternAtom(dpy, "_NET_WM_STRUT", False)) {
    if (ev->state == PropertyNewValue)
      externalbars_register(ev->window);
    else
      externalbars_unregister(ev->window);
    return;
  }
#endif

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

/* Stop the main event loop and begin shutdown. */
void
quit(const Arg *arg)
{
	running = 0;
}

/* Find the monitor that intersects most with the rectangle.
 * Used when moving windows between screens.
 */
Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

/* Remove an icon from the system tray list and free its client data. */
void
removesystrayicon(Client *i)
{
	Client **ii;

	if (!showsystray || !i)
		return;
	for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
	if (*ii)
		*ii = i->next;
	free(i);
}

/* Resize a client after applying size hints and constraints. */
void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

/* Update the bar window geometry for a monitor.
 * This accounts for systray width and optional padding.
 */
void
resizebarwin(Monitor *m) {
	unsigned int w = m->ww;
#if BAR_PADDING
	if (showsystray && m == systraytomon(m) && !systrayonleft)
		w -= getsystraywidth();
	XMoveResizeWindow(dpy, m->barwin, m->wx + sp, m->by + vp, w - 2 * sp, bh);
#else
	if (showsystray && m == systraytomon(m) && !systrayonleft)
		w -= getsystraywidth();
	XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
#endif
}

/* Directly move and resize a client window in X11.
 * This is used after the final size has been determined.
 */
void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

/* Handle resize request events from systray icons. */
void
resizerequest(XEvent *e)
{
	XResizeRequestEvent *ev = &e->xresizerequest;
	Client *i;

	if ((i = wintosystrayicon(ev->window))) {
		updatesystrayicongeom(i, ev->width, ev->height);
		resizebarwin(selmon);
		updatesystray();
	}
}

#if !BETTER_RESIZE
/* Resize the selected window with the mouse.
 * The window grows or shrinks while the pointer moves.
 */
void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
#if LOCK_MOVE_RESIZE_REFRESH_RATE
	Time lasttime = 0;
#endif
	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
#if LOCK_MOVE_RESIZE_REFRESH_RATE
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;
#endif
			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
#if !RESIZING_WINDOWS_IN_ALL_LAYOUTS_FLOATS_THEM
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
#else
      	if (!c->isfloating && (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
#endif
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
#if USE_RESIZECLIENT_FUNC   
        resizeclient(c, c->x, c->y, nw, nh);
#else
        resize(c, c->x, c->y, nw, nh, 1);
#endif
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
#if ENHANCED_TOGGLE_FLOATING && RESTORE_SIZE_AND_POS_ETF
  c->wasmanuallyedited = 1;
  if (c->isfloating) {
    c->sfx = c->x;
    c->sfy = c->y;
    c->sfw = c->w;
    c->sfh = c->h;
  }
#endif
}
#endif //BETTER_RESIZE



/* Restack client windows for a monitor.
 * The selected client is raised and tiled clients are stacked
 * in the correct order below the bar.
 */
void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
#if INFINITE_TAGS
  else {
    if (m->sel && m->sel->isfullscreen)
      return;
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext) {
      if (ISVISIBLE(c) && !c->isfixed && !c->isfullscreen) {
        XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
        wc.sibling = c->win;
      }
    }
    XRaiseWindow(dpy, m->barwin);
  }
#endif
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

/* Main event loop: wait for X events and dispatch handlers. */
void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

/* Scan existing child windows on the root window at startup.
 * Any mapped windows are managed by vxwm.
 */
void
scan(void)
{
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;
 
#if EXTERNAL_BARS
    externalbars_begin_scan();
#endif
    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa)
            || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState) {
#if EXTERNAL_BARS
                if (externalbars_hasstrut(wins[i])) {
                    externalbars_register(wins[i]);
                    continue;
                }
#endif
                manage(wins[i], &wa);
            }
        }
        for (i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(dpy, wins[i], &wa))
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1)
            && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
                manage(wins[i], &wa);
        }
        if (wins)
            XFree(wins);
    }
#if EXTERNAL_BARS
    externalbars_end_scan();
#endif
}

/* Move a client from its current monitor to another monitor.
 * The client is reattached and the layouts are refreshed.
 */
void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	#if EWMH_TAGS
	updatewmdesktop(c);
	#endif
	if (c->isfullscreen)
		resizeclient(c, m->mx, m->my, m->mw, m->mh);
	focus(NULL);
	arrange(NULL);
}

/* Change the WM_STATE property of a client window.
 * This is used for normal, withdrawn, and iconic states.
 */
void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

/* Send a ClientMessage event to a window if the protocol is supported.
 * Used for WM_DELETE_WINDOW, WM_TAKE_FOCUS and XEmbed messages.
 */
int
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
{
	int n;
	Atom *protocols, mt;
	int exists = 0;
	XEvent ev;

	if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
		mt = wmatom[WMProtocols];
		if (XGetWMProtocols(dpy, w, &protocols, &n)) {
			while (!exists && n--)
				exists = protocols[n] == proto;
			XFree(protocols);
		}
	}
	else {
		exists = True;
		mt = proto;
	}

	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = mt;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = d0;
		ev.xclient.data.l[1] = d1;
		ev.xclient.data.l[2] = d2;
		ev.xclient.data.l[3] = d3;
		ev.xclient.data.l[4] = d4;
		XSendEvent(dpy, w, False, mask, &ev);
	}
	return exists;
}

/* Set keyboard focus to the given client and update _NET_ACTIVE_WINDOW.
 * If the client forbids focus, only the active window property is updated.
 */
void
setfocus(Client *c)
{
	if (!c->neverfocus)
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
  XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
  PropModeReplace, (unsigned char *)&c->win, 1);
	sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
}

/* Toggle fullscreen state for a client window.
 * This changes window state, floating behavior, borders, and geometry.
 */
void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

/* Switch the current layout on the selected monitor.
 * If arg->v is NULL, toggle between the two configured layouts.
 */
void
setlayout(const Arg *arg)
{
#if INFINITE_TAGS
    const Layout *temp_new_layout = (arg && arg->v) ? (Layout *)arg->v : selmon->lt[selmon->sellt ^ 1];
    if (temp_new_layout == selmon->lt[selmon->sellt]) return;

    const Layout *old_layout = selmon->lt[selmon->sellt];
#endif    
    if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
        selmon->sellt ^= 1;
    if (arg && arg->v)
        selmon->lt[selmon->sellt] = (Layout *)arg->v;
#if INFINITE_TAGS
    const Layout *new_layout = selmon->lt[selmon->sellt];

    if (old_layout->arrange == NULL && new_layout->arrange != NULL) {
        save_canvas_positions(selmon);
        homecanvas(NULL);  
        Client *c;
        for (c = selmon->clients; c; c = c->next)
            if (!c->isfixed) c->isfloating = 0;
    }
    
    if (new_layout->arrange == NULL) {
        restore_canvas_positions(selmon);
        
        Client *c;
        for (c = selmon->clients; c; c = c->next)
            c->isfloating = 1;
    }
#endif

    strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol - 1);
    selmon->ltsymbol[sizeof selmon->ltsymbol - 1] = '\0';
    arrange(selmon);
}
/* arg > 1.0 will set mfact absolutely */
/* Adjust the master area factor for tiling layouts.
 * Arguments less than 1.0 are relative changes, greater than 1.0 set an absolute value.
 */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

/* Perform initial X11 and window manager setup.
 * This initializes the display, fonts, cursors, bars, atoms, and key grabs.
 */
void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */ //this comment got me giggling so hard man, lol
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
#if !BAR_HEIGHT
	bh = drw->fonts->h + 2;
#else
  bh = user_bh ? user_bh : drw->fonts->h + 2;
#endif
	updategeom();
#if BAR_PADDING
  sp = sidepad;
  vp = (topbar == 1) ? vertpad : - vertpad;
#endif
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	netatom[NetSystemTrayOrientationHorz] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
#if EWMH_TAGS
  netatom[NetDesktopViewport] = XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	netatom[NetDesktopNum] = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
#endif
	xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
	xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
	xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
#if BETTER_RESIZE && BR_CHANGE_CURSOR
  cursor[CurNW] = drw_cur_create(drw, XC_top_left_corner);
  cursor[CurNE] = drw_cur_create(drw, XC_top_right_corner);
  cursor[CurSW] = drw_cur_create(drw, XC_bottom_left_corner);
  cursor[CurSE] = drw_cur_create(drw, XC_bottom_right_corner);
  cursor[CurN]  = drw_cur_create(drw, XC_top_side);
  cursor[CurS]  = drw_cur_create(drw, XC_bottom_side);
  cursor[CurE]  = drw_cur_create(drw, XC_right_side);
  cursor[CurW]  = drw_cur_create(drw, XC_left_side);
#endif
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* init system tray */
	updatesystray();
	/* init bars */
	updatebars();
	updatestatus();
#if BAR_PADDING
  updatebarpos(selmon);
#endif
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "vxwm", 4);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
#if EWMH_TAGS
	setnumdesktops();
	setcurrentdesktop();
	setdesktopnames();
	setviewport();
#endif
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

/* Mark a client as urgent or clear its urgency hint.
 * This updates the WM_HINTS so taskbars and pagers can show it.
 */
void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

/* Recursively show or hide clients based on visibility.
 * Visible windows are moved into place, hidden windows are moved off-screen.
 */
void
showhide(Client *c)
{
	if (!c)
		return;
	
	if (ISVISIBLE(c)) {
		/* show clients top down */
#if !WINDOWMAP
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
#else
		if (!c->ismapped) {
			window_map(dpy, c, 1);
			c->ismapped = 1;
		}
#endif
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		SHOWHIDEPROFILE
	}
}

/* Fork and execute an external command for a key binding.
 * The child process closes the X connection and runs execvp.
 */
void
spawn(const Arg *arg)
{
	struct sigaction sa;

	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("vxwm: execvp '%s' failed:", ((char **)arg->v)[0]);
	}
}

/* Apply a tag to the currently selected client. */
void
tag(const Arg *arg)
{
    if (selmon->sel && arg->ui & TAGMASK) {
#if INFINITE_TAGS
        Client *c = selmon->sel;
        unsigned int target_tag_mask = arg->ui & TAGMASK;
        int i;

        for (i = 0; i < LENGTH(tags); i++) {
            if (target_tag_mask & (1 << i)) {
                
                c->saved_cx = selmon->canvas[i].cx + (selmon->ww - WIDTH(c)) / 2;
                c->saved_cy = selmon->canvas[i].cy + (selmon->wh - HEIGHT(c)) / 2;
                c->saved_cw = c->w;
                c->saved_ch = c->h;
                c->was_on_canvas = 1;
                
                break;
            }
        }
#endif

        selmon->sel->tags = arg->ui & TAGMASK;
		#if EWMH_TAGS
		updatewmdesktop(selmon->sel);
		#endif
        focus(NULL);
        arrange(selmon);
    }
}

/* Move the selected client to a different monitor. */
void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

/* Tile layout: arrange clients in a master area and stack area.
 * This layout is the default tiling mode for vxwm.
 */
void
tile(Monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
#if !GAPS
		mw = m->ww;
	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
			if (my + HEIGHT(c) < m->wh)
				my += HEIGHT(c);
#else //GAPS
    mw = m->ww - m->gappx;
  for (i = 0, my = ty = m->gappx, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i) - m->gappx;
			resize(c, m->wx + m->gappx, m->wy + my, mw - (2*c->bw) - m->gappx, h - (2*c->bw), 0);
			if (my + HEIGHT(c) + m->gappx < m->wh)
				my += HEIGHT(c) + m->gappx;
#endif //GAPS
		} else {
#if !GAPS
			h = (m->wh - ty) / (n - i);
			resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
			if (ty + HEIGHT(c) < m->wh)
				ty += HEIGHT(c);
#else //GAPS
      h = (m->wh - ty) / (n - i) - m->gappx;
			resize(c, m->wx + mw + m->gappx, m->wy + ty, m->ww - mw - (2*c->bw) - 2*m->gappx, h - (2*c->bw), 0);
			if (ty + HEIGHT(c) + m->gappx < m->wh)
				ty += HEIGHT(c) + m->gappx;
#endif //GAPS
		}
}

/* Toggle the visibility of the selected monitor's status bar. */
void
togglebar(const Arg *arg)
{
    selmon->showbar = !selmon->showbar;
    updatebarpos(selmon);

    int bar_y;
    if (selmon->showbar) {
        bar_y = selmon->by;
    } else {
        if (topbar) {
            bar_y = -bh;
        } else {
            bar_y = selmon->mh + bh;
        }
    }

#if !BAR_PADDING
    XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, bar_y, selmon->ww, bh);
#else
    int final_y = (selmon->showbar) ? (bar_y + vp) : bar_y;
    XMoveResizeWindow(dpy, selmon->barwin, selmon->wx + sp, final_y, selmon->ww - 2 * sp, bh);
#endif

    arrange(selmon);
}

/* Toggle whether the selected client is floating.
 * Floating windows are exempt from tiled layout placement.
 */
void
togglefloating(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
			selmon->sel->w, selmon->sel->h, 0);
	arrange(selmon);
}

/* Toggle the selected client's assignment to a tag.
 * The client may be visible on multiple tags after this operation.
 */
void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		#if EWMH_TAGS
		updatewmdesktop(selmon->sel);
		#endif
		focus(NULL);
		arrange(selmon);
	}
#if EWMH_TAGS
  updatecurrentdesktop();
#endif
}

/* Toggle the visibility of a tag on the selected monitor.
 * This allows viewing multiple tags at once if the config supports it.
 */
void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
#if EWMH_TAGS
  updatecurrentdesktop();
#endif
}

/* Remove focus from a client and optionally reset input focus to root. */
void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

/* Stop managing a client and remove it from all internal lists.
 * If the client is not already destroyed, restore its state first.
 */
void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;

	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
#if WARP_TO_CLIENT && WARP_TO_CENTER_OF_PREVIOUS_WINDOW
  if (m == selmon && m->sel)
    warptoclient(m->sel);
#endif
}

/* Handle UnmapNotify events, which occur when a window is hidden.
 * This may unmanage the client or remap systray icons if needed.
 */
void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
	else if ((c = wintosystrayicon(ev->window))) {
		/* KLUDGE! sometimes icons occasionally unmap their windows, but do
		 * _not_ destroy them. We map those windows back */
		XMapRaised(dpy, c->win);
		updatesystray();
	}
#if EXTERNAL_BARS
  externalbars_unregister(ev->window);
#endif
}

/* Create and map bar windows for all monitors.
 * Bar windows are reused if already created.
 */
void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"vxwm", "vxwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		unsigned int w = m->ww;
		if (showsystray && m == systraytomon(m))
			w -= getsystraywidth();
#if !BAR_PADDING
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, w, bh, 0, DefaultDepth(dpy, screen),
#else
    m->barwin = XCreateWindow(dpy, root, m->wx + sp, m->by + vp, w - 2 * sp, bh, 0, DefaultDepth(dpy, screen),
#endif
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		if (showsystray && m == systraytomon(m))
			XMapRaised(dpy, systray->win);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

/* Recalculate bar and window area positions for a monitor.
 * This is needed when bar visibility or geometry changes.
 */
void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
#if !BAR_PADDING
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
#else
    m->wh = m->wh - vertpad - bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh + vertpad;
		m->wy = m->topbar ? m->wy + bh + vp : m->wy;
#endif
	} else
#if !BAR_PADDING
		m->by = -bh;
#else
    m->by = -bh - vp;
#endif
#if EXTERNAL_BARS
    m->wx += m->strut_left;
    m->ww -= m->strut_left + m->strut_right;
    m->wy += m->strut_top;
    m->wh -= m->strut_top + m->strut_bottom;
    if (m->ww < 1) m->ww = 1;
    if (m->wh < 1) m->wh = 1;
 #endif
}

/* Update the root window property that lists all managed clients.
 * This is used by pagers and taskbars that support EWMH.
 */
void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

/* Refresh monitor geometry and handle monitor changes.
 * If monitor layout changes, existing clients may be moved.
 */
int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

/* Determine the modifier mask for Num Lock.
 * This avoids missing key bindings when Num Lock is active.
 */
void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

/* Read and cache size hints from a client window.
 * This includes minimum size, resize increments, and aspect ratios.
 */
void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

/* Update the status text from the root window and redraw the bar. */
void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "");
	drawbar(selmon);
	updatesystray();
}


/* Adjust systray icon geometry to fit the bar height.
 * This preserves aspect ratio for tray icons.
 */
void
updatesystrayicongeom(Client *i, int w, int h)
{
	if (i) {
		i->h = bh;
		if (w == h)
			i->w = bh;
		else if (h == bh)
			i->w = w;
		else
			i->w = (int) ((float)bh * ((float)w / (float)h));
		applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
		/* force icons into the systray dimensions if they don't want to */
		if (i->h > bh) {
			if (i->w == i->h)
				i->w = bh;
			else
				i->w = (int) ((float)bh * ((float)i->w / (float)i->h));
			i->h = bh;
		}
	}
}

/* Handle property changes for XEmbed systray icons.
 * This manages mapping/unmapping and _XEMBED_INFO state updates.
 */
void
updatesystrayiconstate(Client *i, XPropertyEvent *ev)
{
	long flags;
	int code = 0;

	if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
			!(flags = getatomprop(i, xatom[XembedInfo])))
		return;

	if (flags & XEMBED_MAPPED && !i->tags) {
		i->tags = 1;
		code = XEMBED_WINDOW_ACTIVATE;
		XMapRaised(dpy, i->win);
		setclientstate(i, NormalState);
	}
	else if (!(flags & XEMBED_MAPPED) && i->tags) {
		i->tags = 0;
		code = XEMBED_WINDOW_DEACTIVATE;
		XUnmapWindow(dpy, i->win);
		setclientstate(i, WithdrawnState);
	}
	else
		return;
	sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
			systray->win, XEMBED_EMBEDDED_VERSION);
}

/* Rebuild the system tray window and reposition all tray icons. */
void
updatesystray(void)
{
	XSetWindowAttributes wa;
	XWindowChanges wc;
	Client *i;
	Monitor *m = systraytomon(NULL);
	unsigned int x = m->mx + m->mw;
	unsigned int sw = TEXTW(stext) - lrpad + systrayspacing;
	unsigned int w = 1;

	if (!showsystray)
		return;
	if (systrayonleft)
		x -= sw + lrpad / 2;
	if (!systray) {
		/* init systray */
		if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
			die("fatal: could not malloc() %u bytes\n", sizeof(Systray));
		#if BAR_PADDING
systray->win = XCreateSimpleWindow(dpy, root, x, m->by + vp, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
#else
systray->win = XCreateSimpleWindow(dpy, root, x, m->by, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
#endif
		wa.event_mask        = ButtonPressMask | ExposureMask;
		wa.override_redirect = True;
		wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
		XSelectInput(dpy, systray->win, SubstructureNotifyMask);
		XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)&netatom[NetSystemTrayOrientationHorz], 1);
		XChangeWindowAttributes(dpy, systray->win, CWEventMask|CWOverrideRedirect|CWBackPixel, &wa);
		XMapRaised(dpy, systray->win);
		XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
		if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
			sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
			XSync(dpy, False);
		}
		else {
			fprintf(stderr, "dwm: unable to obtain system tray.\n");
			free(systray);
			systray = NULL;
			return;
		}
	}
	for (w = 0, i = systray->icons; i; i = i->next) {
		/* make sure the background color stays the same */
		wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
		XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
		XMapRaised(dpy, i->win);
		w += systrayspacing;
		i->x = w;
		XMoveResizeWindow(dpy, i->win, i->x, 0, i->w, i->h);
		w += i->w;
		if (i->mon != m)
			i->mon = m;
	}
	w = w ? w + systrayspacing : 1;
    x -= w;
#if BAR_PADDING
XMoveResizeWindow(dpy, systray->win, x, m->by + vp, w, bh);
wc.x = x; wc.y = m->by + vp; wc.width = w; wc.height = bh;
#else
XMoveResizeWindow(dpy, systray->win, x, m->by, w, bh);
wc.x = x; wc.y = m->by; wc.width = w; wc.height = bh;
#endif
wc.stack_mode = Above; wc.sibling = m->barwin;
XConfigureWindow(dpy, systray->win, CWX|CWY|CWWidth|CWHeight|CWSibling|CWStackMode, &wc);
XMapWindow(dpy, systray->win);
XMapSubwindows(dpy, systray->win);
    /* redraw background */
XSetForeground(dpy, drw->gc, scheme[SchemeNorm][ColBg].pixel);
XFillRectangle(dpy, systray->win, drw->gc, 0, 0, w, bh);
XSync(dpy, False);
}

/* Update the title string for a client from WM_NAME or _NET_WM_NAME. */
void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

/* Update client state based on window type hints.
 * Dialogs become floating, and fullscreen is applied as needed.
 */
void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}

/* Update urgency and input hint state from WM_HINTS. */
void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

#if TAG_TO_TAG

void
view(const Arg *arg)
{
#if INFINITE_TAGS
    if (selmon->lt[selmon->sellt]->arrange == NULL) {
        save_canvas_positions(selmon);
    }
#endif

    if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        selmon->seltags ^= 1;
    else {
        selmon->seltags ^= 1;
        if (arg->ui & TAGMASK)
            selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
    }

#if INFINITE_TAGS
    int newtag = getcurrenttag(selmon);
    
    if (selmon->lt[selmon->sellt]->arrange == NULL) {
        restore_canvas_positions(selmon);
        
        Client *c;
        for (c = selmon->clients; c; c = c->next)
            if (c->tags & (1 << newtag))
                c->isfloating = 1;
    } else {
        selmon->canvas[newtag].cx = 0;
        selmon->canvas[newtag].cy = 0;
    }
#endif

    focus(NULL);
    arrange(selmon);
#if EWMH_TAGS
    updatecurrentdesktop();
#endif
}

#else

void
view(const Arg *arg)
{
#if INFINITE_TAGS
    if (selmon->lt[selmon->sellt]->arrange == NULL)
        save_canvas_positions(selmon);
#endif

    if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        return;
    selmon->seltags ^= 1;
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

#if INFINITE_TAGS
    int newtag = getcurrenttag(selmon);

    if (selmon->lt[selmon->sellt]->arrange != NULL) {
        selmon->canvas[newtag].cx = 0;
        selmon->canvas[newtag].cy = 0;
    } else {
        restore_canvas_positions(selmon);

        Client *c;
        for (c = selmon->clients; c; c = c->next)
            if (ISVISIBLE(c))
                c->isfloating = 1;
    }
#endif

    focus(NULL);
    arrange(selmon);
#if EWMH_TAGS
    updatecurrentdesktop();
#endif
}

#endif

/* Find the Client corresponding to an X window, if any.
 * This searches all monitors and client lists.
 */
Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

/* Find a system tray icon client by its X window. */
Client *
wintosystrayicon(Window w) {
	Client *i = NULL;

	if (!showsystray || !w)
		return i;
	for (i = systray->icons; i && i->win != w; i = i->next) ;
	return i;
}

/* Return the monitor that contains a given window or point. */
Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
/* Generic X error handler used during normal operation.
 * Many bad-window errors are ignored because they are expected.
 */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "vxwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

/* Dummy X error handler that ignores any errors.
 * Used when XGrabServer is active to avoid reporting expected failures.
 */
int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("vxwm: another window manager is already running");
	return -1;
}

/* Determine which monitor owns the system tray.
 * This supports systray pinning and multi-monitor setups.
 */
Monitor *
systraytomon(Monitor *m) {
	Monitor *t;
	int i, n;
	if(!systraypinning) {
		if(!m)
			return selmon;
		return m == selmon ? m : NULL;
	}
	for(n = 1, t = mons; t && t->next; n++, t = t->next) ;
	for(i = 1, t = mons; t && t->next && i < systraypinning; i++, t = t->next) ;
	if(systraypinningfailfirst && n < systraypinning)
		return mons;
	return t;
}

/* Promote a client to the master position in the tiling layout. */
void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;
#if WARP_TO_CLIENT && WARP_TO_CENTER_OF_ZOOMED_WINDOW
  Client *target = c;
#endif

	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
#if WARP_TO_CLIENT && WARP_TO_CENTER_OF_ZOOMED_WINDOW
  warptoclient(target);
#endif
}

/* Program entry point for vxwm. Parses arguments, initializes X,
 * starts the event loop, and cleans up on exit.
 */
int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("vxwm "VERSION);
  if (argc == 2 && !strcmp("-srcdir", argv[1]))
    die(SRCDIR);
  if (argc == 2 && !strcmp("-ignoreautostart", argv[1]))
    printf("Ignoring autostart");
	else if (argc != 1)
		die("usage: vxwm [-v] [-srcdir]"
#if AUTOSTART
         " [-ignoreautostart]"
#endif
       );
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("vxwm: cannot open display");
	checkotherwm();
#if XRDB
  XrmInitialize();
  loadxrdb();
#endif
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
#if AUTOSTART
  if (!(argc == 2 && !strcmp("-ignoreautostart", argv[1])))
    runautostart();
#endif
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
