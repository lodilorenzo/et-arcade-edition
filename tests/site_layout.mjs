// Static landing-page regression check; Node 22+ and Edge/Chrome, no packages.
// node tests/site_layout.mjs [--baseline]
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');
const baseline = process.argv.includes('--baseline');
const out = join(root, 'build', 'site-layout', baseline ? 'before' : 'after');
mkdirSync(out, { recursive: true });
const browser = process.env.BROWSER || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const profile = mkdtempSync(join(tmpdir(), 'et-site-'));
const proc = spawn(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
  '--remote-debugging-port=0', '--remote-allow-origins=*', `--user-data-dir=${profile}`, 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let socket;
try {
  const portFile = join(profile, 'DevToolsActivePort');
  for (let i = 0; i < 100 && !existsSync(portFile); i++) await sleep(100);
  assert(existsSync(portFile), 'Browser did not start');
  const port = readFileSync(portFile, 'utf8').split('\n')[0];
  const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
  socket = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  let nextId = 0;
  const pending = new Map();
  const errors = [];
  socket.onmessage = ({ data }) => {
    const msg = JSON.parse(data);
    if (msg.method === 'Runtime.exceptionThrown') errors.push(msg.params.exceptionDetails.text);
    if (!msg.id) return;
    const { resolve, reject } = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? reject(new Error(msg.error.message)) : resolve(msg.result);
  };
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++nextId;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async expression => {
    const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  await send('Page.enable');
  await send('Runtime.enable');
  await send('Page.navigate', { url: pathToFileURL(join(root, 'docs/index.html')).href });
  await sleep(600);
  const report = [];
  for (const width of [320, 390, 640, 820, 1024, 1440, 2560]) {
    await send('Emulation.setDeviceMetricsOverride', { width, height: 900, deviceScaleFactor: 1, mobile: false });
    await evaluate(`document.querySelectorAll('img').forEach(i => i.loading = 'eager'); window.scrollTo(0,0)`);
    await evaluate(`Promise.all([...document.images].map(i => i.decode()))`);
    await sleep(100);
    const result = await evaluate(`(() => {
      const overflow = [...document.querySelectorAll('body *:not(.skip-link)')].filter(el => {
        const r = el.getBoundingClientRect();
        return r.width && (r.right > innerWidth + 1 || r.left < -1);
      }).map(el => el.tagName + '.' + el.className);
      return { width: innerWidth, height: document.documentElement.scrollHeight,
        scrollWidth: document.documentElement.scrollWidth, clientWidth: document.documentElement.clientWidth, overflow,
        imagesReady: [...document.images].every(i => i.complete && i.naturalWidth),
        smallTargets: [...document.querySelectorAll('a:not(.skip-link), summary')].filter(el => {
          const r = el.getBoundingClientRect(); return r.width && r.height < 44;
        }).map(el => el.textContent.trim()),
        croppedTitles: [...document.querySelectorAll('h1, .titlebar span, .stat b')].filter(el =>
          el.scrollWidth > el.clientWidth + 1).map(el => el.textContent)
      };
    })()`);
    report.push(result);
    console.log(JSON.stringify(result));
    if (!baseline) {
      assert.equal(result.scrollWidth, result.clientWidth, `Page overflows at ${width}px`);
      assert.deepEqual(result.overflow, [], `Element overflows at ${width}px`);
      assert.deepEqual(result.croppedTitles, [], `Title cropped at ${width}px`);
      assert.deepEqual(result.smallTargets, [], `Small touch target at ${width}px`);
      assert(result.imagesReady, 'Image failed to load');
      assert(await evaluate(`[...document.images].every(i =>
        +i.getAttribute('width') === i.naturalWidth && +i.getAttribute('height') === i.naturalHeight)`), 'Missing or wrong image dimensions');
      await evaluate(`document.querySelectorAll('.dossier, .pad').forEach(d => d.open = true)`);
      assert(await evaluate(`document.documentElement.scrollWidth === document.documentElement.clientWidth`), `Expanded briefing/manual overflows at ${width}px`);
      await evaluate(`document.querySelectorAll('.dossier, .pad').forEach(d => d.open = false)`);
    }
    if ([390, 1440].includes(width)) {
      // Rasterize off-screen decoded images before the full-page capture (Chromium defers their paint).
      await evaluate(`(async () => {
        for (let y = 0; y < document.documentElement.scrollHeight; y += 800) {
          window.scrollTo(0,y); await new Promise(r => setTimeout(r, 80));
        }
        window.scrollTo(0,0); await new Promise(r => setTimeout(r, 80));
      })()`);
      const shot = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true,
        clip: { x: 0, y: 0, width, height: result.height, scale: 1 } });
      writeFileSync(join(out, `${width}.png`), Buffer.from(shot.data, 'base64'));
    }
  }
  if (!baseline) {
    await send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
    await evaluate(`document.querySelector('.mobile-nav').open = true; window.scrollTo(0,0)`);
    assert(await evaluate(`document.querySelector('.mobile-nav a[href="#split"]').getBoundingClientRect().width > 0`));
    assert(await evaluate(`document.documentElement.scrollWidth === document.documentElement.clientWidth`), 'Open navigation overflows');
    const menuShot = await send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(out, 'menu.png'), Buffer.from(menuShot.data, 'base64'));
    await evaluate(`document.querySelector('.mobile-nav a[href="#split"]').click()`);
    assert.equal(await evaluate(`document.querySelector('.mobile-nav').open`), false);
    await evaluate(`document.querySelector('.mobile-nav').open = true`);
    await send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
    assert.equal(await evaluate(`document.querySelector('.mobile-nav').open`), false);
    assert(await evaluate(`document.activeElement === document.querySelector('.mobile-nav summary')`));
    // Playback rejection must leave usable native controls without an unhandled promise.
    await evaluate(`document.querySelector('video').play = () => Promise.reject(new Error('Autoplay blocked'));
      document.querySelector('[data-play]').click()`);
    await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });
    await send('Page.reload');
    await sleep(600);
    assert(await evaluate(`document.querySelector('[data-type]').textContent.includes('Wolfenstein')`));
    assert.equal(await evaluate(`getComputedStyle(document.querySelector('.dial')).animationName`), 'none');
    assert.deepEqual(errors, [], 'Browser JavaScript error');
  }
  writeFileSync(join(out, 'report.json'), JSON.stringify(report, null, 2));
  await send('Browser.close');
  console.log(baseline ? 'Baseline saved.' : 'Landing-page layout checks passed.');
} finally {
  socket?.close();
  if (proc.exitCode === null) proc.kill();
  await sleep(2000); // Windows may retain profile locks briefly after Browser.close.
  try {
    rmSync(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 250 });
  } catch (error) {
    console.warn(`Browser profile cleanup deferred (${error.code}): ${profile}`);
  }
}
