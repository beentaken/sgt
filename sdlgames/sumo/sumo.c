/*
 * sumo - a small game involving powered snooker balls attempting
 * to knock one another off a table.
 * 
 * TODO:
 * 
 *  - Might be nice to have some alternative arena shapes
 *    available, if we aren't going to support the command-line
 *    full configuration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "sdlstuff.h"
#include "game256.h"
#include "swash.h"

#define lenof(x) ( sizeof((x)) / sizeof(*(x)) )

#define sign(x) ( (x) < 0 ? -1 : (x) > 0 ? +1 : 0 )

#ifdef FALSE
#undef FALSE
#endif
#ifdef TRUE
#undef TRUE
#endif
#define FALSE 0
#define TRUE 1

#define JOY_THRESHOLD 4096
static SDL_Joystick *joys[2];

/*
 * Angles go in steps of ten degrees; so sine[0] to sine[35]
 * inclusive would be sufficient to compute sines. We supply an
 * extra quarter-iteration so that we can use sine[angle+9] as a
 * quick way to find cosine[angle].
 */
static int sine[45] = {
    0,695,1368,2000,2571,3064,3464,3759,3939,
    4000,3939,3759,3464,3064,2571,2000,1368,695,
    0,-695,-1368,-2000,-2571,-3064,-3464,-3759,-3939,
    -4000,-3939,-3759,-3464,-3064,-2571,-2000,-1368,-695,
    0,695,1368,2000,2571,3064,3464,3759,3939
};

static int sx1 = 50, sy1 = 100, sx2 = 170, sy2 = 120;

#define MAX_BULLETS 3

static int no_quit_option = 0;

static int sc1, sc2;

static Image fire;
static int make_fire(void);
static unsigned char tile[8+10*10];
static unsigned char bullets[2][8+3*3];

static unsigned char spr[2][36][8+20*20];
static unsigned char deliv[2][26][1608];
static unsigned char deaths[2][1327];
static int deathoffset[14];
static int makedeath(int player, int angle);
static int make_deliveries(void);
static int do_palette(void);

static Image title, draw, win1, win2;
static void expand_title(void);
static void make_text(void);

#ifdef DEBUG
FILE *debugfp;
#endif

/* ----------------------------------------------------------------------
 * Arena handling code. To add a new arena, we need to add a new
 * case to the following functions, and that should be all that's
 * required. (Of course, adding a new case to setup_arena() may
 * very well also necessitate writing some more graphics generation
 * routines.)
 */

static int lava_platform_shape[20] = {
    0x01F80, 0x07FE0, 0x1FFF8, 0x3FFFC, 0x3FFFC,
    0x7FFFE, 0x7FFFE, 0xFFFFF, 0xFFFFF, 0xFFFFF,
    0xFFFFF, 0xFFFFF, 0xFFFFF, 0x7FFFE, 0x7FFFE,
    0x3FFFC, 0x3FFFC, 0x1FFF8, 0x07FE0, 0x01F80,
};
#define LP_SAFE(x,y) ((x) >= 2 && (x) < 22 && (y) >= 2 && (y) < 22 && \
			  lava_platform_shape[(y)-2] & (1 << (21-(x))))

/*
 * Test whether a circle overlaps any of the tiles on the lava
 * platform.
 */
static int lava_platform_check(int x, int y, int radius)
{
    int xt, yt, xu, yu;
    int i, j, r2;

    /*
     * First determine which square the centre of the circle is
     * over. If this square is safe, return true.
     */
    xt = x / (10<<16);
    yt = y / (10<<16);
    if (LP_SAFE(xt, yt))
	return 1;

    /*
     * Failing that: check the horizontal and vertical radii of the
     * circle to see if they overlap on to other squares.
     */
    for (i = xt; i * (10<<16) > x - radius; i--)
	if (LP_SAFE(i-1, yt)) return 1;
    for (i = xt+1; i * (10<<16) < x + radius; i++)
	if (LP_SAFE(i, yt)) return 1;
    for (i = yt; i * (10<<16) > y - radius; i--)
	if (LP_SAFE(xt, i-1)) return 1;
    for (i = yt+1; i * (10<<16) < y + radius; i++)
	if (LP_SAFE(xt, i)) return 1;

    /*
     * Lastly, we must look for _all_ lattice points underneath the
     * circle.
     */
    xt = ((x - radius) + (10<<16) - 1) / (10<<16);
    yt = ((y - radius) + (10<<16) - 1) / (10<<16);
    xu = ((x + radius)                 / (10<<16));
    yu = ((y + radius)                 / (10<<16));
    r2 = (radius >> 8) * (radius >> 8);
    for (i = xt; i <= xu; i++)
	for (j = yt; j <= yu; j++) {
	    int dx, dy;
	    /*
	     * We do the Pythagoras equation in 8-bit fixed point;
	     * it's more accurate than in integer pixel coords, but
	     * our usual 16-bit will overflow when we square it.
	     */

	    dx = (x >> 8) - i * (10<<8);
	    dy = (y >> 8) - j * (10<<8);
	    if (dx*dx + dy*dy < r2 &&
		(LP_SAFE(i-1, j-1) || LP_SAFE(i-1, j) ||
		 LP_SAFE(i, j-1) || LP_SAFE(i, j)))
		return 1;
	}

    return 0;
}

static int player_can_steer(int x, int y)
{
    /*
     * Just check that the very centre of the player is on the
     * arena floor. Note we use (19 << 15) == (9.5 << 16) as the
     * offset from the player coordinates to their centre point,
     * because the player sprite size is even so the centre point
     * is half way between pixels.
     */
    return lava_platform_check(x+(19<<15), y+(19<<15), 1<<16);
}

