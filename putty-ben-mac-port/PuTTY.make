# $Id: PuTTY.make,v 1.1.2.4 1999/02/20 22:10:33 ben Exp $
# This is the Makefile for building PuTTY for the Mac OS.
# Users of non-Mac systems will see some pretty strange characters around.

MAKEFILE     = PuTTY.make
�MondoBuild� = {MAKEFILE}  # Make blank to avoid rebuilds when makefile is modified
Includes     =
Sym�68K      = 
ObjDir�68K   =

COptions     = {Includes} {Sym�68K} 

Objects�68K  = �
		"{ObjDir�68K}mac.c.o" �
		"{ObjDir�68K}misc.c.o" �
#		"{ObjDir�68K}ssh.c.o" �
#		"{ObjDir�68K}sshcrc.c.o" �
#		"{ObjDir�68K}sshdes.c.o" �
#		"{ObjDir�68K}sshmd5.c.o" �
#		"{ObjDir�68K}sshrand.c.o" �
#		"{ObjDir�68K}sshrsa.c.o" �
#		"{ObjDir�68K}sshsha.c.o" �
#		"{ObjDir�68K}telnet.c.o" �
		"{ObjDir�68K}terminal.c.o"


PuTTY �� {�MondoBuild�} {Objects�68K}
	Link �
		-o {Targ} -d {Sym�68K} �
		{Objects�68K} �
		-t 'APPL' �
		-c 'pTTY' �
		#"{Libraries}MathLib.o" �
		#"{CLibraries}Complex.o" �
		"{CLibraries}StdCLib.o" �
		"{Libraries}MacRuntime.o" �
		"{Libraries}IntEnv.o" �
		#"{Libraries}ToolLibs.o" �
		"{Libraries}Interface.o"


PuTTY �� {�MondoBuild�} putty.r macresid.h
	Rez putty.r -o {Targ} {Includes} -append


"{ObjDir�68K}mac.c.o" � {�MondoBuild�} mac.c putty.h macresid.h
	{C} mac.c -o {Targ} {COptions}

"{ObjDir�68K}misc.c.o" � {�MondoBuild�} misc.c putty.h
	{C} misc.c -o {Targ} {COptions}

"{ObjDir�68K}ssh.c.o" � {�MondoBuild�} ssh.c
	{C} ssh.c -o {Targ} {COptions}

"{ObjDir�68K}sshcrc.c.o" � {�MondoBuild�} sshcrc.c
	{C} sshcrc.c -o {Targ} {COptions}

"{ObjDir�68K}sshdes.c.o" � {�MondoBuild�} sshdes.c
	{C} sshdes.c -o {Targ} {COptions}

"{ObjDir�68K}sshmd5.c.o" � {�MondoBuild�} sshmd5.c
	{C} sshmd5.c -o {Targ} {COptions}

"{ObjDir�68K}sshrand.c.o" � {�MondoBuild�} sshrand.c
	{C} sshrand.c -o {Targ} {COptions}

"{ObjDir�68K}sshrsa.c.o" � {�MondoBuild�} sshrsa.c
	{C} sshrsa.c -o {Targ} {COptions}

"{ObjDir�68K}sshsha.c.o" � {�MondoBuild�} sshsha.c
	{C} sshsha.c -o {Targ} {COptions}

"{ObjDir�68K}telnet.c.o" � {�MondoBuild�} telnet.c
	{C} telnet.c -o {Targ} {COptions}

"{ObjDir�68K}terminal.c.o" � {�MondoBuild�} terminal.c putty.h
	{C} terminal.c -o {Targ} {COptions}

