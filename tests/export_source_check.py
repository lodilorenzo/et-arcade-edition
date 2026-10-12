"""Dependency-free source export regression check: python tests/export_source_check.py."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from export_source import export_source
from package import SOURCE_ITEMS, stage_source


def write(root, name, text='local data'):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return path


def refused(root, destination, message):
    try:
        export_source(root, destination)
    except SystemExit as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError(f'Expected refusal: {destination}')


def main():
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        root = work / 'project'
        for item in SOURCE_ITEMS:
            if (ROOT / item).is_dir():
                (root / item).mkdir(parents=True, exist_ok=True)
            else:
                write(root, item, f'source: {item}')
        code = write(root, 'engine/src/game/current.c', '/* modified working source */')
        write(root, 'engine/src/ui/new.h', '/* untracked working source */')
        write(root, 'engine/etmain/ui/menudef.h', '/* build input */')
        write(root, 'native/ui/art/custom.tga', 'custom art')
        write(root, 'native/ui_art_source/custom.png', 'custom art source')
        excluded = (
            'runtime/etmain/pak0.pk3', 'reference/sdk/license.txt', 'build/et.exe',
            'output/imagegen/local.png', 'site/index.html', 'trailer/render.py',
            'installer.exe', 'engine/.git/config', 'native/.env', 'native/.env.production',
            'native/.git/config', 'native/ETKEY', 'native/profiles/me/notes.txt',
            'native/players/me/notes.txt', 'native/private.pem', 'native/private.key',
            'engine/src/curl-7.12.2/tests/stunnel.pem', 'tools/__pycache__/cached.pyc',
            'tests/.vs/settings.json', 'native/build/generated.c', 'tools/session.log',
            'native/ui/autoexec.cfg', 'native/saved.pk3', 'native/local.dll',
        )
        for name in excluded:
            write(root, name)

        output = export_source(root, work / 'public')
        assert (output / 'engine/src/game/current.c').read_bytes() == code.read_bytes()
        assert (output / 'engine/src/ui/new.h').is_file()
        assert (output / 'engine/etmain/ui/menudef.h').is_file()
        assert (output / 'native/ui/art/custom.tga').is_file()
        assert (output / 'native/ui_art_source/custom.png').is_file()
        assert (output / 'LICENSE.txt').read_bytes() == (root / 'engine/COPYING.txt').read_bytes()
        assert 'git init -b main' in (output / 'PUBLISHING.md').read_text()
        assert all(not (output / name).exists() for name in excluded)
        assert all((root / name).is_file() for name in excluded), 'Source files must stay untouched'
        assert code.read_text() == '/* modified working source */'
        refused(root, output, 'already exists')
        refused(root, root / 'inside', 'outside the project')
        assert not (root / 'inside').exists()

        # The existing release source ZIP uses exactly the same filtering helper.
        stage = work / 'zip-source'
        stage_source(stage, root)
        assert all(not (stage / name).exists() for name in excluded)
        assert (stage / 'engine/src/ui/new.h').is_file()

        (root / 'Cargo.toml').unlink()
        refused(root, work / 'missing-source', 'Missing source item')
        assert not (work / 'missing-source').exists()
        write(root, 'Cargo.toml')
        oversized = root / 'native/too-large.c'
        with oversized.open('wb') as file:
            file.truncate(100 * 2**20)
        refused(root, work / 'oversized', '100 MiB')
        assert not (work / 'oversized').exists()
        oversized.unlink()

        link = root / 'native/linked.c'
        outside = write(work, 'private.c')
        try:
            link.symlink_to(outside)
        except OSError:
            print('SKIP symlink check: symlink creation is not permitted')
        else:
            refused(root, work / 'linked', 'linked source file')
            assert not (work / 'linked').exists()
    subprocess.run([sys.executable, str(ROOT / 'tools/export_source.py'), '--help'], check=True,
                   stdout=subprocess.DEVNULL)
    print('PASS source export: current source, exclusions, notices, no overwrite, safe failure')


if __name__ == '__main__':
    main()