static int player_is_alive(int x, int y)
{
    /*
     * Check that at least one part of the player is on the arena
     * floor.
     */
    return lava_platform_check(x+(10<<16), y+(10<<16), (10<<16));
}

static int bullet_can_exist(int x, int y)
{
    return lava_platform_check(x+(19<<15), y+(19<<15), 0);
}

static int setup_arena(void)
{
    int i, j;

    scr_prep();
    drawimage(fire,0,0,-1);
    for (i = 2; i < 22; i++)
	for (j = 2; j < 22; j++)
	    if (lava_platform_shape[j-2] & (1 << (21-i)))
		drawimage(tile,10*i,10*j,-1);
    scr_done();
}

/* ----------------------------------------------------------------------
 * End of arena support functions.
 */

static int game_screen(void)
{
    scr_prep();
    drawimage(title,240,0,-1);
    drawimage(spr[0][9],270,97,0);
    drawimage(spr[1][9],270,157,0);
    scr_done();
}

static void plotpoint(void *ctx, int x, int y)
{
    scrdata[640*y+2*x] = scrdata[640*y+2*x+1] = (int)ctx;
}

static int score(int sc, int x, int y)
{
    int i, j;
    char buf[4];
    for (i = 0; i < 7*3; i++)
	for (j = 0; j < 9; j++)
	    plotpoint((void *)128, x+i, y+j);
    buf[2] = '0' + sc % 10; sc /= 10;
    buf[1] = '0' + sc % 10; sc /= 10;
    buf[0] = '0' + sc % 10;
    buf[3] = '\0';
    swash_text(x, y, buf, plotpoint, (void *)137);
}

static int show_scores(void)
{
    if (sc1 == 1000) sc1 = 0;
    if (sc2 == 1000) sc2 = 0;
    scr_prep();
    score(sc1,270,119);
    score(sc2,270,179);
    scr_done();
}

static int collide(int sx, int sy, int *u1x, int *u1y, int *u2x, int *u2y)
{
    int l1x,l1y,l1l,l2x,l2y,l2l,sl;

    sl = (int) (0.5 + sqrt(sx*sx+sy*sy));
    l1l = (*u1x*sx+*u1y*sy) / sl;  l2l = (*u2x*sx+*u2y*sy) / sl;
    l1x = (l1l*sx) / sl;           l2x = (l2l*sx) / sl;
    l1y = (l1l*sy) / sl;           l2y = (l2l*sy) / sl;
    *u1x = *u1x-l1x+l2x;           *u2x = *u2x-l2x+l1x;
    *u1y = *u1y-l1y+l2y;           *u2y = *u2y-l2y+l1y;
}

static int check_esc(void)
{
    SDL_Event event;

    /*
     * Listen to waiting events in case ESC was pressed.
     */
    while (SDL_PollEvent(&event)) {
	int action;

	switch(event.type) {
	  case SDL_KEYDOWN:
	    /* Standard catch-all quit-on-escape clause. */
	    if (event.key.keysym.sym == SDLK_ESCAPE)
		exit(1);
	}
    }
}

