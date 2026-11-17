// Next-Gen Golf browser client. All golf logic (swing analysis, ball physics, course generation,
// rules) runs in the GolfCore WebAssembly module; this file is input, rendering, camera and UI only.
(function () {
'use strict';

// ---------------------------------------------------------------------------------------------------
// WebAssembly core
// ---------------------------------------------------------------------------------------------------
let core = null;
function b64ToBytes(b64) {
  const bin = atob(b64), out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
function loadCore() {
  const wasi = new Proxy({}, { get: () => () => 0 }); // the core does no I/O
  const mod = new WebAssembly.Module(b64ToBytes(window.GOLF_WASM_B64));
  const inst = new WebAssembly.Instance(mod, { wasi_snapshot_preview1: wasi });
  core = inst.exports;
  if (core._initialize) core._initialize();
}
function cstr(ptr) {
  const u = new Uint8Array(core.memory.buffer);
  let s = '';
  while (u[ptr]) s += String.fromCharCode(u[ptr++]);
  return s;
}
function buf(n) { return new Float64Array(core.memory.buffer, core.ng_buf(), n || 64); }

// ---------------------------------------------------------------------------------------------------
// Settings and saved data
// ---------------------------------------------------------------------------------------------------
const store = {
  get(key, fallback) { try { const v = localStorage.getItem('ngg.' + key); return v ? JSON.parse(v) : fallback; } catch (e) { return fallback; } },
  set(key, value) { try { localStorage.setItem('ngg.' + key, JSON.stringify(value)); } catch (e) { /* private mode: play on without saving */ } },
};
const defaults = { swingDifficulty: 0, swingMode: 'natural', sensitivity: 1, swingLength: 380, downswingTime: 0.28, units: 'yd',
                   leftHanded: false, quality: 1, volume: 0.6, showFps: false, tracer: true };
const settings = Object.assign({}, defaults, store.get('settings', {}));
function saveSettings() { store.set('settings', settings); }

const SURF = { Tee: 0, Fairway: 1, FirstCut: 2, Rough: 3, DeepRough: 4, Sand: 5, Green: 6, Fringe: 7, Hard: 8, Water: 9, OOB: 10 };
const PUTTER = 16, SHOT_COUNT = 12;
const SWING_DIFF = ['Beginner', 'Intermediate', 'Advanced', 'Simulation'];
const COURSE_DIFF = ['Beginner', 'Intermediate', 'Advanced', 'Championship'];
const TEES = ['Championship', 'Back', 'Middle', 'Forward'];
const STYLES = ['Full', 'Three-quarter', 'Half', 'Controlled'];

function dist(m) { return settings.units === 'yd' ? Math.round(m / 0.9144) + ' yd' : Math.round(m) + ' m'; }
function shortDist(m) { // putts and chips
  if (m >= 25) return dist(m);
  return settings.units === 'yd' ? (m / 0.3048).toFixed(m < 3 ? 1 : 0) + ' ft' : m.toFixed(1) + ' m';
}
function windText(ms) { return settings.units === 'yd' ? Math.round(ms * 2.23694) + ' mph' : Math.round(ms * 3.6) + ' km/h'; }
function toPar(n) { return n === 0 ? 'E' : (n > 0 ? '+' + n : '' + n); }
const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------------------------------------------
// Sound (synthesised; every sound is triggered by a game event)
// ---------------------------------------------------------------------------------------------------
let audio = null;
function ensureAudio() {
  if (audio) return;
  try { audio = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) { audio = null; }
}
function noiseBurst(duration, freq, q, gain) {
  if (!audio || settings.volume <= 0) return;
  const n = Math.floor(audio.sampleRate * duration), b = audio.createBuffer(1, n, audio.sampleRate), d = b.getChannelData(0);
  for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / n, 2.5);
  const src = audio.createBufferSource(); src.buffer = b;
  const f = audio.createBiquadFilter(); f.type = 'bandpass'; f.frequency.value = freq; f.Q.value = q;
  const g = audio.createGain(); g.gain.value = gain * settings.volume;
  src.connect(f); f.connect(g); g.connect(audio.destination); src.start();
}
function tone(freq, duration, gain) {
  if (!audio || settings.volume <= 0) return;
  const o = audio.createOscillator(), g = audio.createGain();
  o.frequency.value = freq; g.gain.value = gain * settings.volume;
  g.gain.exponentialRampToValueAtTime(0.0001, audio.currentTime + duration);
  o.connect(g); g.connect(audio.destination); o.start(); o.stop(audio.currentTime + duration);
}
const sfx = {
  impact(club, quality) {
    if (club === PUTTER) { noiseBurst(0.05, 1800, 3, 0.5); return; }
    const wood = club <= 4;
    noiseBurst(wood ? 0.12 : 0.09, wood ? 2600 : 3800, wood ? 1.2 : 2.0, 0.9 * (0.5 + 0.5 * quality));
    if (wood) tone(950, 0.08, 0.15);
  },
  land(surface, hard) {
    if (surface === SURF.Water) { noiseBurst(0.5, 700, 0.6, 0.8); return; }
    if (surface === SURF.Sand) { noiseBurst(0.18, 500, 0.8, 0.5); return; }
    noiseBurst(0.08, surface === SURF.Green ? 300 : 220, 1.0, 0.25 + 0.5 * hard);
  },
  tree() { noiseBurst(0.15, 1200, 0.7, 0.6); tone(180, 0.1, 0.2); },
  cup() { tone(620, 0.09, 0.3); setTimeout(() => tone(420, 0.12, 0.25), 70); setTimeout(() => noiseBurst(0.1, 900, 2, 0.3), 120); },
  ui() { tone(880, 0.04, 0.08); },
};

// ---------------------------------------------------------------------------------------------------
// Renderer and scene
// ---------------------------------------------------------------------------------------------------
const canvas = $('view');
let renderer, scene, camera;
const world = { group: null, heights: null, nx: 0, ny: 0, cell: 2, gx: 0, gy: 0, env: 0 };
const V = (x, y, z) => new THREE.Vector3(x, z, -y); // simulation (x right, y downrange, z up) -> three.js

function initRenderer() {
  renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(52, 1, 0.3, 2400);
  scene.add(new THREE.HemisphereLight(0xe6f1ff, 0x5a6a44, 1.2));
  const sun = new THREE.DirectionalLight(0xfff2d8, 1.05);
  sun.position.set(-0.55, 0.8, 0.35);
  scene.add(sun);
  applyQuality();
  window.addEventListener('resize', resize);
  resize();
}
function applyQuality() {
  const cap = [1, 1.5, 2][settings.quality];
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, cap));
}
function resize() {
  renderer.setSize(window.innerWidth, window.innerHeight, false);
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
}

function heightAt(x, y) {
  const w = world;
  if (!w.heights) return 0;
  const fx = Math.min(Math.max((x - w.gx) / w.cell, 0), w.nx - 1.001), fy = Math.min(Math.max((y - w.gy) / w.cell, 0), w.ny - 1.001);
  const ix = Math.floor(fx), iy = Math.floor(fy), tx = fx - ix, ty = fy - iy, H = w.heights, n = w.nx;
  const a = H[iy * n + ix], b = H[iy * n + ix + 1], c = H[(iy + 1) * n + ix], d = H[(iy + 1) * n + ix + 1];
  return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}

// Colour of each surface, per environment. Order matches the core's Surface enum.
function palette(env) {
  const p = [[88, 152, 76], [94, 162, 74], [80, 142, 64], [64, 120, 54], [52, 100, 46], [228, 210, 162], [116, 190, 100], [100, 170, 82],
             [150, 140, 120], [48, 104, 150], [50, 92, 44]];
  if (env === 4) { p[3] = [176, 150, 104]; p[4] = [160, 134, 92]; p[10] = [150, 124, 86]; }                 // desert
  if (env === 5) { p[3] = [136, 140, 82]; p[4] = [150, 142, 86]; p[10] = [128, 128, 78]; }                  // links fescue
  if (env === 2) { p[4] = [168, 160, 112]; p[10] = [150, 146, 104]; }                                       // coastal dunes
  if (env === 3) { p[3] = [52, 104, 52]; p[4] = [42, 86, 46]; p[10] = [38, 76, 44]; }                       // alpine
  if (env === 1) { p[3] = [70, 116, 58]; p[4] = [84, 100, 60]; p[10] = [74, 92, 56]; }                      // highlands heather
  return p;
}
const SKY = [ // per weather: top, horizon, fog density
  [0x3f8fe0, 0xbfe0f7, 0.0009], [0x6f9fd0, 0xcfdde8, 0.0011], [0x8a97a3, 0xc5ccd2, 0.0016],
  [0x74808a, 0xaeb7bd, 0.0024], [0x4a90d8, 0xc8e2f4, 0.0010], [0xb9c2c6, 0xd0d6d8, 0.0060]];

function clearWorld() {
  if (!world.group) return;
  scene.remove(world.group);
  world.group.traverse((o) => {
    if (o.geometry) o.geometry.dispose();
    if (o.material) { if (o.material.map) o.material.map.dispose(); o.material.dispose(); }
  });
  world.group = null;
}

