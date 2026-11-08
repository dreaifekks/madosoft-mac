"""Build a self-contained arm64 macOS app for one Artemis game, with no game data.

  python3 macos/package_macos.py --game DIR
  python3 macos/package_macos.py --game DIR --sign "Developer ID Application: You (TEAM)" --install

The app only records where the game is. It runs the original scripts and assets from the game directory
with the openartemis runtime bundled inside. Saves go to ~/Library/Application Support/Madosoft Artemis/<game>/.

What it mounts, lowest priority first:
  1. the base pack (root.pfs or <exe>.pfs) and its numbered parts, plus loose files in the game directory;
  2. the launcher's own numbered patch packs (<launcher>.pfs.NNN, e.g. a Chinese patch);
  3. files an Enigma Virtual Box launcher maps from its external package, extracted once with evbunpack
     into the Application Support folder (never into the game directory).
"""
import argparse
import json
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
from pfs import Pfs  # noqa: E402

SUPPORT = Path.home() / 'Library/Application Support/Madosoft Artemis'
SYSTEM_LIBRARIES = ('/usr/lib/', '/System/Library/')
RT_ICON, RT_GROUP_ICON, RT_VERSION = 3, 14, 16


def run(*command, **options):
    return subprocess.run([str(c) for c in command], check=True, **options)


# ---------------------------------------------------------------- PE resources (name and icon)

class Pe:
    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('not a PE file')
        count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
        magic = struct.unpack_from('<H', data, pe + 24)[0]
        directories = pe + 24 + (112 if magic == 0x20b else 96)
        self.sections = []
        for n in range(count):
            size, rva, raw_size, raw = struct.unpack_from('<IIII', data, pe + 24 + optional + 40 * n + 8)
            self.sections.append((rva, max(size, raw_size), raw))
        rsrc = struct.unpack_from('<I', data, directories + 2 * 8)[0]
        self.root = self.offset(rsrc) if rsrc else None

    def offset(self, rva):
        return next((rva - start + raw for start, size, raw in self.sections if start <= rva < start + size), None)

    def entries(self, directory):
        named, ids = struct.unpack_from('<HH', self.data, directory + 12)
        return [struct.unpack_from('<II', self.data, directory + 16 + 8 * n) for n in range(named + ids)]

    def resources(self, kind):
        found = {}
        for type_id, types in self.entries(self.root) if self.root is not None else []:
            if type_id != kind or not types & 0x80000000:
                continue
            for name, names in self.entries(self.root + (types & 0x7fffffff)):
                languages = self.entries(self.root + (names & 0x7fffffff)) if names & 0x80000000 else []
                if languages:
                    rva, size = struct.unpack_from('<II', self.data, self.root + languages[0][1])
                    start = self.offset(rva)
                    if start is not None:
                        found[name] = self.data[start:start + size]
        return found


def largest_icon(pe):
    """The biggest image of the program's first icon, as a one-image .ico file."""
    groups, images = pe.resources(RT_GROUP_ICON), pe.resources(RT_ICON)
    if not groups:
        return None
    group, best = groups[min(groups)], None
    for n in range(struct.unpack_from('<H', group, 4)[0]):
        width, height, colors, _, planes, bits, _, image = struct.unpack_from('<BBBBHHIH', group, 6 + 14 * n)
        if image in images and (best is None or (width or 256, bits) > best[0]):
            best = ((width or 256, bits), width, height, colors, planes, bits, images[image])
    if best is None:
        return None
    _, width, height, colors, planes, bits, image = best
    return struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', width, height, colors, 0, planes, bits, len(image), 22) + image


def version_name(pe):
    block = next(iter(pe.resources(RT_VERSION).values()), b'')
    for key in ('ProductName', 'FileDescription'):
        needle = key.encode('utf-16le') + b'\0\0'
        at = block.find(needle)
        if at < 0:
            continue
        at += len(needle)
        while block[at:at + 2] == b'\0\0':
            at += 2
        end = at
        while end + 2 <= len(block) and block[end:end + 2] != b'\0\0':
            end += 2
        name = block[at:end].decode('utf-16le', 'replace').strip()
        if name:
            return name
    return None


# ---------------------------------------------------------------- game layout

def find_ci(folder, name):
    return next((p for p in folder.iterdir() if p.name.lower() == name.lower()), None)


def numbered(folder, stem):
    """<stem>.NNN files, in number order."""
    pattern = re.compile(re.escape(stem) + r'\.(\d{3})$', re.IGNORECASE)
    found = [(int(m.group(1)), p) for p in folder.iterdir() if p.is_file() and (m := pattern.match(p.name))]
    return [p for _, p in sorted(found)]


