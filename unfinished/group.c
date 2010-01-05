/*
 * group.c: a Latin-square puzzle, but played with groups' Cayley
 * tables. That is, you are given a Cayley table of a group with
 * most elements blank and a few clues, and you must fill it in
 * so as to preserve the group axioms.
 *
 * This is a perfectly playable and fully working puzzle, but I'm
 * leaving it for the moment in the 'unfinished' directory because
 * it's just too esoteric (not to mention _hard_) for me to be
 * comfortable presenting it to the general public as something they
 * might (implicitly) actually want to play.
 *
 * TODO:
 *
 *  - more solver techniques?
 *     * Inverses: once we know that gh = e, we can immediately
 * 	 deduce hg = e as well; then for any gx=y we can deduce
 * 	 hy=x, and for any xg=y we have yh=x.
 *     * Hard-mode associativity: we currently deduce based on
 * 	 definite numbers in the grid, but we could also winnow
 * 	 based on _possible_ numbers.
 *     * My overambitious original thoughts included wondering if we
 * 	 could infer that there must be elements of certain orders
 * 	 (e.g. a group of order divisible by 5 must contain an
 * 	 element of order 5), but I think in fact this is probably
 * 	 silly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h"

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(A) \
    A(TRIVIAL,Trivial,NULL,t) \
    A(NORMAL,Normal,solver_normal,n) \
    A(HARD,Hard,NULL,h) \
    A(EXTREME,Extreme,NULL,x) \
    A(UNREASONABLE,Unreasonable,NULL,u)
#define ENUM(upper,title,func,lower) DIFF_ ## upper,
#define TITLE(upper,title,func,lower) #title,
#define ENCODE(upper,title,func,lower) #lower
#define CONFIG(upper,title,func,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const group_diffnames[] = { DIFFLIST(TITLE) };
static char const group_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_USER,
    COL_HIGHLIGHT,
    COL_ERROR,
    COL_PENCIL,
    NCOLOURS
};

/*
 * In identity mode, we number the elements e,a,b,c,d,f,g,h,...
 * Otherwise, they're a,b,c,d,e,f,g,h,... in the obvious way.
 */
#define E_TO_FRONT(c,id) ( (id) && (c)<=5 ? (c) % 5 + 1 : (c) )
#define E_FROM_FRONT(c,id) ( (id) && (c)<=5 ? ((c) + 3) % 5 + 1 : (c) )

#define FROMCHAR(c,id) E_TO_FRONT((((c)-('A'-1)) & ~0x20), id)
#define ISCHAR(c) (((c)>='A'&&(c)<='Z') || ((c)>='a'&&(c)<='z'))
#define TOCHAR(c,id) (E_FROM_FRONT(c,id) + ('a'-1))

struct game_params {
    int w, diff, id;
};

struct game_state {
    game_params par;
    digit *grid;
    unsigned char *immutable;
    int *pencil;		       /* bitmaps using bits 1<<1..1<<n */
    int completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 6;
    ret->diff = DIFF_NORMAL;
    ret->id = TRUE;

    return ret;
}

const static struct game_params group_presets[] = {
    {  6, DIFF_NORMAL, TRUE },
    {  6, DIFF_NORMAL, FALSE },
    {  8, DIFF_NORMAL, TRUE },
    {  8, DIFF_NORMAL, FALSE },
    {  8, DIFF_HARD, TRUE },
    {  8, DIFF_HARD, FALSE },
    { 12, DIFF_NORMAL, TRUE },
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(group_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = group_presets[i]; /* structure copy */

    sprintf(buf, "%dx%d %s%s", ret->w, ret->w, group_diffnames[ret->diff],
	    ret->id ? "" : ", identity hidden");

    *name = dupstr(buf);
    *params = ret;
    return TRUE;
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
    char const *p = string;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    params->diff = DIFF_NORMAL;
    params->id = TRUE;

    while (*p) {
	if (*p == 'd') {
	    int i;
	    p++;
	    params->diff = DIFFCOUNT+1; /* ...which is invalid */
	    if (*p) {
		for (i = 0; i < DIFFCOUNT; i++) {
		    if (*p == group_diffchars[i])
			params->diff = i;
		}
		p++;
	    }
	} else if (*p == 'i') {
	    params->id = FALSE;
	    p++;
	} else {
	    /* unrecognised character */
	    p++;
	}
    }
}

static char *encode_params(game_params *params, int full)
{
    char ret[80];

    sprintf(ret, "%d", params->w);
    if (full)
        sprintf(ret + strlen(ret), "d%c", group_diffchars[params->diff]);
    if (!params->id)
        sprintf(ret + strlen(ret), "i");

    return dupstr(ret);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "Grid size";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Difficulty";
    ret[1].type = C_CHOICES;
    ret[1].sval = DIFFCONFIG;
    ret[1].ival = params->diff;

    ret[2].name = "Show identity";
    ret[2].type = C_BOOLEAN;
    ret[2].sval = NULL;
    ret[2].ival = params->id;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->diff = cfg[1].ival;
    ret->id = cfg[2].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->w < 3 || params->w > 26)
        return "Grid size must be between 3 and 26";
    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";
    if (!params->id && params->diff == DIFF_TRIVIAL) {
	/*
	 * We can't have a Trivial-difficulty puzzle (i.e. latin
	 * square deductions only) without a clear identity, because
	 * identityless puzzles always have two rows and two columns
	 * entirely blank, and no latin-square deduction permits the
	 * distinguishing of two such rows.
	 */
	return "Trivial puzzles must have an identity";
    }
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

static int solver_normal(struct latin_solver *solver, void *vctx)
{
    int w = solver->o;
#ifdef STANDALONE_SOLVER
    char **names = solver->names;
#endif
    digit *grid = solver->grid;
    int i, j, k;

    /*
     * Deduce using associativity: (ab)c = a(bc).
     *
     * So we pick any a,b,c we like; then if we know ab, bc, and
     * (ab)c we can fill in a(bc).
     */
    for (i = 1; i < w; i++)
	for (j = 1; j < w; j++)
	    for (k = 1; k < w; k++) {
		if (!grid[i*w+j] || !grid[j*w+k])
		    continue;
		if (grid[(grid[i*w+j]-1)*w+k] &&
		    !grid[i*w+(grid[j*w+k]-1)]) {
		    int x = grid[j*w+k]-1, y = i;
		    int n = grid[(grid[i*w+j]-1)*w+k];
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%*sassociativity on %s,%s,%s: %s*%s = %s*%s\n",
			       solver_recurse_depth*4, "",
			       names[i], names[j], names[k],
			       names[grid[i*w+j]-1], names[k],
			       names[i], names[grid[j*w+k]-1]);
			printf("%*s  placing %s at (%d,%d)\n",
			       solver_recurse_depth*4, "",
			       names[n-1], x+1, y+1);
		    }
#endif
		    if (solver->cube[(x*w+y)*w+n-1]) {
			latin_solver_place(solver, x, y, n);
			return 1;
		    } else {
#ifdef STANDALONE_SOLVER
			if (solver_show_working)
			    printf("%*s  contradiction!\n",
				   solver_recurse_depth*4, "");
			return -1;
#endif
		    }
		}
		if (!grid[(grid[i*w+j]-1)*w+k] &&
		    grid[i*w+(grid[j*w+k]-1)]) {
		    int x = k, y = grid[i*w+j]-1;
		    int n = grid[i*w+(grid[j*w+k]-1)];
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%*sassociativity on %s,%s,%s: %s*%s = %s*%s\n",
			       solver_recurse_depth*4, "",
			       names[i], names[j], names[k],
			       names[grid[i*w+j]-1], names[k],
			       names[i], names[grid[j*w+k]-1]);
			printf("%*s  placing %s at (%d,%d)\n",
			       solver_recurse_depth*4, "",
			       names[n-1], x+1, y+1);
		    }
