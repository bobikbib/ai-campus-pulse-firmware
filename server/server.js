'use strict';
// Campus Pulse webcam server. Standard library only.
//   node server/server.js [--port 8080] [--lan]
// Browser POSTs frames to /api/frame; one cp_cam child per room turns them
// into presence lines. /api/sensor accepts external presence JSON (receiver.py
// compatible). Missing binary: /api/frame still answers 200 (the frame is
// accepted and dropped; the failure is logged to stderr).
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');

const ROOT = path.join(__dirname, '..');
const WEB = path.join(ROOT, 'web');
const STALE_AFTER_S = 15;
const MAX_BODY = 4096;
const ROOM_RE = /^[A-Za-z0-9_-]{1,23}$/;
const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8', '.wasm': 'application/wasm',
  '.tflite': 'application/octet-stream', '.png': 'image/png', '.svg': 'image/svg+xml',
};

function defaultBin() {
  return path.join(ROOT, 'build', process.platform === 'win32' ? 'cp_cam.exe' : 'cp_cam');
}

function stamp() {
  const d = new Date(), p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

function startServer({ port = 8080, host = '127.0.0.1', binPath = defaultBin(),
  logPath = path.join(ROOT, 'data', 'readings.jsonl') } = {}) {
  const state = new Map();     // room -> {msg, received}
  const children = new Map();  // room -> {proc, buf}
  fs.mkdirSync(path.dirname(logPath), { recursive: true });

  function store(msg) {
    state.set(msg.room, { msg, received: Date.now() / 1000 });
    try {
      fs.appendFileSync(logPath, JSON.stringify({ received_at: stamp(), ...msg }) + '\n');
    } catch (e) { console.error('log write failed:', e.message); }
  }

  function view(e, now) {
    const m = e.msg, age = now - e.received;
    let status;
    if (age > STALE_AFTER_S) status = 'no data';
    else if (m.sensor !== 'ok') status = `sensor ${m.sensor}`;
    else status = m.occupied ? 'occupied' : 'empty';
    return { ...m, status, last_seen_s: Math.round(age * 10) / 10 };
  }

  function handleLine(room, line) {
    if (!line.startsWith('CP>')) return;
    let m;
    try { m = JSON.parse(line.slice(3)); } catch (e) { return; }
    if (m.type === 'presence') {
      if (m.room === room) store(m); // ignore the boot line with the default room id
    } else if (m.type === 'error') {
      console.error(`cp_cam[${room}] error:`, line);
    }
  }

  function dropChild(room, c) {
    if (children.get(room) === c) children.delete(room);
  }

  function getChild(room) {
    let c = children.get(room);
    if (c) return c;
    let proc;
    try {
      proc = spawn(binPath, [], { stdio: ['pipe', 'pipe', 'inherit'], windowsHide: true });
    } catch (e) {
      console.error(`cp_cam[${room}] spawn failed:`, e.message);
      return null;
    }
    c = { proc, buf: '' };
    children.set(room, c);
    proc.on('error', (e) => { console.error(`cp_cam[${room}] error:`, e.message); dropChild(room, c); });
    proc.on('exit', (code, sig) => { console.error(`cp_cam[${room}] exited (${code ?? sig})`); dropChild(room, c); });
    proc.stdin.on('error', () => dropChild(room, c));
    proc.stdout.on('error', () => {});
    proc.stdout.setEncoding('utf8');
    proc.stdout.on('data', (chunk) => {
      c.buf += chunk;
      let i;
      while ((i = c.buf.indexOf('\n')) >= 0) {
        const line = c.buf.slice(0, i).replace(/\r$/, '');
        c.buf = c.buf.slice(i + 1);
        handleLine(room, line);
      }
    });
    send(c, `ROOM ${room}`);
    return c;
  }

  function send(c, line) {
    const s = c.proc.stdin;
    if (!s || s.destroyed || !s.writable) return false;
    try { s.write(line + '\n'); return true; } catch (e) { return false; }
  }

  const tick = setInterval(() => {
    for (const c of children.values()) send(c, 'TICK');
  }, 100);

  function readBody(req, res, cb) {
    const len = parseInt(req.headers['content-length'] || '0', 10);
    if (!(len > 0) || len > MAX_BODY) {
      req.resume();
      return json(res, 400, { error: 'bad length' });
    }
    const chunks = []; let n = 0; let over = false;
    req.on('data', (d) => { n += d.length; if (n > MAX_BODY) over = true; else chunks.push(d); });
    req.on('end', () => {
      if (over) return json(res, 400, { error: 'body too large' });
      let obj;
      try { obj = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch (e) {
        return json(res, 400, { error: 'invalid JSON' });
      }
      cb(obj);
    });
    req.on('error', () => {});
  }

  function json(res, code, obj) {
    const d = Buffer.from(JSON.stringify(obj));
    res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': d.length });
    res.end(d);
  }

  function serveFile(res, file) {
    fs.readFile(file, (err, data) => {
      if (err) return json(res, 404, { error: 'not found' });
      res.writeHead(200, { 'Content-Type': MIME[path.extname(file).toLowerCase()] || 'application/octet-stream',
        'Content-Length': data.length });
      res.end(data);
    });
  }

  function serveWeb(res, rel) {
    let dec;
    try { dec = decodeURIComponent(rel); } catch (e) { return json(res, 404, { error: 'not found' }); }
    if (dec.includes('\0') || dec.includes('\\')) return json(res, 404, { error: 'not found' });
    const file = path.resolve(WEB, dec);
    if (file !== WEB && !file.startsWith(WEB + path.sep)) return json(res, 404, { error: 'not found' });
    serveFile(res, file);
  }

  function validFrame(b) {
    if (!b || typeof b !== 'object' || Array.isArray(b)) return 'body must be an object';
    if (typeof b.room !== 'string' || !ROOM_RE.test(b.room)) return 'bad room';
    if (!Number.isInteger(b.people) || b.people < 0 || b.people > 999) return 'bad people';
    if (b.noise_db != null && (!Number.isInteger(b.noise_db) || b.noise_db < 0 || b.noise_db > 120)) return 'bad noise_db';
    if (b.voice != null && typeof b.voice !== 'boolean') return 'bad voice';
    return null;
  }

  const server = http.createServer((req, res) => {
    try {
      const url = req.url.split('?')[0];
      if (req.method === 'GET') {
        if (url === '/api/rooms') {
          const now = Date.now() / 1000;
          const out = [...state.values()].map((e) => view(e, now)).sort((a, b) => (a.room < b.room ? -1 : a.room > b.room ? 1 : 0));
          return json(res, 200, out);
        }
        if (url === '/') return serveFile(res, path.join(WEB, 'dashboard.html'));
        if (url === '/sensor') return serveFile(res, path.join(WEB, 'sensor.html'));
        if (url.startsWith('/web/')) return serveWeb(res, url.slice(5));
        return json(res, 404, { error: 'not found' });
      }
      if (req.method === 'POST' && url === '/api/frame') {
        return readBody(req, res, (b) => {
          const err = validFrame(b);
          if (err) return json(res, 400, { error: err });
          const c = getChild(b.room);
          if (c) {
            send(c, `DET ${b.people}`);
            if (b.noise_db != null && b.voice != null) send(c, `AUD ${b.noise_db} ${b.voice ? 1 : 0}`);
          }
          json(res, 200, { ok: true });
        });
      }
      if (req.method === 'POST' && url === '/api/sensor') {
        return readBody(req, res, (b) => {
          if (!b || typeof b !== 'object' || Array.isArray(b) || typeof b.room !== 'string' || !b.room) {
            return json(res, 400, { error: 'room missing' });
          }
          store(b);
          json(res, 200, { ok: true });
        });
      }
      json(res, 404, { error: 'not found' });
    } catch (e) {
      console.error('request error:', e);
      if (!res.headersSent) json(res, 500, { error: 'internal' });
    }
  });

  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => {
      let closed = null;
      resolve({
        port: server.address().port,
        children,
        _childPid: (room) => { const c = children.get(room); return c && c.proc.pid ? c.proc.pid : null; },
        close() {
          if (closed) return closed;
          clearInterval(tick);
          for (const c of children.values()) { try { c.proc.stdin.end(); } catch (e) { /* ignore */ } }
          const procs = [...children.values()].map((c) => c.proc);
          children.clear();
          closed = new Promise((r) => {
            const t = setTimeout(() => { procs.forEach((p) => { try { p.kill(); } catch (e) { /* */ } }); }, 1000);
            t.unref();
            server.close(() => r());
            server.closeAllConnections();
          });
          return closed;
        },
      });
    });
  });
}

if (require.main === module) {
  const a = process.argv.slice(2);
  const pi = a.indexOf('--port');
  const port = pi >= 0 ? parseInt(a[pi + 1], 10) : 8080;
  const host = a.includes('--lan') ? '0.0.0.0' : '127.0.0.1';
  startServer({ port, host }).then((s) => {
    console.log(`Campus Pulse server on http://${host === '0.0.0.0' ? '0.0.0.0' : 'localhost'}:${s.port}`);
    process.on('SIGINT', () => s.close().then(() => process.exit(0)));
  }, (e) => { console.error(e.message); process.exit(1); });
}

module.exports = { startServer };
