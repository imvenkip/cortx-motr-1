import platform, os

build = '%s.%s' % (platform.machine(), platform.node())
print 'Building at: %s' % build

cwd = os.popen('pwd').read().strip()
db4dir = ARGUMENTS.get('with-db4', '%s/../colibri-db4' % cwd)
build_unix = db4dir + "/build_unix"

env = Environment(CPPPATH = [db4dir, build_unix, '#include'],
            	  LIBPATH = Dir(build_unix), LIBS = ['db', 'pthread'])

if int(ARGUMENTS.get('debug', 0)):
        env.Append(CCFLAGS = ' -O0 -g')
else:
        env.Append(CCFLAGS = ' -O2')

env.Append(SRCDIR = cwd)
env.Append(BUILDDIR = build)
env.Append(DB4DIR = db4dir)
Export('env')

SConscript(['doc/SConscript'], variant_dir='%s/doc' % build, duplicate=0)
SConscript(['man/SConscript'], variant_dir='%s/man' % build, duplicate=0)
SConscript(['src/SConscript'], variant_dir='%s/src' % build, duplicate=0)
SConscript(['src/lib/SConscript'], variant_dir='%s/src/lib' % build, duplicate=0)
SConscript(['src/addb/SConscript'], variant_dir='%s/src/addb' % build, duplicate=0)
SConscript(['src/ctdb/SConscript'], variant_dir='%s/src/ctdb' % build, duplicate=0)
SConscript(['src/fol/SConscript'], variant_dir='%s/src/fol' % build, duplicate=0)
SConscript(['src/nrs/SConscript'], variant_dir='%s/src/nrs' % build, duplicate=0)
SConscript(['src/sns/SConscript'], variant_dir='%s/src/sns' % build, duplicate=0)
SConscript(['tests/SConscript'], variant_dir='%s/tests' % build, duplicate=0)