function buildHole(index, env, weather) {
  clearWorld();
  const g = new THREE.Group();
  world.group = g;
  core.ng_hole_info(index);
  const info = Array.from(buf(48));
  const nx = info[2], ny = info[3], cell = info[4], gx = info[5], gy = info[6];
  const heights = new Float32Array(new Float32Array(core.memory.buffer, core.ng_hole_heights(index), nx * ny)); // copy
  Object.assign(world, { heights, nx, ny, cell, gx, gy, env });

  // Trees first: their shadows are painted into the terrain texture.
  const nTrees = core.ng_hole_trees(index);
  const trees = Array.from(buf(nTrees * 5));

  // Terrain texture from the core's surface map.
  const res = 0.5;
  const mapPtr = core.ng_surface_map(index, res);
  const mw = buf(2)[0], mh = buf(2)[1];
  const map = new Uint8Array(core.memory.buffer, mapPtr, mw * mh);
  const cv = document.createElement('canvas'); cv.width = mw; cv.height = mh;
  const ctx = cv.getContext('2d');
  const img = ctx.createImageData(mw, mh), px = img.data, pal = palette(env);
  for (let y = 0; y < mh; y++) {
    for (let x = 0; x < mw; x++) {
      const s = map[y * mw + x], c = pal[s], i = (y * mw + x) * 4;
      let k = ((x * 73856093) ^ (y * 19349663)) >>> 0; k = ((k >>> 13) ^ k) * 1274126177 >>> 0;
      let n = ((k & 255) / 255 - 0.5) * (s === SURF.Sand ? 10 : s === SURF.Green ? 6 : 14);
      if (s === SURF.Fairway) n += (Math.floor(y * res / 7) % 2 ? 7 : -7);                    // mowing stripes
      if (s === SURF.Green) n += (Math.floor((x + y) * res / 2.5) % 2 ? 4 : -4);
      if (s === SURF.Rough || s === SURF.DeepRough || s === SURF.OOB) n += 9 * Math.sin(x * 0.045) * Math.sin(y * 0.038);
      if (s === SURF.Water) n = 6 * Math.sin(x * 0.11 + y * 0.07);
      px[i] = c[0] + n; px[i + 1] = c[1] + n; px[i + 2] = c[2] + n; px[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  ctx.fillStyle = 'rgba(10,30,14,0.30)';
  for (let t = 0; t < nTrees; t++) {
    const tx = trees[t * 5], ty = trees[t * 5 + 1], r = trees[t * 5 + 3], h = trees[t * 5 + 4];
    ctx.beginPath();
    ctx.ellipse((tx + h * 0.45 - gx) / res, (ty - h * 0.25 - gy) / res, r * 1.5 / res, r * 1.1 / res, 0, 0, Math.PI * 2);
    ctx.fill();
  }
  const tex = new THREE.CanvasTexture(cv);
  tex.flipY = false;
  tex.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());
  if ('colorSpace' in tex) tex.colorSpace = THREE.SRGBColorSpace;

  // Terrain mesh.
  const pos = new Float32Array(nx * ny * 3), uv = new Float32Array(nx * ny * 2);
  let minH = 1e9;
  for (let iy = 0; iy < ny; iy++) for (let ix = 0; ix < nx; ix++) {
    const k = iy * nx + ix, h = heights[k];
    pos[k * 3] = gx + ix * cell; pos[k * 3 + 1] = h; pos[k * 3 + 2] = -(gy + iy * cell);
    uv[k * 2] = ix / (nx - 1); uv[k * 2 + 1] = iy / (ny - 1);
    if (h < minH) minH = h;
  }
  const idx = new Uint32Array((nx - 1) * (ny - 1) * 6);
  let q = 0;
  for (let iy = 0; iy < ny - 1; iy++) for (let ix = 0; ix < nx - 1; ix++) {
    const a = iy * nx + ix, b = a + 1, c = a + nx, d = c + 1;
    idx[q++] = a; idx[q++] = b; idx[q++] = c; idx[q++] = b; idx[q++] = d; idx[q++] = c;
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  geo.setIndex(new THREE.BufferAttribute(idx, 1));
  geo.computeVertexNormals();
  g.add(new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ map: tex })));

  // Surroundings: open sea for coastal courses, otherwise distant ground.
  const far = pal[10];
  const surroundColor = env === 2 ? 0x2f6f9c : (far[0] << 16) | (far[1] << 8) | far[2];
  const surround = new THREE.Mesh(new THREE.PlaneGeometry(6000, 6000), new THREE.MeshLambertMaterial({ color: surroundColor }));
  surround.rotation.x = -Math.PI / 2;
  surround.position.set(gx + nx * cell / 2, minH - (env === 2 ? 2.5 : 1.2), -(gy + ny * cell / 2));
  g.add(surround);

  // Trees (instanced).
  if (nTrees > 0) {
    const pine = env === 3 || env === 1;
    const canopyGeo = pine ? new THREE.ConeGeometry(1, 1, 7) : new THREE.IcosahedronGeometry(1, 1);
    const canopyColor = [0x3f7d3a, 0x3a6b3c, 0x4b7f45, 0x24552f, 0x77803f, 0x5d7a40][env];
    const canopy = new THREE.InstancedMesh(canopyGeo, new THREE.MeshLambertMaterial({ color: canopyColor, flatShading: true }), nTrees);
    const trunk = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.22, 0.3, 1, 6), new THREE.MeshLambertMaterial({ color: 0x5b4532 }), nTrees);
    const m = new THREE.Matrix4(), s = new THREE.Vector3(), p = new THREE.Vector3(), rot = new THREE.Quaternion();
    for (let t = 0; t < nTrees; t++) {
      const tx = trees[t * 5], ty = trees[t * 5 + 1], tz = trees[t * 5 + 2], r = trees[t * 5 + 3], h = trees[t * 5 + 4];
      rot.setFromAxisAngle(new THREE.Vector3(0, 1, 0), t * 1.7);
      if (pine) { p.set(tx, tz + h * 0.58, -ty); s.set(r, h * 0.84, r); }
      else { p.set(tx, tz + h * 0.66, -ty); s.set(r * 1.15, h * 0.36, r * 1.15); }
      canopy.setMatrixAt(t, m.compose(p, rot, s));
      p.set(tx, tz + h * 0.25, -ty); s.set(1, h * 0.5, 1);
      trunk.setMatrixAt(t, m.compose(p, rot, s));
    }
    g.add(canopy, trunk);
  }

  // Flag and cup.
  const pin = V(info[7], info[8], info[9]);
  const cup = new THREE.Mesh(new THREE.CircleGeometry(0.054, 20), new THREE.MeshBasicMaterial({ color: 0x0a0a0a }));
  cup.rotation.x = -Math.PI / 2; cup.position.copy(pin); cup.position.y += 0.012;
  const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 2.1, 6), new THREE.MeshLambertMaterial({ color: 0xf2f2e8 }));
  pole.position.copy(pin); pole.position.y += 1.05;
  const flagGeo = new THREE.BufferGeometry();
  flagGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array([0, 2.1, 0, 0, 1.72, 0, 0.55, 1.91, 0]), 3));
  const flag = new THREE.Mesh(flagGeo, new THREE.MeshBasicMaterial({ color: 0xd8342a, side: THREE.DoubleSide }));
  flag.position.copy(pin);
  world.flag = flag; world.pole = pole;
  g.add(cup, pole, flag);

  // Tee markers.
  for (const side of [-1, 1]) {
    const mk = new THREE.Mesh(new THREE.SphereGeometry(0.07, 10, 8), new THREE.MeshLambertMaterial({ color: 0x2d62c8 }));
    mk.position.copy(V(info[10] + side * 1.6, info[11], info[12] + 0.07));
    g.add(mk);
  }

  // Sky and fog follow the weather.
  const sky = SKY[weather] || SKY[0];
  const skyGeo = new THREE.SphereGeometry(2200, 24, 12);
  const colors = new Float32Array(skyGeo.attributes.position.count * 3);
  const top = new THREE.Color(sky[0]), hor = new THREE.Color(sky[1]), c = new THREE.Color();
  for (let i = 0; i < skyGeo.attributes.position.count; i++) {
    const t = Math.min(1, Math.max(0, skyGeo.attributes.position.getY(i) / 900));
    c.copy(hor).lerp(top, Math.pow(t, 0.6));
    colors[i * 3] = c.r; colors[i * 3 + 1] = c.g; colors[i * 3 + 2] = c.b;
  }
  skyGeo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  const skyMesh = new THREE.Mesh(skyGeo, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.BackSide, fog: false, depthWrite: false }));
  world.sky = skyMesh;
  g.add(skyMesh);
  scene.fog = new THREE.FogExp2(sky[1], sky[2]);
  scene.add(g);
  return info;
}

