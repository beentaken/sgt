#!/usr/bin/env python

# magic paragraph wrapping algorithm

import sys
import string

def wrap_none(list):
    sys.stdout.write(string.join(list, " ") + "\n")

def wrap_greedy(list):
    wid = -1
    line = []
    for w in list:
        newwid = wid + 1 + len(w)
        if wid >= 0 and newwid > width:
            sys.stdout.write(string.join(line, " ") + "\n")
            wid = -1
            line = []
        wid = wid + 1 + len(w)
        line.append(w)
    if len(line) > 0:
        sys.stdout.write(string.join(line, " ") + "\n")

def wrap_optimal(list):
    class holder:
        "container class"
        pass
    if verbose:
        print "width =", width
        print "word lengths:"
        lens = []
        for i in list:
            lens.append(len(i))
        print lens
    n = len(list)
    z = []
    if owidth != None:
        opt_width = owidth
    else:
        opt_width = width
    for i in range(n-1, -1, -1):
        if verbose:
            print "wrapping from word", i
        wid = -1
        best = None
        bestcost = None
        if i == 0:
            wmax = width - max(indent, 0)
            wopt = opt_width - max(indent, 0)
        else:
            wmax = width - max(-indent, 0)
            wopt = opt_width - max(-indent, 0)
        z = [holder()] + z
        for j in range(1, n-i+1):
            wid = wid + 1 + len(list[i+j-1])
            if wid > wmax and j > 1:
                # (we permit too-long lines that are only one word,
                # because it means the word itself won't fit)
                break
            if j >= len(z):
                if wid <= wopt:
                    # We allow the last line to be short without
                    # penalty, although we still penalise it for being
                    # longer than opt_width.
                    cost = 0
                    if verbose:
                        print j, "words: end of line, cost", cost
                else:
                    cost = (wopt-wid) ** 2
                    if verbose:
                        print j, "words: end of line but long, cost", cost
            else:
                thiscost = (wopt-wid) ** 2
                cost = thiscost + z[j].cost
                if verbose:
                    print j, "words: cost", thiscost, "+", z[j].cost, "=", cost
            if bestcost == None or bestcost >= cost:
                bestcost = cost
                best = j
        if best == None:
            z[0].cost = 0
            z[0].n = n - i
        else:
            z[0].cost = bestcost
            z[0].n = best
        if verbose:
            print "optimal:", z[0].n, "words, at cost", z[0].cost
    i = 0
    while i < len(list):
        n = z[i].n
        if i == 0:
            thisindent = max(indent, 0)
        else:
            thisindent = max(-indent, 0)
        sys.stdout.write(" " * thisindent +
                         string.join(list[i:i+n], " ") + "\n")
        i = i + n

def dofile(f):
    para = []
    while 1:
        s = f.readline()
        if s == "":
            break
        k = string.split(s)
        if len(k) == 0:
            if len(para) > 0:
                wrapfn(para)
            para = []
            sys.stdout.write(s)
        else:
            if single_lines:
                wrapfn(k)
            else:
                para = para + k
    if len(para) > 0:
        wrapfn(para)

width = 72
owidth = None
indent = 0
single_lines = 0
verbose = 0
wrapfn = wrap_optimal

donefile = 0
for arg in sys.argv[1:]:
    if arg[0] == "-":
        if arg[:2] == "-w":
            width = string.atoi(arg[2:])
        elif arg[:2] == "-o":
            owidth = string.atoi(arg[2:])
        elif arg[:2] == "-i":
            indent = string.atoi(arg[2:])
        elif arg[:2] == "-s":
            single_lines = 1
        elif arg[:2] == "-v":
            verbose = 1
        elif arg[:2] == "-g":
            wrapfn = wrap_greedy
        elif arg[:2] == "-d":
            wrapfn = wrap_none
        else:
            print "wrap: unknown option " + arg[:2]
            print "usage: wrap [-v] [-g] [-d] [-wWIDTH] [-oOPTWIDTH] [file [file...]]"
            sys.exit(0)
    else:
        donefile = 1
        f = open(arg, "r")
        dofile(f)
        f.close()

if not donefile:
    dofile(sys.stdin)
