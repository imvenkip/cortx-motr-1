from distutils.core import setup, Extension

mero = Extension('mero',
                 define_macros=[('M0_INTERNAL', ''), ('M0_EXTERN', 'extern')],
                 include_dirs=['../../', '../../extra-libs/galois/include/'],
                 sources=['mero.c'])


setup(name='mero', version='1.0',
      description='Module helps get size of a structure required by m0spiel',
      ext_modules=[mero])
