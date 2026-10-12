"""Real four-player controller wheels, stock menu pages and bot-match complaints."""
import math
import re
import shutil
import subprocess
from PIL import Image
from play_game import prepare_game
from smoke_server import ROOT


def main():
    modules = ROOT / 'build/slice/Release'
    home = modules / 'smoke/controller-wheels'
    shutil.rmtree(home, ignore_errors=True)
    command = prepare_game(modules, home)
    mod = home / 'et-rust'
    settings = dict(ui_rustMenuTest=1, developer=0, logfile=1, cl_splitScreen=4, cl_keyboardPlayer=0,
                    ui_matchPlayers=8, g_matchPlayers=8, omnibot_enable=1, sv_maxclients=52,
                    g_gametype=2, g_doWarmup=0, g_warmup=0, bot_enable=0, com_maxfps=60, r_mode=-1, r_customwidth=1280,
                    r_customheight=720, r_customaspect=1280/720, r_fullscreen=0,
                    r_swapInterval=0, r_ignorehwgamma=1, s_initsound=0, g_friendlyFire=0,
                    in_gamepad=1, in_mouse=0, in_joystick=0, net_port=27982,
                    sv_pure=0, net_ip='127.0.0.1',
                    sv_master1='', sv_master2='', sv_master3='', sv_master4='')
    for i, cls in enumerate((2, 1, 3, 4), 1):
        settings[f'ui_oasisTeam{i}'] = 2
        settings[f'ui_oasisClass{i}'] = cls
        settings[f'ui_oasisWeapon{i}'] = 0
        settings['in_gamepadIndex' + (str(i) if i > 1 else '')] = i-1
    commands = ['devmap oasis', 'wait 300']
    commands += [f'local_join {i}' for i in range(1, 5)]
    # Do not leave warmup's queued restart behind the rest of this exec file.
    commands += ['wait 120', 'map_restart 0 0', 'wait 300', 'set developer 1', 'omnibot_test']

    def pad(slot, lx=0, ly=0, x=0, y=0, buttons=0):
        commands.append(f'gamepad_test {slot} {lx} {ly} {x} {y} {buttons} 1')

    for slot in range(4):
        pad(slot)
    commands += ['wait 10']
    for slot in range(4):
        pad(slot, buttons=32768)
    commands += ['wait 30', 'screenshot four-equipment']
    commands += [f'radial_test state {i}' for i in range(1, 5)]
    for slot in range(4):
        pad(slot, ly=-32768, x=32767, buttons=32768)
    commands += ['wait 8', 'screenshot four-equipment-selected']
    # Buttons A/B/X/Start neither confirm nor cancel a held wheel.
    for slot in range(4):
        pad(slot, ly=-32768, x=32767, buttons=32768|4096|8192|16384|16)
    commands += ['wait 3']
    for slot in range(4):
        pad(slot, ly=-32768, x=32767)
    commands += ['wait 6', 'screenshot four-equipment-feedback', 'wait 2']
    for slot in range(4):
        pad(slot)
    commands += ['wait 120'] + [f'oasis_test player {i}' for i in range(4)]
    # One-level class requests: north is Medic. Left stick must not select a command.
    for slot in range(4):
        pad(slot, buttons=1)
    commands += ['wait 30', 'screenshot four-voice']
    for slot in range(4):
        pad(slot, lx=-32768, y=32767, buttons=1)
    commands += ['wait 5', 'screenshot four-requests']
    for slot in range(4):
        pad(slot, lx=-32768, y=32767)
    commands += ['wait 6', 'screenshot four-voice-feedback', 'wait 2']
    for slot in range(4):
        pad(slot)
    commands += ['wait 60']
    # Release at center selects nothing; B alone cannot close the wheel.
    pad(3, buttons=32768); commands += ['wait 30']
    pad(3, ly=-32768, buttons=40960); commands += ['wait 4', 'radial_test state 4']
    pad(3, buttons=32768); commands += ['wait 4']
    pad(3); commands += ['wait 30', 'radial_test state 4']
    # Back opens the original limbo in just the pressing player's view; Back again closes it.
    pad(0, buttons=32); commands += ['wait 4']; pad(0)
    commands += ['wait 60', 'ui_localLoadout', 'screenshot limbo-one']
    for slot in range(1,4):
        pad(slot, buttons=32); commands += ['wait 4']; pad(slot)
    commands += ['wait 60', 'ui_localLoadout']
    for slot in range(4):
        pad(slot, buttons=32); commands += ['wait 4']; pad(slot)
    commands += ['wait 30', 'ui_localLoadout']
    # Holding L3 shows the original scoreboard in that view only (players 1 and 3).
    commands += ['wait 30', 'screenshot scores-off']
    for slot in (0, 2):
        pad(slot, buttons=64)
    commands += ['wait 60', 'screenshot scores-held', 'split_cgame 1 scoreboard_test', 'split_cgame 3 scoreboard_test']
    for slot in (0, 2):
        pad(slot)
    commands += ['wait 10']
    pad(0, buttons=128); commands += ['wait 30', 'screenshot full-map-one']
    for slot in range(4):
        pad(slot, buttons=128)
    commands += ['wait 30', 'screenshot full-map-four']
    for slot in range(4):
        pad(slot)
    commands += ['wait 30', 'screenshot full-map-released', 'set cl_gamepad 0', 'set developer 0']
    for menu in ('options', 'options_customise_game', 'options_customise_hud', 'options_controls', 'ingame_serverinfo', 'ingame_vote'):
        commands += [f'ui_test {menu}', 'wait 5', 'ui_test pad state', 'screenshot stock-'+menu, 'ui_test script closeingame']
    commands += ['set developer 1', 'set g_friendlyFire 1', 'wait 5', 'oasis_test teamkill 1 0', 'wait 30', 'quit']
    (mod/'wheels.cfg').write_text('\n'.join(commands)+'\n')
    # ET has a 1023-byte startup limit: settings are in a private autoexec instead.
    (mod/'autoexec.cfg').write_text((mod/'autoexec.cfg').read_text()+'\n'+
        '\n'.join(f'set {k} "{v}"' for k,v in settings.items())+'\n')
    result = subprocess.run(command+['+exec','wheels.cfg'], cwd=home, timeout=360, capture_output=True)
    log = mod/'etconsole.log'; text = log.read_text(encoding='latin1')
    assert result.returncode == 0 and '----- CL_Shutdown -----' in text, (result.returncode, log, result.stderr[-2000:])
    assert not re.search(r'ERROR:|Unknown UI script|Menu load error',text), log
    assert re.findall(r'RADIAL selected player=(\d+) equipment bank=(\d+)',text) == [(str(i),'5') for i in range(1,5)], log
    assert re.findall(r'RADIAL selected player=(\d+) voice=(\w+) audience=(\d+)',text) == [(str(i),'Medic','0') for i in range(1,5)], log
    rows = re.findall(r'OASIS_TEST player client=(\d+) team=(\d+) class=(\d+).*?weapon=(\d+)',text)
    assert rows == [('0','2','2','21'),('1','2','1','11'),('2','2','3','22'),('3','2','4','30')], rows
    kill = re.search(r'OASIS_TEST teamkill victim=1 attacker=0 complaint=(-?\d+) health=(-?\d+)',text)
    assert kill and int(kill[1]) <= 0 and int(kill[2]) <= 0, (kill,log)
    assert not re.search(r'CL_GetServerCommand.*complaint ',text), log
    for menu in ('options','options_customise_game','options_customise_hud','options_controls','ingame_serverinfo','ingame_vote'):
        assert f'PAD_CHECK menu="{menu}"' in text, (menu,log)
    assert 'RADIAL_TEST player=4 kind=1 selected=4 closing=0 feedback=0' in text, log
    # Wheels (native/radial.c) are 72% size: equipment on the left of each view, requests on the right.
    u = min(640/640, 360/480); s = u*.72; R = 180*s
    def wheel_center(i, equipment):
        vx, vy = i%2*640, i//2*360
        return (vx + 28*u + R if equipment else vx + 640 - 28*u - R), vy + 180 - 8*u
    for shot in ('four-equipment','four-voice','four-requests','four-equipment-feedback','four-voice-feedback'):
        image = Image.open(mod/f'screenshots/{shot}.tga').convert('RGB')
        assert image.size == (1280,720)
        for i in range(4):
            # ET's bright icons/text must be on the wheel's side of the view.
            left = i%2*640 + (0 if 'equipment' in shot else 320)
            view = image.crop((left,i//2*360,left+320,i//2*360+360))
            assert max(view.getextrema()[j][1] for j in range(3)) > 160, (shot,i)
    # The hovered choice is shown by its icon alone: its segment keeps the same tint as the others.
    image = Image.open(mod/'screenshots/four-equipment-selected.tga').convert('RGB')
    def tint(x, y):
        px = [image.getpixel((int(x)+dx, int(y)+dy)) for dx in range(-3, 4) for dy in range(-3, 4)]
        return sum(sum(p) for p in px) / len(px) / 3
    for i in range(4):
        cx, cy = wheel_center(i, True)
        spots = [tint(cx + math.sin(slot*math.pi/4 + .26)*85*s, cy - math.cos(slot*math.pi/4 + .26)*85*s) for slot in range(8)]
        others = sum(spots[:4] + spots[5:]) / 7
        assert abs(spots[4] - others) < 12, ('selected segment tinted', i, spots)  # South (bank 5) is selected.
    # Back: stock limbo per view (1, then all four), then closed by Back.
    opened = re.findall(r'LOADOUT opened player=(\d) stock_limbo=1', text)
    assert opened[-4:] == ['1', '2', '3', '4'] and opened.count('1') >= 1, opened
    assert re.findall(r'"ui_localLoadout" is:"(\d+)', text)[-3:] == ['1', '15', '0'], log
    # L3: original scoreboard with real scores; only the held views darken behind it.
    boards = re.findall(r'SCOREBOARD_TEST begin player=\d+ scores=(\d+)', text)
    assert len(boards) >= 2 and all(int(n) >= 8 for n in boards[-2:]), boards
    off = Image.open(mod/'screenshots/scores-off.tga').convert('L'); held = Image.open(mod/'screenshots/scores-held.tga').convert('L')
    def mean(image, i):
        x, y = i%2*640, i//2*360
        data = image.crop((x+60, y+70, x+580, y+300)).getdata()
        return sum(data)/len(data)
    change = [mean(held, i)/max(mean(off, i), 1) for i in range(4)]
    assert change[0] < .9 and change[2] < .9 and .9 < change[1] < 1.1 and .9 < change[3] < 1.1, change
    # Expanded maps have a dark 250-pixel vertical border in each owned viewport.
    # This fails on the old queued +mapexpand/-mapexpand path, not merely its input log.
    for shot, expanded in (('full-map-one',(1,0,0,0)),('full-map-four',(1,1,1,1)),('full-map-released',(0,0,0,0))):
        image = Image.open(mod/f'screenshots/{shot}.tga').convert('RGB')
        for i, shown in enumerate(expanded):
            colors = (image.getpixel((i%2*640+277,i//2*360+y)) for y in range(20,270))
            dark = sum(r<80 and g<65 and b<60 for r,g,b in colors)
            assert (dark>200) == bool(shown), (shot,i,dark)
    assert re.search(r'OMNIBOT_TEST count=[1-9]\d* requested=8',text), log
    print(f'PASS per-view Back limbo open/close, held L3 original scoreboard with scores, fast wheels, full map, original menus and bot-match teamkill suppression. {log}')


if __name__ == '__main__':
    main()