// Persistent objects: ball, shadow, tracer, aim aids, golfer.
const actors = {};
function initActors() {
  actors.ball = new THREE.Mesh(new THREE.SphereGeometry(0.0213, 16, 12), new THREE.MeshLambertMaterial({ color: 0xffffff }));
  actors.shadow = new THREE.Mesh(new THREE.CircleGeometry(0.03, 12), new THREE.MeshBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.35, depthWrite: false }));
  actors.shadow.rotation.x = -Math.PI / 2;
  actors.tracerGeo = new THREE.BufferGeometry();
  actors.tracerGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(3 * 4000), 3));
  actors.tracer = new THREE.Line(actors.tracerGeo, new THREE.LineBasicMaterial({ color: 0xffe27a, transparent: true, opacity: 0.9 }));
  actors.tracer.frustumCulled = false;
  actors.aimGeo = new THREE.BufferGeometry();
  actors.aimGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(3 * 64), 3));
  actors.aim = new THREE.Line(actors.aimGeo, new THREE.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.55 }));
  actors.aim.frustumCulled = false;
  actors.readGeo = new THREE.BufferGeometry();
  actors.readGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(3 * 300), 3));
  actors.read = new THREE.Line(actors.readGeo, new THREE.LineBasicMaterial({ color: 0x7fe0ff, transparent: true, opacity: 0.85 }));
  actors.read.frustumCulled = false;
  actors.ring = new THREE.Mesh(new THREE.RingGeometry(0.8, 1.0, 40), new THREE.MeshBasicMaterial({ color: 0xffe27a, transparent: true, opacity: 0.85, side: THREE.DoubleSide, depthWrite: false }));
  actors.ring.rotation.x = -Math.PI / 2;

  // A simple golfer: enough to read the swing, not a character model.
  const skin = new THREE.MeshLambertMaterial({ color: 0xd9a77c }), shirt = new THREE.MeshLambertMaterial({ color: 0xf0f0f0 }),
        trousers = new THREE.MeshLambertMaterial({ color: 0x2b3440 }), steel = new THREE.MeshLambertMaterial({ color: 0xb8bcc2 });
  const gf = new THREE.Group();
  const legs = new THREE.Mesh(new THREE.CylinderGeometry(0.17, 0.14, 0.9, 8), trousers); legs.position.y = 0.45;
  const torso = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.18, 0.62, 8), shirt); torso.position.y = 1.2; torso.rotation.z = -0.22; torso.position.x = 0.07;
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.11, 12, 10), skin); head.position.set(0.16, 1.66, 0);
  const cap = new THREE.Mesh(new THREE.SphereGeometry(0.115, 12, 6, 0, Math.PI * 2, 0, Math.PI / 2), trousers); cap.position.copy(head.position); cap.position.y += 0.02;
  gf.add(legs, torso, head, cap);
  actors.golfer = gf;
  actors.arm = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.04, 1, 6), shirt);
  actors.club = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 1, 6), steel);
  actors.head = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.05, 0.07), new THREE.MeshLambertMaterial({ color: 0x23262b }));
  for (const k of ['ball', 'shadow', 'tracer', 'aim', 'read', 'ring', 'golfer', 'arm', 'club', 'head']) { actors[k].visible = false; scene.add(actors[k]); }
}
const UP = new THREE.Vector3(0, 1, 0);
function stretch(mesh, a, b) { // place a unit cylinder between two points
  const d = new THREE.Vector3().subVectors(b, a), len = d.length();
  mesh.position.copy(a).addScaledVector(d, 0.5);
  mesh.scale.set(1, len, 1);
  mesh.quaternion.setFromUnitVectors(UP, d.normalize());
}
function poseGolfer(ball, aimDeg, swingAngle, visible) {
  for (const k of ['golfer', 'arm', 'club', 'head']) actors[k].visible = visible;
  if (!visible) return;
  const a = aimDeg * Math.PI / 180, side = settings.leftHanded ? -1 : 1;
  const aim = new THREE.Vector3(Math.sin(a), 0, -Math.cos(a));           // target direction
  const face = new THREE.Vector3(Math.cos(a), 0, Math.sin(a)).multiplyScalar(side); // golfer looks at the ball
  const feet = ball.clone().addScaledVector(face, -0.95);
  feet.y = heightAt(feet.x, -feet.z);
  actors.golfer.position.copy(feet);
  actors.golfer.rotation.y = Math.atan2(face.z, -face.x) + Math.PI; // local +X faces the ball
  const shoulders = feet.clone().addScaledVector(face, 0.2); shoulders.y += 1.4;
  const hands = feet.clone().addScaledVector(face, 0.42); hands.y += 0.88;
  // The club rotates about the line from the golfer to the ball: back and up, then through.
  const shaft = ball.clone().sub(hands).applyAxisAngle(face, swingAngle * side);
  const handsNow = hands.clone().addScaledVector(shaft, 0.12 * Math.min(1, Math.abs(swingAngle)));
  const tip = handsNow.clone().add(shaft);
  stretch(actors.arm, shoulders, handsNow);
  stretch(actors.club, handsNow, tip);
  actors.head.position.copy(tip);
  actors.head.quaternion.copy(actors.club.quaternion);
  void aim;
}

// ---------------------------------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------------------------------
const game = {
  mode: 'menu',            // menu | aim | swing | flight | result | holeout | done
  kind: 'round',           // round | range
  daily: null,             // daily id when playing the Daily Course
  holes: 0, hole: 0, info: null, course: null,
  card: [],                // finished holes
  bag: [], club: 0, shotType: 0, style: 0, curve: 0, traj: 0, aim: 0,
  state: null,             // latest ng_state
  swingAngle: 0, swingAnimT: -1,
  path: null, pathN: 0, playT: 0, outcome: null, landed: false, flightSpeed: 1,
  classic: null, assisted: null,
  camPos: new THREE.Vector3(), camLook: new THREE.Vector3(), camSnap: true, overhead: false,
  resultTimer: 0, puttRead: null, preview: null, previewDirty: true,
};

function readState() {
  core.ng_state();
  const b = buf(16);
  game.state = { x: b[0], y: b[1], z: b[2], lie: b[3], strokes: b[4], holed: b[5] > 0, toPin: b[6], putts: b[7], penalties: b[8], par: b[9],
                 fairwayApplies: b[10] > 0, fairwayHit: b[11] > 0, gir: b[12] > 0, drive: b[13], pinHeading: b[14], pinElev: b[15] };
  return game.state;
}
function ballVec() { const s = game.state; return V(s.x, s.y, s.z + 0.0213); }

function startCourse(kind, generate) {
  showOverlay('loading'); $('loadText').textContent = kind === 'range' ? 'Setting up the range…' : 'Designing and validating your course…';
  setTimeout(() => {
    game.kind = kind;
    pausedFrom = null;
    if (kind === 'range') { core.ng_use_range(1); game.holes = 1; game.course = { name: 'Practice Range', par: 0, env: 0, weather: 0, code: '' }; }
    else {
      core.ng_use_range(0);
      const n = generate();
      if (!n) { showMenu(mainMenu, 'That course could not be created. Check the code and try again.'); return; }
      core.ng_course_info();
      const c = buf(12);
      game.holes = n;
      game.course = { par: c[1], wind: c[2], temp: c[4], stimp: c[5], weather: c[6], env: c[8], diff: c[9], tee: c[10], length: c[11],
                      name: cstr(core.ng_course_name()), code: cstr(core.ng_share_code()), weatherName: cstr(core.ng_weather_name()) };
    }
    const nb = core.ng_bag();
    game.bag = Array.from(buf(nb));
    game.card = [];
    beginHole(0);
    hideOverlay();
  }, 30);
}

function beginHole(i) {
  game.hole = i;
  game.info = buildHole(i, game.course.env || 0, game.course.weather || 0);
  core.ng_start_hole(i);
  readState();
  game.overhead = false;
  setupShot(true);
  const par = game.info[0];
  if (game.kind === 'round') flashBanner('Hole ' + (i + 1), 'Par ' + par + ' · ' + dist(game.info[1]) + ' · ' + cstr(core.ng_hole_template(i)), 2600);
  else flashBanner('Practice Range', 'Hit any club. Targets are the fairway and the flag at ' + dist(330), 2600);
}

// Prepare for the next stroke: caddie picks a club, aim at the pin (or down the fairway).
function setupShot(snapCamera) {
  const s = readState();
  const rec = core.ng_recommend();
  game.club = rec; game.shotType = buf(1)[0]; game.style = 0; game.curve = 0; game.traj = 0;
  game.aim = s.pinHeading;
  if (game.kind === 'round' && s.lie !== SURF.Green) {
    // If the pin is out of reach, aim along the hole instead of over the trees.
    core.ng_preview(game.club, game.shotType, 0, 0, 0, game.aim, 1);
    const total = buf(8)[7];
    if (total < s.toPin * 0.92) game.aim = fairwayHeading(s, Math.max(60, total));
  }
  game.mode = 'aim';
  game.camSnap = !!snapCamera;
  game.swingAngle = 0; game.swingAnimT = -1;
  game.classic = null; game.assisted = null;
  game.previewDirty = true; game.puttRead = null;
  actors.tracer.visible = false;
  setMeter(0, 0, '');
  $('result').style.display = 'none';
  updateHud();
}
function fairwayHeading(s, reach) {
  // Aim at the centreline point `reach` metres further along than the ball.
  const n = game.info[25], pts = [];
  for (let k = 0; k < n; k++) pts.push([game.info[26 + 2 * k], game.info[27 + 2 * k]]);
  let best = 0, bestD = 1e9, acc = 0; const cum = [0];
  for (let k = 1; k < n; k++) cum.push(cum[k - 1] + Math.hypot(pts[k][0] - pts[k - 1][0], pts[k][1] - pts[k - 1][1]));
  for (let k = 1; k < n; k++) {
    const ax = pts[k - 1][0], ay = pts[k - 1][1], bx = pts[k][0] - ax, by = pts[k][1] - ay, l2 = bx * bx + by * by;
    const t = Math.min(1, Math.max(0, ((s.x - ax) * bx + (s.y - ay) * by) / l2));
    const d = Math.hypot(s.x - ax - bx * t, s.y - ay - by * t);
    if (d < bestD) { bestD = d; best = cum[k - 1] + t * Math.sqrt(l2); }
  }
  let target = Math.min(best + reach, cum[n - 1]), tx = pts[n - 1][0], ty = pts[n - 1][1];
  for (let k = 1; k < n; k++) if (target <= cum[k]) {
    const t = (target - cum[k - 1]) / (cum[k] - cum[k - 1]);
    tx = pts[k - 1][0] + (pts[k][0] - pts[k - 1][0]) * t; ty = pts[k - 1][1] + (pts[k][1] - pts[k - 1][1]) * t; break;
  }
  void acc;
  return Math.atan2(tx - s.x, ty - s.y) * 180 / Math.PI;
}

function cycleClub(dir) {
  if (game.mode !== 'aim') return;
  const order = game.bag;                       // driver ... putter
  let i = order.indexOf(game.club);
  i = Math.min(order.length - 1, Math.max(0, i + dir));
  game.club = order[i];
  if (game.club !== PUTTER && !core.ng_shot_allowed(game.shotType, game.club)) game.shotType = 0;
  game.previewDirty = true; sfx.ui(); updateHud();
}
function cycleShotType(dir) {
  if (game.mode !== 'aim' || game.club === PUTTER) return;
  let t = game.shotType;
  for (let k = 0; k < SHOT_COUNT; k++) {
    t = (t + dir + SHOT_COUNT) % SHOT_COUNT;
    if (core.ng_shot_allowed(t, game.club)) break;
  }
  game.shotType = t; game.previewDirty = true; sfx.ui(); updateHud();
}
function nudge(what, d) {
  if (game.mode !== 'aim') return;
  if (what === 'curve') game.curve = Math.round(Math.min(1, Math.max(-1, game.curve + d)) * 4) / 4;
  if (what === 'traj') game.traj = Math.round(Math.min(1, Math.max(-1, game.traj + d)) * 4) / 4;
  if (what === 'style') game.style = (game.style + 1) % 4;
  if (what === 'reset') { game.curve = 0; game.traj = 0; game.style = 0; game.shotType = 0; }
  game.previewDirty = true; sfx.ui(); updateHud();
}

// ---------------------------------------------------------------------------------------------------
// Swing input
// ---------------------------------------------------------------------------------------------------
let pointerLocked = false, buttonDown = false, lockFailed = false, lockChangedAt = -1;
function now() { return performance.now() / 1000; }