static int play_game(void)
{
    Sprite p[2], bs[2][MAX_BULLETS], q;
    int x1,y1,x15,y15,x2,y2,i,j,jj,gameover;
    int dx,dy,dvx,dvy,sp;
    int bx[2][MAX_BULLETS],by[2][MAX_BULLETS];
    int bvx[2][MAX_BULLETS], bvy[2][MAX_BULLETS];
    int x[2],y[2],vx[2],vy[2],rangle[2],angle[2],dying[2];
    int ba[2][MAX_BULLETS];
    int pfire[2];
    int d1,d2,done,esc,b;
#ifdef TIME_DEBUG
    long long timebefore, timeafter;
    char debugbuf[80];
#endif

    const int dead = 30;

#define ERASEALLSPRITES do { \
    int i, j; \
    for (i = 2; i-- ;) erasesprite(p[i]); \
    for (i = 2; i-- ;) for (j = MAX_BULLETS; j-- ;) erasesprite(bs[i][j]); \
} while (0)

    for (i = 0; i < 2; i++)
	p[i] = makesprite(40, 40, 0);
    for (i = 0; i < 2; i++)
	for (j = 0; j < MAX_BULLETS; j++)
	    bs[i][j] = makesprite(3, 3, 0);
    q = makesprite(0x6C,0x11,0x81);

    for (i = 0; i < 26; i++) {
	initframe(65000);
	scr_prep();
	putsprite(p[0],deliv[0][i],sx1,sy1);
	putsprite(p[1],deliv[1][i],sx2,sy2);
	scr_done();
	endframe();
	vsync();
	check_esc();
    }

    x[0] = sx1 << 16; y[0] = sy1 << 16; vx[0] = 0; vy[0] = 0; rangle[0] = 0;
    angle[0] = 0;
    x[1] = sx2 << 16; y[1] = sy2 << 16; vx[1] = 0; vy[1] = 0; rangle[1] = 36;
    angle[1] = 18;
    for (i = 0; i < 2; i++) for (j = 0; j < MAX_BULLETS; j++)
	ba[i][j] = FALSE;
    dying[0] = 0; dying[1] = 0; gameover = 0;

    do {

	scr_prep();

	ERASEALLSPRITES;

	/*
	 * Animate the bullets.
	 */
	for (i = 0; i < 2; i++) for (j = 0; j < MAX_BULLETS; j++)
	    if (ba[i][j]) {
		bx[i][j] = bx[i][j]+bvx[i][j];
		by[i][j] = by[i][j]+bvy[i][j];
		if (bullet_can_exist(bx[i][j], by[i][j]))
		    putsprite(bs[i][j],bullets[i],
			      bx[i][j] >> 16,by[i][j] >> 16);
		else
		    ba[i][j] = FALSE;
	    }

	for (i = 0; i < 2; i++) {
	    if (dying[i] > 0 && dying[i] < 28)
		putsprite(p[i],deaths[i]+deathoffset[(dying[i]-1)/2],
			  x[i] >> 16,y[i] >> 16);
	    else if (dying[i]==0)
		putsprite(p[i],spr[i][angle[i]],x[i] >> 16,y[i] >> 16);
	}

	for (i = 0; i < 2; i++) {
	    if (dying[i]>0) {
		if (dying[i]<dead) dying[i]++;
	    } else {

		/*
		 * If the player's centre point is on solid ground,
		 * they can steer.
		 */

		if (player_can_steer(x[i], y[i])) {
		    int axis, accel, brake, fire;
		    axis = SDL_JoystickGetAxis(joys[i], 0) / JOY_THRESHOLD;
		    accel = SDL_JoystickGetButton(joys[i], 1);   /* X */
		    brake = SDL_JoystickGetButton(joys[i], 0);   /* Square */
		    fire = SDL_JoystickGetButton(joys[i], 3);   /* Triangle */
		    if (axis < 0) rangle[i] = (rangle[i]+1) % 72;
		    if (axis > 0) rangle[i] = (rangle[i]+71) % 72;
		    angle[i] = rangle[i] >> 1;
		    if (brake) {
			vx[i] = vx[i]-sign(vx[i])*(abs(vx[i]) >> 5);
			vy[i] = vy[i]-sign(vy[i])*(abs(vy[i]) >> 5);
		    }
		    if (accel) {
			vx[i] = vx[i]+sine[angle[i]+9];
			vy[i] = vy[i]-sine[angle[i]];
		    }
		    if (fire && !pfire[i]) {
			jj = -1;
			for (j = 0; j < MAX_BULLETS; j++)
			    if(!ba[i][j]) {
				jj = j;
				break;
			    }
			if (jj != -1) {
			    bx[i][j] = x[i]+622592+sine[angle[i]+9]*165;
			    by[i][j] = y[i]+622592-sine[angle[i]]*165;
			    bvx[i][j] = 38*sine[angle[i]+9];
			    bvy[i][j] = -38*sine[angle[i]];
			    ba[i][j] = TRUE;
			    vx[i] = vx[i]-4*sine[angle[i]+9];
			    vy[i] = vy[i]+4*sine[angle[i]];
			}
		    }
		    pfire[i] = fire;
		    x[i] = x[i]+vx[i];
		    y[i] = y[i]+vy[i];
		} else {
		    if (player_is_alive(x[i], y[i])) {
			x[i] = x[i]+vx[i];
			y[i] = y[i]+vy[i];
		    } else {
			makedeath(i,angle[i]);
			dying[i] = 1;
			gameover = 1;
		    }
		}
		if (x[i]<0) x[i] = 0;
		if (y[i]<0) y[i] = 0;
		if (x[i]>(220 << 16)) x[i] = 220 << 16;
		if (y[i]>(220 << 16)) y[i] = 220 << 16;
	    }
	}

	dx = x[1]-x[0]; dx = sign(dx)*(abs(dx) >> 16);
	dy = y[1]-y[0]; dy = sign(dy)*(abs(dy) >> 16);
	/* The first two terms of this `if' statement are a
	 * theoretically redundant optimisation.  */
	if (dx<20 && dy<20 && dx*dx+dy*dy<400) {
	    dvx = vx[0]-vx[1];
	    dvy = vy[0]-vy[1];
	    sp = dx*dvx+dy*dvy;
	    if (sp>0) collide(dx,dy,&vx[0],&vy[0],&vx[1],&vy[1]);
	}

	for (i = 0; i < 2; i++)
	    for (j = 0; j < MAX_BULLETS; j++)
		if (ba[1-i][j]) {
		    dx = x[i]+622592-bx[1-i][j]; dx = sign(dx)*(abs(dx) >> 16);
		    dy = y[i]+622592-by[1-i][j]; dy = sign(dy)*(abs(dy) >> 16);
		    if (dx<10 && dy<10) {
			dvx = dx*dx+dy*dy;
			if (dvx<100) {
			    dvx = (int) (0.5 + sqrt(dvx));
			    vx[i] = vx[i]+(dx << 14) / dvx;
			    vy[i] = vy[i]+(dy << 14) / dvx;
			    ba[1-i][j] = FALSE;
			}
		    }
		}

	if (gameover > 0) gameover++;

	d1 = (dying[0]==dead);
	d2 = (dying[1]==dead);

	if (dying[0]>0 && dying[0]<dead) done = FALSE;
	else if (dying[1]>0 && dying[1]<dead) done = FALSE;
	else done = (gameover>=50 && (d1 || d2)) || (d1 && d2);

	/*
	 * Hideous, _hideous_ hackery to try to work around the
	 * lack of double buffering in SDL's management of the PS2
	 * video hardware. Compiling with TIME_DEBUG defined will
	 * print diagnostics to fd 3 suggesting that scr_done()
	 * (which copies the internal frame buffer to the real
	 * screen) takes roughly 5290 microseconds, while vsync()
	 * ends up waiting about 14250 microseconds. Hence, we
	 * expect to be able to wait for about 8960 microseconds at
	 * the end of our frame before starting our scr_done()
	 * copy. I'm going to cut that to 8000 for safety, but with
	 * any luck that should move the nasty flickery line where
	 * the memory copy overtakes the electron beam well down
	 * into the dead area at the bottom of the screen.
	 * 
	 * (Actually, I don't see why we can't wait for 13000 us or
	 * so, but it doesn't seem to work when I try it. Bah.)
	 */
	usleep(8000);

#ifdef TIME_DEBUG
	before = bigclock();
#endif
	scr_done();
#ifdef TIME_DEBUG
	after = bigclock();
	sprintf(buf, "SDL_Flip took %lld usec\r\n", after-before);
	write(3, buf, strlen(buf));
#endif
	check_esc();
#ifdef TIME_DEBUG
	before = bigclock();
#endif
	vsync();
#ifdef TIME_DEBUG
	after = bigclock();
	sprintf(buf, "SDL_Flip took %lld usec\r\n", after-before);
	write(3, buf, strlen(buf));
#endif
    } while (!done);

    scr_prep();
    if (d1 && d2) putsprite(q,draw,(240-XSIZE(draw))/2,92);
    else if (d2) putsprite(q,win1,(240-XSIZE(win1))/2,92);
    else putsprite(q,win2,(240-XSIZE(win2))/2,92);

    if (d2 && !d1) sc1++;
    else if (d1 && !d2) sc2++;
    scr_done();

    show_scores();

    /*
     * Now wait until no joystick buttons are pressed at all, and
     * then wait for three seconds or until one is.
     */
    while (1) {
	int pushed = FALSE;
	for (i = 0; i < 2; i++) {
	    int n = SDL_JoystickNumButtons(joys[i]);
	    for (j = 0; j < n; j++)
		if (SDL_JoystickGetButton(joys[i], j))
		    pushed = TRUE;
	}
	if (!pushed) break;
	check_esc();
	vsync();
    }

    for (i = 150; i-- ;) {
	initframe(20000);
	endframe();
	{
	    SDL_Event event;
	    int pushed = 0;

	    while (SDL_PollEvent(&event)) {
		int action;

		switch(event.type) {
		  case SDL_KEYDOWN:
		    /* Standard catch-all quit-on-escape clause. */
		    if (event.key.keysym.sym == SDLK_ESCAPE)
			exit(1);
		    break;
		  case SDL_JOYBUTTONDOWN:
		    pushed = 1;
		    break;
		}
	    }
	    if (pushed) break;
	}
    }

    scr_prep();
    erasesprite(q);
    ERASEALLSPRITES;
    scr_done();

    freesprite(q);
    for (i = 2; i-- ;) for (j = MAX_BULLETS; j-- ;) freesprite(bs[i][j]);
    for (i = 2; i-- ;) freesprite(p[i]);

