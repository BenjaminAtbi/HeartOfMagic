"""Write tome-reach-test's inputs from an MO2 profile: the active plugins in load
order with the file MO2 would serve for each, the data folders in priority
order (game Data, then mods low to high, then overwrite), and the INI archive
lists the game registers before the plugins' archives.

    python make_inputs.py <MO2 root> <profile> > inputs.json
"""
import json, os, re, sys

root, profile = sys.argv[1], sys.argv[2]
prof = os.path.join(root, 'profiles', profile)
game = os.path.join(root, 'Stock Game')
game_data = os.path.join(game, 'Data')

def lines(path):
    try:
        with open(path, encoding='utf-8-sig', errors='replace') as f:
            return [l.rstrip('\r\n') for l in f]
    except OSError:
        return []

def profile_lines(name):
    return lines(os.path.join(prof, name))

mods = [l[1:] for l in profile_lines('modlist.txt') if l.startswith('+')]
data_dirs = [game_data] + [os.path.join(root, 'mods', m) for m in reversed(mods)] + [os.path.join(root, 'overwrite')]
data_dirs = [d for d in data_dirs if os.path.isdir(d)]

where = {}
for d in data_dirs:
    for name in os.listdir(d):
        if name.lower().endswith(('.esp', '.esm', '.esl')):
            where[name.lower()] = (name, os.path.join(d, name))

active = {l[1:].lower() for l in profile_lines('plugins.txt') if l.startswith('*')}
implicit = {'skyrim.esm', 'update.esm', 'dawnguard.esm', 'hearthfires.esm', 'dragonborn.esm', '_resourcepack.esl'}
plugins = []
for l in profile_lines('loadorder.txt'):
    low = l.lower()
    if not l or l.startswith('#') or low not in where:
        continue
    if low in active or low in implicit or low.startswith('cc'):
        name, path = where[low]
        plugins.append({'name': name, 'path': path})

# [Archive] sResourceArchiveList and sResourceArchiveList2: Skyrim_Default.ini, then Skyrim.ini and
# SkyrimCustom.ini (the profile's own when MO2's LocalSettings is on), a later file's list replacing one
LISTS = ('sresourcearchivelist', 'sresourcearchivelist2')

def archive_lists(path):
    out, section = {}, None
    for line in lines(path):
        s = line.strip()
        if s.startswith('['):
            section = s[1:s.find(']')].strip().lower() if ']' in s else None
        elif section == 'archive' and '=' in s and not s.startswith((';', '#')):
            k, v = s.split('=', 1)
            if k.strip().lower() in LISTS:
                out[k.strip().lower()] = [a.strip() for a in v.split(',') if a.strip()]
    return out

def find(d, name):
    try:
        return next((os.path.join(d, f) for f in os.listdir(d) if f.lower() == name.lower()), os.path.join(d, name))
    except OSError:
        return os.path.join(d, name)

local = any(re.fullmatch(r'\s*localsettings\s*=\s*true\s*', l, re.I) for l in profile_lines('settings.ini'))
ini_dir = prof if local else os.path.join(os.path.expanduser('~'), 'Documents', 'My Games', 'Skyrim Special Edition')
lists = archive_lists(os.path.join(game, 'Skyrim_Default.ini'))
for f in ('Skyrim.ini', 'SkyrimCustom.ini'):
    lists.update(archive_lists(find(ini_dir, f)))
ini_archives = [a for k in LISTS for a in lists.get(k, [])]

json.dump({'plugins': plugins, 'dataDirs': data_dirs, 'iniArchives': ini_archives}, sys.stdout, ensure_ascii=False)