function armSwing() {
  core.ng_swing_begin(settings.swingDifficulty, settings.sensitivity, settings.swingLength, settings.downswingTime, settings.leftHanded ? 1 : 0);
  game.mode = 'swing';
  game.swingAngle = 0;
}
function disarmSwing() { if (game.mode === 'swing') { game.mode = 'aim'; game.swingAngle = 0; game.classic = null; game.assisted = null; setMeter(0, 0, ''); } }

function onMove(e) {
  // Browsers can report one huge bogus movement as the pointer is captured or released; ignore that moment.
  if (now() - lockChangedAt < 0.12) return;
  if (game.mode === 'aim' && !buttonDown && pointerLocked) {
    game.aim += e.movementX * (e.shiftKey ? 0.012 : 0.05);
    game.previewDirty = true;
    return;
  }
  if (game.mode !== 'swing' || settings.swingMode !== 'natural') return;
  // Use every coalesced sample the browser has (up to the mouse's own report rate), each with its own timestamp.
  const events = e.getCoalescedEvents ? e.getCoalescedEvents() : null;
  const list = events && events.length ? events : [e];
  let phase = 0;
  for (const ev of list) phase = core.ng_swing_sample(ev.timeStamp / 1000, ev.movementX || 0, ev.movementY || 0);
  swingPhaseChanged(phase);
}
function swingPhaseChanged(phase) {
  if (game.mode !== 'swing') return;
  if (phase === 3) strike(() => core.ng_strike(game.club, game.shotType, game.style, game.curve, game.traj, game.aim));
  else if (phase === 4) { armSwing(); setMeter(0, 0, 'Backed off. Pull back again when ready.'); }
}

function onDown(e) {
  if (e.button !== 0 || game.mode === 'menu') return;
  ensureAudio();
  if (audio && audio.state === 'suspended') audio.resume();
  if (game.mode === 'result') { continueAfterShot(); return; }
  if (game.mode === 'flight') { game.flightSpeed = 4; return; }
  if (game.mode !== 'aim') return;
  if (!pointerLocked && canvas.requestPointerLock && !lockFailed) {
    // The first click only captures the mouse, so capture can never be mistaken for part of a swing.
    const plain = () => { try { const q = canvas.requestPointerLock(); if (q && q.catch) q.catch(() => { lockFailed = true; }); } catch (err) { lockFailed = true; } };
    try {
      const p = canvas.requestPointerLock({ unadjustedMovement: true }); // raw counts where the browser allows
      if (p && p.catch) p.catch(plain);
    } catch (err) { plain(); }
    return;
  }
  buttonDown = true;
  if (settings.swingMode === 'natural') armSwing();
  else if (settings.swingMode === 'classic') {
    if (!game.classic) game.classic = { stage: 1, t0: now(), power: 0 };
  } else game.assisted = { t0: now() };
  if (settings.swingMode !== 'natural') game.mode = 'swing';
}
function onUp(e) {
  if (e.button !== 0) return;
  buttonDown = false;
  if (game.mode !== 'swing') return;
  if (settings.swingMode === 'natural') disarmSwing();        // letting go before impact aborts safely
  else if (settings.swingMode === 'assisted' && game.assisted) {
    const power = assistedPower();
    game.assisted = null;
    if (power < 0.08) { game.mode = 'aim'; setMeter(0, 0, ''); return; }
    strike(() => core.ng_strike_assisted(power, game.club, game.shotType, game.style, game.curve, game.traj, game.aim), true);
  }
}
function classicClick() {
  const c = game.classic;
  if (!c) return;
  if (c.stage === 1) { c.power = classicPower(); c.stage = 2; c.t1 = now(); }
  else if (c.stage === 2) {
    const acc = classicAccuracy();
    const power = c.power;
    game.classic = null;
    if (power < 0.08) { game.mode = 'aim'; setMeter(0, 0, ''); return; }
    strike(() => core.ng_strike_classic(power, acc, game.club, game.shotType, game.style, game.curve, game.traj, game.aim), true);
  }
}
function classicPower() { const t = (now() - game.classic.t0) / 1.1; return Math.min(1.1, t <= 1.1 ? t : 2.2 - t); }
function classicAccuracy() { const t = (now() - game.classic.t1) / 0.55; return Math.max(-1, Math.min(1, (t - 1) * 2.2)); } // 0 at the line
function assistedPower() { const t = (now() - game.assisted.t0) / 1.3; return Math.max(0, Math.min(1, t <= 1 ? t : 2 - t)); }

// Run a strike call into the core, then start playing the result back.
function strike(call, initSettings) {
  if (initSettings) core.ng_swing_begin(settings.swingDifficulty, settings.sensitivity, settings.swingLength, settings.downswingTime, settings.leftHanded ? 1 : 0);
  const ok = call();
  if (!ok) { game.mode = 'aim'; return; }
  core.ng_outcome();
  const o = Array.from(buf(32));
  game.outcome = { carry: o[0], total: o[1], apex: o[2], landAngle: o[3], lateral: o[4], curve: o[5], reason: o[6], surface: o[7], bounces: o[8],
                   ballSpeed: o[9], launch: o[10], azimuth: o[11], spin: o[12], axis: o[13], headSpeed: o[14], power: o[15], path: o[16], face: o[17],
                   timing: o[18], tempo: o[19], contact: o[20], quality: o[21], n: o[22], landing: [o[23], o[24], o[25]], putt: o[26] > 0,
                   tree: o[27] > 0, time: o[28], overswing: o[29],
                   shape: cstr(core.ng_shape_name()), feedback: cstr(core.ng_feedback()).split('\n'), club: game.club, from: Object.assign({}, game.state) };
  game.pathN = game.outcome.n;
  game.path = new Float64Array(new Float64Array(core.memory.buffer, core.ng_path(), game.pathN * 5)); // copy out of wasm memory
  game.playT = 0; game.landed = false; game.flightSpeed = 1; game.treeSounded = false;
  game.mode = 'flight';
  game.swingAnimT = 0;
  actors.tracer.visible = settings.tracer && !game.outcome.putt;
  actors.tracerGeo.setDrawRange(0, 0);
  sfx.impact(game.club, game.outcome.quality);
  setMeter(game.outcome.power, game.outcome.path / 10, tempoLabel(game.outcome));
  updateHud();
}
function tempoLabel(o) {
  if (o.timing < -0.01) return 'Tempo: early (' + Math.round(-o.timing * 100) + '% fast)';
  if (o.timing > 0.01) return 'Tempo: late (' + Math.round(o.timing * 100) + '% slow)';
  return 'Tempo: perfect';
}

function pathAt(t, out) {
  const P = game.path, n = game.pathN;
  let i = game.pathIdx || 0;
  if (i >= n - 1 || P[i * 5] > t) i = 0;
  while (i < n - 2 && P[(i + 1) * 5] < t) i++;
  game.pathIdx = i;
  const t0 = P[i * 5], t1 = P[(i + 1) * 5], u = t1 > t0 ? Math.min(1, Math.max(0, (t - t0) / (t1 - t0))) : 1;
  out.x = P[i * 5 + 1] + (P[(i + 1) * 5 + 1] - P[i * 5 + 1]) * u;
  out.y = P[i * 5 + 2] + (P[(i + 1) * 5 + 2] - P[i * 5 + 2]) * u;
  out.z = P[i * 5 + 3] + (P[(i + 1) * 5 + 3] - P[i * 5 + 3]) * u;
  out.air = P[i * 5 + 4] > 0.5;
  return out;
}

function finishFlight() {
  const o = game.outcome, before = o.from;
  const s = readState();
  game.mode = 'result';
  game.resultTimer = 0;
  if (o.reason === 3) sfx.cup();
  // Result panel.
  let title = o.putt ? (s.holed ? 'Holed!' : 'Putt') : o.shape;
  let nums = o.putt ? shortDist(o.total) + ' rolled' : 'Carry ' + dist(o.carry) + ' · Total ' + dist(o.total);
  let extra = '';
  if (o.reason === 1) extra = '<div style="color:var(--bad)">In the water. One-stroke penalty; dropped where it crossed.</div>';
  if (o.reason === 2) extra = '<div style="color:var(--bad)">Out of bounds. One-stroke penalty; replay from the same spot.</div>';
  if (o.tree) extra += '<div style="color:var(--bad)">Hit a tree.</div>';
  if (!s.holed && o.reason === 0) extra += '<div>' + ['Tee', 'Fairway', 'First cut', 'Rough', 'Deep rough', 'Bunker', 'On the green', 'Fringe', 'Hard ground'][s.lie] +
    ' · ' + shortDist(s.toPin) + ' to the pin</div>';
  const why = o.feedback.map((f) => '<li>' + f + '</li>').join('');
  const adv = o.putt ? 'Pace ' + o.ballSpeed.toFixed(2) + ' m/s · face ' + o.azimuth.toFixed(1) + '°'
    : 'Ball ' + Math.round(o.ballSpeed * 2.23694) + ' mph · launch ' + o.launch.toFixed(1) + '° · spin ' + Math.round(o.spin) + ' rpm · apex ' + dist(o.apex) +
      '<br>Path ' + o.path.toFixed(1) + '° · face ' + o.face.toFixed(1) + '° · tempo ' + o.tempo.toFixed(1) + ':1 · power ' + Math.round(o.power * 100) + '%';
  const r = $('result');
  r.innerHTML = '<h2>' + title + '</h2><div class="nums">' + nums + '</div>' + extra + '<ul class="why">' + why + '</ul><div class="adv">' + adv +
    '</div><div class="adv" style="border:0">Click or press Space to continue</div>';
  r.style.display = 'block';
  void before;
  updateHud();
}

function continueAfterShot() {
  if (game.mode !== 'result') return;
  $('result').style.display = 'none';
  const s = game.state;
  if (game.kind === 'range') { core.ng_start_hole(0); readState(); const keep = [game.club, game.shotType, game.style, game.curve, game.traj, game.aim];
    setupShot(false); [game.club, game.shotType, game.style, game.curve, game.traj, game.aim] = keep; game.previewDirty = true; updateHud(); return; }
  const maxStrokes = 10;
  if (s.holed || s.strokes >= maxStrokes) { holeFinished(s.holed); return; }
  setupShot(false);
}