def launcher(game):
    """The exe the game is started with: a *_chs launcher when there is one, else the largest exe."""
    exes = [p for p in game.iterdir() if p.is_file() and p.suffix.lower() == '.exe' and not p.name.startswith('._')]
    chs = [p for p in exes if p.stem.lower().endswith('_chs')]
    if chs:
        return chs[0]
    return max(exes, key=lambda p: p.stat().st_size, default=None)


def base_pack(game, exe):
    stems = ['root']
    if exe:
        stems += [exe.stem, re.sub(r'_chs$', '', exe.stem, flags=re.IGNORECASE)]
    for stem in stems:
        pack = find_ci(game, stem + '.pfs')
        if pack:
            return pack
    raise SystemExit(f'{game}: no root.pfs or <exe>.pfs, so this is not an Artemis game directory')


def evb_package(exe):
    """The external package an Enigma Virtual Box launcher maps in, if any."""
    data = exe.read_bytes()
    # UTF-16 name, ended by NULs or straight by the virtual file system header.
    m = re.search(re.escape('%DEFAULT FOLDER%\\'.encode('utf-16le')) + rb'((?:..)+?)(?=\0\0|EVB\0)', data, re.DOTALL)
    if not m:
        return None
    package = exe.parent / m.group(1).decode('utf-16le')
    if not package.is_file():
        raise SystemExit(f'{exe.name} maps in {package}, which is missing')
    with package.open('rb') as f:
        if f.read(4) != b'EVB\0':
            raise SystemExit(f'{package}: not an Enigma Virtual Box package (no EVB header)')
    return package


def extract_evb(package, target, evbunpack):
    """Extract once; a stamp records which package (size, mtime) the folder came from."""
    stamp = target / '.source'
    source = f'{package}\n{package.stat().st_size}\n{int(package.stat().st_mtime)}\n'
    if stamp.is_file() and stamp.read_text(encoding='utf-8') == source:
        return
    if not shutil.which(evbunpack):
        raise SystemExit(f'{package.name} is an Enigma Virtual Box package; install evbunpack '
                         '(https://github.com/mos9527/evbunpack) or pass --evbunpack PATH')
    shutil.rmtree(target, ignore_errors=True)
    run(evbunpack, '--ignore-pe', package, target, stdout=subprocess.DEVNULL)
    stamp.write_text(source, encoding='utf-8')


def game_title(packs):
    """The title the game puts in its window caption, from the highest-priority language table."""
    for pack in reversed(packs):
        if pack.is_dir():
            continue
        archive = Pfs(str(pack))
        if archive.find('system\\table\\list_windows_ja.tbl') is None:
            continue
        text = archive.get('system\\table\\list_windows_ja.tbl').decode('utf-8', 'replace')
        m = re.search(r'\bgametitle\s*=\s*"([^"]+)"', text)
        if m:
            return re.sub(r'^【[^】]*】', '', m.group(1)).strip()
    return None


# ---------------------------------------------------------------- app

def foreign_libraries(path):
    with tempfile.TemporaryDirectory() as folder:
        link = Path(folder) / 'binary'
        link.symlink_to(Path(path).resolve())
        lines = subprocess.run(['otool', '-L', str(link)], capture_output=True, text=True, check=True).stdout.splitlines()[1:]
    return [line.split()[0] for line in lines if not line.strip().startswith(SYSTEM_LIBRARIES)]


def make_icon(work, source):
    if source.suffix.lower() == '.icns':
        return source
    png = work / 'icon-source.png'
    run('sips', '-s', 'format', 'png', source, '--out', png, stdout=subprocess.DEVNULL)
    width = int(subprocess.run(['sips', '-g', 'pixelWidth', str(png)], capture_output=True, text=True,
                               check=True).stdout.split()[-1])
    iconset = work / 'app.iconset'
    iconset.mkdir()
    for size in [16, 32, 128, 256, 512]:
        for scale, suffix in [(1, ''), (2, '@2x')]:
            if size * scale <= width:
                run('sips', '-z', size * scale, size * scale, png, '--out', iconset / f'icon_{size}x{size}{suffix}.png',
                    stdout=subprocess.DEVNULL)
    run('iconutil', '-c', 'icns', iconset, '-o', work / 'app.icns')
    return work / 'app.icns'


def default_identity():
    """A keychain code-signing identity, so the system keeps the app's disk permission across rebuilds."""
    out = subprocess.run(['security', 'find-identity', '-v', '-p', 'codesigning'], capture_output=True, text=True).stdout
    names = re.findall(r'"([^"]+)"', out)
    return next((n for n in names if n.startswith('Developer ID Application')), names[0] if names else '-')