#endif
		    if (solver->cube[(x*w+y)*w+n-1]) {
			latin_solver_place(solver, x, y, n);
			return 1;
		    } else {
#ifdef STANDALONE_SOLVER
			if (solver_show_working)
			    printf("%*s  contradiction!\n",
				   solver_recurse_depth*4, "");
			return -1;
#endif
		    }
		}
	    }

    return 0;
}

#define SOLVER(upper,title,func,lower) func,
static usersolver_t const group_solvers[] = { DIFFLIST(SOLVER) };

static int solver(game_params *params, digit *grid, int maxdiff)
{
    int w = params->w;
    int ret;
    struct latin_solver solver;
#ifdef STANDALONE_SOLVER
    char *p, text[100], *names[50];
    int i;
#endif

    latin_solver_alloc(&solver, grid, w);
#ifdef STANDALONE_SOLVER
    for (i = 0, p = text; i < w; i++) {
	names[i] = p;
	*p++ = TOCHAR(i+1, params->id);
	*p++ = '\0';
    }
    solver.names = names;
#endif

    ret = latin_solver_main(&solver, maxdiff,
			    DIFF_TRIVIAL, DIFF_HARD, DIFF_EXTREME,
			    DIFF_EXTREME, DIFF_UNREASONABLE,
			    group_solvers, NULL, NULL, NULL);

    latin_solver_free(&solver);

    return ret;
}

/* ----------------------------------------------------------------------
 * Grid generation.
 */

static char *encode_grid(char *desc, digit *grid, int area)
{
    int run, i;
    char *p = desc;

    run = 0;
    for (i = 0; i <= area; i++) {
	int n = (i < area ? grid[i] : -1);

	if (!n)
	    run++;
	else {
	    if (run) {
		while (run > 0) {
		    int c = 'a' - 1 + run;
		    if (run > 26)
			c = 'z';
		    *p++ = c;
		    run -= c - ('a' - 1);
		}
	    } else {
		/*
		 * If there's a number in the very top left or
		 * bottom right, there's no point putting an
		 * unnecessary _ before or after it.
		 */
		if (p > desc && n > 0)
		    *p++ = '_';
	    }
	    if (n > 0)
		p += sprintf(p, "%d", n);
	    run = 0;
	}
    }
    return p;
}

/* ----- data generated by group.gap begins ----- */

struct group {
    unsigned long autosize;
    int order, ngens;
    const char *gens;
};
struct groups {
    int ngroups;
    const struct group *groups;
};