#undef ERASEALLSPRITES

}

static int adjust_screen(int which)
{
    int border = (which == 0 ? 130 : 134);
    int highlight = (which == 0 ? 27 : 28);
    Image img;

    struct {
	char *text;
	enum { RETURN, QUIT, ADJUST } action;
    } menu[3];
    int nmenu = 0;
    int h, y, prevval[2];
    int dx, dy;

    h = 39 + 16*2;
    y = (200 - h) / 2;

    img = makeimage(120, h, 0, 0);
    pickimage(img, 100, y);

    scr_prep();
    bar(100, y, 219, y+h-1, border);
    bar(102, y+2, 217, y+h-3, 0);
    
    swash_centre(100, 219, y+7, "ADJUST SCREEN", plotpoint, (void *)26);

    prevval[0] = prevval[1] = 0;

    dx = get_dx();
    dy = get_dy();

    scr_done();

    while (1) {
	SDL_Event event;
	int pushed = 0;
	int redraw = 0;
	char buf[20];

	scr_prep();
	bar(105, y+34, 214, y+64, 0);
	sprintf(buf, "DX =%4d", dx);
	swash_centre(100, 219, y+37, buf, plotpoint, (void *)26);
	sprintf(buf, "DY =%4d", dy);
	swash_centre(100, 219, y+53, buf, plotpoint, (void *)26);
	scr_done();

	while (SDL_WaitEvent(&event)) {
	    int action;

	    switch(event.type) {
	      case SDL_KEYDOWN:
		/* Standard catch-all quit-on-escape clause. */
		if (event.key.keysym.sym == SDLK_ESCAPE)
		    exit(1);
		break;
	      case SDL_JOYAXISMOTION:
		if (event.jaxis.which == which &&
		    (event.jaxis.axis | 1) == 1) {
		    int val = event.jaxis.value / JOY_THRESHOLD;
		    int axis = event.jaxis.axis;

		    if (val < 0 && prevval[axis] >= 0) {
			if (axis == 0) dx--; else dy--;
			redraw = 1;
		    } else if (val > 0 && prevval[axis] <= 0) {
			if (axis == 0) dx++; else dy++;
			redraw = 1;
		    }
		    prevval[axis] = val;
		}
		break;
	      case SDL_JOYBUTTONDOWN:
		if (event.jbutton.which == which) {
		    pushed = 1;
		}
		break;
	    }
	    if (pushed || redraw) break;
	}

	if (pushed) break;
	set_dx(dx); set_dy(dy);
    }

    scr_prep();
    drawimage(img, 100, y, -1);
    scr_done();

    free(img);
}