function holeFinished(holed) {
  const s = game.state;
  const strokes = holed ? s.strokes : Math.max(s.strokes, 10);
  game.card.push({ par: s.par, strokes, putts: s.putts, penalties: s.penalties, fairwayApplies: s.fairwayApplies, fairwayHit: s.fairwayHit, gir: s.gir, holed });
  game.mode = 'holeout';
  const name = holed ? cstr(core.ng_score_name(strokes, s.par)) : 'Picked up';
  flashBanner(name, holed ? strokes + ' on a par ' + s.par : 'Stroke limit reached: 10 recorded', 2400);
  setTimeout(() => {
    if (game.mode !== 'holeout') return;
    if (game.hole + 1 < game.holes) showMenu(betweenHolesMenu);
    else finishRound();
  }, 2500);
}

function totals() {
  const t = { strokes: 0, par: 0, putts: 0, fir: 0, firN: 0, gir: 0, birdies: 0, eagles: 0, pars: 0, penalties: 0 };
  for (const h of game.card) {
    t.strokes += h.strokes; t.par += h.par; t.putts += h.putts; t.penalties += h.penalties;
    if (h.fairwayApplies) { t.firN++; if (h.fairwayHit) t.fir++; }
    if (h.gir) t.gir++;
    const d = h.strokes - h.par;
    if (h.holed && d === -1) t.birdies++; if (h.holed && d <= -2) t.eagles++; if (h.holed && d === 0) t.pars++;
  }
  return t;
}

function finishRound() {
  game.mode = 'done';
  const t = totals();
  const rounds = store.get('rounds', []);
  const entry = { date: new Date().toISOString(), course: game.course.name, code: game.course.code, strokes: t.strokes, par: t.par, putts: t.putts,
                  fir: t.fir, firN: t.firN, gir: t.gir, holes: game.card.length, daily: game.daily || null, official: false,
                  complete: game.card.every((h) => h.holed) };
  if (game.daily) entry.official = !rounds.some((r) => r.daily === game.daily && r.official);
  rounds.push(entry);
  store.set('rounds', rounds.slice(-300));
  showMenu(() => roundSummaryMenu(entry));
}

// ---------------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------------
function setMeter(power, lateral, text) {
  $('meterFill').style.width = Math.min(100, power * 100 / 1.1) + '%';
  $('meterMark').style.left = (100 / 1.1) + '%';
  $('pathDot').style.left = (50 + Math.max(-1, Math.min(1, lateral)) * 46) + '%';
  if (text !== null && text !== undefined) $('meterText').textContent = text;
}
function shapeText() {
  const c = game.curve, t = game.traj, parts = [];
  if (c !== 0) parts.push((c < 0 ? 'Draw ' : 'Fade ') + Math.round(Math.abs(c) * 100) + '%');
  if (t !== 0) parts.push((t < 0 ? 'Low ' : 'High ') + Math.round(Math.abs(t) * 100) + '%');
  if (game.style !== 0) parts.push(STYLES[game.style]);
  return parts.length ? parts.join(' · ') : 'Straight · Full';
}
function updateHud() {
  const inPlay = game.mode !== 'menu';
  for (const id of ['hudHole', 'hudWind', 'hudShot', 'hudHelp', 'meter']) $(id).style.display = inPlay ? 'block' : 'none';
  if (!inPlay || !game.state) return;
  const s = game.state, t = totals();
  if (game.kind === 'round') {
    $('hHole').textContent = 'Hole ' + (game.hole + 1) + ' · Par ' + s.par;
    $('hScore').textContent = 'Stroke ' + (s.strokes + (s.holed ? 0 : 1)) + ' · Round ' + toPar(t.strokes - t.par) + (game.card.length ? ' thru ' + game.card.length : '');
    $('hCourse').textContent = game.course.name + (game.daily ? ' · Daily ' + game.daily : '') + ' · ' + game.course.weatherName;
  } else {
    $('hHole').textContent = 'Practice Range'; $('hScore').textContent = 'No scoring'; $('hCourse').textContent = 'Calm, level ground';
  }
  const windSpeed = game.info[22], windTo = game.info[21];
  $('hWind').textContent = windSpeed < 0.3 ? 'Calm' : windText(windSpeed);
  $('windArrow').style.transform = 'rotate(' + (windTo - game.aim) + 'deg)';
  $('windArrow').style.opacity = windSpeed < 0.3 ? 0.25 : 1;
  $('hClub').textContent = cstr(core.ng_club_name(game.club));
  const elev = s.pinElev;
  $('hDist').textContent = shortDist(s.toPin) + (Math.abs(elev) >= 1 ? (elev > 0 ? ' ▲' : ' ▼') + shortDist(Math.abs(elev)) : '');
  $('hLie').textContent = cstr(core.ng_lie_name());
  $('hType').textContent = game.club === PUTTER ? 'Putt' : cstr(core.ng_shot_type_name(game.shotType));
  $('hShape').textContent = game.club === PUTTER ? '—' : shapeText();
  const capture = !pointerLocked && canvas.requestPointerLock && !lockFailed ? '<b style="color:var(--accent)">Click once to capture the mouse</b><br>' : '';
  const modeHelp = capture + (settings.swingMode === 'natural'
    ? '<kbd>Hold left mouse</kbd> pull back, then push through'
    : settings.swingMode === 'classic' ? '<kbd>Click</kbd> start · <kbd>click</kbd> power · <kbd>click</kbd> on the line' : '<kbd>Hold</kbd> to fill power · release to hit');
  $('hudHelp').innerHTML = modeHelp + '<br>Aim <kbd>mouse</kbd>/<kbd>A</kbd><kbd>D</kbd> · Club <kbd>W</kbd><kbd>S</kbd> · Shot <kbd>Q</kbd><kbd>E</kbd><br>' +
    'Draw/Fade <kbd>Z</kbd><kbd>X</kbd> · Low/High <kbd>C</kbd><kbd>V</kbd> · Style <kbd>F</kbd> · Reset <kbd>R</kbd><br>Map <kbd>M</kbd> · Card <kbd>Tab</kbd> · Menu <kbd>Esc</kbd>';
  $('fps').style.display = settings.showFps ? 'block' : 'none';
}
let bannerTimer = 0;
function flashBanner(title, sub, ms) {
  const b = $('banner');
  b.innerHTML = title + (sub ? '<small>' + sub + '</small>' : '');
  b.style.display = 'block';
  clearTimeout(bannerTimer);
  bannerTimer = setTimeout(() => { b.style.display = 'none'; }, ms || 2000);
}

// ---------------------------------------------------------------------------------------------------
// Menus (every button here does something)
// ---------------------------------------------------------------------------------------------------
function showOverlay(which) { $('loading').classList.toggle('show', which === 'loading'); $('ui').classList.toggle('show', which === 'ui'); }
function hideOverlay() { $('loading').classList.remove('show'); $('ui').classList.remove('show'); }
let currentMenu = null;
function showMenu(builder, error) {
  if (document.pointerLockElement) document.exitPointerLock();
  currentMenu = builder;
  const body = $('uiBody');
  body.innerHTML = '';
  builder(body, error);
  showOverlay('ui');
  const first = body.querySelector('button');
  if (first) first.focus();
}
function el(tag, props, html) { const e = document.createElement(tag); Object.assign(e, props || {}); if (html !== undefined) e.innerHTML = html; return e; }
function button(parent, label, sub, fn, cls) {
  const b = el('button', { className: cls || '' }, label + (sub ? '<small>' + sub + '</small>' : ''));
  b.addEventListener('click', () => { ensureAudio(); sfx.ui(); fn(); });
  parent.appendChild(b);
  return b;
}
function select(parent, label, options, value, onChange) {
  const l = el('label', {}, '<span>' + label + '</span>');
  const s = el('select');
  options.forEach((o, i) => s.appendChild(el('option', { value: String(o.value !== undefined ? o.value : i), textContent: o.label || o })));
  s.value = String(value);
  s.addEventListener('change', () => onChange(s.value));
  l.appendChild(s); parent.appendChild(l);
  return s;
}
function slider(parent, label, min, max, step, value, fmt, onChange) {
  const l = el('label', {}, '<span>' + label + ' <b></b></span>');
  const r = el('input', { type: 'range', min, max, step, value });
  const out = l.querySelector('b');
  const show = () => { out.textContent = fmt(Number(r.value)); };
  r.addEventListener('input', () => { onChange(Number(r.value)); show(); });
  show(); l.appendChild(r); parent.appendChild(l);
}

function goMenu() { pausedFrom = null; game.mode = 'menu'; clearWorld(); for (const k in actors) if (actors[k].visible !== undefined) actors[k].visible = false; updateHud(); showMenu(mainMenu); }

function mainMenu(body, error) {
  game.mode = 'menu'; updateHud();
  body.appendChild(el('h1', {}, 'Next-Gen Golf'));
  body.appendChild(el('p', { className: 'sub' }, 'Browser preview build · mouse swing · procedural courses'));
  if (error) body.appendChild(el('div', { className: 'err' }, error));
  button(body, 'Random 9', 'A brand-new, validated nine-hole course every time', () => showMenu(randomMenu));
  const unix = Date.now() / 1000, id = cstr(core.ng_daily_id(unix)), left = core.ng_seconds_to_next_daily(unix);
  const official = store.get('rounds', []).find((r) => r.daily === id && r.official);
  button(body, 'Daily Course · ' + id, (official ? 'Official score ' + official.strokes + ' (' + toPar(official.strokes - official.par) + '). Further rounds are practice. ' : 'Your first finished round today is your official score. ') +
    'Next course in ' + Math.floor(left / 3600) + 'h ' + Math.floor(left % 3600 / 60) + 'm',
    () => { game.daily = id; startCourse('round', () => core.ng_generate_daily(unix)); });
  button(body, 'Play a shared course', 'Enter a course code from a friend', () => showMenu(codeMenu));
  button(body, 'Practice Range', 'Learn the swing with every club; no score', () => { game.daily = null; startCourse('range'); });
  button(body, 'Statistics', 'Your saved rounds on this device', () => showMenu(statsMenu));
  button(body, 'Settings', 'Swing mode, difficulty, sensitivity, units, graphics', () => showMenu((b) => settingsMenu(b, mainMenu)));
  button(body, 'How to play', '', () => showMenu(helpMenu));
}

