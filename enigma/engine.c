/*
 * enigma/engine.c - the main game engine of Enigma. Provides the
 * make_move() function, which is given a move and a structure
 * containing a game state, and transforms the game state to
 * reflect the consequences of the move.
 * 
 * Copyright 2000 Simon Tatham. All rights reserved.
 * 
 * Enigma is licensed under the MIT licence. See the file LICENCE for
 * details.
 * 
 * - we are all amf -
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "enigma.h"

#define addlist(pos,list) do { \
	pos = smalloc(sizeof(posn)); \
	list##tail.prev->next = pos; \
	pos->prev = list##tail.prev; \
	pos->next = &list##tail; \
	list##tail.prev = pos; \
    } while (0)

#define remlist(pos) do { \
	pos->next->prev = pos->prev; \
	pos->prev->next = pos->next; \
	free (pos); \
    } while (0)

/*
 * Set up a game_state from a level.
 */
gamestate *init_game (level *lev) {
    gamestate *ret;
    int i, j;

    ret = gamestate_new(lev->width, lev->height);
    memcpy(ret->leveldata, lev->leveldata, lev->width * lev->height);
    ret->title = lev->title;
    ret->movenum = 0;
    ret->gold_got = 0;
    ret->status = PLAYING;
    ret->gold_total = 0;
    for (j = 0; j < ret->height; j++) {
	for (i = 0; i < ret->width; i++) {
	    if (ret->leveldata[j*ret->width+i] == '$')
		ret->gold_total++;
	    if (ret->leveldata[j*ret->width+i] == '@') {
		ret->player_x = i;
		ret->player_y = j;
	    }
	}
    }
    return ret;
}

#define destroyable(x,y) \
    ((x) > 0 && (x) < w-1 && (y) > 0 && (y) < h-1 && \
     ret->leveldata[w*((y)-1)+(x)] != '=' && \
     ret->leveldata[w*((y)+1)+(x)] != '=' && \
     ret->leveldata[w*(y)+(x)-1] != '=' && \
     ret->leveldata[w*(y)+(x)+1] != '=')

/*
 * 'key' is h, j, k or l
 */