static int options_menu(int which)
{
    int border = (which == 0 ? 130 : 134);
    int highlight = (which == 0 ? 27 : 28);
    Image img;

    struct {
	char *text;
	enum { RETURN, QUIT, ADJUST } action;
    } menu[3];
    int nmenu = 0;
    int h, y, i, mpos, prevval;

    menu[nmenu].text = "RETURN TO GAME"; menu[nmenu++].action = RETURN;
    menu[nmenu].text = "ADJUST SCREEN"; menu[nmenu++].action = ADJUST;
    if (!no_quit_option) {
	menu[nmenu].text = "EXIT SUMO"; menu[nmenu++].action = QUIT;
    }

    h = 39 + 16*nmenu;
    y = (200 - h) / 2;

    img = makeimage(120, h, 0, 0);
    pickimage(img, 100, y);

    scr_prep();
    bar(100, y, 219, y+h-1, border);
    bar(102, y+2, 217, y+h-3, 0);
    
    swash_centre(100, 219, y+7, "OPTIONS MENU", plotpoint, (void *)26);

    mpos = 0;
    prevval = 0;

    scr_done();

    while (1) {
	SDL_Event event;
	int pushed = 0;
	int redraw = 0;

	scr_prep();
	for (i = 0; i < nmenu; i++) {
	    bar(105, y+34+i*16, 214, y+48+i*16,
		i==mpos ? highlight : 0);
	    swash_centre(100, 219, y+37+i*16, menu[i].text,
			 plotpoint, (void *)26);
	}
	scr_done();

	while (SDL_WaitEvent(&event)) {
	    int action;

	    switch(event.type) {
	      case SDL_KEYDOWN:
		/* Standard catch-all quit-on-escape clause. */
		if (event.key.keysym.sym == SDLK_ESCAPE)
		    exit(1);
		break;
	      case SDL_JOYAXISMOTION:
		if (event.jaxis.which == which && event.jaxis.axis == 1) {
		    int val = event.jaxis.value / JOY_THRESHOLD;

		    if (val < 0 && prevval >= 0) {
			mpos = (mpos+nmenu-1) % nmenu;
			redraw = 1;
		    } else if (val > 0 && prevval <= 0) {
			mpos = (mpos+1) % nmenu;
			redraw = 1;
		    }
		    prevval = val;
		}
		break;
	      case SDL_JOYBUTTONDOWN:
		if (event.jbutton.which == which) {
		    pushed = 1;
		}
		break;
	    }
	    if (pushed || redraw) break;
	}

	if (pushed && menu[mpos].action == QUIT) exit(1);
	if (pushed && menu[mpos].action == RETURN) break;
	if (pushed && menu[mpos].action == ADJUST) adjust_screen(which);
    }

    scr_prep();
    drawimage(img, 100, y, -1);
    scr_done();

    free(img);
}

int main(int argc, char **argv)
{
    int p;

    if (argc > 1 && !strcmp(argv[1], "-n"))
	no_quit_option = 1;

#ifdef DEBUG
debugfp = fdopen(3, "w");
setbuf(debugfp, NULL);
#endif

    /* Set up the SDL game environment */
    setup();

    joys[0] = SDL_JoystickOpen(0);
    joys[1] = SDL_JoystickOpen(1);

    make_deliveries();
    make_fire(); 
    expand_title();
    make_text();
    do_palette();
    game_screen();
    setup_arena();
    sc1 = sc2 = 0;
    show_scores();

    /*
     * Get rid of the initial event queue from opening the
     * joysticks (a bunch of button-up events).
     */
    {
	SDL_Event event;
	while (SDL_PollEvent(&event));
    }

    /*
     * FIXME: we will need an alternative means to zero the scores
     * (sc1 = sc2 = 0, show_scores()), and possibly also to quit
     * the game.
     */

    while (1) {
	while (1) {
	    initframe(20000);
	    endframe();
	    {
		SDL_Event event;
		int pushed = 0;

		while (SDL_PollEvent(&event)) {
		    int action;

		    switch(event.type) {
		      case SDL_KEYDOWN:
			/* Standard catch-all quit-on-escape clause. */
			if (event.key.keysym.sym == SDLK_ESCAPE)
			    exit(1);
			break;
		      case SDL_JOYBUTTONDOWN:
			if (event.jbutton.button == 8) {
			    /*
			     * Pressing Select displays the options
			     * menu, under the control of whoever
			     * pressed it.
			     */
			    options_menu(event.jbutton.which);
			} else {
			    pushed = 1;
			}
			break;
		    }
		}
		if (pushed) break;
	    }
	}
	play_game();
    }
    return 0;
}

/* ----------------------------------------------------------------------
 * Tedious graphics stuff.
 */

/*
 * Make the fiery backdrop underneath the lava-platform arena.
 */
static int make_fire(void)
{
    int a[244][244];
    int random,x,y,xx,yy,minx,miny,maxx,maxy,min,max;

    for (y = 0; y < 244; y++)
	for (x = 0; x < 244; x++)
	    a[y][x] = 0;

    for (y = 1; y < 243; y++)
	for (x = 1; x < 243; x++) {
	    do {
		random = rand() / (RAND_MAX/64);
	    } while (random >= 64);
	    a[y-1][x-1] += random;
	    a[y-1][x+1] += random;
	    a[y+1][x-1] += random;
	    a[y+1][x+1] += random;
	    a[y][x-1] += random*2;
	    a[y][x+1] += random*2;
	    a[y-1][x] += random*2;
	    a[y+1][x] += random*2;
	    a[y][x] += random*3;
	}
    min = max = a[2][2];
    for (y = 2; y < 242; y++)
	for (x = 2; x < 242; x++) {
	    xx = a[y][x];
	    if (min > xx) min = xx;
	    if (max < xx) max = xx;
	}

    max -= min;

    fire = makeimage(240, 240, 0, 0);
    xx = 8;

    for (y = 2; y < 242; y++)
	for (x = 2; x < 242; x++)
	    fire[xx++] = 208 + 47*(a[y][x]-min)/max;
}

/*
 * Some graphics, and primitives used to construct graphics.
 */

