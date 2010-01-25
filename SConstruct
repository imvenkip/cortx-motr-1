import platform, os

build = ARGUMENTS.get('build-dir', 'build/%s.%s' % (platform.machine(), platform.node()))
print 'Building at: %s' % build

cwd = os.popen('pwd').read().strip()
db4dir = ARGUMENTS.get('with-db4', '%s/../colibri-db4' % cwd)
build_unix = db4dir + "/build_unix"
dist = db4dir + "/dist"

if int(ARGUMENTS.get('debug', 0)):
        ccflags = '-O0 -g -DENABLE_DEBUG'
else:
        ccflags = '-O2'

env = Environment(
    CPPPATH = ['#include', '#.', '.', db4dir, build_unix],
    CCFLAGS = ccflags,
    LIBPATH=[Dir(build_unix), Dir(cwd + "/" + build + "/src/fol")],
    SRCDIR = cwd,
    BUILDDIR = build,
    DB4DIR = db4dir
)

conf = Configure(env)

# Checking for headers, libraries, etc
#if not env.GetOption('clean'):

env = conf.Finish()

Export('env')

SConscript(['doc/SConscript'], variant_dir='%s/doc' % build, duplicate=0)
SConscript(['man/SConscript'], variant_dir='%s/man' % build, duplicate=0)
SConscript(['src/lib/SConscript'], variant_dir='%s/src/lib' % build, duplicate=0)
SConscript(['src/net/SConscript'], variant_dir='%s/src/net' % build, duplicate=0)
SConscript(['src/addb/SConscript'], variant_dir='%s/src/addb' % build, duplicate=0)
SConscript(['src/ctdb/SConscript'], variant_dir='%s/src/ctdb' % build, duplicate=0)
SConscript(['src/fol/SConscript'], variant_dir='%s/src/fol' % build, duplicate=0)
SConscript(['src/nrs/SConscript'], variant_dir='%s/src/nrs' % build, duplicate=0)
SConscript(['src/sns/SConscript'], variant_dir='%s/src/sns' % build, duplicate=0)
SConscript(['src/mds/SConscript'], variant_dir='%s/src/mds' % build, duplicate=0)
SConscript(['tests/SConscript'], variant_dir='%s/tests' % build, duplicate=0)

