'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const { startServer } = require('./server.js');

const ROOT = path.join(__dirname, '..');
const BIN = path.join(ROOT, 'build', process.platform === 'win32' ? 'cp_cam.exe' : 'cp_cam');
if (!fs.existsSync(BIN)) {
  throw new Error(`${BIN} missing: build it first (make cam CC=gcc)`);
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Raw request helper (fetch would normalise "/web/../" paths).
function req(srv, method, p, body) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? null : (Buffer.isBuffer(body) || typeof body === 'string' ? body : JSON.stringify(body));
    const r = http.request({ host: '127.0.0.1', port: srv.port, method, path: p,
      headers: data ? { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) } : {} }, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => {
        const text = Buffer.concat(chunks).toString();
        let json = null;
        try { json = JSON.parse(text); } catch (e) { /* not json */ }
        resolve({ status: res.statusCode, text, json, headers: res.headers });
      });
    });
    r.on('error', reject);
    if (data) r.write(data);
    r.end();
  });
}

async function withServer(opts, fn) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cp-srv-'));
  const logPath = path.join(dir, 'readings.jsonl');
  const srv = await startServer({ port: 0, host: '127.0.0.1', binPath: BIN, logPath, ...opts });
  try {
    await fn(srv, logPath);
  } finally {
    await srv.close();
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

async function rooms(srv) { return (await req(srv, 'GET', '/api/rooms')).json; }

async function waitFor(fn, ms = 2500) {
  const end = Date.now() + ms;
  let v;
  while (Date.now() < end) {
    v = await fn();
    if (v) return v;
    await sleep(50);
  }
  return v;
}

test('frames for T-1 produce an occupied room and a log line', () => withServer({}, async (srv, logPath) => {
  for (let i = 0; i < 6; i++) {
    const r = await req(srv, 'POST', '/api/frame', { room: 'T-1', people: 1 });
    assert.strictEqual(r.status, 200);
    assert.deepStrictEqual(r.json, { ok: true });
    await sleep(50);
  }
  const room = await waitFor(async () => {
    const t = (await rooms(srv)).find((x) => x.room === 'T-1');
    return t && t.occupied ? t : null;
  });
  assert.ok(room, 'T-1 missing or not occupied');
  assert.strictEqual(room.people_count, 1);
  assert.strictEqual(room.status, 'occupied');
  assert.ok(typeof room.last_seen_s === 'number');
  const lines = fs.readFileSync(logPath, 'utf8').trim().split('\n').map((l) => JSON.parse(l));
  const l = lines.find((x) => x.room === 'T-1');
  assert.ok(l, 'no T-1 line in log');
  assert.match(l.received_at, /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$/);
}));

test('audio fields flow through', () => withServer({}, async (srv) => {
  const room = await waitFor(async () => {
    await req(srv, 'POST', '/api/frame', { room: 'T-A', people: 1, noise_db: 55, voice: true });
    const t = (await rooms(srv)).find((x) => x.room === 'T-A');
    return t && t.noise_db === 55 && t.voice === true ? t : null;
  }, 4000);
  assert.ok(room, 'noise_db 55 / voice true never appeared');
}));

test('bad bodies are rejected with 400 and create nothing', () => withServer({}, async (srv) => {
  const ok = { room: 'B-1', people: 1 };
  const bad = [
    { ...ok, room: 'a"b' },
    { ...ok, room: 'x'.repeat(24) },
    { ...ok, people: -1 },
    { ...ok, people: 1000 },
    { ...ok, people: 1.5 },
    { ...ok, noise_db: 121 },
    { ...ok, voice: 'yes' },
    'not json{',
    JSON.stringify({ ...ok, pad: 'x'.repeat(5000) }),
  ];
  for (const b of bad) {
    const r = await req(srv, 'POST', '/api/frame', b);
    assert.strictEqual(r.status, 400, JSON.stringify(b).slice(0, 80));
    assert.ok(r.json && typeof r.json.error === 'string');
  }
  await sleep(200);
  assert.deepStrictEqual(await rooms(srv), []);
}));

test('crashed child is respawned on the next frame', () => withServer({}, async (srv) => {
  await req(srv, 'POST', '/api/frame', { room: 'T-2', people: 1 });
  assert.ok(await waitFor(async () => (await rooms(srv)).some((x) => x.room === 'T-2')), 'no T-2 initially');
  const pid1 = srv._childPid('T-2');
  assert.ok(pid1, 'no child pid');
  process.kill(pid1);
  assert.ok(await waitFor(() => srv._childPid('T-2') === null), 'child not dropped');
  const before = (await rooms(srv)).find((x) => x.room === 'T-2').last_seen_s;
  await sleep(1200);
  for (let i = 0; i < 4; i++) { await req(srv, 'POST', '/api/frame', { room: 'T-2', people: 2 }); await sleep(50); }
  const room = await waitFor(async () => {
    const t = (await rooms(srv)).find((x) => x.room === 'T-2');
    return t && t.people_count === 2 ? t : null;
  });
  assert.ok(room, 'no presence after respawn');
  assert.ok(room.last_seen_s < before + 1.2);
  const pid2 = srv._childPid('T-2');
  assert.ok(pid2 && pid2 !== pid1, 'child was not replaced');
}));

test('missing binary: frame answers 200 (accepted, nothing stored) and server stays up', () =>
  withServer({ binPath: path.join(os.tmpdir(), 'no-such-cp-cam.exe') }, async (srv) => {
    const r = await req(srv, 'POST', '/api/frame', { room: 'T-3', people: 1 });
    assert.ok(r.status === 200 || r.status === 503);
    await sleep(300);
    const g = await req(srv, 'GET', '/api/rooms');
    assert.strictEqual(g.status, 200);
    assert.deepStrictEqual(g.json, []);
  }));

test('POST /api/sensor stores external readings', () => withServer({}, async (srv, logPath) => {
  const msg = { v: 1, type: 'presence', room: 'EXT-1', people_count: 3, occupied: true, empty_for_s: 0, sensor: 'ok', mode: 'camera' };
  const r = await req(srv, 'POST', '/api/sensor', msg);
  assert.strictEqual(r.status, 200);
  const room = (await rooms(srv)).find((x) => x.room === 'EXT-1');
  assert.ok(room);
  assert.strictEqual(room.status, 'occupied');
  assert.ok(fs.readFileSync(logPath, 'utf8').includes('"EXT-1"'));
  assert.strictEqual((await req(srv, 'POST', '/api/sensor', { room: '' })).status, 400);
  assert.strictEqual((await req(srv, 'POST', '/api/sensor', [1])).status, 400);
  const s = await req(srv, 'POST', '/api/sensor', { ...msg, room: 'EXT-2', sensor: 'fault' });
  assert.strictEqual(s.status, 200);
  assert.strictEqual((await rooms(srv)).find((x) => x.room === 'EXT-2').status, 'sensor fault');
}));

test('path traversal under /web is a 404', () => withServer({}, async (srv) => {
  assert.strictEqual((await req(srv, 'GET', '/web/../server/server.js')).status, 404);
  assert.strictEqual((await req(srv, 'GET', '/web/%2e%2e/server/server.js')).status, 404);
  assert.strictEqual((await req(srv, 'GET', '/web/..%5cserver%5cserver.js')).status, 404);
}));