const quick = Object.assign({ env: -1, diff: 1, tee: 1 }, store.get('quick', {}));
function randomMenu(body) {
  body.appendChild(el('h2', {}, 'Random 9'));
  const envs = [{ value: -1, label: 'Surprise me' }];
  for (let i = 0; i < 6; i++) envs.push({ value: i, label: cstr(core.ng_environment_name(i)) });
  select(body, 'Environment', envs, quick.env, (v) => { quick.env = Number(v); });
  select(body, 'Course difficulty', COURSE_DIFF, quick.diff, (v) => { quick.diff = Number(v); });
  select(body, 'Tees', TEES, quick.tee, (v) => { quick.tee = Number(v); });
  body.appendChild(el('p', { className: 'note' }, 'Course difficulty changes the architecture (fairway width, hazards, green speed). Swing difficulty is separate, under Settings.'));
  button(body, 'Generate and play', '', () => {
    store.set('quick', quick);
    const r = new Uint32Array(2);
    if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(r); else { r[0] = Math.random() * 4294967296; r[1] = Math.random() * 4294967296; }
    const env = quick.env < 0 ? r[0] % 6 : quick.env;
    game.daily = null;
    startCourse('round', () => core.ng_generate(r[0], r[1], env, quick.diff, quick.tee, 1, 1));
  }, 'primary');
  button(body, 'Back', '', () => showMenu(mainMenu));
}
function codeMenu(body, error) {
  body.appendChild(el('h2', {}, 'Play a shared course'));
  const input = el('input', { type: 'text', placeholder: 'R9-XXXX-XXXX-XXXX-XXXX', maxLength: 40, style: 'width:100%' });
  body.appendChild(input);
  const err = el('div', { className: 'err' }, error || ''); body.appendChild(err);
  const go = () => {
    const code = input.value.trim();
    if (!code) { err.textContent = 'Enter a code first.'; return; }
    const bytes = new Uint8Array(core.memory.buffer, core.ng_strbuf(), 512);
    for (let i = 0; i < 511; i++) bytes[i] = i < code.length ? code.charCodeAt(i) & 127 : 0;
    game.daily = null;
    startCourse('round', () => core.ng_generate_code());
  };
  input.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Enter') go(); });
  button(body, 'Play', '', go, 'primary');
  button(body, 'Back', '', () => showMenu(mainMenu));
  setTimeout(() => input.focus(), 0);
}
function settingsMenu(body, back) {
  body.appendChild(el('h2', {}, 'Settings'));
  select(body, 'Swing mode', [{ value: 'natural', label: 'Natural mouse swing' }, { value: 'classic', label: 'Classic 3-click' }, { value: 'assisted', label: 'Assisted (hold and release)' }],
    settings.swingMode, (v) => { settings.swingMode = v; saveSettings(); });
  select(body, 'Swing difficulty', SWING_DIFF, settings.swingDifficulty, (v) => { settings.swingDifficulty = Number(v); saveSettings(); });
  slider(body, 'Mouse sensitivity', 0.3, 3, 0.05, settings.sensitivity, (v) => v.toFixed(2) + '×', (v) => { settings.sensitivity = v; saveSettings(); });
  slider(body, 'Backswing length', 150, 900, 10, settings.swingLength, (v) => v + ' counts', (v) => { settings.swingLength = v; saveSettings(); });
  slider(body, 'Downswing tempo', 0.16, 0.5, 0.01, settings.downswingTime, (v) => v.toFixed(2) + ' s', (v) => { settings.downswingTime = v; saveSettings(); });
  select(body, 'Handedness', ['Right-handed', 'Left-handed'], settings.leftHanded ? 1 : 0, (v) => { settings.leftHanded = v === '1'; saveSettings(); });
  select(body, 'Units', [{ value: 'yd', label: 'Yards / feet / mph' }, { value: 'm', label: 'Metres / km/h' }], settings.units, (v) => { settings.units = v; saveSettings(); });
  select(body, 'Graphics', ['Performance', 'Balanced', 'Quality'], settings.quality, (v) => { settings.quality = Number(v); saveSettings(); applyQuality(); resize(); });
  slider(body, 'Sound volume', 0, 1, 0.05, settings.volume, (v) => Math.round(v * 100) + '%', (v) => { settings.volume = v; saveSettings(); });
  select(body, 'Shot tracer', ['Off', 'On'], settings.tracer ? 1 : 0, (v) => { settings.tracer = v === '1'; saveSettings(); });
  select(body, 'Frame-time display', ['Off', 'On'], settings.showFps ? 1 : 0, (v) => { settings.showFps = v === '1'; saveSettings(); });
  body.appendChild(el('p', { className: 'note' }, 'A full backswing is the distance your mouse travels for 100% power. Shorten it if you run out of desk; lengthen it for finer control.'));
  button(body, 'Done', '', () => { if (back === 'resume') resume(); else showMenu(back); }, 'primary');
}
function helpMenu(body) {
  body.appendChild(el('h2', {}, 'How to play'));
  body.appendChild(el('div', {}, '<p><b>Swing.</b> Hold the left mouse button, pull the mouse toward you for the backswing, then push it away through the ball. ' +
    'How far you pull back sets power. Let go of the button before impact to back off.</p>' +
    '<p><b>Tempo.</b> A smooth 3:1 rhythm (slow back, quick through) is ideal. Rushing closes the face (ball goes left for a right-hander); a lazy downswing leaves it open and loses distance.</p>' +
    '<p><b>Path.</b> Push straight. Drifting right on the way through swings in-to-out (push-draw); drifting left swings out-to-in (pull-fade).</p>' +
    '<p><b>Contact.</b> Stop short of the ball and you hit it thin; slow down into it and you hit it fat. Accelerate through.</p>' +
    '<p><b>Shaping.</b> <kbd>Z</kbd>/<kbd>X</kbd> dial in a draw or fade, <kbd>C</kbd>/<kbd>V</kbd> lower or raise the flight, <kbd>Q</kbd>/<kbd>E</kbd> choose punch, pitch, chip, flop and more.</p>' +
    '<p><b>Putting.</b> Same motion, much smaller. The blue line (on easier difficulties) shows the path of a correctly paced putt.</p>'));
  button(body, 'Back', '', () => showMenu(mainMenu), 'primary');
}
function scoreTable() {
  const cells = (label, f) => '<tr><th>' + label + '</th>' + Array.from({ length: game.holes }, (_, i) => f(i)).join('') + '</tr>';
  let pars = 0, strokes = 0;
  const html = '<table class="sc">' + cells('Hole', (i) => '<th>' + (i + 1) + '</th>') +
    cells('Par', (i) => { const p = i < game.card.length ? game.card[i].par : (i === game.hole && game.state ? game.state.par : ''); if (p) pars += p; return '<td>' + p + '</td>'; }) +
    cells('Score', (i) => { const h = game.card[i]; if (!h) return '<td></td>'; strokes += h.strokes; const d = h.strokes - h.par;
      return '<td class="' + (d < 0 ? 'under' : d > 0 ? 'over' : '') + '">' + h.strokes + '</td>'; }) + '</table>';
  return html;
}
function statBlock(t) {
  return '<div class="stats"><div>Score <b>' + t.strokes + ' (' + toPar(t.strokes - t.par) + ')</b></div><div>Putts <b>' + t.putts + '</b></div>' +
    '<div>Fairways hit <b>' + t.fir + ' / ' + t.firN + '</b></div><div>Greens in regulation <b>' + t.gir + ' / ' + game.card.length + '</b></div>' +
    '<div>Birdies or better <b>' + (t.birdies + t.eagles) + '</b></div><div>Penalty strokes <b>' + t.penalties + '</b></div></div>';
}
function betweenHolesMenu(body) {
  const last = game.card[game.card.length - 1];
  body.appendChild(el('h2', {}, 'Hole ' + game.card.length + ': ' + (last.holed ? cstr(core.ng_score_name(last.strokes, last.par)) : 'Picked up')));
  body.appendChild(el('div', {}, scoreTable() + statBlock(totals())));
  button(body, 'Next hole', '', () => { hideOverlay(); beginHole(game.hole + 1); }, 'primary');
  button(body, 'Quit to menu', 'This round will not be saved', goMenu);
}
function roundSummaryMenu(body, entry) {
  const t = totals();
  body.appendChild(el('h2', {}, game.course.name + ' · ' + t.strokes + ' (' + toPar(t.strokes - t.par) + ')'));
  body.appendChild(el('div', {}, scoreTable() + statBlock(t)));
  if (entry.daily) body.appendChild(el('p', { className: 'note' }, entry.official ? 'Saved as your official Daily Course score for ' + entry.daily + '.' : 'Practice round: your official score for today was already recorded.'));
  const row = el('p', {}, 'Course code: <b style="user-select:all">' + game.course.code + '</b>');
  body.appendChild(row);
  const copy = button(body, 'Copy course code', 'Send it to a friend so they can play the same course', () => {
    const done = (ok) => { copy.querySelector('small').textContent = ok ? 'Copied.' : 'Select the code above and copy it manually.'; };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(game.course.code).then(() => done(true), () => done(false)); else done(false);
  });
  body.appendChild(el('p', { className: 'note' }, 'Scores are stored on this device only. There is no online leaderboard yet.'));
  button(body, 'Back to menu', '', goMenu, 'primary');
}
function statsMenu(body) {
  body.appendChild(el('h2', {}, 'Statistics'));
  const rounds = store.get('rounds', []);
  if (!rounds.length) body.appendChild(el('p', { className: 'sub' }, 'No finished rounds yet.'));
  else {
    const full = rounds.filter((r) => r.complete !== false);
    const avg = (f) => full.length ? (full.reduce((a, r) => a + f(r), 0) / full.length) : 0;
    const best = full.reduce((b, r) => (!b || r.strokes - r.par < b.strokes - b.par ? r : b), null);
    const firN = full.reduce((a, r) => a + r.firN, 0), fir = full.reduce((a, r) => a + r.fir, 0), girN = full.reduce((a, r) => a + r.holes, 0), gir = full.reduce((a, r) => a + r.gir, 0);
    const dailies = rounds.filter((r) => r.official).map((r) => r.daily).sort();
    let streak = 0;
    for (let d = new Date(); ; d = new Date(d.getTime() - 86400000)) {
      if (dailies.indexOf(d.toISOString().slice(0, 10)) >= 0) streak++; else if (streak > 0 || d.toISOString().slice(0, 10) !== new Date().toISOString().slice(0, 10)) break;
      if (streak > 400) break;
    }
    body.appendChild(el('div', { className: 'stats' },
      '<div>Rounds finished <b>' + rounds.length + '</b></div><div>Average to par <b>' + (full.length ? toPar(Math.round(avg((r) => r.strokes - r.par) * 10) / 10) : '—') + '</b></div>' +
      '<div>Best round <b>' + (best ? best.strokes + ' (' + toPar(best.strokes - best.par) + ')' : '—') + '</b></div><div>Putts per round <b>' + (full.length ? avg((r) => r.putts).toFixed(1) : '—') + '</b></div>' +
      '<div>Fairways hit <b>' + (firN ? Math.round(100 * fir / firN) + '%' : '—') + '</b></div><div>Greens in regulation <b>' + (girN ? Math.round(100 * gir / girN) + '%' : '—') + '</b></div>' +
      '<div>Daily streak <b>' + streak + '</b></div><div>Daily rounds <b>' + dailies.length + '</b></div>'));
    const list = rounds.slice(-8).reverse().map((r) => '<tr><td style="text-align:left;padding-left:8px">' + r.date.slice(0, 10) + ' · ' + r.course + (r.daily ? ' (Daily)' : '') +
      '</td><td>' + r.strokes + '</td><td>' + toPar(r.strokes - r.par) + '</td></tr>').join('');
    body.appendChild(el('div', {}, '<table class="sc"><tr><th>Recent rounds</th><th>Score</th><th>To par</th></tr>' + list + '</table>'));
  }
  body.appendChild(el('p', { className: 'note' }, 'Local to this browser. Nothing is uploaded.'));
  button(body, 'Back', '', () => showMenu(mainMenu), 'primary');
}
function pauseMenu(body) {
  body.appendChild(el('h2', {}, 'Paused'));
  if (game.kind === 'round') body.appendChild(el('div', {}, scoreTable() + (game.card.length ? statBlock(totals()) : '')));
  button(body, 'Resume', '', resume, 'primary');
  button(body, 'Settings', '', () => showMenu((b) => settingsMenu(b, 'resume')));
  button(body, 'Quit to menu', game.kind === 'round' ? 'This round will not be saved' : '', goMenu);
}
let pausedFrom = null;
function pause() { if (game.mode === 'menu' || pausedFrom) return; if (game.mode === 'swing') disarmSwing(); pausedFrom = game.mode; showMenu(pauseMenu); }
function resume() { hideOverlay(); pausedFrom = null; updateHud(); game.previewDirty = true; }
function paused() { return pausedFrom !== null; }