gamestate *make_move (gamestate *state, char key) {
    gamestate *ret;
    int xfrom, yfrom, pfrom, xto, yto, pto;
    int i, j, k;
    int w, h;
    typedef struct posn posn;
    struct posn {
	posn *next, *prev;
	int x, y;
    };
    posn movehead = { NULL, NULL, -1, -1 }, movetail = { NULL, NULL, -1, -1 };
    posn nmovhead = { NULL, NULL, -1, -1 }, nmovtail = { NULL, NULL, -1, -1 };
    posn expshead = { NULL, NULL, -1, -1 }, expstail = { NULL, NULL, -1, -1 };
    posn *pos;

    /*
     * initialise those lists
     */
    movehead.next = &movetail; movetail.prev = &movehead;
    nmovhead.next = &nmovtail; nmovtail.prev = &nmovhead;
    expshead.next = &expstail; expstail.prev = &expshead;

    /*
     * copy most of the state
     */
    ret = gamestate_copy(state);

    /*
     * get the level dimensions
     */
    w = ret->width;
    h = ret->height;

    /*
     * sanity check the state
     */
    if (ret->leveldata[w*ret->player_y+ret->player_x] != '@') {
	ret->status = BROKEN;
	return ret;
    }

    /*
     * determine move destination
     */
    xto = xfrom = ret->player_x;
    yto = yfrom = ret->player_y;
    switch (key) {
      case 'h': xto--; break; case 'l': xto++; break;
      case 'k': yto--; break; case 'j': yto++; break;
    }
    pfrom = yfrom * w + xfrom;
    pto = yto * w + xto;

    /*
     * disallow the move if the destination isn't movable to
     */
    i = ret->leveldata[pto];
    if (i == '+' || i == '|' || i == '-' || i == '%' ||
	(i == 'E' && ret->gold_got < ret->gold_total))
	return ret;		       /* do nothing */

    /*
     * increment the gold counter if we're moving on to a $
     */
    if (i == '$')
	ret->gold_got++;

    /*
     * is this a push move?
     */
    if (i == '8' || i == 'o' ||
	i == 'v' || i == '>' || i == '<' || i == '^' ||
	i == 'W' || i == 'X' || i == 'Y' || i == 'Z') {
	/*
	 * Check the next space along to see if we can push into
	 * it. Disallow the move otherwise.
	 */
	j = pto + (pto - pfrom);
	k = ret->leveldata[j];
	if (k != ' ' && k != '.')
	    return ret;		       /* do nothing */

	/*
	 * Also disallow the move if we're trying to push something
	 * against its direction.
	 */
	if (((i == 'v' || i == 'X') && yto == yfrom-1) ||
	    ((i == '>' || i == 'W') && xto == xfrom-1) ||
	    ((i == '<' || i == 'Y') && xto == xfrom+1) ||
	    ((i == '^' || i == 'Z') && yto == yfrom+1))
	    return ret;		       /* do nothing */

	/*
	 * Now we know we can make the move. Move the pushed piece,
	 * and add it to the moving list if there's a blank in its
	 * direction. The movement of the player itself will be
	 * performed outside this conditional block.
	 */
	ret->leveldata[j] = ret->leveldata[pto];
	if (((i == 'v' || i == 'X') && ret->leveldata[j+w] == ' ') ||
	    ((i == '>' || i == 'W') && ret->leveldata[j+1] == ' ') ||
	    ((i == '<' || i == 'Y') && ret->leveldata[j-1] == ' ') ||
	    ((i == '^' || i == 'Z') && ret->leveldata[j-w] == ' ') ||
	    i == 'o') {
	    addlist(pos, move);
	    pos->x = xto + (xto - xfrom);
	    pos->y = yto + (yto - yfrom);
	    if (ret->leveldata[j] == 'o') {
		if (yto-yfrom == -1)
		    ret->leveldata[j] = 'a' | 0x80;   /* a is upmoving o */
		else if (yto-yfrom == +1)
		    ret->leveldata[j] = 'b' | 0x80;   /* b is downmoving o */
		else if (xto-xfrom == +1)
		    ret->leveldata[j] = 'c' | 0x80;   /* c is rightmoving o */
		else if (xto-xfrom == -1)
		    ret->leveldata[j] = 'd' | 0x80;   /* d is leftmoving o */
	    } else
		ret->leveldata[j] |= 0x80;  /* just flag as moving */
	}
    }

    /*
     * Move the player and expose the square it has left. Increment
     * the move count here as well, and add the move to the stored
     * sequence.
     */
    ret->leveldata[pfrom] = ' ';
    ret->leveldata[pto] = '@';
    ret->player_x = xto;
    ret->player_y = yto;
    ret->movenum++;
    if (ret->movenum > ret->sequence_size) {
	ret->sequence_size = ret->movenum + 128;
	ret->sequence = srealloc(ret->sequence, ret->sequence_size);
    }
    ret->sequence[ret->movenum-1] = key;
    addlist(pos, exps);
    pos->x = xfrom;
    pos->y = yfrom;

    /*
     * Kill the player if he walked into a &, or flag the level as
     * complete if he legitimately walked into the E. In this
     * situation we still do the movements around him, partly for
     * looks and mostly because if you walk into the E and a bomb
     * blows it up in the same move, you _are_ dead.
     */
    if (i == '&')
	ret->status = DIED;
    else if (i == 'E' && ret->gold_got >= ret->gold_total)
	ret->status = COMPLETED;

    /*
     * Now repeatedly process exposed cells and moving objects
     * until none of either remain.
     */
    while (expshead.next->x != -1 || movehead.next->x != -1) {
	/*
	 * Process exposed cells, which may generate more moving
	 * objects.
	 */
	while (expshead.next->x != -1) {
	    posn *p, *q;
	    p = expshead.next;
	    while (p->x != -1) {
		q = p->next;
		j = p->y * w + p->x;
		if ((ret->leveldata[j-w] & 0x7F) == 'v' ||
		    (ret->leveldata[j-w] & 0x7F) == 'X') {
		    /*
		     * Falling object above the exposed cell.
		     */
		    if (!(ret->leveldata[j-w] & 0x80)) {
			addlist(pos, move);
			pos->x = p->x;
			pos->y = p->y-1;
			ret->leveldata[j-w] |= 0x80;/* flag as moving */
		    }
		} else if ((ret->leveldata[j-1] & 0x7F) == '>' ||
			   (ret->leveldata[j-1] & 0x7F) == 'W') {
		    /*
		     * Right-moving object to the left of the
		     * exposed cell.
		     */
		    if (!(ret->leveldata[j-1] & 0x80)) {
			addlist(pos, move);
			pos->x = p->x-1;
			pos->y = p->y;
			ret->leveldata[j-1] |= 0x80;/* flag as moving */
		    }
		} else if ((ret->leveldata[j+1] & 0x7F) == '<' ||
			   (ret->leveldata[j+1] & 0x7F) == 'Y') {
		    /*
		     * Left-moving object to the right of the
		     * exposed cell.
		     */
		    if (!(ret->leveldata[j+1] & 0x80)) {
			addlist(pos, move);
			pos->x = p->x+1;
			pos->y = p->y;
			ret->leveldata[j+1] |= 0x80;/* flag as moving */
		    }
		} else if ((ret->leveldata[j+w] & 0x7F) == '^' ||
			   (ret->leveldata[j+w] & 0x7F) == 'Z') {
		    /*
		     * Up-moving object below the exposed cell.
		     */
		    if (!(ret->leveldata[j+w] & 0x80)) {
			addlist(pos, move);
			pos->x = p->x;
			pos->y = p->y+1;
			ret->leveldata[j+w] |= 0x80;/* flag as moving */
		    }
		}
		remlist(p);
		p = q;
	    }
	}

	/*
	 * Process moving objects, which may generate more exposed
	 * cells. We empty the `move' list and fill up the `nmov'
	 * list with things which are still moving at the end of
	 * the turn. Then we manhandle the new list back into
	 * `move'.
	 */
	nmovhead.next = &nmovtail;
	nmovtail.prev = &nmovhead;

	while (movehead.next->x != -1) {
	    posn *p, *q;
	    int dx, dy, dp, bomb, sack;

	    p = movehead.next;
	    while (p->x != -1) {
		q = p->next;
		j = p->y * w + p->x;
		k = ret->leveldata[j] & 0x7F;
		dx = dy = bomb = sack = 0;
		switch (k) {
		  case 'a': dx =  0; dy = -1; bomb = 0; sack = 1; break;
		  case 'b': dx =  0; dy = +1; bomb = 0; sack = 1; break;
		  case 'c': dx = +1; dy =  0; bomb = 0; sack = 1; break;
		  case 'd': dx = -1; dy =  0; bomb = 0; sack = 1; break;
		  case 'v': dx =  0; dy = +1; bomb = 0; sack = 0; break;
		  case '>': dx = +1; dy =  0; bomb = 0; sack = 0; break;
		  case '<': dx = -1; dy =  0; bomb = 0; sack = 0; break;
		  case '^': dx =  0; dy = -1; bomb = 0; sack = 0; break;
		  case 'X': dx =  0; dy = +1; bomb = 1; sack = 0; break;
		  case 'W': dx = +1; dy =  0; bomb = 1; sack = 0; break;
		  case 'Z': dx =  0; dy = -1; bomb = 1; sack = 0; break;
		  case 'Y': dx = -1; dy =  0; bomb = 1; sack = 0; break;
		  case 'x': dx = +1; dy =  0; bomb = 2; sack = 0; break;
		  case 'w': dx =  0; dy = +1; bomb = 2; sack = 0; break;
		  case 'z': dx = +1; dy =  0; bomb = 2; sack = 0; break;
		  case 'y': dx =  0; dy = +1; bomb = 2; sack = 0; break;
		}
		dp = dy*w+dx;
		if (bomb == 2) {       /* special case: exploding thing */
		    ret->leveldata[j] = ' ';   /* remove the bomb */
		    addlist(pos, exps);   /* and expose the vacated cell */
		    pos->x = p->x;
		    pos->y = p->y;
		    if (destroyable(p->x-dx, p->y-dy)) {
			ret->leveldata[j-dp] = ' ';  /* and the left/up cell */
			addlist(pos, exps);  /* expose the vacated cell */
			pos->x = p->x - dx;
			pos->y = p->y - dy;
		    }
		    if (destroyable(p->x+dx, p->y+dy)) {
			ret->leveldata[j+dp] = ' ';  /* the right/down cell */
			addlist(pos, exps);  /* expose the vacated cell */
			pos->x = p->x + dx;
			pos->y = p->y + dy;
		    }
		} else {
		    if (!bomb && !sack && (ret->leveldata[j+dp] == 'X' ||
					   ret->leveldata[j+dp] == 'W' ||
					   ret->leveldata[j+dp] == 'Y' ||
					   ret->leveldata[j+dp] == 'Z')) {
			ret->leveldata[j] = ' ';   /* remove the detonator */
			addlist(pos, exps);   /* expose the vacated cell */
			pos->x = p->x;
			pos->y = p->y;
			ret->leveldata[j+dp] += 32;   /* lowercase it */
			addlist(pos, nmov);   /* add exploding bomb to list */
			pos->x = p->x + dx;
			pos->y = p->y + dy;
		    } else {
			if (ret->leveldata[j+dp] == ' ' ||
			    (ret->leveldata[j+dp] == '.' && !sack) ||
			    ret->leveldata[j+dp] == '@') {
			    ret->leveldata[j+dp] = k | 0x80;   /* stay moving */
			    ret->leveldata[j] = ' ';   /* fall */
			    addlist(pos, exps);  /* expose the vacated cell */
			    pos->x = p->x;
			    pos->y = p->y;
			    addlist(pos, nmov);   /* keep the object moving */
			    pos->x = p->x + dx;
			    pos->y = p->y + dy;
			} else {
			    if (sack)
				ret->leveldata[j] = 'o';
			    else
				ret->leveldata[j] &= 0x7F; /* non-moving */
			}
		    }
		}
		remlist(p);
		p = q;
	    }
	}

	if (nmovhead.next->next) {
	    movehead.next = nmovhead.next;
	    movetail.prev = nmovtail.prev;
	    movehead.next->prev = &movehead;
	    movetail.prev->next = &movetail;
	} else {		       /* special case - nmov list empty */
	    movehead.next = &movetail;
	    movetail.prev = &movehead;
	}
    }

    /*
     * If we've killed the player at any point, flag him as dead.
     */
    if (ret->leveldata[w*ret->player_y+ret->player_x] != '@')
	ret->status = DIED;

    /*
     * I think we're done!
     */
    return ret;
}

