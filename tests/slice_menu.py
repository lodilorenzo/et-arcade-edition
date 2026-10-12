"""Isolated native Oasis menus: wrong profile, video recovery/rollback, two sessions.
Run with game closed: python tests/slice_menu.py. Requires the build/slice build.
"""
from pathlib import Path
import re
import shutil
import subprocess
from PIL import Image
from smoke_server import ROOT, PLAY_VIDEO_SETTINGS


def main():
    modules=ROOT/'build/slice/Release'
    home=modules/'smoke/slice-menu-check'
    mod=home/'et-rust';profile=mod/'profiles/MenuFixture'
    profile.mkdir(parents=True,exist_ok=True)
    for name in ('qagame','cgame','ui'):
        shutil.copy2(modules/f'{name}_mp_x86.dll',ROOT/'runtime/et-rust')
    shutil.copytree(ROOT/'native/ui',ROOT/'runtime/et-rust/ui',dirs_exist_ok=True)
    (mod/'profiles/defaultprofile.dat').write_text('"MenuFixture"')
    (profile/'profile.dat').write_text('"Menu Fixture"')
    (profile/'etconfig.cfg').write_text('seta r_mode "4"\nseta r_fullscreen "0"\nseta r_customwidth "1600"\nseta r_customheight "1024"\n')
    settings={**PLAY_VIDEO_SETTINGS,'ui_matchPlayers':'0','ui_rustSlice':'1','bot_enable':'0',
              'in_mouse':'0','in_joystick':'0','in_gamepadIndex':'3','in_gamepadIndex2':'2',
              'g_gametype':'2','g_doWarmup':'0','g_warmup':'0','g_timelimit':'0',
              'sv_maxclients':'8','sv_pure':'0','net_ip':'127.0.0.1','net_port':'27978',
              'sv_master1':'','sv_master2':'','sv_master3':'','sv_master4':'',
              'com_hunkMegs':'256','logfile':'2','developer':'1','ui_rustMenuTest':'1','com_maxfps':'60','r_swapInterval':'0',
              'ui_rustMonitorWidth':'1920','ui_rustMonitorHeight':'1080','ui_rustMonitorRefresh':'0'}
    # Let the deliberately wrong profile own initial geometry; test the menu recovery.
    for key in ('r_mode','r_customwidth','r_customheight','r_customaspect','r_fullscreen','r_displayRefresh'):
        settings.pop(key,None)
    (mod/'autoexec.cfg').write_text('\n'.join(f'set {k} "{v}"' for k,v in settings.items())+'\n')
    script='''wait 100
screenshot main
ui_test options_system
wait 20
screenshot wrong-profile
ui_test options_gamepad
set cl_gamepadYawSpeed 150
set cl_gamepadLookDeadzone 0.25
wait 20
screenshot gamepad
ui_test options_system
ui_test script sliceMonitor
set ui_r_fullscreen 0
ui_test script sliceApply
wait 100
screenshot recovered
ui_test script vidConfirm
ui_test script sliceBack
ui_test script sliceStart
wait 1000
set developer 1
wait 5
loadout_test state
oasis_test player 0
screenshot class-engineer
loadout_test key down
loadout_test key right
loadout_test state
loadout_test key up
loadout_test key left
loadout_test key a
wait 5
screenshot class-medic
loadout_test key left
loadout_test key a
wait 5
screenshot class-soldier
loadout_test key right
loadout_test key right
loadout_test key right
loadout_test key a
wait 5
screenshot class-fieldops
loadout_test key right
loadout_test key a
wait 5
screenshot class-covertops
loadout_test key left
loadout_test key left
loadout_test key a
loadout_test key down
loadout_test key down
loadout_test key a
wait 30
local_class 1
loadout_test state
loadout_test click 460 282
wait 5
oasis_test player 0
loadout_test key b
wait 5
oasis_test player 0
ui_test pad start
ui_test pad state
wait 20
screenshot pause
ui_test options_system
ui_test script sliceBack
ui_test script Leave
wait 100
screenshot returned
cvarlist sv_running
ui_test options_system
set ui_r_customheight 0
ui_test script sliceApply
wait 20
screenshot invalid
ui_test script systemCvarsReset
ui_test script vidConfirm
ui_test script sliceBack
ui_test script sliceStart
wait 1000
loadout_test state
loadout_test key down
loadout_test key down
loadout_test key a
wait 10
ui_test ingame_main
ui_test script Leave
wait 100
ui_test options_system
set ui_r_customwidth 1280
set ui_r_customheight 720
ui_test script sliceApply
wait 100
screenshot pending-revert
ui_test script vidReset
vid_restart
wait 100
screenshot reverted
writeconfig menu-result.cfg
quit
'''
    (mod/'menu-check.cfg').write_text(script)
    log=mod/'etconsole.log';log.unlink(missing_ok=True)
    for screenshot in (mod/'screenshots').glob('*.tga'):
        screenshot.unlink()
    command=[str(modules/'et.exe'),'+set','fs_basepath',str(ROOT/'runtime'),'+set','fs_homepath',str(home),'+set','fs_game','et-rust','+exec','menu-check.cfg']
    result=subprocess.run(command,cwd=ROOT/'runtime',capture_output=True,timeout=240)
    text=log.read_text(encoding='latin1')
    assert result.returncode==0 and '----- CL_Shutdown -----' in text,str(log)
    assert not re.search(r'ERROR:|menu parse error|unsupported server action rejected: ui_test',text,re.I),str(log)
    assert 'Parsing menu file: ui/slice.menu' in text
    assert 'Invalid custom resolution (320x240 through 16384x16384).' in text
    assert 'Parsing menu file: ui/profile_create_initial.menu' not in text
    assert text.count('LOADOUT confirmed player=1')==2,'Only confirming the stock loadout should join each session'
    players = re.findall(r'OASIS_TEST player client=0 team=(\d+)', text)
    assert players[0] == '3' and players[1:3] == ['2', '2'] and all(p == '3' for p in players[3:]), ('Spectator before confirmation; joining is not undone by canceling later edits', players)
    checks = re.findall(r'PAD_CHECK menu="([^"]*)" item="([^"]*)"', text)
    assert checks == [('ingame_main', 'RESUME')], checks
    assert text.index('LOADOUT_TEST player=1 state=1') < text.index('LOADOUT confirmed player=1'), 'Selection must precede joining'
    selected = re.findall(r'LOADOUT_TEST player=1 state=1 team=2 class=(\d+)', text)
    assert set(selected) == set('01234'), ('Original class buttons must all work', selected)
    loadouts = re.findall(r'LOADOUT_TEST player=1 state=1 team=2 class=(\d+) primary=(\d+)', text)
    assert any(c == '2' and int(w) not in (0, 8) for c, w in loadouts), 'Controller must select the engineer rifle'
    assert all(w == '8' for c, w in loadouts if c == '1'), 'Switching to medic resets the primary to its SMG'
    assert re.search(r'OASIS_TEST player client=0 team=2 class=2 latched=0,8 ', text), 'Original mouse class changes must wait for the next spawn'
    assert re.search(r'sv_running[^\r\n]*"0"',text),'Leave Match did not stop hosting'
    screenshots=mod/'screenshots'
    for name,size in [('main',(800,600)),('wrong-profile',(800,600)),('gamepad',(800,600)),('recovered',(1920,1080)),
                      ('pause',(1920,1080)),('returned',(1920,1080)),('invalid',(1920,1080)),('pending-revert',(1280,720)),('reverted',(1920,1080))]:
        assert Image.open(screenshots/f'{name}.tga').size==size,(name,size)
    image = Image.open(screenshots/'class-engineer.tga').convert('L')
    scale = min(image.width / 640, image.height / 512)
    bias = (image.width - 640 * scale) / 2
    icons = []
    for index in range(5):
        x = 435 + 110 / 6 - 9 + index * (20 + 110 / 6)
        icon = image.crop((int(bias + x * scale), int(266 * scale),
                           int(bias + (x + 38) * scale), int(300 * scale)))
        mask = icon.point(lambda p: 255 if p > 120 else 0)  # Stock inactive icons are grey, not white.
        assert mask.histogram()[255] > 20, ('Missing original class icon', index)
        icons.append(mask.tobytes())
    assert len(set(icons)) == 5, 'All five original class icons must be distinct'
    ok = image.crop((int(bias + 454 * scale), int(454 * scale), int(bias + 536 * scale), int(474 * scale)))
    assert ok.getextrema()[1] - ok.getextrema()[0] > 50, 'Original OK button must fit inside the full window'
    saved=(mod/'menu-result.cfg').read_text(encoding='latin1')
    assert 'seta r_customwidth "1920"' in saved and 'seta r_customheight "1080"' in saved
    assert 'seta r_mode "-1"' in saved
    assert 'seta cl_gamepadYawSpeed "150"' in saved and 'seta cl_gamepadLookDeadzone "0.25"' in saved
    assert 'seta cg_gamepadAimAssist "1"' in saved
    assert re.search(r'ui_rustVideoPending(?: "0"| 0)',text),'video confirmation/rollback did not clear pending state'
    # Native shutdown writes the selected profile, not only the root file.
    assert 'seta r_customwidth "1920"' in (profile/'etconfig.cfg').read_text(encoding='latin1')
    print(f'PASS native Oasis menu: main-first, post-load class selection, five class icons, explicit joins/reopen, profile recovery, video keep/revert, two start/leave cycles, saved profile. {home}')


if __name__=='__main__':
    main()
