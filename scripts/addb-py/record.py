"""trace class accepts a stream of incoming records (represented by 
   the record class) and produces the output in the form of an SVG image, 
   describing the stream.

   Some assumptions are made about the input stream:

       - all records are from the same process,

       - the time-stamps of incoming records are monotonically
         increasing. m0addb2dump output does not necessarily conform to this
         restriction. This can always be fixed by doing

             m0addb2dump -f | sort -k2.1,2.29 -s | m0addb2dump -d

       - if a trace does not contain all records produced by a Mero process from
         start-up to shutdown, certain corner cases (like re-use of the same
         memory address for a new fom), can result in an incorrect
         interpretation of a final portion of the trace.

    Output layout.

    The entirety of the output image is divided into vertical stripes,
    corresponding to the localities, divided by 10px-wide vertical lines . The
    number of localities is specified by the "loc_nr" parameter.

    Vertical axis corresponds to time (from top to bottom). Horizontal dashed
    lines mark time-steps with the granularity specified by the "step"
    parameter.

    In the area, corresponding to a locality, the foms, executed in this
    locality are represented. Each fom is represented as a rectangle with fixed
    width and with the height corresponding to the fom life-time. The left
    border of this rectangle is marked by a black line, other borders are
    transparent.

    The interior of a fom rectangle is divided into 3 vertical "lanes". The
    first lane is used for labels. When a fom is created, its type and address
    are written to the label lane. When the fom experiences a phase transition,
    the name of the new phase is written to the label lane.

    The second lane, represents phases, marked by different colours.

    The third lane contains states, marked by different colours.

    The line at the left border of fom area can be missing if the final state
    transition for the fom is missing from the log.

    If fom phase state machine doesn't have transition descriptions, the phase
    lane will be empty and phase labels will be missing.

    By specifying "starttime" and "duration" parameters, view area can be
    narrowed to produce more manageable images. When view area is narrowed, the
    entire trace is still processed and all SVG elements are generated, but they
    fall out of the visible image.

"""

import datetime
import svgwrite

class trace(object):
    def __init__(self, width, height, loc_nr, duration, starttime = None,
                 step = 100, outname = "out.svg"):
        if starttime != None:
            self.start = datetime.datetime.strptime(starttime, timeformat)
        else:
            self.start = None
        self.prep   = False
        self.width  = width
        self.height = height
        self.loc_nr = loc_nr
        self.usec   = duration * 1000000
        self.step   = step
        self.out    = svgwrite.Drawing(outname, profile='full', \
                                       size = (str(width)  + "px",
                                               str(height) + "px"))
        self.margin      = width * 0.01
        self.iowidth     = width * 0.05
        self.loc_width   = (width - 2*self.margin - self.iowidth) / loc_nr
        self.maxfom      = 20 if loc_nr != 1 else 80
        self.loc_margin  = self.loc_width * 0.02
        self.fom_width   = (self.loc_width - 2*self.loc_margin) / self.maxfom
        self.maxlane     = 4
        self.lane_margin = self.fom_width * 0.10
        self.lane_width  = (self.fom_width - 2*self.lane_margin) / self.maxlane
        self.axis        = svgwrite.rgb(0, 0, 0, '%')
        self.locality    = []
        self.inflight    = 0
        self.iomax       = 128
        self.iolane      = self.iowidth / self.iomax
        self.iostart     = width - self.iowidth
        self.lastio      = None
        for i in range(loc_nr):
            x = self.getloc(i)
            self.locality.append(locality(self, i))
            self.out.add(self.out.line((x, 0), (x, height),
                                       stroke = self.axis, stroke_width = 10))
            self.out.add(self.out.text("locality " + str(i),
                                       insert = (x + 10, 20)))

    def done(self):
        self.out.save()

    def fomadd(self, fom):
        self.locality[fom.getloc()].fomadd(fom)

    def fomdel(self, fom):
        self.locality[fom.getloc()].fomdel(fom)

    def getloc(self, idx):
        return self.margin + self.loc_width * idx

    def getlane(self, fom, lane):
        assert 0 <= lane and lane < self.maxlane
        return self.getloc(fom.loc.idx) + self.loc_margin + \
            self.fom_width * fom.loc_idx + self.lane_margin + \
            self.lane_width * lane

    def getpos(self, stamp):
        interval = stamp - self.start
        usec = interval.seconds * 1000000 + interval.microseconds
        return self.height * usec / self.usec

    def fomfind(self, rec):
        return foms[rec.get("fom")]

    def fomcolour(self, fom):
        seed = fom.phase + "^" + fom.phase
        red   = hash(seed + "r") % 100
        green = hash(seed + "g") % 100
        blue  = hash(seed + "b") % 100
        return svgwrite.rgb(red, green, blue, '%')

    def fomrect(self, fom, lane, start, end):
        start  = self.getpos(start)
        height = self.getpos(end) - start
        lane   = self.getlane(fom, lane)
        return { "insert": (lane, start), "size": (self.lane_width, height) }

    def statecolour(self, fom):
        state = fom.state
        if state == "Init":
            return svgwrite.rgb(100, 100, 0, '%')
        elif state == "Ready":
            return svgwrite.rgb(100, 0, 0, '%')
        elif state == "Running":
            return svgwrite.rgb(0, 100, 0, '%')
        elif state == "Waiting":
            return svgwrite.rgb(0, 0, 100, '%')
        else:
            return svgwrite.rgb(10, 10, 10, '%')

    def fomtext(self, fom, text, time):
        self.out.add(self.out.text(text, insert = (self.getlane(fom, 0),
                                                   self.getpos(time))))

    def prepare(self, time):
        if self.start == None:
            self.start = time
        out = self.out
        duration = datetime.timedelta(microseconds = self.usec)
        delta = datetime.timedelta(milliseconds = self.step)
        n = 0
        while n*delta <= duration:
            t = self.start + n * delta
            y = self.getpos(t)
            label = t.strftime(timeformat)
            out.add(out.line((0, y), (self.width, y),
                             stroke = self.axis, stroke_width = 1,
                             stroke_dasharray = "20,10,5,5,5,10"))
            out.add(out.text(label, insert = (0, y - 10)))
            for i in range(self.loc_nr):
                out.add(out.text(label, insert = (self.getloc(i) + 10, y - 10)))
            n = n + 1
        self.prep = True

    def iodraw(self, time):
        x = self.iostart
        y = self.getpos(self.lastio)
        width = self.iolane * max(self.inflight, 0)
        height = self.getpos(time) - y
        out = self.out
        out.add(out.rect(fill = svgwrite.rgb(30, 30, 30, '%'),
                         insert = (x, y), size = (width, height)))
        out.add(out.text(str(max(self.inflight, 0)), insert = (x - 30, y)))
                         

    def ioadd(self, time):
        if self.lastio != None:
            self.iodraw(time)
        self.lastio = time
        self.inflight = self.inflight + 1

    def iodel(self, time):
        assert self.lastio != None
        self.iodraw(time)
        self.lastio = time
        self.inflight = self.inflight - 1


class locality(object):
    def __init__(self, trace, idx):
        self.trace  = trace
        self.foms   = {}
        self.idx    = idx

    def fomadd(self, fom):
        j = len(self.foms)
        for i in range(len(self.foms)):
            if not (i in self.foms):
                j = i
                break
        self.foms[j] = fom
        fom.loc_idx = j
        fom.loc = self
        assert j < self.trace.maxfom

    def fomdel(self, fom):
        assert self.foms[fom.loc_idx] == fom
        del self.foms[fom.loc_idx]
        
def parse(trace, words):
    # 2016-03-24-09:18:46.359427942
    stamp = words[0][:-3] # cut to microsecond precision
    tag   = words[1]
    if tag in tags:
        obj        = tags[tag]()
        obj.ctx    = {}
        obj.time   = datetime.datetime.strptime(stamp, timeformat)
        obj.params = words[2:]
        obj.trace  = trace
        if not trace.prep:
            trace.prepare(obj.time)
    else:
        obj = None
    return obj

class record(object):
    def add(self, words):
        key = words[0]
        val = words[1:]
        assert not (key is self.ctx)
        self.ctx[key] = val
        self.trace = None

    def done(self, trace):
        self.trace = trace

    def get(self, label):
        return self.ctx[label][0]

    def fomexists(self):
        return "fom" in self.ctx

    def __str__(self):
        return str(self.time)

    def getloc(self):
        loc = int(self.get("locality"))
        if self.trace.loc_nr == 1:
            loc = 0
        assert 0 <= loc and loc < self.trace.loc_nr
        return loc

class fstate(record):
    def done(self, trace):
        state = self.params[2]
        super(fstate, self).done(trace)
        if self.fomexists():
            fom = trace.fomfind(self)
            out = trace.out
            out.add(out.rect(fill = trace.statecolour(fom),
                             **trace.fomrect(fom, 3, fom.state_time, self.time)))
            fom.state_time = self.time
            fom.state = state
            if state == "Finished":
                start = trace.getpos(fom.time)
                end   = trace.getpos(self.time)
                lane  = trace.getlane(fom, 0) - 5
                out.add(out.line((lane, start), (lane, end), stroke = trace.axis,
                                 stroke_width = 3))
                trace.fomtext(fom, fom.params[5], fom.time)
                self.trace.fomdel(fom)
                del foms[self.get("fom")]
            

class fphase(record):
    def done(self, trace):
        super(fphase, self).done(trace)
        if (len(self.params) in (2, 3) and self.fomexists()):
            fom = trace.fomfind(self)
            out = trace.out
            out.add(out.rect(fill = trace.fomcolour(fom), \
                **trace.fomrect(fom, 2, fom.phase_time, self.time)))
            trace.fomtext(fom, fom.phase, fom.phase_time)
            fom.phase_time = self.time
            fom.phase = self.params[-1]
            
class fom(record):
    def done(self, trace):
        addr = self.get("fom")
        assert "locality" in self.ctx
        # assert not (addr in foms)
        foms[addr] = self
        super(fom, self).done(trace)
        self.loc_idx = -1
        self.trace.fomadd(self)
        self.state = "Ready"
        self.phase = "init"
        self.state_time = self.time
        self.phase_time = self.time
        trace.fomtext(self, self.params[5] + str(addr), self.time)

    def __str__(self):
        return str(self.time) + " " + self.get("fom")

class forq(record):
    def done(self, trace):
        if not ("locality" in self.ctx):
            return # ast in 0-locality
        loc_id = self.getloc()
        super(forq, self).done(trace)
        nanoseconds = float(self.params[0][:-1]) # Cut final comma.
        duration = datetime.timedelta(microseconds = nanoseconds / 1000)
        x = self.trace.getloc(loc_id) + 10
        y = self.trace.getpos(self.time - duration)
        out = self.trace.out
        out.add(out.line((x, y), (x, self.trace.getpos(self.time)),
                         stroke = svgwrite.rgb(80, 10, 10, '%'),
                         stroke_width = 5))
        out.add(out.text(self.params[1], insert = (x + 10, y)))

class ioend(record):
    def done(self, trace):
        super(ioend, self).done(trace)
        trace.iodel(self.time)
        
class iolaunch(record):
    def done(self, trace):
        super(iolaunch, self).done(trace)
        trace.ioadd(self.time)
        
foms = {}

tags = {
    "fom-descr"         : fom,
    "fom-state"         : fstate,
    "fom-phase"         : fphase,
    "loc-forq-duration" : forq,
    "stob-io-launch"    : iolaunch,
    "stob-io-end"       : ioend
}

timeformat = "%Y-%m-%d-%H:%M:%S.%f"