static const struct group groupdata[] = {
    /* order 2 */
    {1L, 2, 1, "BA"},
    /* order 3 */
    {2L, 3, 1, "BCA"},
    /* order 4 */
    {2L, 4, 1, "BCDA"},
    {6L, 4, 2, "BADC" "CDAB"},
    /* order 5 */
    {4L, 5, 1, "BCDEA"},
    /* order 6 */
    {6L, 6, 2, "CFEBAD" "BADCFE"},
    {2L, 6, 1, "DCFEBA"},
    /* order 7 */
    {6L, 7, 1, "BCDEFGA"},
    /* order 8 */
    {4L, 8, 1, "BCEFDGHA"},
    {8L, 8, 2, "BDEFGAHC" "EGBHDCFA"},
    {8L, 8, 2, "EGBHDCFA" "BAEFCDHG"},
    {24L, 8, 2, "BDEFGAHC" "CHDGBEAF"},
    {168L, 8, 3, "BAEFCDHG" "CEAGBHDF" "DFGAHBCE"},
    /* order 9 */
    {6L, 9, 1, "BDECGHFIA"},
    {48L, 9, 2, "BDEAGHCIF" "CEFGHAIBD"},
    /* order 10 */
    {20L, 10, 2, "CJEBGDIFAH" "BADCFEHGJI"},
    {4L, 10, 1, "DCFEHGJIBA"},
    /* order 11 */
    {10L, 11, 1, "BCDEFGHIJKA"},
    /* order 12 */
    {12L, 12, 2, "GLDKJEHCBIAF" "BCEFAGIJDKLH"},
    {4L, 12, 1, "EHIJKCBLDGFA"},
    {24L, 12, 2, "BEFGAIJKCDLH" "FJBKHLEGDCIA"},
    {12L, 12, 2, "GLDKJEHCBIAF" "BAEFCDIJGHLK"},
    {12L, 12, 2, "FDIJGHLBKAEC" "GIDKFLHCJEAB"},
    /* order 13 */
    {12L, 13, 1, "BCDEFGHIJKLMA"},
    /* order 14 */
    {42L, 14, 2, "ELGNIBKDMFAHCJ" "BADCFEHGJILKNM"},
    {6L, 14, 1, "FEHGJILKNMBADC"},
    /* order 15 */
    {8L, 15, 1, "EGHCJKFMNIOBLDA"},
    /* order 16 */
    {8L, 16, 1, "MKNPFOADBGLCIEHJ"},
    {96L, 16, 2, "ILKCONFPEDJHGMAB" "BDFGHIAKLMNCOEPJ"},
    {32L, 16, 2, "MIHPFDCONBLAKJGE" "BEFGHJKALMNOCDPI"},
    {32L, 16, 2, "IFACOGLMDEJBNPKH" "BEFGHJKALMNOCDPI"},
    {16L, 16, 2, "MOHPFKCINBLADJGE" "BDFGHIEKLMNJOAPC"},
    {16L, 16, 2, "MIHPFDJONBLEKCGA" "BDFGHIEKLMNJOAPC"},
    {32L, 16, 2, "MOHPFDCINBLEKJGA" "BAFGHCDELMNIJKPO"},
    {16L, 16, 2, "MIHPFKJONBLADCGE" "GDPHNOEKFLBCIAMJ"},
    {32L, 16, 2, "MIBPFDJOGHLEKCNA" "CLEIJGMPKAOHNFDB"},
    {192L, 16, 3,
     "MCHPFAIJNBLDEOGK" "BEFGHJKALMNOCDPI" "GKLBNOEDFPHJIAMC"},
    {64L, 16, 3, "MCHPFAIJNBLDEOGK" "LOGFPKJIBNMEDCHA" "CMAIJHPFDEONBLKG"},
    {192L, 16, 3,
     "IPKCOGMLEDJBNFAH" "BEFGHJKALMNOCDPI" "CMEIJBPFKAOGHLDN"},
    {48L, 16, 3, "IPDJONFLEKCBGMAH" "FJBLMEOCGHPKAIND" "DGIEKLHNJOAMPBCF"},
    {20160L, 16, 4,
     "EHJKAMNBOCDPFGIL" "BAFGHCDELMNIJKPO" "CFAIJBLMDEOGHPKN"
     "DGIAKLBNCOEFPHJM"},
    /* order 17 */
    {16L, 17, 1, "EFGHIJKLMNOPQABCD"},
    /* order 18 */
    {54L, 18, 2, "MKIQOPNAGLRECDBJHF" "BAEFCDJKLGHIOPMNRQ"},
    {6L, 18, 1, "ECJKGHFOPDMNLRIQBA"},
    {12L, 18, 2, "ECJKGHBOPAMNFRDQLI" "KNOPQCFREIGHLJAMBD"},
    {432L, 18, 3,
     "IFNAKLQCDOPBGHREMJ" "NOQCFRIGHKLJAMPBDE" "BAEFCDJKLGHIOPMNRQ"},
    {48L, 18, 2, "ECJKGHBOPAMNFRDQLI" "FDKLHIOPBMNAREQCJG"},
    /* order 19 */
    {18L, 19, 1, "EFGHIJKLMNOPQRSABCD"},
    /* order 20 */
    {40L, 20, 2, "GTDKREHOBILSFMPCJQAN" "EABICDFMGHJQKLNTOPRS"},
    {8L, 20, 1, "EHIJLCMNPGQRSKBTDOFA"},
    {20L, 20, 2, "DJSHQNCLTRGPEBKAIFOM" "EABICDFMGHJQKLNTOPRS"},
    {40L, 20, 2, "GTDKREHOBILSFMPCJQAN" "ECBIAGFMDKJQHONTLSRP"},
    {24L, 20, 2, "IGFMDKJQHONTLSREPCBA" "FDIJGHMNKLQROPTBSAEC"},
    /* order 21 */
    {42L, 21, 2, "ITLSBOUERDHAGKCJNFMQP" "EJHLMKOPNRSQAUTCDBFGI"},
    {12L, 21, 1, "EGHCJKFMNIPQLSTOUBRDA"},
    /* order 22 */
    {110L, 22, 2, "ETGVIBKDMFOHQJSLUNAPCR" "BADCFEHGJILKNMPORQTSVU"},
    {10L, 22, 1, "FEHGJILKNMPORQTSVUBADC"},
    /* order 23 */
    {22L, 23, 1, "EFGHIJKLMNOPQRSTUVWABCD"},
    /* order 24 */
    {24L, 24, 2, "QXEJWPUMKLRIVBFTSACGHNDO" "HRNOPSWCTUVBLDIJXFGAKQME"},
    {8L, 24, 1, "MQBTUDRWFGHXJELINOPKSAVC"},
    {24L, 24, 2, "IOQRBEUVFWGHKLAXMNPSCDTJ" "NJXOVGDKSMTFIPQELCURBWAH"},
    {48L, 24, 2, "QUEJWVXFKLRIPGMNSACBOTDH" "HSNOPWLDTUVBRIAKXFGCQEMJ"},
    {24L, 24, 2, "QXEJWPUMKLRIVBFTSACGHNDO" "TWHNXLRIOPUMSACQVBFDEJGK"},
    {48L, 24, 2, "QUEJWVXFKLRIPGMNSACBOTDH" "BAFGHCDEMNOPIJKLTUVQRSXW"},
    {48L, 24, 3,
     "QXKJWVUMESRIPGFTLDCBONAH" "JUEQRPXFKLWCVBMNSAIGHTDO"
     "HSNOPWLDTUVBRIAKXFGCQEMJ"},
    {24L, 24, 3,
     "QUKJWPXFESRIVBMNLDCGHTAO" "JXEQRVUMKLWCPGFTSAIBONDH"
     "TRONXLWCHVUMSAIJPGFDEQBK"},
    {16L, 24, 2, "MRGTULWIOPFXSDJQBVNEKCHA" "VKXHOQASNTPBCWDEUFGIJLMR"},
    {16L, 24, 2, "MRGTULWIOPFXSDJQBVNEKCHA" "RMLWIGTUSDJQOPFXEKCBVNAH"},
    {48L, 24, 2, "IULQRGXMSDCWOPNTEKJBVFAH" "GLMOPRSDTUBVWIEKFXHJQANC"},
    {24L, 24, 2, "UJPXMRCSNHGTLWIKFVBEDQOA" "NRUFVLWIPXMOJEDQHGTCSABK"},
    {24L, 24, 2, "MIBTUAQRFGHXCDEWNOPJKLVS" "OKXVFWSCGUTNDRQJBPMALIHE"},
    {144L, 24, 3,
     "QXKJWVUMESRIPGFTLDCBONAH" "JUEQRPXFKLWCVBMNSAIGHTDO"
     "BAFGHCDEMNOPIJKLTUVQRSXW"},
    {336L, 24, 3,
     "QTKJWONXESRIHVUMLDCPGFAB" "JNEQRHTUKLWCOPXFSAIVBMDG"
     "HENOPJKLTUVBQRSAXFGWCDMI"},
    /* order 25 */
    {20L, 25, 1, "EHILMNPQRSFTUVBJWXDOYGAKC"},
    {480L, 25, 2, "EHILMNPQRSCTUVBFWXDJYGOKA" "BDEGHIKLMNAPQRSCTUVFWXJYO"},
    /* order 26 */
    {156L, 26, 2,
     "EXGZIBKDMFOHQJSLUNWPYRATCV" "BADCFEHGJILKNMPORQTSVUXWZY"},
    {12L, 26, 1, "FEHGJILKNMPORQTSVUXWZYBADC"},
};

