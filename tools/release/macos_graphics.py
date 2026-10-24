# SPDX-License-Identifier: GPL-2.0-or-later
"""Probe the native CGL capability GLFW's NSGL backend requires, without a window.

GLFW 3.4 requires an accelerated renderer (src/nsgl_context.m). Hosted macOS
VMs may have no matching renderer even when compilation/packaging work.
"""
import ctypes as C
import re


class GraphicsUnavailable(RuntimeError):
    pass


def probe(library=None):
    gl = library or C.CDLL('/System/Library/Frameworks/OpenGL.framework/OpenGL')
    signatures = {
        'CGLChoosePixelFormat': ([C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.POINTER(C.c_int)], C.c_int),
        'CGLCreateContext': ([C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p)], C.c_int),
        'CGLGetCurrentContext': ([], C.c_void_p),
        'CGLSetCurrentContext': ([C.c_void_p], C.c_int),
        'CGLDestroyContext': ([C.c_void_p], C.c_int),
        'CGLDestroyPixelFormat': ([C.c_void_p], C.c_int),
        'glGetString': ([C.c_uint], C.c_char_p),
    }
    for name, (arguments, result) in signatures.items():
        fn = getattr(gl, name)
        fn.argtypes, fn.restype = arguments, result
    # OpenGL/CGLTypes.h; these match GLFW's NSGL default framebuffer and
    # modern core profile. No multisampling, so AA cannot cause a false skip.
    attrs = (C.c_int * 16)(73, 74, 99, 0x3200, 8, 24, 11, 8, 12, 24, 13, 8, 5, 55, 0, 0)
    pixel, count, context = C.c_void_p(), C.c_int(), C.c_void_p()
    previous = gl.CGLGetCurrentContext()
    current = False
    try:
        code = gl.CGLChoosePixelFormat(attrs, C.byref(pixel), C.byref(count))
        # Bad attributes, allocation failures, etc. are probe/host errors,
        # not evidence that the graphics check may be skipped.
        if code not in (0, 10002, 10009):
            raise RuntimeError(f'CGLChoosePixelFormat failed: {code}')
        if code or not pixel.value or count.value == 0:
            raise GraphicsUnavailable(f'No accelerated CGL core pixel format (error {code}, formats {count.value})')
        code = gl.CGLCreateContext(pixel, None, C.byref(context))
        if code in (10002, 10009):
            raise GraphicsUnavailable(f'No accelerated CGL core context (error {code})')
        if code or not context.value:
            raise RuntimeError(f'CGLCreateContext failed: {code}')
        code = gl.CGLSetCurrentContext(context)
        if code:
            raise RuntimeError(f'CGLSetCurrentContext failed: {code}')
        current = True
        version = (gl.glGetString(0x1F02) or b'').decode('utf-8', 'replace')
        renderer = (gl.glGetString(0x1F01) or b'').decode('utf-8', 'replace')
        match = re.match(r'(\d+)\.(\d+)', version)
        if not match:
            raise RuntimeError(f'CGL returned no readable OpenGL version: {version!r}')
        if tuple(map(int, match.groups())) < (3, 3):
            raise GraphicsUnavailable(f'OpenGL 3.3 required; got {version} ({renderer})')
        return {'available': True, 'version': version, 'renderer': renderer}
    finally:
        if current:
            gl.CGLSetCurrentContext(previous)
        if context.value:
            gl.CGLDestroyContext(context)
        if pixel.value:
            gl.CGLDestroyPixelFormat(pixel)
