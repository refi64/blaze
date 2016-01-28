from fbuild.builders.bison import Bison
from fbuild.builders.c import guess_static
from fbuild.builders import find_program
from fbuild.config.c import posix
from fbuild.record import Record
from fbuild.path import Path
import fbuild.db

class Flex(fbuild.db.PersistentObject):
    def __init__(self, ctx, exe=None, flags=[], *, suffix='.c'):
        self.ctx = ctx
        self.exe = find_program(ctx, [exe or 'flex'])
        self.flags = flags
        self.suffix = suffix

    @fbuild.db.cachemethod
    def __call__(self, src: fbuild.db.SRC, header, dst=None) -> fbuild.db.DSTS:
        dst = Path.addroot(dst or src, self.ctx.buildroot).replaceext(self.suffix)
        dst.parent.makedirs()
        header = Path.addroot(header, self.ctx.buildroot)
        header.parent.makedirs()

        cmd = [self.exe]
        cmd.extend(self.flags)
        cmd.extend(('-o', dst))
        cmd.append('--header='+header)
        cmd.append(src)

        self.ctx.execute(cmd, self.exe, '%s -> %s %s' % (src, dst, header),
            color='yellow')

        return [dst, header]

@fbuild.db.caches
def configure(ctx):
    flex = Flex(ctx)
    bison = Bison(ctx, flags=['-Wno-other', '-v', '--report-file',
                              ctx.buildroot / 'report'])
    c = guess_static(ctx, flags=['-fdiagnostics-color'], debug=True,
        external_libs=['ds'], platform_options=[
            ({'posix'}, {'flags+': ['-Wall', '-Werror']}),
            ({'clang'}, {'flags+': ['-Wno-unneeded-internal-declaration']}),
            ({'gcc'}, {'flags+': ['-Wno-return-type', '-Wno-unused-function']}),
        ])
    pthread = posix.pthread_h(c)
    pthread.header
    return Record(flex=flex, bison=bison, c=c, pthread=pthread)

def build(ctx):
    rec = configure(ctx)
    flex = rec.flex
    bison = rec.bison
    c = rec.c
    pthread = rec.pthread
    lex, hdr = flex('src/lex.l', 'lex.h')
    yacc = bison('src/parse.y', defines=True)
    c.build_exe('tst', ['tst.c', lex, yacc]+Path.glob('src/*.c'),
        includes=['src', hdr.parent])

    lightbuild_opts = {}
    if pthread.header:
        lightbuild_opts['macros'] = ['HAVE_PTHREAD_H=1']
        lightbuild_opts['external_libs'] = pthread.external_libs
    c.build_exe('lightbuild', ['lightbuild/lightbuild.c'], **lightbuild_opts)
