import fileinput
import record

def recdone(tr, rec):
    if rec != None:
        rec.done(tr)

def processinput():
    tr  = record.trace(40000, 1000000, 4, 80)
    rec = None;
    for line in fileinput.input():
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
    processinput()

        
