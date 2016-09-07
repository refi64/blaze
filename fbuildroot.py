# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from fbuild.config.c import posix, openssl
from fbuild.builders.pkg_config import PkgConfig
from fbuild.builders.bison import Bison
from fbuild.builders.c import guess_static
from fbuild.builders import find_program
from fbuild.record import Record
from fbuild.path import Path
import fbuild.db

from optparse import make_option

def pre_options(parser):
    group = parser.add_option_group('config options')
    group.add_options((
        make_option('--cc', help='Use the given C compiler'),
        make_option('--cflag', help='Pass the given flag to the C compiler',
                    action='append', default=[]),
        make_option('--release', help='Build a release build',
                    action='store_true'),
        make_option('--threads', help='Build Lightbuild with threads',
                    action='store_true', default=True),
        make_option('--no-threads', help='Build Lightbuild without threads',
                    action='store_false', dest='threads'),
        make_option('--use-color', help='Force the C compiler to give colored' \
                                        ' output', action='store_true'),
        make_option('--debug-lexer', help='Tell Flex to emit debugging info',
                    action='store_true'),
        make_option('--address-sanitizer',
                    help="Enable Clang and/or GCC's address sanitizer",
                    action='store_true'),
        make_option('--no-builtins',
                    help="Have the Blaze compiler not build the builtins",
                    action='store_true'),
    ))

class Flex(fbuild.db.PersistentObject):
    def __init__(self, ctx, exe=None, debug=False, flags=[], *, suffix='.c'):
        self.ctx = ctx
        self.exe = find_program(ctx, [exe or 'flex'])
        self.debug = debug
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
        if self.debug: cmd.append('-d')
        cmd.extend(('-o', dst))
        cmd.append('--header='+header)
        cmd.append(src)

        self.ctx.execute(cmd, self.exe, '%s -> %s %s' % (src, dst, header),
            color='yellow')

        return [dst, header]

@fbuild.db.caches
def configure(ctx):
    flex = Flex(ctx, debug=ctx.options.debug_lexer)
    bison = Bison(ctx, flags=['-Wno-other', '-v', '--report-file',
                              ctx.buildroot / 'report'])
    opts = {'macros': []}
    flags = ctx.options.cflag
    if ctx.options.release:
        opts['optimize'] = True
    else:
        opts['debug'] = True
    if not ctx.options.threads:
        opts['macros'].append('NO_THREADS')
    if ctx.options.no_builtins:
        opts['macros'].append('NO_BUILTINS')
    if ctx.options.use_color:
        flags.append('-fdiagnostics-color')
    if ctx.options.address_sanitizer:
        flags.extend(('-fsanitize=address', '-fno-omit-frame-pointer'))

    c = guess_static(ctx, external_libs=['ds'], exe=ctx.options.cc, flags=flags,
        platform_options=[
            ({'posix'}, {'flags+': ['-Wall', '-Werror']}),
            ({'clang'}, {'flags+': ['-Wno-unneeded-internal-declaration']}),
            ({'gcc'}, {'flags+': ['-Wno-return-type', '-Wno-unused-function']}),
        ], **opts)
    pthread = posix.pthread_h(c)
    pthread.header # Force the check.

    if not (openssl.ssl_h(c).header and openssl.sha_h(c).header):
        raise fbuild.ConfigFailed('OpenSSL is required.')

    openssl_pkg = PkgConfig(ctx, 'openssl')
    ldlibs = openssl_pkg.libs().split(' ')

    ctx.logger.check('checking for pkg-config package lua5.2')
    try:
        lua_pkg = PkgConfig(ctx, 'lua5.2')
    except fbuild.ConfigFailed:
        ctx.logger.failed()
        cflags = []
    else:
        ctx.logger.passed()
        cflags = lua_pkg.cflags()
        ldlibs.extend(lua_pkg.libs().split(' '))

    return Record(flex=flex, bison=bison, c=c, pthread=pthread, cflags=cflags,
                  ldlibs=ldlibs)

def build(ctx):
    rec = configure(ctx)
    flex = rec.flex
    bison = rec.bison
    c = rec.c
    pthread = rec.pthread
    cflags = rec.cflags
    ldlibs = rec.ldlibs

    lex, hdr = flex('src/lex.l', 'lex.h')
    yacc = bison('src/parse.y', defines=True)
    c.build_exe('tst', ['tst.c', lex, yacc]+Path.glob('src/*.c'),
        cflags=cflags, includes=['src', hdr.parent], ldlibs=ldlibs)

    lightbuild_opts = {}
    if pthread.header:
        lightbuild_opts['macros'] = ['HAVE_PTHREAD_H=1']
        lightbuild_opts['external_libs'] = pthread.external_libs
    c.build_exe('lightbuild', ['lightbuild/lightbuild.c'], ldlibs=ldlibs,
                **lightbuild_opts)