static const struct groups groups[] = {
    {0, NULL},                  /* trivial case: 0 */
    {0, NULL},                  /* trivial case: 1 */
    {1, groupdata + 0},         /* 2 */
    {1, groupdata + 1},         /* 3 */
    {2, groupdata + 2},         /* 4 */
    {1, groupdata + 4},         /* 5 */
    {2, groupdata + 5},         /* 6 */
    {1, groupdata + 7},         /* 7 */
    {5, groupdata + 8},         /* 8 */
    {2, groupdata + 13},        /* 9 */
    {2, groupdata + 15},        /* 10 */
    {1, groupdata + 17},        /* 11 */
    {5, groupdata + 18},        /* 12 */
    {1, groupdata + 23},        /* 13 */
    {2, groupdata + 24},        /* 14 */
    {1, groupdata + 26},        /* 15 */
    {14, groupdata + 27},       /* 16 */
    {1, groupdata + 41},        /* 17 */
    {5, groupdata + 42},        /* 18 */
    {1, groupdata + 47},        /* 19 */
    {5, groupdata + 48},        /* 20 */
    {2, groupdata + 53},        /* 21 */
    {2, groupdata + 55},        /* 22 */
    {1, groupdata + 57},        /* 23 */
    {15, groupdata + 58},       /* 24 */
    {2, groupdata + 73},        /* 25 */
    {2, groupdata + 75},        /* 26 */
};

/* ----- data generated by group.gap ends ----- */

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int w = params->w, a = w*w;
    digit *grid, *soln, *soln2;
    int *indices;
    int i, j, k, qh, qt;
    int diff = params->diff;
    const struct group *group;
    char *desc, *p;

    /*
     * Difficulty exceptions: some combinations of size and
     * difficulty cannot be satisfied, because all puzzles of at
     * most that difficulty are actually even easier.
     *
     * Remember to re-test this whenever a change is made to the
     * solver logic!
     *
     * I tested it using the following shell command:

for d in t n h x u; do
  for id in '' i; do
    for i in {3..9}; do
      echo -n "./group --generate 1 ${i}d${d}${id}: "
      perl -e 'alarm 30; exec @ARGV' \
        ./group --generate 1 ${i}d${d}${id} >/dev/null && echo ok
    done
  done
done

     * Of course, it's better to do that after taking the exceptions
     * _out_, so as to detect exceptions that should be removed as
     * well as those which should be added.
     */
#if 0
    if (w < 5 && diff == DIFF_UNREASONABLE)
	diff--;
    if ((w < 5 || ((w == 6 || w == 8) && params->id)) && diff == DIFF_EXTREME)
	diff--;
    if ((w < 6 || (w == 6 && params->id)) && diff == DIFF_HARD)
	diff--;
    if ((w < 4 || (w == 4 && params->id)) && diff == DIFF_NORMAL)
	diff--;
#endif

    grid = snewn(a, digit);
    soln = snewn(a, digit);
    soln2 = snewn(a, digit);
    indices = snewn(a, int);

    while (1) {
	/*
	 * Construct a valid group table, by picking a group from
	 * the above data table, decompressing it into a full
	 * representation by BFS, and then randomly permuting its
	 * non-identity elements.
	 *
	 * We build the canonical table in 'soln' (and use 'grid' as
	 * our BFS queue), then transfer the table into 'grid'
	 * having shuffled the rows.
	 */
	assert(w >= 2);
	assert(w < lenof(groups));
	group = groups[w].groups + random_upto(rs, groups[w].ngroups);
	assert(group->order == w);
	memset(soln, 0, a);
	for (i = 0; i < w; i++)
	    soln[i] = i+1;
	qh = qt = 0;
	grid[qt++] = 1;
	while (qh < qt) {
	    digit *row, *newrow;

	    i = grid[qh++];
	    row = soln + (i-1)*w;

	    for (j = 0; j < group->ngens; j++) {
		int nri;
		const char *gen = group->gens + j*w;

		/*
		 * Apply each group generator to row, constructing a
		 * new row.
		 */
		nri = gen[row[0]-1] - 'A' + 1;   /* which row is it? */
		newrow = soln + (nri-1)*w;
		if (!newrow[0]) {   /* not done yet */
		    for (k = 0; k < w; k++)
			newrow[k] = gen[row[k]-1] - 'A' + 1;
		    grid[qt++] = nri;
		}
	    }
	}
	/* That's got the canonical table. Now shuffle it. */
	for (i = 0; i < w; i++)
	    soln2[i] = i;
	if (params->id)		       /* do we shuffle in the identity? */
	    shuffle(soln2+1, w-1, sizeof(*soln2), rs);
	else
	    shuffle(soln2, w, sizeof(*soln2), rs);
	for (i = 0; i < w; i++)
	    for (j = 0; j < w; j++)
		grid[(soln2[i])*w+(soln2[j])] = soln2[soln[i*w+j]-1]+1;

	/*
	 * Remove entries one by one while the puzzle is still
	 * soluble at the appropriate difficulty level.
	 */
	memcpy(soln, grid, a);
	if (!params->id) {
	    /*
	     * Start by blanking the entire identity row and column,
	     * and also another row and column so that the player
	     * can't trivially determine which element is the
	     * identity.
	     */

	    j = 1 + random_upto(rs, w-1);  /* pick a second row/col to blank */
	    for (i = 0; i < w; i++) {
		grid[(soln2[0])*w+i] = grid[i*w+(soln2[0])] = 0;
		grid[(soln2[j])*w+i] = grid[i*w+(soln2[j])] = 0;
	    }

	    memcpy(soln2, grid, a);
	    if (solver(params, soln2, diff) > diff)
		continue;	       /* go round again if that didn't work */
	}

	k = 0;
	for (i = (params->id ? 1 : 0); i < w; i++)
	    for (j = (params->id ? 1 : 0); j < w; j++)
		if (grid[i*w+j])
		    indices[k++] = i*w+j;
	shuffle(indices, k, sizeof(*indices), rs);

	for (i = 0; i < k; i++) {
	    memcpy(soln2, grid, a);
	    soln2[indices[i]] = 0;
	    if (solver(params, soln2, diff) <= diff)
		grid[indices[i]] = 0;
	}

	/*
	 * Make sure the puzzle isn't too easy.
	 */
	if (diff > 0) {
	    memcpy(soln2, grid, a);
	    if (solver(params, soln2, diff-1) < diff)
		continue;	       /* go round and try again */
	}

	/*
	 * Done.
	 */
	break;
    }

    /*
     * Encode the puzzle description.
     */
    desc = snewn(a*20, char);
    p = encode_grid(desc, grid, a);
    *p++ = '\0';
    desc = sresize(desc, p - desc, char);

    /*
     * Encode the solution.
     */
    *aux = snewn(a+2, char);
    (*aux)[0] = 'S';
    for (i = 0; i < a; i++)
	(*aux)[i+1] = TOCHAR(soln[i], params->id);
    (*aux)[a+1] = '\0';

    sfree(grid);
    sfree(soln);
    sfree(soln2);
    sfree(indices);

    return desc;
}