static unsigned char vehicle[20][20] = {
     0, 0, 0, 0, 0, 0,24,24,24,23,22,21,20,18, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0,25,26,25,99,99,99,99,99,99,19,18,15, 0, 0, 0, 0,
     0, 0, 0,26,26,99,99,25,24,23,23,22,21,99,99,17,14, 0, 0, 0,
     0, 0,26,26,99,26,25,25,24,24,23,21,21,20,18,99,15,13, 0, 0,
     0,25,26,99,26,25,25,25,24,23,22,22,20,19,18,16,99,13,10, 0,
     0,25,99,25,26,25,25,25,24,23,22,21,20,19,18,16,15,99,11, 0,
    24,25,99,25,25,25,24,24,23,22,22,20,20,18,17,16,14,99,10, 8,
    24,99,25,25,25,25,24,23,22,22,21,20,19,18,17,15,14,12,99, 8,
    24,99,24,24,24,23,23,23,22,21,20,19,18,17,16,15,13,12,99, 7,
    23,99,24,24,23,23,22,22,21,20,20,19,18,16,15,14,12,11,99, 7,
    22,99,23,22,23,22,22,21,20,20,18,18,16,16,15,13,12,10,99, 6,
    21,99,22,21,21,21,20,20,19,19,17,17,16,15,13,12,10, 9,99, 5,
    20,99,21,21,21,20,20,19,18,18,17,16,14,14,12,11,10, 8,99, 3,
    18,19,99,20,19,19,18,18,17,16,16,14,14,12,11,10, 8,99, 5, 2,
     0,18,99,18,18,18,17,17,16,15,14,14,12,11,10, 9, 7,99, 3, 0,
     0,15,17,99,17,16,16,15,15,14,13,12,11,10, 9, 7,99, 4, 1, 0,
     0, 0,14,15,99,15,14,14,13,12,12,11, 9, 9, 7,99, 4, 1, 0, 0,
     0, 0, 0,13,13,99,99,12,12,11,10, 9, 8,99,99, 4, 1, 0, 0, 0,
     0, 0, 0, 0,10,11,10,99,99,99,99,99,99, 4, 3, 1, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 8, 8, 7, 6, 6, 5, 3, 2, 0, 0, 0, 0, 0, 0,
};

static unsigned char angles[20][20] = {
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,10,10, 9, 8, 7, 7,99,99,99,99,99,99,99,
    99,99,99,99,99,12,11,11,10, 9, 8, 7, 6, 6, 5,99,99,99,99,99,
    99,99,99,99,13,12,12,11,10, 9, 8, 7, 6, 5, 5, 4,99,99,99,99,
    99,99,99,14,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 3,99,99,99,
    99,99,99,15,14,14,13,12,11, 9, 8, 6, 5, 4, 3, 3, 2,99,99,99,
    99,99,16,15,15,15,14,13,12,10, 7, 5, 4, 3, 2, 2, 2, 1,99,99,
    99,99,16,16,16,16,15,14,13,10, 7, 4, 3, 2, 1, 1, 1, 1,99,99,
    99,99,17,17,17,17,17,16,16,13, 4, 1, 1, 0, 0, 0, 0, 0,99,99,
    99,99,18,18,18,18,18,19,19,22,31,34,34,35,35,35,35,35,99,99,
    99,99,19,19,19,19,20,21,22,25,28,31,32,33,34,34,34,34,99,99,
    99,99,19,20,20,20,21,22,23,25,28,30,31,32,33,33,33,34,99,99,
    99,99,99,20,21,21,22,23,24,26,27,29,30,31,32,32,33,99,99,99,
    99,99,99,21,21,22,23,24,25,26,27,28,29,30,31,32,32,99,99,99,
    99,99,99,99,22,23,23,24,25,26,27,28,29,30,30,31,99,99,99,99,
    99,99,99,99,99,23,24,24,25,26,27,28,29,29,30,99,99,99,99,99,
    99,99,99,99,99,99,99,25,25,26,27,28,28,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
};

static unsigned char tile[8+10*10] = { 0,0,0,0,10,0,10,0,
    31,31,31,31,31,31,31,31,31,32,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,
    32,33,33,33,33,33,33,33,33,33
};

static unsigned char fourtiles[8+20*20] = { 0,0,0,0,20,0,20,0,
    31,31,31,31,31,31,31,31,31,32,31,31,31,31,31,31,31,31,31,32,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    32,33,33,33,33,33,33,33,33,33,32,33,33,33,33,33,33,33,33,33,
    31,31,31,31,31,31,31,31,31,32,31,31,31,31,31,31,31,31,31,32,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    31,32,32,32,32,32,32,32,32,33,31,32,32,32,32,32,32,32,32,33,
    32,33,33,33,33,33,33,33,33,33,32,33,33,33,33,33,33,33,33,33
};

static unsigned char bullets[2][8+3*3] = {
    {1,0,1,0,3,0,3,0,0,27,0,27,29,27,0,27,0},
    {1,0,1,0,3,0,3,0,0,28,0,28,30,28,0,28,0},
};

static int deathoffset[14] = {
    0,332,596,800,952,1060,1132,1176,1200,1212,1224,1248,1292,1316
};

static int deathsize[14] = {
    9,8,7,6,5,4,3,2,1,1,2,3,2,1
};

/* this setcolour() macro translates VGA 64-level palettes to 256-level */
#define setcolour(c,r,g,b) do { \
    SDL_Color colour = { (r)*255/63, (g)*255/63, (b)*255/63 }; \
    SDL_SetColors(screen, &colour, c, 1); \
} while (0)

