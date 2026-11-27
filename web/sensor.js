// Browser room sensor: counts people (camera) and measures noise/voice (mic)
// locally with MediaPipe, and POSTs only numbers to /api/frame.
import { FilesetResolver, ObjectDetector } from './vendor/tasks-vision/vision_bundle.mjs';
import { FilesetResolver as AudioFileset, AudioClassifier } from './vendor/tasks-audio/audio_bundle.mjs';

const VOICE_NAMES = new Set(['Speech', 'Child speech, kid speaking', 'Conversation',
  'Narration, monologue', 'Babbling', 'Laughter', 'Shout', 'Yell', 'Whispering']);
const VOICE_SCORE = 0.3;
const SEND_MS = 200;

const $ = (id) => document.getElementById(id);
const video = $('video'), canvas = $('overlay'), ctx = canvas.getContext('2d');

// Latest measurements (null = unavailable).
const state = { people: 0, noise_db: null, voice: null };
let cameraOk = false;
let lastDetectAt = 0;     // performance.now() of the last successful detection
let trackEnded = false;   // camera unplugged / revoked
let stalled = false;      // currently not sending because the camera gives no frames

function setStatus(text, isErr) {
  $('status').textContent = text;
  $('status').className = isErr ? 'err' : 'note';
}

try { $('room').value = localStorage.getItem('cp_room') || $('room').value; } catch (e) { /* no storage */ }

if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
  $('insecure').hidden = false;
  $('start').disabled = true;
}

$('start').addEventListener('click', start);

async function start() {
  if (!/^[A-Za-z0-9_-]{1,23}$/.test($('room').value.trim())) {
    setStatus('Неверное имя комнаты: только латинские буквы, цифры, «-» и «_», до 23 символов (например A-101).', true);
    return;
  }
  $('start').disabled = true;
  $('room').disabled = true;
  try { localStorage.setItem('cp_room', $('room').value); } catch (e) { /* no storage */ }

  try {
    const stream = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 480 } });
    video.srcObject = stream;
    stream.getVideoTracks()[0].addEventListener('ended', () => { trackEnded = true; });
    await video.play();
  } catch (e) {
    setStatus('Камера недоступна или доступ запрещён. Данные не отправляются. (' + e.name + ')', true);
    $('start').disabled = false; $('room').disabled = false;
    return;
  }

  setStatus('Загрузка моделей...');
  let detector;
  try {
    detector = await makeDetector();
  } catch (e) {
    setStatus('Не удалось загрузить модель камеры. Запускали tools/fetch_models.sh? (' + e.message + ')', true);
    return;
  }
  cameraOk = true;
  setStatus('Работает. Камера включена.');
  detectLoop(detector);
  setInterval(sendFrame, SEND_MS);
  startAudio(); // independent: failure here does not stop the camera
}

async function makeDetector() {
  const fileset = await FilesetResolver.forVisionTasks('/web/vendor/tasks-vision/wasm');
  const make = (delegate) => ObjectDetector.createFromOptions(fileset, {
    baseOptions: { modelAssetPath: '/web/models/efficientdet_lite0.tflite', delegate },
    runningMode: 'VIDEO', categoryAllowlist: ['person'], scoreThreshold: 0.5, maxResults: 20,
  });
  try { return await make('GPU'); } catch (e) { return await make('CPU'); }
}

function detectLoop(detector) {
  let lastTime = -1;
  const tick = () => {
    if (video.readyState >= 2 && video.currentTime !== lastTime) {
      lastTime = video.currentTime;
      const res = detector.detectForVideo(video, performance.now());
      state.people = res.detections.length;
      lastDetectAt = performance.now();
      drawBoxes(res.detections);
    }
    requestAnimationFrame(tick);
  };
  tick();
}

function drawBoxes(dets) {
  canvas.width = video.videoWidth; canvas.height = video.videoHeight;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.lineWidth = 3; ctx.strokeStyle = '#4cc26b';
  for (const d of dets) {
    const b = d.boundingBox;
    ctx.strokeRect(b.originX, b.originY, b.width, b.height);
  }
}

