#!/bin/sh 

# Convenient means of invoking Python with several handy modules
# preloaded.
python -i -c 'powtmp=pow
import mathlib
from mathlib import *
import rational
from rational import *
import math
from math import *
pow=powtmp' "$@"