static int do_palette(void)
{
    int i;

    setcolour(0,0,0,0);
    setcolour(1,12,12,12);
    setcolour(2,14,14,14);
    setcolour(3,16,16,16);
    setcolour(4,18,18,18);
    setcolour(5,20,20,20);
    setcolour(6,22,22,22);
    setcolour(7,24,24,24);
    setcolour(8,26,26,26);
    setcolour(9,28,28,28);
    setcolour(10,30,30,30);
    setcolour(11,32,32,32);
    setcolour(12,34,34,34);
    setcolour(13,37,37,37);
    setcolour(14,39,39,39);
    setcolour(15,41,41,41);
    setcolour(16,43,43,43);
    setcolour(17,45,45,45);
    setcolour(18,47,47,47);
    setcolour(19,49,49,49);
    setcolour(20,51,51,51);
    setcolour(21,53,53,53);
    setcolour(22,55,55,55);
    setcolour(23,57,57,57);
    setcolour(24,59,59,59);
    setcolour(25,61,61,61);
    setcolour(26,63,63,63);
    setcolour(27,63,0,0);
    setcolour(28,0,40,0);
    setcolour(29,63,32,0);
    setcolour(30,1,63,0);
    setcolour(31,30,40,50);
    setcolour(32,23,34,40);
    setcolour(33,20,28,33);
    setcolour(34,63,63,16);
    setcolour(128,0,0,0);
    setcolour(129,42,42,42);
    setcolour(130,59,0,0);
    setcolour(131,42,0,0);
    setcolour(132,55,34,0);
    setcolour(133,63,59,0);
    setcolour(134,34,63,0);
    setcolour(135,0,34,0);
    setcolour(136,0,46,25);
    setcolour(137,0,63,63);
    setcolour(138,0,0,63);
    setcolour(139,0,0,50);
    setcolour(140,0,0,42);
    setcolour(141,0,0,34);
    setcolour(142,0,0,21);
    setcolour(143,0,0,13);
    setcolour(144,50,42,21);
    setcolour(145,59,21,8);
    setcolour(146,42,21,8);
    setcolour(147,63,50,42);
    setcolour(148,0,13,13);
    setcolour(149,0,17,17);
    setcolour(150,0,21,21);
    setcolour(151,0,25,25);
    setcolour(152,0,29,29);
    setcolour(153,0,34,34);
    setcolour(154,0,38,38);
    setcolour(155,0,42,42);
    setcolour(156,0,50,50);
    setcolour(157,0,55,55);
    setcolour(158,0,59,59);
    setcolour(159,63,63,63);
    for (i = 0; i < 63; i++)
	setcolour(192+i,63,i,0);
}

static int makedeath(int player, int angle)
{
    int i,j,k,x,y;
    int zz[30];

#define PUT(b) do { deaths[player][j++] = (b); } while (0)

    j = 0;
    for (i = 0; i < 9; i++) {
	PUT(255-i); PUT(255); PUT(255-i); PUT(255);
	PUT(18-i*2); PUT(0); PUT(18-i*2); PUT(0);
	k = 17-i*2;
	for (x = 0; x <= k; x++) zz[x] = 19*x/k;
	for (y = 0; y <= k; y++)
	    for (x = 0; x <= k; x++)
		PUT(spr[player][angle][8+zz[y]*20+zz[x]]);
    }

    for (i = 0; i < 5; i++) switch(abs(i-3)) {
      case 2:
	PUT(247); PUT(255); PUT(247); PUT(255); PUT(2); PUT(0); PUT(2); PUT(0);
	PUT(192); PUT(192);
	PUT(192); PUT(192);
	break;
      case 1:
	PUT(248); PUT(255); PUT(248); PUT(255); PUT(4); PUT(0); PUT(4); PUT(0);
	PUT(  0); PUT(192); PUT(192); PUT(  0);
	PUT(192); PUT(223); PUT(223); PUT(192);
	PUT(192); PUT(223); PUT(223); PUT(192);
	PUT(  0); PUT(192); PUT(192); PUT(  0);
	break;
      case 0:
	PUT(249); PUT(255); PUT(249); PUT(255); PUT(6); PUT(0); PUT(6); PUT(0);
	PUT(  0); PUT(  0); PUT(192); PUT(192); PUT(  0); PUT(  0);
	PUT(  0); PUT(192); PUT(223); PUT(223); PUT(192); PUT(  0);
	PUT(192); PUT(223); PUT(255); PUT(255); PUT(223); PUT(192);
	PUT(192); PUT(223); PUT(255); PUT(255); PUT(223); PUT(192);
	PUT(  0); PUT(192); PUT(223); PUT(223); PUT(192); PUT(  0);
	PUT(  0); PUT(  0); PUT(192); PUT(192); PUT(  0); PUT(  0);
	break;
    }

#undef PUT

}

