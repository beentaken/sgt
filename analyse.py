#!/usr/bin/env python

import string
import sys
import math

# Read in the score file.
players = {}
games = []
epsilon = 2.0 ** -32
line = 0
while 1:
    s = sys.stdin.readline()
    if s == "": break
    sa = string.split(s)
    if len(sa) % 2:
	sys.stderr.write("um, odd number of fields at line %d?\n" % line)
	continue
    game = {}
    for i in range(len(sa)/2):
	name = sa[2*i]
	score = string.atof(sa[2*i+1])
	if score != int(score):
	    score = int(score) + epsilon   # normalise tie-breaking wins
	game[name] = score
	players[name] = 1 # track overall set of players just in case
    games.append(game)
players = players.keys()
players.sort()

# Utility function: the integral of the standard normal distribution.
#
# The standard normal distribution itself, ignoring the
# 1/sqrt(2*pi) scale factor, is
#   exp(-x*x/2) = 1 - x^2/2 + x^4/(2^2*2!) - x^6/(2^3*3!) + x^8/(2^4*4!) - ...
#
# and so the power series for its integral must be
#      x - x^3/(3*2) + x^5/(5*2^2*2!) - x^7/(7*2^3*3!) + x^9/(9*2^4*4!) - ...
def cnorm(x):
    x = float(x)
    num = x / math.sqrt(2*math.pi)
    denom = 1.0
    sum = 0.0
    n = 0
    while 1:
	term = num / (denom * (2*n+1))
	newsum = sum + term
	if newsum == sum: return sum
	sum = newsum
	num = num * (-x*x)
	n = n + 1
	denom = denom * 2 * n

# Define our preferred significance level.
siglevel = 0.05    # 5% level

# Normalisation routines, to take account (in various ways) of the
# fact that some games are more generally high-scoring than others.

def normalise_game(g):
    # Normalise the scores in each game to have mean 0 and variance
    # 1, thus giving an indication of how well each player did
    # relative to the rest of the pack.
    sx = sxx = n = 0
    for x in g.values():
	n = n + 1
	sx = sx + x
	sxx = sxx + x*x
    mean = sx / n
    var = (n * sxx - sx * sx) / (n*n)
    sd = math.sqrt(var)

    ng = {}
    for name, score in g.items():
	ng[name] = (score - mean) / sd

    return ng

def normalise_rank_01(g):
    # Discard the quantitative scores completely in favour of
    # ranking the players from 1 (winner) to 0 (loser) in
    # equal-sized steps.

    gl = g.items()
    gl.sort(lambda a, b: a[1] > b[1] or (a[1] != b[1] and -1))
    ng = {}
    i = 0
    for n, v in gl:
	ng[n] = float(i) / (len(gl)-1)
	i = i + 1

    return ng

def normalise_rank_top(g):
    # Discard the quantitative scores completely in favour of
    # ranking the players from 1 (winner) to n (loser).

    gl = g.items()
    gl.sort(lambda b, a: a[1] > b[1] or (a[1] != b[1] and -1))
    ng = {}
    i = 1
    for n, v in gl:
	ng[n] = float(i)
	i = i + 1

    return ng

# Analyse a set of scores.

def analyse(title, games, sortsign):

    # Go through the games and count (n, sigma x, sigma x^2) for
    # the scores of each player. Also count the same data for each
    # player in games containing each other player (so that later
    # we can determine whether, for example, Simon's presence in
    # the game has a statistically significant effect on Gareth's
    # score).
    player = {}   # maps P -> (n, sx, sxx) for each player P
    players = {}  # maps (P1,P2) -> (n, sx, sxx) for player P1 given player P2
    for g in games:
	for name, score in g.items():
	    t = player.get(name, (0, 0.0, 0.0))
	    player[name] = (t[0]+1, t[1]+score, t[2]+score*score)
	    for name2 in g.keys():
		if name2 != name:
		    t = players.get((name, name2), (0, 0.0, 0.0))
		    players[(name, name2)] = (t[0]+1, t[1]+score, t[2]+score*score)

    output = []
    for name, t in player.items():
	output.append((name, t[1] / t[0] * sortsign, t))
    output.sort(lambda b, a: a[1] > b[1] or (a[1] != b[1] and -1))
    print "Analysis of", title+":"
    for name, mean, t in output:
	print "%-3s played %3d average % f sd % f" % \
	(name, t[0], t[1] / t[0], \
	math.sqrt((t[0] * t[2] - t[1] * t[1]) / (t[0]*t[0])))

    # Now analyse each player's performance in the presence of
    # each other player, to see whether any player
    # significantly affects another's performance.
    for fred, bill in players.keys():
	# We seek to determine whether Bill's presence in a game
	# has a significant effect on Fred's performance.
	f = player[fred]
	fb = players[(fred, bill)]

	# Retrieve the global mean and variance of Fred's scores.
	fmean = f[1] / f[0]
	fvar = (f[0] * f[2] - f[1] * f[1]) / (f[0]*f[0])

	# Assuming Fred's scores _in general_ are independent
	# samples from some sort of vaguely Normal-ish distribution
	# with the above mean and variance, we expect the sum of
	# Fred's scores when Bill was involved to be a sample from
	# a considerably more Normal distribution with n times the
	# mean and n times the variance - implying that the _mean_
	# of Fred's scores involving Bill can be modelled as a
	# Normal distribution with the same mean and 1/n the
	# variance. n is fb[0], so...
	fbmean = fmean
	fbvar = fvar / fb[0]

	# Now we can take the square root of fbvar to convert it
	# into a standard deviation, and that allows us to
	# normalise the _real_ mean fb[1]/fb[0] and hence treat it
	# like a sample from a _standard_ Normal distribution (with
	# mean 0 and variance 1).
	fbsd = math.sqrt(fbvar)
	if fbsd != 0:
	    fbnorm = (fb[1]/fb[0] - fbmean) / fbsd
	else:
	    fbnorm = 0
	# Determine the probability of getting this far out by
	# chance. This is a two-tail test, so we look for the
	# probability of getting this far out in _either_
	# direction.
	prob = 1 - 2*cnorm(abs(fbnorm))

	if prob < siglevel:
	    print "%-3s given %-4s average % f (%3d games, prob %f)" % \
	    (fred, bill, fb[1]/fb[0], fb[0], prob)

analyse("absolute scores", games, +1)
analyse("normalised scores", map(normalise_game, games), +1)
analyse("ranks", map(normalise_rank_top, games), -1)
analyse("ranks normalised to [0,1]", map(normalise_rank_01, games), +1)
