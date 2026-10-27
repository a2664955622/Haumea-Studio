# SPDX-License-Identifier: GPL-2.0-or-later
"""Small original FX definitions for the synthetic game; no GTA assets."""
def channel(label, keys):
    return f'{label}:\nFX_INTERP_DATA:\nLOOPED: 0\nNUM_KEYS: {len(keys)}\n' + ''.join(
        f'FX_KEYFLOAT_DATA:\nTIME: {time}\nVAL: {value}\n' for time, value in keys)


def info(name, channels, particle=False):
    return f'FX_INFO_{name}_DATA:\n' + ('TIMEMODEPRT: 1\n' if particle else '') + ''.join(
        channel(label, keys) for label, keys in channels)


def project():
    text = 'FX_PROJECT_DATA:\n'
    specs = [('fire', 'preview_fire', 1, 0, (255, 120, 40)),
             ('cigarette_smoke', 'preview_smoke', .4, .6, (160, 160, 160)),
             ('water_fountain', 'preview_water', 8, -9.8, (90, 150, 255)),
             ('prt_spark', 'preview_fire', 3, -5, (255, 220, 80)),
             ('camflash', 'preview_fire', 0, 0, (255, 255, 255))]
    for name, texture, speed, gravity, colour in specs:
        text += f'''FX_SYSTEM_DATA:
109
FILENAME: fixture/{name}.fxs
NAME: {name}
LENGTH: {.2 if name == 'camflash' else 2}
LOOPINTERVALMIN: 0
LENGTH: 0
PLAYMODE: {0 if name == 'camflash' else 2}
CULLDIST: 100
NUM_PRIMS: 1
FX_PRIM_EMITTER_DATA:
FX_PRIM_BASE_DATA:
NAME: fixture
MATRIX: 1 0 0 0 1 0 0 0 1 0 0 0
TEXTURE: {texture}
TEXTURE2: NULL
TEXTURE3: NULL
TEXTURE4: NULL
ALPHAON: 1
SRCBLENDID: 4
DSTBLENDID: 5
NUM_INFOS: 7
'''
        text += info('EMRATE', [('RATE', [(0, 60)])])
        text += info('EMSPEED', [('SPEED', [(0, speed)]), ('BIAS', [(0, 0)])])
        text += info('EMLIFE', [('LIFE', [(0, .5 if name == 'camflash' else 1.5)]), ('BIAS', [(0, 0)])])
        text += info('EMDIR', [(axis, [(0, value)]) for axis, value in zip(('DIRX', 'DIRY', 'DIRZ'), (0, 0, 1))])
        text += info('FORCE', [(axis, [(0, value)]) for axis, value in zip(('FORCEX', 'FORCEY', 'FORCEZ'), (0, 0, gravity))], True)
        text += info('SIZE', [('SIZEX', [(0, .25), (1, .8)]), ('SIZEY', [(0, .25), (1, .8)])], True)
        text += info('COLOUR', [(axis, [(0, value), (1, value)]) for axis, value in zip(('RED', 'GREEN', 'BLUE'), colour)] + [('ALPHA', [(0, 200), (1, 0)])], True)
        text += 'OMITTEXTURES: 0\nTXDNAME: NOTXDSET\n'
    return text