// ---- audio ----
async function startAudio() {
  let stream;
  try {
    stream = await navigator.mediaDevices.getUserMedia({ audio: true });
  } catch (e) {
    $('micmsg').textContent = 'микрофон недоступен';
    return;
  }
  const actx = new AudioContext();
  await actx.resume();
  const src = actx.createMediaStreamSource(stream);
  const proc = actx.createScriptProcessor(4096, 1, 1);
  const mute = actx.createGain(); mute.gain.value = 0; // keep the node alive, play nothing
  src.connect(proc); proc.connect(mute); mute.connect(actx.destination);

  state.noise_db = 0; state.voice = false;
  let classifier = null;
  try {
    const fs = await AudioFileset.forAudioTasks('/web/vendor/tasks-audio/wasm');
    classifier = await AudioClassifier.createFromOptions(fs, {
      baseOptions: { modelAssetPath: '/web/models/yamnet.tflite' }, maxResults: 5,
    });
  } catch (e) {
    $('micmsg').textContent = 'модель голоса не загрузилась, шум измеряется (' + e.message + ')';
    state.voice = null;
  }

  let buf = [], buffered = 0;
  proc.onaudioprocess = (ev) => {
    const data = new Float32Array(ev.inputBuffer.getChannelData(0)); // copy
    state.noise_db = noiseDb(data);
    if (!classifier) return;
    buf.push(data); buffered += data.length;
    if (buffered >= actx.sampleRate) { // ~1 s
      const all = new Float32Array(buffered);
      let o = 0; for (const c of buf) { all.set(c, o); o += c.length; }
      buf = []; buffered = 0;
      try { state.voice = isVoice(classifier.classify(all, actx.sampleRate)); } catch (e) { state.voice = null; }
    }
  };
}

function noiseDb(samples) {
  let sum = 0;
  for (let i = 0; i < samples.length; i++) sum += samples[i] * samples[i];
  const rms = Math.sqrt(sum / samples.length);
  if (rms <= 0) return 0;
  return Math.max(0, Math.min(120, Math.round(20 * Math.log10(rms) + 100)));
}

function isVoice(results) {
  for (const r of results) for (const c of r.classifications) for (const cat of c.categories) {
    if (VOICE_NAMES.has(cat.categoryName) && cat.score >= VOICE_SCORE) return true;
  }
  return false;
}

// ---- sending ----
let failures = 0;
async function sendFrame() {
  // Never send a stale people count: if detection stopped, send nothing
  // (the server then marks the sensor stale) and resume automatically.
  if (trackEnded || performance.now() - lastDetectAt > 1000) {
    stalled = true;
    setStatus('камера не даёт кадров — данные не отправляются', true);
    return;
  }
  if (stalled) { stalled = false; setStatus('Работает. Камера включена.'); }
  const frame = { room: $('room').value.trim(), people: state.people,
                  noise_db: state.noise_db, voice: state.voice };
  $('people').textContent = frame.people;
  $('noise').textContent = frame.noise_db === null ? 'нет' : frame.noise_db;
  $('voice').textContent = frame.voice === null ? 'нет данных' : (frame.voice ? 'речь слышна' : 'тихо');
  if (frame.noise_db === null && !$('micmsg').textContent) $('micmsg').textContent = 'микрофон недоступен';
  $('json').textContent = JSON.stringify(frame, null, 2);
  try {
    const r = await fetch('/api/frame', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(frame) });
    if (!r.ok) {
      let msg = 'HTTP ' + r.status;
      try { msg = (await r.json()).error || msg; } catch (e) { /* not JSON */ }
      $('srvmsg').textContent = 'сервер отклонил данные: ' + msg;
      return;
    }
    failures = 0; $('srvmsg').textContent = '';
  } catch (e) {
    if (++failures >= 2) $('srvmsg').textContent = 'сервер не отвечает (повторяю...)';
  }
}
