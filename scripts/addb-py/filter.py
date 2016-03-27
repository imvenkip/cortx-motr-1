#!/usr/bin/env python

import fileinput
import record
import getopt
import sys

def filter(argv):
    tr = record.trace(height = 10, width = 10, loc_nr = 1, duration = 1,
                      step = 1)
    for line in fileinput.input([]):
        params = line[1:].split()
        if line[0] == "*":
            keep = record.keep(line.split()[2])
        if keep:
            sys.stdout.write(line)

if __name__ == "__main__":
    filter(sys.argv)

        
