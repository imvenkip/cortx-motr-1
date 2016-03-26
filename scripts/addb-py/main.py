#!/usr/bin/env python

import fileinput
import record
import getopt
import sys

def recdone(tr, rec):
    if rec != None:
        rec.done(tr)

def processinput(argv):
    try:
        opts, args = getopt.getopt(argv[1:], "h:w:l:d:s:t:o:f:")
    except getopt.GetoptError:
        print ("{} -h height -w width -l localities -f maxfom" \
               " -d duration -s step -t starttime -o outfile\n\n" \
               "Height and width are in pixels.\n" \
               "Duration is in seconds, step is in milliseconds.\n" \
               "Starttime is in the same format as produced by m0addb2dump.\n").\
        format(argv[0])
        sys.exit(2)
    kw = {
        "height"    : 1000000,
        "width"     :   40000,
        "loc_nr"    :       4,
        "duration"  :     100,
        "step"      :     100,
        "starttime" :    None
    }
    xlate = {
        "-h" : ("height",    int),
        "-w" : ("width",     int),
        "-d" : ("duration",  int),
        "-l" : ("loc_nr",    int),
        "-s" : ("step",      int),
        "-f" : ("maxfom",    int),
        "-t" : ("starttime", str),
        "-o" : ("outname",   str)
    }
    for opt, arg in opts:
        xl = xlate[opt]
        kw[xl[0]] = xl[1](arg)
   
    tr = record.trace(**kw)
    rec = None;
    for line in fileinput.input([]):
        params = line[1:].split()
        if line[0] == "*":
            recdone(tr, rec)
            rec = record.parse(tr, params)
        elif line[0] == "|":
            if rec != None:
                rec.add(params)
        else:
            assert False
    recdone(tr, rec)
    tr.done()

if __name__ == "__main__":
    processinput(sys.argv)

        
