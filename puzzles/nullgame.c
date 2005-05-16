/*
 * nullgame.c [FIXME]: Template defining the null game (in which no
 * moves are permitted and nothing is ever drawn). This file exists
 * solely as a basis for constructing new game definitions - it
 * helps to have something which will compile from the word go and
 * merely doesn't _do_ very much yet.
 * 
 * Parts labelled FIXME actually want _removing_ (e.g. the dummy
 * field in each of the required data structures, and this entire
 * comment itself) when converting this source file into one
 * describing a real game.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND,
    NCOLOURS
};

struct game_params {
    int FIXME;
};

struct game_state {
    int FIXME;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->FIXME = 0;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    return FALSE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
}

static char *encode_params(game_params *params, int full)
{
    return dupstr("FIXME");
}

static config_item *game_configure(game_params *params)
{
    return NULL;
}

static game_params *custom_params(config_item *cfg)
{
    return NULL;
}

static char *validate_params(game_params *params)
{
    return NULL;
}

static char *new_game_desc(game_params *params, random_state *rs,
			   game_aux_info **aux)
{
    return dupstr("FIXME");
}

static void game_free_aux_info(game_aux_info *aux)
{
    assert(!"Shouldn't happen");
}

static char *validate_desc(game_params *params, char *desc)
{
    return NULL;
}

static game_state *new_game(game_params *params, char *desc)
{
    game_state *state = snew(game_state);

    state->FIXME = 0;

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->FIXME = state->FIXME;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state);
}

static game_state *solve_game(game_state *state, game_aux_info *aux,
			      char **error)
{
    return NULL;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static game_state *make_move(game_state *from, game_ui *ui, int x, int y,
			     int button)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct game_drawstate {
    int FIXME;
};

static void game_size(game_params *params, int *x, int *y)
{
    *x = *y = 200;                     /* FIXME */
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->FIXME = 0;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    /*
     * The initial contents of the window are not guaranteed and
     * can vary with front ends. To be on the safe side, all games
     * should start by drawing a big background-colour rectangle
     * covering the whole window.
     */
    draw_rect(fe, 0, 0, 200, 200, COL_BACKGROUND);
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir)
{
    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return FALSE;
}

#ifdef COMBINED
#define thegame nullgame
#endif

const struct game thegame = {
    "Null Game", NULL,
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    FALSE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    game_free_aux_info,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    FALSE, solve_game,
    FALSE, game_text_format,
    new_ui,
    free_ui,
    make_move,
    game_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
};