static int make_deliveries(void)
{
    static const unsigned char delhdr[8] = { 10,0,10,0,40,0,40,0 };
    static const int iris[26] = {
	0,1,2,3,4,5,6,7,8,9,10,10,10,10,10,10,9,8,7,6,5,4,3,2,1,0
    };

    int i, j, k, l, x, y, w;
    int zz[30];

    for (i = 0; i < 36; i++)
	for (x = 0; x < 20; x++)
	    for (y = 0; y < 20; y++) {
		if (angles[y][x] == 99)
		    w = 2 * (vehicle[y][x] == 99);
		else
		    w = (angles[y][x]+39-i) % 36 < 6;
		if (w) {
		    for (j = 0; j < 2; j++)
			spr[j][i][8+20*y+x] = j+25+2*w;
		} else {
		    for (j = 0; j < 2; j++)
			spr[j][i][8+20*y+x] = vehicle[y][x];
		}
	    }

    for (j = 0; j < 2; j++)
	for (i = 0; i < 36; i++) {
	    spr[j][i][0] = spr[j][i][1] = spr[j][i][2] = spr[j][i][3] = 0;
	    spr[j][i][4] = spr[j][i][6] = 20;
	    spr[j][i][5] = spr[j][i][7] = 0;
	}

    for (i = 0; i < 2; i++)
	for (j = 0; j < 26; j++) {
	    memset(deliv[i][j], 0, 1608);
	    memcpy(deliv[i][j], delhdr, 8);

	    k = j-6; if (k<5) k = 5; if (k>10) k = 10;
	    x = 10-k; y = 9+k-x;
	    for (l = 0; l < 20; l++) zz[l] = -1;
	    for (l = x; l <= 9+k; l++) zz[l] = 19*(l-x)/y;
	    for (x = 0; x < 20; x++)
		for (y = 0; y < 20; y++) {
		    l = 0;
		    if (zz[x]>=0 && zz[y]>=0)
			l = spr[i][18*i][8+20*zz[y]+zz[x]];
		    if (l == 0) {
			l = 127;
			if ((x>=4 && x<16 && y>=4 && y<16) || (x+y==19))
			    l = 32;
			else if (x+y<19)
			    l = 33;
			else
			    l = 31;
		    }
		    imagepixel(deliv[i][j],x,y,l);
		}
	    imageonimage(deliv[i][j],tile,0,-iris[j],0);
	    imageonimage(deliv[i][j],tile,-iris[j],10,0);
	    imageonimage(deliv[i][j],tile,10+iris[j],0,0);
	    imageonimage(deliv[i][j],tile,10,10+iris[j],0);
	    if (j>=16)
		imageonimage(deliv[i][j],spr[i][18*i],0,0,0);
	}
}

struct minimax {
    int started;
    int xmin, ymin;
    int xmax, ymax;
};

static void minimax(void *ctx, int x, int y)
{
    struct minimax *mm = (struct minimax *)ctx;

    if (!mm->started) {
	mm->xmin = mm->xmax = x;
	mm->ymin = mm->ymax = y;
	mm->started = 1;
    } else {
	if (mm->xmin > x) mm->xmin = x;
	if (mm->xmax < x) mm->xmax = x;
	if (mm->ymin > y) mm->ymin = y;
	if (mm->ymax < y) mm->ymax = y;
    }
}

struct imageplot {
    Image image;
    int fg, bg, mask;
};

static void plot_on_image(void *ctx, int x, int y)
{
    struct imageplot *img = (struct imageplot *)ctx;
    int i, j;

    for (i = -1; i < 2; i++)
	for (j = -1; j < 2; j++) {
	    if (!i && !j)
		imagepixel(img->image, x+i, y+j, img->fg);
	    else if (img->bg >= 0 &&
		     getimagepixel(img->image, x+i, y+j) == img->mask)
		imagepixel(img->image, x+i, y+j, img->bg);
	}
}

static void make_text(void)
{
    struct minimax mm;
    struct imageplot p;
    char *const texts[3] = {
	"PLAYER ONE WINS", "PLAYER TWO WINS", "ROUND DRAWN"
    };
    Image *targets[3] = { &win1, &win2, &draw };
    int colours[3] = { 130, 134, 137 };
    Image img;
    int i, w, h;

    for (i = 0; i < 3; i++) {
	mm.started = 0;
	swash_text(0, 0, texts[i], minimax, &mm);

	assert(mm.started);
	/*
	 * xmax-xmin+1 is the number of pixels for the text. In
	 * addition, we are surrounding the text with a one-pixel
	 * black border, so we need to add another two.
	 */
	w = mm.xmax - mm.xmin + 3;
	h = mm.ymax - mm.ymin + 3;

	img = makeimage(w, h, 0, 0);
	memset(img+8, 129, w*h);       /* mask value */

	p.image = img;
	p.fg = colours[i];
	p.bg = 128;
	p.mask = 129;

	swash_text(1-mm.xmin, 1-mm.ymin, texts[i], plot_on_image, &p);

	*targets[i] = img;
    }
}

static void expand_title(void)
{
    struct imageplot p;

    title = makeimage(80, 240, 0, 0);

    /*
     * Now we want to draw a set of glowing blue lines inside which
     * we will put the players' titles, pictures and scores. To
     * make this easier, we'll draw our `lines' as rectangles.
     */
    {
	struct { int x1, y1, x2, y2; } rects[] = {
	    {6, 75, 6, 195},
	    {74, 75, 74, 195},
	    {6, 75, 74, 75},
	    {6, 135, 74, 135},
	    {6, 195, 74, 195},
	};
	int i;

	for (i = 0; i < lenof(rects); i++) {
	    int c, x, y;

	    for (c = 6; c-- ;)
		for (x = rects[i].x1-c; x <= rects[i].x2+c; x++)
		    for (y = rects[i].y1-c; y <= rects[i].y2+c; y++) {
			int cc = getimagepixel(title,x,y);
			if (cc == 0 || (cc > 138+c && cc < 138+6))
			    imagepixel(title,x,y,138+c);
		    }
	}
    }

    p.image = title;
    p.fg = 137;
    p.bg = 138;
    p.mask = 0;
    swash_centre(0, 80, 5, "SUMO", plot_on_image, &p);

    p.bg = p.mask = -1;

    swash_centre(1, 80, 25, "(C) 1994,2002", plot_on_image, &p);
    /* \x7F inhibits sideways extensions from the Swash font */
    swash_centre(0, 80, 41, "BY SIMON", plot_on_image, &p);
    swash_centre(0, 80, 57, "TATHAM", plot_on_image, &p);

    swash_centre(0, 80, 208, "`SELECT' KEY", plot_on_image, &p);
    swash_centre(0, 80, 224, "FOR OPTIONS", plot_on_image, &p);

    p.fg = 130;
    swash_centre(0, 80, 81, "PLAYER 1", plot_on_image, &p);

    p.fg = 134;
    swash_centre(0, 80, 141, "PLAYER 2", plot_on_image, &p);

}