/* ----------------------------------------------------------------------
 * Gameplay.
 */

static char *validate_grid_desc(const char **pdesc, int range, int area)
{
    const char *desc = *pdesc;
    int squares = 0;
    while (*desc && *desc != ',') {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            int val = atoi(desc-1);
            if (val < 1 || val > range)
                return "Out-of-range number in game description";
            squares++;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else
            return "Invalid character in game description";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";
    *pdesc = desc;
    return NULL;
}

static char *validate_desc(game_params *params, char *desc)
{
    int w = params->w, a = w*w;
    const char *p = desc;

    return validate_grid_desc(&p, w, a);
}

static char *spec_to_grid(char *desc, digit *grid, int area)
{
    int i = 0;
    while (*desc && *desc != ',') {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
            grid[i++] = atoi(desc-1);
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);
    return desc;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int w = params->w, a = w*w;
    game_state *state = snew(game_state);
    int i;

    state->par = *params;	       /* structure copy */
    state->grid = snewn(a, digit);
    state->immutable = snewn(a, unsigned char);
    state->pencil = snewn(a, int);
    for (i = 0; i < a; i++) {
	state->grid[i] = 0;
	state->immutable[i] = 0;
	state->pencil[i] = 0;
    }

    desc = spec_to_grid(desc, state->grid, a);
    for (i = 0; i < a; i++)
	if (state->grid[i] != 0)
	    state->immutable[i] = TRUE;

    state->completed = state->cheated = FALSE;

    return state;
}

static game_state *dup_game(game_state *state)
{
    int w = state->par.w, a = w*w;
    game_state *ret = snew(game_state);

    ret->par = state->par;	       /* structure copy */

    ret->grid = snewn(a, digit);
    ret->immutable = snewn(a, unsigned char);
    ret->pencil = snewn(a, int);
    memcpy(ret->grid, state->grid, a*sizeof(digit));
    memcpy(ret->immutable, state->immutable, a*sizeof(unsigned char));
    memcpy(ret->pencil, state->pencil, a*sizeof(int));

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->immutable);
    sfree(state->pencil);
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    int w = state->par.w, a = w*w;
    int i, ret;
    digit *soln;
    char *out;

    if (aux)
	return dupstr(aux);

    soln = snewn(a, digit);
    memcpy(soln, state->grid, a*sizeof(digit));

    ret = solver(&state->par, soln, DIFFCOUNT-1);

    if (ret == diff_impossible) {
	*error = "No solution exists for this puzzle";
	out = NULL;
    } else if (ret == diff_ambiguous) {
	*error = "Multiple solutions exist for this puzzle";
	out = NULL;
    } else {
	out = snewn(a+2, char);
	out[0] = 'S';
	for (i = 0; i < a; i++)
	    out[i+1] = TOCHAR(soln[i], state->par.id);
	out[a+1] = '\0';
    }

    sfree(soln);
    return out;
}

static int game_can_format_as_text_now(game_params *params)
{
    return TRUE;
}

static char *game_text_format(game_state *state)
{
    int w = state->par.w;
    int x, y;
    char *ret, *p, ch;

    ret = snewn(2*w*w+1, char);	       /* leave room for terminating NUL */

    p = ret;
    for (y = 0; y < w; y++) {
	for (x = 0; x < w; x++) {
	    digit d = state->grid[y*w+x];

            if (d == 0) {
		ch = '.';
	    } else {
		ch = TOCHAR(d, state->par.id);
	    }

	    *p++ = ch;
	    if (x == w-1) {
		*p++ = '\n';
	    } else {
		*p++ = ' ';
	    }
	}
    }

    assert(p - ret == 2*w*w);
    *p = '\0';
    return ret;
}

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, if hshow = 1.
     */
    int hx, hy;
    /*
     * This indicates whether the current highlight is a
     * pencil-mark one or a real one.
     */
    int hpencil;
    /*
     * This indicates whether or not we're showing the highlight
     * (used to be hx = hy = -1); important so that when we're
     * using the cursor keys it doesn't keep coming back at a
     * fixed position. When hshow = 1, pressing a valid number
     * or letter key or Space will enter that number or letter in the grid.
     */
    int hshow;
    /*
     * This indicates whether we're using the highlight as a cursor;
     * it means that it doesn't vanish on a keypress, and that it is
     * allowed on immutable squares.
     */
    int hcursor;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = 0;
    ui->hpencil = ui->hshow = ui->hcursor = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    int w = newstate->par.w;
    /*
     * We prevent pencil-mode highlighting of a filled square, unless
     * we're using the cursor keys. So if the user has just filled in
     * a square which we had a pencil-mode highlight in (by Undo, or
     * by Redo, or by Solve), then we cancel the highlight.
     */
    if (ui->hshow && ui->hpencil && !ui->hcursor &&
        newstate->grid[ui->hy * w + ui->hx] != 0) {
        ui->hshow = 0;
    }
}

#define PREFERRED_TILESIZE 48
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE / 2)
#define LEGEND (TILESIZE)
#define GRIDEXTRA max((TILESIZE / 32),1)
#define COORD(x) ((x)*TILESIZE + BORDER + LEGEND)
#define FROMCOORD(x) (((x)+(TILESIZE-BORDER-LEGEND)) / TILESIZE - 1)

#define FLASH_TIME 0.4F

#define DF_HIGHLIGHT 0x0400
#define DF_HIGHLIGHT_PENCIL 0x0200
#define DF_IMMUTABLE 0x0100
#define DF_DIGIT_MASK 0x001F

#define EF_DIGIT_SHIFT 5
#define EF_DIGIT_MASK ((1 << EF_DIGIT_SHIFT) - 1)
#define EF_LEFT_SHIFT 0
#define EF_RIGHT_SHIFT (3*EF_DIGIT_SHIFT)
#define EF_LEFT_MASK ((1UL << (3*EF_DIGIT_SHIFT)) - 1UL)
#define EF_RIGHT_MASK (EF_LEFT_MASK << EF_RIGHT_SHIFT)
#define EF_LATIN (1UL << (6*EF_DIGIT_SHIFT))

