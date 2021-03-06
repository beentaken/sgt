#!/bin/sh

#
# Make Contact
#
echo
MACHINE=`telnet mono.org 99 2>/dev/null`
set -- $MACHINE

if [ $# != 9 ]
then
  shift; shift
  if [ a$one != a-n ]
  then
    MACHINE=$8
  else
    MACHINE=$9
  fi
else
  echo Could not get a machine
  MACHINE=NOLOGIN
fi

#
# Check for login barring ....
#

if [ $MACHINE != NOLOGIN ]
then
  rsh $MACHINE -l mono 2>/dev/null | tee logfile
else
  echo Sorry - Logins are disabled at the moment. This is probably due to a
  echo system upgrade. Please try again later.
  echo 
fi