/*
 * Validate a level structure to ensure it contains no obvious
 * mistakes. Returns an error message in `buffer', or NULL.
 */
#define iswalloroutdoors(c) \
    ((c) == '+' || (c) == '-' || (c) == '|' || \
	 (c) == '%' || (c) == '&' || (c) == '=')

char *validate(level *level, char *buffer, int buflen)
{
    int x, y, x1, y1;
    int c, c1;
    int w = level->width, h = level->height;
    char internalbuf[256];	       /* big enough for any of our sprintfs */

    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {

	    c = level->leveldata[w*y+x];

	    /*
	     * Check that the boundary of the level is either solid
	     * wall or the `=' outdoors character.
	     */
	    if ((x == 0 || y == 0 || x == w-1 || y == h-1) &&
		!iswalloroutdoors(c)) {
		sprintf(internalbuf, "level boundary is not wall or `=' at"
			" line %d column %d", y, x);
		strncpy(buffer, internalbuf, buflen);
		buffer[buflen-1] = '\0';
		return buffer;
	    }

	    /*
	     * Check that any `=' outdoors character is adjacent
	     * only to walls and other `='.
	     */
	    if (c == '=' &&
		((x > 0 && !iswalloroutdoors(level->leveldata[w*y+(x-1)])) ||
		 (x < w-1 && !iswalloroutdoors(level->leveldata[w*y+(x+1)])) ||
		 (y > 0 && !iswalloroutdoors(level->leveldata[w*(y-1)+x])) ||
		 (y < h-1 && !iswalloroutdoors(level->leveldata[w*(y+1)+x]))))
	    {
		sprintf(internalbuf, "`=' is adjacent to something other than"
			" wall or `=' at line %d column %d", y, x);
		strncpy(buffer, internalbuf, buflen);
		buffer[buflen-1] = '\0';
		return buffer;		
	    }

	    switch (c) {
	      case 'v': case 'X':
		/* Down-falling piece; check below it. */
		x1 = x; y1 = y + 1; break;
	      case '>': case 'W':
		/* Right-falling piece; check to its right. */
		x1 = x + 1; y1 = y; break;
	      case '^': case 'Z':
		/* Up-falling piece; check above it. */
		x1 = x; y1 = y - 1; break;
	      case '<': case 'Y':
		/* Left-falling piece; check to its left. */
		x1 = x - 1; y1 = y; break;
	      case '@': case '$': case '%': case '-': case '+': case '|':
	      case '&': case 'E': case 'o': case '8': case '.': case ':':
	      case ' ': case '=':
		/* Non-falling piece; carry on. */
		continue;
	      default:
		/* Unrecognised character. */
		if (isprint(c))
		    sprintf(internalbuf, "unrecognised character `%c' at"
			    " line %d column %d", c, y, x);
		else
		    sprintf(internalbuf, "unrecognised character %d at"
			    " line %d column %d", (unsigned char)c, y, x);
		strncpy(buffer, internalbuf, buflen);
		buffer[buflen-1] = '\0';
		return buffer;
	    }

	    /*
	     * This is an assert rather than a validation check,
	     * because the boundary check above should have ruled
	     * out the possibility of it failing _now_ by failing
	     * earlier.
	     */
	    assert(x1 >= 0 && x1 < w && y1 >= 0 && y1 < h);
	    c1 = level->leveldata[w*y1+x1];
	    if (c1 == ' ') {
		sprintf(internalbuf, "`%c' piece resting on thin air"
			" at line %d column %d", c, y, x);
		strncpy(buffer, internalbuf, buflen);
		buffer[buflen-1] = '\0';
		return buffer;
	    }
	}

    return NULL;
}