// ---------------------------------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------------------------------
const tmp = { x: 0, y: 0, z: 0, air: false };
const keys = {};
let lastFrame = 0, fpsAcc = 0, fpsN = 0, fpsWorst = 0;

function refreshAids() {
  game.previewDirty = false;
  const s = game.state, d = settings.swingDifficulty;
  actors.ring.visible = false; actors.read.visible = false;
  // Aim line draped over the ground.
  const a = game.aim * Math.PI / 180, len = game.club === PUTTER ? Math.min(12, s.toPin * 1.2 + 1) : 40;
  const pos = actors.aimGeo.attributes.position, n = 32;
  for (let i = 0; i < n; i++) {
    const r = 0.3 + (len - 0.3) * i / (n - 1), x = s.x + Math.sin(a) * r, y = s.y + Math.cos(a) * r;
    pos.setXYZ(i, x, heightAt(x, y) + 0.04, -y);
  }
  pos.needsUpdate = true; actors.aimGeo.setDrawRange(0, n);
  actors.aim.visible = d < 3 || game.club === PUTTER;
  if (game.club === PUTTER) {
    if (d <= 1 && s.lie === SURF.Green) {              // green-reading aid
      core.ng_putt_read();
      const b = buf(6 + 900), m = b[5], rp = actors.readGeo.attributes.position;
      for (let i = 0; i < m; i++) rp.setXYZ(i, b[6 + i * 3], b[8 + i * 3] + 0.03, -b[7 + i * 3]);
      rp.needsUpdate = true; actors.readGeo.setDrawRange(0, m);
      actors.read.visible = m > 1;
    }
    return;
  }
  if (d <= 1) {                                         // landing preview for a perfectly struck full swing
    core.ng_preview(game.club, game.shotType, game.style, game.curve, game.traj, game.aim, 1);
    const b = buf(10);
    if (b[6] > 1) {
      actors.ring.position.set(b[0], heightAt(b[0], b[1]) + 0.08, -b[1]);
      const sc = Math.max(1.2, b[6] * 0.022);
      actors.ring.scale.set(sc, sc, sc);
      actors.ring.visible = true;
      game.preview = { carry: b[6], total: b[7] };
      $('hClub').textContent = cstr(core.ng_club_name(game.club)) + ' · ' + dist(b[6]) + ' carry';
    }
  }
}

function updateCamera(dt, ball) {
  const s = game.state;
  let pos, look;
  const a = game.aim * Math.PI / 180, fx = Math.sin(a), fy = Math.cos(a);
  if (game.overhead && (game.mode === 'aim' || game.mode === 'result')) {
    const pinX = game.info[7], pinY = game.info[8];
    const mx = (s.x + pinX) / 2, my = (s.y + pinY) / 2, span = Math.max(60, Math.hypot(pinX - s.x, pinY - s.y));
    pos = V(mx - (pinX - s.x) * 0.05, my - (pinY - s.y) * 0.05, heightAt(mx, my) + span * 1.25 + 30);
    look = V(mx, my, heightAt(mx, my));
  } else if (game.mode === 'flight' || game.mode === 'result' || game.mode === 'holeout') {
    const o = game.outcome;
    const bx = ball.x, by = -ball.z;
    if (o && !o.putt && o.carry > 55 && game.playT > Math.min(1.6, o.time * 0.3)) {
      // Landing camera: we already know where the ball comes down, so wait for it there.
      const lx = o.landing[0], ly = o.landing[1];
      const dx = lx - o.from.x, dy = ly - o.from.y, l = Math.hypot(dx, dy) || 1;
      const cx = lx - dx / l * 16 + dy / l * 7, cy = ly - dy / l * 16 - dx / l * 7;
      pos = V(cx, cy, Math.max(heightAt(cx, cy), heightAt(lx, ly)) + 4.5);
      look = ball.clone();
    } else {
      const back = o && o.putt ? 3.2 : 6.5, up = o && o.putt ? 1.5 : 2.4;
      const cx = o ? o.from.x - fx * back : bx - fx * back, cy = o ? o.from.y - fy * back : by - fy * back;
      pos = V(cx, cy, heightAt(cx, cy) + up);
      look = ball.clone(); if (o && !o.putt) look.y = Math.max(look.y, pos.y - 0.5);
    }
  } else {
    const putting = game.club === PUTTER;
    const back = putting ? 3.2 : 6.5, up = putting ? 1.5 : 2.4, side = putting ? 0 : 0.5;
    const cx = s.x - fx * back + fy * side, cy = s.y - fy * back - fx * side;
    pos = V(cx, cy, Math.max(heightAt(cx, cy) + up, s.z + up * 0.8));
    const ahead = putting ? Math.min(8, s.toPin + 1) : 45;
    const lx = s.x + fx * ahead, ly = s.y + fy * ahead;
    look = V(lx, ly, putting ? heightAt(lx, ly) : (s.z + heightAt(lx, ly)) / 2 + 1.5);
  }
  const k = game.camSnap ? 1 : 1 - Math.exp(-dt * (game.mode === 'flight' ? 3.2 : 6));
  game.camPos.lerp(pos, k);
  game.camLook.lerp(look, game.camSnap ? 1 : 1 - Math.exp(-dt * 9));
  game.camSnap = false;
  const ground = heightAt(game.camPos.x, -game.camPos.z) + 0.6;
  if (game.camPos.y < ground) game.camPos.y = ground;
  camera.position.copy(game.camPos);
  camera.lookAt(game.camLook);
  if (world.sky) world.sky.position.set(camera.position.x, 0, camera.position.z);
}