struct game_drawstate {
    game_params par;
    int w, tilesize;
    int started;
    long *tiles, *pencil, *errors;
    long *errtmp;
};

static int check_errors(game_state *state, long *errors)
{
    int w = state->par.w, a = w*w;
    digit *grid = state->grid;
    int i, j, k, x, y, errs = FALSE;

    if (errors)
	for (i = 0; i < a; i++)
	    errors[i] = 0;

    for (y = 0; y < w; y++) {
	unsigned long mask = 0, errmask = 0;
	for (x = 0; x < w; x++) {
	    unsigned long bit = 1UL << grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = TRUE;
	    errmask &= ~1UL;
	    if (errors) {
		for (x = 0; x < w; x++)
		    if (errmask & (1UL << grid[y*w+x]))
			errors[y*w+x] |= EF_LATIN;
	    }
	}
    }

    for (x = 0; x < w; x++) {
	unsigned long mask = 0, errmask = 0;
	for (y = 0; y < w; y++) {
	    unsigned long bit = 1UL << grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = TRUE;
	    errmask &= ~1UL;
	    if (errors) {
		for (y = 0; y < w; y++)
		    if (errmask & (1UL << grid[y*w+x]))
			errors[y*w+x] |= EF_LATIN;
	    }
	}
    }

    for (i = 1; i < w; i++)
	for (j = 1; j < w; j++)
	    for (k = 1; k < w; k++)
		if (grid[i*w+j] && grid[j*w+k] &&
		    grid[(grid[i*w+j]-1)*w+k] &&
		    grid[i*w+(grid[j*w+k]-1)] &&
		    grid[(grid[i*w+j]-1)*w+k] != grid[i*w+(grid[j*w+k]-1)]) {
		    if (errors) {
			int a = i+1, b = j+1, c = k+1;
			int ab = grid[i*w+j], bc = grid[j*w+k];
			int left = (ab-1)*w+(c-1), right = (a-1)*w+(bc-1);
			/*
			 * If the appropriate error slot is already
			 * used for one of the squares, we don't
			 * fill either of them.
			 */
			if (!(errors[left] & EF_LEFT_MASK) &&
			    !(errors[right] & EF_RIGHT_MASK)) {
			    long err;
			    err = a;
			    err = (err << EF_DIGIT_SHIFT) | b;
			    err = (err << EF_DIGIT_SHIFT) | c;
			    errors[left] |= err << EF_LEFT_SHIFT;
			    errors[right] |= err << EF_RIGHT_SHIFT;
			}
		    }
		    errs = TRUE;
		}

    return errs;
}

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int w = state->par.w;
    int tx, ty;
    char buf[80];

    button &= ~MOD_MASK;

    tx = FROMCOORD(x);
    ty = FROMCOORD(y);

    if (tx >= 0 && tx < w && ty >= 0 && ty < w) {
        if (button == LEFT_BUTTON) {
	    if (tx == ui->hx && ty == ui->hy &&
		ui->hshow && ui->hpencil == 0) {
                ui->hshow = 0;
            } else {
                ui->hx = tx;
                ui->hy = ty;
		ui->hshow = !state->immutable[ty*w+tx];
                ui->hpencil = 0;
            }
            ui->hcursor = 0;
            return "";		       /* UI activity occurred */
        }
        if (button == RIGHT_BUTTON) {
            /*
             * Pencil-mode highlighting for non filled squares.
             */
            if (state->grid[ty*w+tx] == 0) {
                if (tx == ui->hx && ty == ui->hy &&
                    ui->hshow && ui->hpencil) {
                    ui->hshow = 0;
                } else {
                    ui->hpencil = 1;
                    ui->hx = tx;
                    ui->hy = ty;
                    ui->hshow = 1;
                }
            } else {
                ui->hshow = 0;
            }
            ui->hcursor = 0;
            return "";		       /* UI activity occurred */
        }
    }
    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->hx, &ui->hy, w, w, 0);
        ui->hshow = ui->hcursor = 1;
        return "";
    }
    if (ui->hshow &&
        (button == CURSOR_SELECT)) {
        ui->hpencil = 1 - ui->hpencil;
        ui->hcursor = 1;
        return "";
    }

    if (ui->hshow &&
	((ISCHAR(button) && FROMCHAR(button, state->par.id) <= w) ||
	 button == CURSOR_SELECT2 || button == '\b')) {
	int n = FROMCHAR(button, state->par.id);
	if (button == CURSOR_SELECT2 || button == '\b')
	    n = 0;

        /*
         * Can't make pencil marks in a filled square. This can only
         * become highlighted if we're using cursor keys.
         */
        if (ui->hpencil && state->grid[ui->hy*w+ui->hx])
            return NULL;

	/*
	 * Can't do anything to an immutable square.
	 */
        if (state->immutable[ui->hy*w+ui->hx])
            return NULL;

	sprintf(buf, "%c%d,%d,%d",
		(char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

        if (!ui->hcursor) ui->hshow = 0;

	return dupstr(buf);
    }

    if (button == 'M' || button == 'm')
        return dupstr("M");

    return NULL;
}

static game_state *execute_move(game_state *from, char *move)
{
    int w = from->par.w, a = w*w;
    game_state *ret;
    int x, y, i, n;

    if (move[0] == 'S') {
	ret = dup_game(from);
	ret->completed = ret->cheated = TRUE;

	for (i = 0; i < a; i++) {
	    if (!ISCHAR(move[i+1]) || FROMCHAR(move[i+1], from->par.id) > w) {
		free_game(ret);
		return NULL;
	    }
	    ret->grid[i] = FROMCHAR(move[i+1], from->par.id);
	    ret->pencil[i] = 0;
	}

	if (move[a+1] != '\0') {
	    free_game(ret);
	    return NULL;
	}

	return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
	x >= 0 && x < w && y >= 0 && y < w && n >= 0 && n <= w) {
	if (from->immutable[y*w+x])
	    return NULL;

	ret = dup_game(from);
        if (move[0] == 'P' && n > 0) {
            ret->pencil[y*w+x] ^= 1 << n;
        } else {
            ret->grid[y*w+x] = n;
            ret->pencil[y*w+x] = 0;

            if (!ret->completed && !check_errors(ret, NULL))
                ret->completed = TRUE;
        }
	return ret;
    } else if (move[0] == 'M') {
	/*
	 * Fill in absolutely all pencil marks everywhere. (I
	 * wouldn't use this for actual play, but it's a handy
	 * starting point when following through a set of
	 * diagnostics output by the standalone solver.)
	 */
	ret = dup_game(from);
	for (i = 0; i < a; i++) {
	    if (!ret->grid[i])
		ret->pencil[i] = (1 << (w+1)) - (1 << 1);
	}
	return ret;
    } else
	return NULL;		       /* couldn't parse move string */
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define SIZE(w) ((w) * TILESIZE + 2*BORDER + LEGEND)

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = *y = SIZE(params->w);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.78F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.78F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.78F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    int w = state->par.w, a = w*w;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->w = w;
    ds->par = state->par;	       /* structure copy */
    ds->tilesize = 0;
    ds->started = FALSE;
    ds->tiles = snewn(a, long);
    ds->pencil = snewn(a, long);
    ds->errors = snewn(a, long);
    for (i = 0; i < a; i++)
	ds->tiles[i] = ds->pencil[i] = -1;
    ds->errtmp = snewn(a, long);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds->pencil);
    sfree(ds->errors);
    sfree(ds->errtmp);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y, long tile,
		      long pencil, long error)
{
    int w = ds->w /* , a = w*w */;
    int tx, ty, tw, th;
    int cx, cy, cw, ch;
    char str[64];

    tx = BORDER + LEGEND + x * TILESIZE + 1;
    ty = BORDER + LEGEND + y * TILESIZE + 1;

    cx = tx;
    cy = ty;
    cw = tw = TILESIZE-1;
    ch = th = TILESIZE-1;

    clip(dr, cx, cy, cw, ch);

    /* background needs erasing */
    draw_rect(dr, cx, cy, cw, ch,
	      (tile & DF_HIGHLIGHT) ? COL_HIGHLIGHT : COL_BACKGROUND);

    /* pencil-mode highlight */
    if (tile & DF_HIGHLIGHT_PENCIL) {
        int coords[6];
        coords[0] = cx;
        coords[1] = cy;
        coords[2] = cx+cw/2;
        coords[3] = cy;
        coords[4] = cx;
        coords[5] = cy+ch/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    /* new number needs drawing? */
    if (tile & DF_DIGIT_MASK) {
	str[1] = '\0';
	str[0] = TOCHAR(tile & DF_DIGIT_MASK, ds->par.id);
	draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/2,
		  FONT_VARIABLE, TILESIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  (error & EF_LATIN) ? COL_ERROR :
		  (tile & DF_IMMUTABLE) ? COL_GRID : COL_USER, str);

	if (error & EF_LEFT_MASK) {
	    int a = (error >> (EF_LEFT_SHIFT+2*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int b = (error >> (EF_LEFT_SHIFT+1*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int c = (error >> (EF_LEFT_SHIFT                 ))&EF_DIGIT_MASK;
	    char buf[10];
	    sprintf(buf, "(%c%c)%c", TOCHAR(a, ds->par.id),
		    TOCHAR(b, ds->par.id), TOCHAR(c, ds->par.id));
	    draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/6,
		      FONT_VARIABLE, TILESIZE/6, ALIGN_VCENTRE | ALIGN_HCENTRE,
		      COL_ERROR, buf);
	}
	if (error & EF_RIGHT_MASK) {
	    int a = (error >> (EF_RIGHT_SHIFT+2*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int b = (error >> (EF_RIGHT_SHIFT+1*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int c = (error >> (EF_RIGHT_SHIFT                 ))&EF_DIGIT_MASK;
	    char buf[10];
	    sprintf(buf, "%c(%c%c)", TOCHAR(a, ds->par.id),
		    TOCHAR(b, ds->par.id), TOCHAR(c, ds->par.id));
	    draw_text(dr, tx + TILESIZE/2, ty + TILESIZE - TILESIZE/6,
		      FONT_VARIABLE, TILESIZE/6, ALIGN_VCENTRE | ALIGN_HCENTRE,
		      COL_ERROR, buf);
	}
    } else {
        int i, j, npencil;
	int pl, pr, pt, pb;
	float bestsize;
	int pw, ph, minph, pbest, fontsize;

        /* Count the pencil marks required. */
        for (i = 1, npencil = 0; i <= w; i++)
            if (pencil & (1 << i))
		npencil++;
	if (npencil) {

	    minph = 2;

	    /*
	     * Determine the bounding rectangle within which we're going
	     * to put the pencil marks.
	     */
	    /* Start with the whole square */
	    pl = tx + GRIDEXTRA;
	    pr = pl + TILESIZE - GRIDEXTRA;
	    pt = ty + GRIDEXTRA;
	    pb = pt + TILESIZE - GRIDEXTRA;

	    /*
	     * We arrange our pencil marks in a grid layout, with
	     * the number of rows and columns adjusted to allow the
	     * maximum font size.
	     *
	     * So now we work out what the grid size ought to be.
	     */
	    bestsize = 0.0;
	    pbest = 0;
	    /* Minimum */
	    for (pw = 3; pw < max(npencil,4); pw++) {
		float fw, fh, fs;

		ph = (npencil + pw - 1) / pw;
		ph = max(ph, minph);
		fw = (pr - pl) / (float)pw;
		fh = (pb - pt) / (float)ph;
		fs = min(fw, fh);
		if (fs > bestsize) {
		    bestsize = fs;
		    pbest = pw;
		}
	    }
	    assert(pbest > 0);
	    pw = pbest;
	    ph = (npencil + pw - 1) / pw;
	    ph = max(ph, minph);

	    /*
	     * Now we've got our grid dimensions, work out the pixel
	     * size of a grid element, and round it to the nearest
	     * pixel. (We don't want rounding errors to make the
	     * grid look uneven at low pixel sizes.)
	     */
	    fontsize = min((pr - pl) / pw, (pb - pt) / ph);

	    /*
	     * Centre the resulting figure in the square.
	     */
	    pl = tx + (TILESIZE - fontsize * pw) / 2;
	    pt = ty + (TILESIZE - fontsize * ph) / 2;

	    /*
	     * Now actually draw the pencil marks.
	     */
	    for (i = 1, j = 0; i <= w; i++)
		if (pencil & (1 << i)) {
		    int dx = j % pw, dy = j / pw;

		    str[1] = '\0';
		    str[0] = TOCHAR(i, ds->par.id);
		    draw_text(dr, pl + fontsize * (2*dx+1) / 2,
			      pt + fontsize * (2*dy+1) / 2,
			      FONT_VARIABLE, fontsize,
			      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
		    j++;
		}
	}
    }

    unclip(dr);

    draw_update(dr, cx, cy, cw, ch);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int w = state->par.w /*, a = w*w */;
    int x, y;

    if (!ds->started) {
	/*
	 * The initial contents of the window are not guaranteed and
	 * can vary with front ends. To be on the safe side, all
	 * games should start by drawing a big background-colour
	 * rectangle covering the whole window.
	 */
	draw_rect(dr, 0, 0, SIZE(w), SIZE(w), COL_BACKGROUND);

	/*
	 * Big containing rectangle.
	 */
	draw_rect(dr, COORD(0) - GRIDEXTRA, COORD(0) - GRIDEXTRA,
		  w*TILESIZE+1+GRIDEXTRA*2, w*TILESIZE+1+GRIDEXTRA*2,
		  COL_GRID);

	/*
	 * Table legend.
	 */
	for (x = 0; x < w; x++) {
	    char str[2];
	    str[1] = '\0';
	    str[0] = TOCHAR(x+1, ds->par.id);
	    draw_text(dr, COORD(x) + TILESIZE/2, BORDER + TILESIZE/2,
		      FONT_VARIABLE, TILESIZE/2,
		      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_GRID, str);
	    draw_text(dr, BORDER + TILESIZE/2, COORD(x) + TILESIZE/2,
		      FONT_VARIABLE, TILESIZE/2,
		      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_GRID, str);
	}

	draw_update(dr, 0, 0, SIZE(w), SIZE(w));

	ds->started = TRUE;
    }

    check_errors(state, ds->errtmp);

    for (y = 0; y < w; y++) {
	for (x = 0; x < w; x++) {
	    long tile = 0L, pencil = 0L, error;

	    if (state->grid[y*w+x])
		tile = state->grid[y*w+x];
	    else
		pencil = (long)state->pencil[y*w+x];

	    if (state->immutable[y*w+x])
		tile |= DF_IMMUTABLE;

	    if (ui->hshow && ui->hx == x && ui->hy == y)
		tile |= (ui->hpencil ? DF_HIGHLIGHT_PENCIL : DF_HIGHLIGHT);

            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 ||
                 flashtime >= FLASH_TIME*2/3))
                tile |= DF_HIGHLIGHT;  /* completion flash */

	    error = ds->errtmp[y*w+x];

	    if (ds->tiles[y*w+x] != tile ||
		ds->pencil[y*w+x] != pencil ||
		ds->errors[y*w+x] != error) {
		ds->tiles[y*w+x] = tile;
		ds->pencil[y*w+x] = pencil;
		ds->errors[y*w+x] = error;
		draw_tile(dr, ds, x, y, tile, pencil, error);
	    }
	}
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    if (state->completed)
	return FALSE;
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * We use 9mm squares by default, like Solo.
     */
    game_compute_size(params, 900, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int w = state->par.w;
    int ink = print_mono_colour(dr, 0);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, 3 * TILESIZE / 40);
    draw_rect_outline(dr, BORDER + LEGEND, BORDER + LEGEND,
		      w*TILESIZE, w*TILESIZE, ink);

    /*
     * Legend on table.
     */
    for (x = 0; x < w; x++) {
	char str[2];
	str[1] = '\0';
	str[0] = TOCHAR(x+1, state->par.id);
	draw_text(dr, BORDER+LEGEND + x*TILESIZE + TILESIZE/2,
		  BORDER + TILESIZE/2,
		  FONT_VARIABLE, TILESIZE/2,
		  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
	draw_text(dr, BORDER + TILESIZE/2,
		  BORDER+LEGEND + x*TILESIZE + TILESIZE/2,
		  FONT_VARIABLE, TILESIZE/2,
		  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
    }

    /*
     * Main grid.
     */
    for (x = 1; x < w; x++) {
	print_line_width(dr, TILESIZE / 40);
	draw_line(dr, BORDER+LEGEND+x*TILESIZE, BORDER+LEGEND,
		  BORDER+LEGEND+x*TILESIZE, BORDER+LEGEND+w*TILESIZE, ink);
    }
    for (y = 1; y < w; y++) {
	print_line_width(dr, TILESIZE / 40);
	draw_line(dr, BORDER+LEGEND, BORDER+LEGEND+y*TILESIZE,
		  BORDER+LEGEND+w*TILESIZE, BORDER+LEGEND+y*TILESIZE, ink);
    }

    /*
     * Numbers.
     */
    for (y = 0; y < w; y++)
	for (x = 0; x < w; x++)
	    if (state->grid[y*w+x]) {
		char str[2];
		str[1] = '\0';
		str[0] = TOCHAR(state->grid[y*w+x], state->par.id);
		draw_text(dr, BORDER+LEGEND + x*TILESIZE + TILESIZE/2,
			  BORDER+LEGEND + y*TILESIZE + TILESIZE/2,
			  FONT_VARIABLE, TILESIZE/2,
			  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
	    }
}

#ifdef COMBINED
#define thegame group
#endif

const struct game thegame = {
    "Group", NULL, NULL,
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    TRUE, FALSE, game_print_size, game_print,
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    REQUIRE_RBUTTON | REQUIRE_NUMPAD,  /* flags */
};

#ifdef STANDALONE_SOLVER

#include <stdarg.h>

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc, *err;
    digit *grid;
    int grade = FALSE;
    int ret, diff, really_show_working = FALSE;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            really_show_working = TRUE;
        } else if (!strcmp(p, "-g")) {
            grade = TRUE;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-g | -v] <game_id>\n", argv[0]);
        return 1;
    }

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: game id expects a colon in it\n", argv[0]);
        return 1;
    }
    *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    s = new_game(NULL, p, desc);

    grid = snewn(p->w * p->w, digit);

    /*
     * When solving a Normal puzzle, we don't want to bother the
     * user with Hard-level deductions. For this reason, we grade
     * the puzzle internally before doing anything else.
     */
    ret = -1;			       /* placate optimiser */
    solver_show_working = FALSE;
    for (diff = 0; diff < DIFFCOUNT; diff++) {
	memcpy(grid, s->grid, p->w * p->w);
	ret = solver(&s->par, grid, diff);
	if (ret <= diff)
	    break;
    }

    if (diff == DIFFCOUNT) {
	if (grade)
	    printf("Difficulty rating: ambiguous\n");
	else
	    printf("Unable to find a unique solution\n");
    } else {
	if (grade) {
	    if (ret == diff_impossible)
		printf("Difficulty rating: impossible (no solution exists)\n");
	    else
		printf("Difficulty rating: %s\n", group_diffnames[ret]);
	} else {
	    solver_show_working = really_show_working;
	    memcpy(grid, s->grid, p->w * p->w);
	    ret = solver(&s->par, grid, diff);
	    if (ret != diff)
		printf("Puzzle is inconsistent\n");
	    else {
		memcpy(s->grid, grid, p->w * p->w);
		fputs(game_text_format(s), stdout);
	    }
	}
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */