import datetime
import svgwrite

class trace(object):
    def __init__(self, width, height, nr, duration):
        self.start  = None
        self.width  = width
        self.height = height
        self.nr     = nr
        self.usec   = duration * 1000000
        self.out    = svgwrite.Drawing('out.svg', profile='full', \
                                       size = (str(width)  + "px",
                                               str(height) + "px"))
        self.margin      = width * 0.05
        self.loc_width   = (width - 2*self.margin) / nr
        self.maxfom      = 12
        self.loc_margin  = self.loc_width * 0.10
        self.fom_width   = (self.loc_width - 2*self.loc_margin) / self.maxfom
        self.maxlane     = 4
        self.lane_margin = self.fom_width * 0.10
        self.lane_width  = (self.fom_width - 2*self.lane_margin) / self.maxlane
        self.axis        = svgwrite.rgb(0, 0, 0, '%')
        self.locality    = []
        for i in range(nr):
            self.locality.append(locality(self, i))
            self.out.add(self.out.line((self.getloc(i), 0),       \
                                       (self.getloc(i), height),  \
                                       stroke = self.axis, stroke_width = 10))

    def done(self):
        self.out.save()

    def fomadd(self, fom):
        loc = int(fom.get("locality"))
        assert 0 <= loc and loc < self.nr
        self.locality[loc].fomadd(fom)

    def fomdel(self, fom):
        loc = int(fom.get("locality"))
        assert 0 <= loc and loc < self.nr
        self.locality[loc].fomdel(fom)

    def getloc(self, idx):
        return self.margin + self.loc_width * idx

    def getlane(self, fom, lane):
        assert 0 <= lane and lane < self.maxlane
        return self.getloc(fom.loc.idx) + self.loc_margin + \
            self.fom_width * fom.loc_idx + self.lane_margin + \
            self.lane_width * lane

    def getpos(self, stamp):
        interval = stamp - self.start
        assert interval.days == 0
        usec = interval.seconds * 1000000 + interval.microseconds
        return self.height * usec / self.usec

    def fomfind(self, rec):
        return foms[rec.get("fom")]

    def fomcolour(self, fom):
        seed = fom.phase
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
        self.start = time
        out = self.out
        duration = datetime.timedelta(microseconds = self.usec)
        delta = datetime.timedelta(milliseconds = 100)
        n = 0
        while n*delta <= duration:
            t = time + n*delta
            y = self.getpos(t)
            label = t.strftime(timeformat)
            out.add(out.line((0, y), (self.width, y),
                             stroke = self.axis, stroke_width = 1,
                             stroke_dasharray = "20,10,5,5,5,10"))
            out.add(out.text(label, insert = (0, y - 10)))
            for i in range(self.nr):
                out.add(out.text(label, insert = (self.getloc(i) + 10, y - 10)))
            n = n + 1
        

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
        if trace.start == None:
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

class fstate(record):
    def done(self, trace):
        state = self.params[2]
        super(fstate, self).done(trace)
        trace = self.trace
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
        if (len(self.params) == 3 and self.fomexists()):
            trace = self.trace
            fom = trace.fomfind(self)
            out = trace.out
            out.add(out.rect(fill = trace.fomcolour(fom), \
                **trace.fomrect(fom, 2, fom.phase_time, self.time)))
            trace.fomtext(fom, fom.phase, fom.phase_time)
            fom.phase_time = self.time
            fom.phase = self.params[2]
            
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


foms = {}

tags = {
    "fom-descr" : fom,
    "fom-state" : fstate,
    "fom-phase" : fphase
}

timeformat = "%Y-%m-%d-%H:%M:%S.%f"