def info_plist(name, identifier, version):
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>{name}</string>
  <key>CFBundleDisplayName</key><string>{name}</string>
  <key>CFBundleIdentifier</key><string>{identifier}</string>
  <key>CFBundleExecutable</key><string>openartemis</string>
  <key>CFBundleIconFile</key><string>app.icns</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>CFBundleVersion</key><string>{version}</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
'''


def xml_escape(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, required=True, help='game directory the app opens')
    parser.add_argument('--name', help='app name; defaults to the exe version info, else the game\'s window title')
    parser.add_argument('--icon', type=Path, help='icon image or .icns; defaults to the largest icon in the launcher exe')
    parser.add_argument('--sign', default='auto', metavar='IDENTITY',
                        help='codesign identity (security find-identity -v -p codesigning), "-" for ad hoc; '
                             'default: a keychain identity if there is one')
    parser.add_argument('--install', nargs='?', const=Path('/Applications'), type=Path, metavar='DIR',
                        help='also copy the app there, replacing the same name; DIR defaults to /Applications')
    parser.add_argument('--build', type=Path, default=ROOT / 'build/macos-arm64', help='CMake build directory')
    parser.add_argument('--evbunpack', default='evbunpack', help='evbunpack command for Enigma Virtual Box packages')
    parser.add_argument('--version', default='1.0.0')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('package_macos.py needs an Apple Silicon Mac')

    game = args.game.expanduser().resolve()
    if not game.is_dir():
        raise SystemExit(f'{game}: not a directory; is the disk connected?')
    binary = args.build / 'src/app/openartemis'
    if not binary.is_file():
        raise SystemExit(f'{binary}: build it first (cmake --preset macos-arm64 && cmake --build --preset macos-arm64)')

    exe = launcher(game)
    base = base_pack(game, exe)
    slug = re.sub(r'[^a-z0-9]+', '-', base.stem.lower() if base.stem.lower() != 'root' else
                  re.sub(r'_chs$', '', exe.stem, flags=re.IGNORECASE).lower()).strip('-')
    support = SUPPORT / slug
    overlays = numbered(game, exe.name[:-4] + '.pfs') if exe and exe.stem.lower() != base.stem.lower() else []
    package = evb_package(exe) if exe else None
    if package:
        evb = support / 'evb'
        extract_evb(package, evb, args.evbunpack)
        overlays += [p for p in sorted(evb.iterdir()) if re.search(r'\.pfs(\.\d{3})?$', p.name, re.IGNORECASE)]
        overlays.append(evb)

    pe = Pe(exe.read_bytes()) if exe else None
    name = args.name or (pe and version_name(pe)) or game_title([base] + numbered(game, base.name) + overlays) or game.name
    if '/' in name or ':' in name:
        raise SystemExit(f'{name}: an app name cannot hold / or :')

    dist = ROOT / 'build/distribution'
    work = ROOT / 'build/macos-package'
    app = dist / (name + '.app')
    for stale in [app, work]:
        shutil.rmtree(stale, ignore_errors=True)
    work.mkdir(parents=True)
    icon = args.icon
    if icon is None and pe and (ico := largest_icon(pe)):
        icon = work / 'game.ico'
        icon.write_bytes(ico)

    contents = app / 'Contents'
    (contents / 'MacOS').mkdir(parents=True)
    (contents / 'Resources').mkdir()
    shutil.copy2(binary, contents / 'MacOS/openartemis')
    identifier = f'com.dreaife.madosoft-artemis.{slug}'
    (contents / 'Info.plist').write_text(info_plist(xml_escape(name), identifier, args.version), encoding='utf-8')
    if icon:
        shutil.copy2(make_icon(work, icon), contents / 'Resources/app.icns')
    conf = [f'title={name}', f'project={base}'] + [f'overlay={o}' for o in overlays] + \
           [f'save={(support / "save").relative_to(Path.home() / "Library/Application Support")}']
    (contents / 'Resources/game.conf').write_text('\n'.join(conf) + '\n', encoding='utf-8')
    shutil.copy2(ROOT / 'LICENSE', contents / 'Resources/LICENSE')

    if foreign := foreign_libraries(contents / 'MacOS/openartemis'):
        raise SystemExit(f'not self-contained, links: {foreign}')
    identity = default_identity() if args.sign == 'auto' else args.sign
    run('codesign', '--force', '--sign', identity, app)
    run('codesign', '--verify', '--strict', app)

    final = app
    if args.install:
        args.install.mkdir(parents=True, exist_ok=True)
        final = args.install / app.name
        if final.exists():
            shutil.rmtree(final)
        run('ditto', app, final)
    print(json.dumps({'app': str(final), 'name': name, 'game': str(game), 'launcher': exe.name if exe else None,
                      'project': str(base), 'overlays': [str(o) for o in overlays], 'saves': str(support / 'save'),
                      'identifier': identifier, 'signed_by': identity}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