function frame(ms) {
  requestAnimationFrame(frame);
  const dt = Math.min(0.1, (ms - lastFrame) / 1000 || 0.016);
  lastFrame = ms;
  if (settings.showFps) {
    fpsAcc += dt; fpsN++; fpsWorst = Math.max(fpsWorst, dt);
    if (fpsAcc >= 0.5) { $('fps').textContent = Math.round(fpsN / fpsAcc) + ' fps · avg ' + (fpsAcc / fpsN * 1000).toFixed(1) + ' ms · worst ' + (fpsWorst * 1000).toFixed(1) + ' ms'; fpsAcc = 0; fpsN = 0; fpsWorst = 0; }
  }
  if (game.mode === 'menu' || !game.state) { renderer.render(scene, camera); return; }
  const live = !paused();

  if (live && game.mode === 'aim') {
    const rate = (keys.ShiftLeft || keys.ShiftRight ? 4 : 22) * dt * (game.club === PUTTER ? 0.4 : 1);
    if (keys.KeyA || keys.ArrowLeft) { game.aim -= rate; game.previewDirty = true; }
    if (keys.KeyD || keys.ArrowRight) { game.aim += rate; game.previewDirty = true; }
    if (game.previewDirty) refreshAids();
    $('windArrow').style.transform = 'rotate(' + (game.info[21] - game.aim) + 'deg)';
  }

  if (live && game.mode === 'swing') {
    if (settings.swingMode === 'natural') {
      swingPhaseChanged(core.ng_swing_tick(now()));
      if (game.mode === 'swing') {
        const p = core.ng_swing_live_power();
        game.swingAngle = -Math.min(1.15, p) * 3.6;
        setMeter(p, core.ng_swing_live_lateral() * 3, p > 1.02 ? 'Overswing!' : Math.round(Math.min(p, 1.1) * 100) + '%');
      }
    } else if (settings.swingMode === 'classic' && game.classic) {
      const c = game.classic;
      if (c.stage === 1) { const p = classicPower(); setMeter(Math.max(0, p), 0, 'Click to set power'); game.swingAngle = -Math.max(0, p) * 3.6; if (now() - c.t0 > 2.2) { game.classic = null; game.mode = 'aim'; setMeter(0, 0, ''); } }
      else { const acc = classicAccuracy(); $('meterAcc').style.display = 'block'; $('meterAcc').style.left = (50 - acc * 50) + '%'; setMeter(c.power, 0, 'Click when the bar reaches the centre');
        game.swingAngle = -c.power * 3.6 * Math.max(0, Math.min(1, -acc));
        if (acc >= 1) classicClick(); }
    } else if (settings.swingMode === 'assisted' && game.assisted) {
      const p = assistedPower(); setMeter(p, 0, Math.round(p * 100) + '%'); game.swingAngle = -p * 3.6;
      if (now() - game.assisted.t0 > 2.6) { game.assisted = null; game.mode = 'aim'; setMeter(0, 0, ''); }
    }
  }
  if (game.mode !== 'swing' || settings.swingMode !== 'classic' || !game.classic || game.classic.stage !== 2) $('meterAcc').style.display = 'none';

  // Club animation through impact and into the follow-through.
  if (game.swingAnimT >= 0) {
    game.swingAnimT += dt;
    const u = Math.min(1, game.swingAnimT / 0.28);
    game.swingAngle = (game.outcome && game.outcome.putt ? 0.5 : 3.3) * (1 - Math.pow(1 - u, 3)) * (game.outcome ? Math.max(0.25, game.outcome.power) : 1);
  }

  let ball = ballVec();
  if (game.mode === 'flight') {
    if (live) game.playT += dt * game.flightSpeed;
    const p = pathAt(game.playT, tmp);
    ball = V(p.x, p.y, p.z + 0.0213);
    if (!p.air && !game.landed && game.playT > 0.05) { game.landed = true; if (!game.outcome.putt) sfx.land(game.outcome.reason === 1 ? SURF.Water : game.outcome.surface, Math.min(1, game.outcome.carry / 200)); }
    if (game.outcome.tree && !game.treeSounded && p.air && game.playT > 0.2 && Math.hypot(p.x - game.outcome.from.x, p.y - game.outcome.from.y) > 8) {
      // approximate: play when the ball slows abruptly is not tracked; sound once early in the flight that hit a tree
      game.treeSounded = true; sfx.tree();
    }
    if (actors.tracer.visible) {
      const tp = actors.tracerGeo.attributes.position; let n = 0;
      const P = game.path;
      for (let i = 0; i < game.pathN && P[i * 5] <= game.playT && n < 3999; i++) { tp.setXYZ(n++, P[i * 5 + 1], P[i * 5 + 3] + 0.03, -P[i * 5 + 2]); }
      tp.setXYZ(n++, ball.x, ball.y, ball.z);
      tp.needsUpdate = true; actors.tracerGeo.setDrawRange(0, n);
    }
    if (game.playT >= game.path[(game.pathN - 1) * 5]) finishFlight();
  }
  if (game.mode === 'result') {
    if (live) game.resultTimer += dt;
    if (game.resultTimer > 6 && game.kind === 'round') continueAfterShot();
  }

  // Ball, shadow, aids, golfer.
  const holedNow = game.state.holed && game.mode !== 'flight';
  actors.ball.visible = !holedNow;
  actors.ball.position.copy(ball);
  const camDist = camera.position.distanceTo(ball);
  const scale = Math.max(1, camDist * 0.0035 / 0.0213);          // keep the ball visible at range
  actors.ball.scale.setScalar(scale);
  actors.ball.position.y += 0.0213 * (scale - 1);
  const gh = heightAt(ball.x, -ball.z);
  actors.shadow.visible = !holedNow;
  actors.shadow.position.set(ball.x, gh + 0.02, ball.z);
  actors.shadow.scale.setScalar(scale * (1 + Math.min(3, (ball.y - gh) * 0.08)));
  const aiming = game.mode === 'aim' || game.mode === 'swing';
  if (!aiming) { actors.aim.visible = false; actors.ring.visible = false; actors.read.visible = false; }
  // The putting camera sits where the golfer would stand, so the figure is only drawn for full shots.
  const putterShot = aiming ? game.club === PUTTER : !!(game.outcome && game.outcome.putt);
  const showGolfer = !putterShot && (aiming || (game.mode === 'flight' && game.playT < 1.2));
  const addr = game.outcome && game.mode === 'flight' ? V(game.outcome.from.x, game.outcome.from.y, game.outcome.from.z + 0.0213) : ball;
  poseGolfer(addr, game.aim, game.swingAngle, showGolfer && !game.overhead);
  if (world.flag) { const near = game.state.toPin < 2.5 && game.club === PUTTER && aiming; world.flag.visible = !near; world.pole.visible = !near; }

  updateCamera(dt, ball);
  renderer.render(scene, camera);
}

// ---------------------------------------------------------------------------------------------------
// Input wiring
// ---------------------------------------------------------------------------------------------------
function onKey(e) {
  if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT')) return;
  keys[e.code] = e.type === 'keydown';
  if (e.type !== 'keydown') return;
  if (e.code === 'Tab') e.preventDefault();
  if (e.code === 'Escape' || e.code === 'KeyP' || e.code === 'Tab') {
    if (paused() && currentMenu === pauseMenu) { resume(); return; }
    if (game.mode !== 'menu' && !$('ui').classList.contains('show')) pause();
    return;
  }
  if (game.mode === 'menu' || paused() || $('ui').classList.contains('show')) return;
  if (e.repeat && !['KeyZ', 'KeyX', 'KeyC', 'KeyV'].includes(e.code)) return;
  switch (e.code) {
    case 'KeyW': case 'ArrowUp': cycleClub(-1); break;
    case 'KeyS': case 'ArrowDown': cycleClub(1); break;
    case 'KeyQ': cycleShotType(-1); break;
    case 'KeyE': cycleShotType(1); break;
    case 'KeyZ': nudge('curve', -0.25); break;
    case 'KeyX': nudge('curve', 0.25); break;
    case 'KeyC': nudge('traj', -0.25); break;
    case 'KeyV': nudge('traj', 0.25); break;
    case 'KeyF': nudge('style'); break;
    case 'KeyR': nudge('reset'); break;
    case 'KeyM': game.overhead = !game.overhead; break;
    case 'KeyT': settings.tracer = !settings.tracer; saveSettings(); break;
    case 'Space': e.preventDefault();
      if (game.mode === 'result') continueAfterShot();
      else if (game.mode === 'flight') game.flightSpeed = 4;
      else if (game.mode === 'swing' && settings.swingMode === 'classic') classicClick();
      break;
    default: break;
  }
}

function boot() {
  try {
    if (!window.WebGLRenderingContext) throw new Error('This browser does not support WebGL.');
    loadCore();
    initRenderer();
    initActors();
  } catch (err) {
    $('loadText').textContent = 'Could not start: ' + err.message;
    return;
  }
  canvas.addEventListener('pointerdown', (e) => {
    if (game.mode === 'swing' && settings.swingMode === 'classic' && e.button === 0) { classicClick(); return; }
    onDown(e);
  });
  window.addEventListener('pointerup', onUp);
  canvas.addEventListener('pointermove', onMove);
  canvas.addEventListener('wheel', (e) => { cycleClub(e.deltaY > 0 ? 1 : -1); e.preventDefault(); }, { passive: false });
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  document.addEventListener('pointerlockchange', () => {
    const was = pointerLocked;
    pointerLocked = document.pointerLockElement === canvas;
    lockChangedAt = now();
    if (game.mode === 'swing') disarmSwing();
    updateHud();
    if (was && !pointerLocked && game.mode !== 'menu' && !$('ui').classList.contains('show')) pause(); // Esc released the mouse
  });
  document.addEventListener('pointerlockerror', () => { lockFailed = true; updateHud(); });
  window.addEventListener('blur', () => { for (const k in keys) keys[k] = false; buttonDown = false; if (game.mode === 'swing') disarmSwing(); });
  window.addEventListener('keydown', onKey);
  window.addEventListener('keyup', onKey);
  showMenu(mainMenu);
  requestAnimationFrame(frame);

  // Automated-test hook: drives a swing through exactly the same sample path as real mouse input.
  window.__golfTest = {
    game, settings, core: () => core,
    swing(opts) {
      const o = Object.assign({ power: 1, timeScale: 1, pathDeg: 0 }, opts || {});
      if (game.mode !== 'aim') return false;
      armSwing();
      const top = o.power * settings.swingLength / settings.sensitivity, td = settings.downswingTime * (0.6 + 0.4 * Math.min(1, o.power)) * o.timeScale, tb = td / o.timeScale * 3;
      let t = now(), phase = 0, py = 0, pxl = 0;
      const tan = Math.tan(o.pathDeg * Math.PI / 180);
      for (let i = 1; i <= 400; i++) { const y = top * 0.5 * (1 - Math.cos(Math.PI * i / 400)); phase = core.ng_swing_sample(t + tb * i / 400, 0, y - py); py = y; }
      for (let i = 1; i <= 160 && phase !== 3; i++) { const u = i / 150, y = top - top * u * u, x = tan * (top - y); phase = core.ng_swing_sample(t + tb + td * u, x - pxl, y - py); py = y; pxl = x; }
      swingPhaseChanged(phase);
      if (phase !== 3) disarmSwing();
      return phase === 3;
    },
    skip() { if (game.mode === 'flight') { game.playT = game.path[(game.pathN - 1) * 5]; } },
    next() { continueAfterShot(); },
  };
}
boot();
})();
