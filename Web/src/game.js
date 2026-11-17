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
                   leftHanded: false, quality: 1, volume: 0.6, showFps: false, tracer: true, golfer: 'sam', puttGrid: true };
const settings = Object.assign({}, defaults, store.get('settings', {}));
function saveSettings() { store.set('settings', settings); }

// Selectable golfers. Attributes: power, accuracy, short game, putting, recovery, consistency (0..1).
// All four are original characters; players can rename any of them.
const ROSTER = [
  { id: 'sam', name: 'Sam Rivers', blurb: 'Balanced all-rounder.', attr: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
    look: { height: 1.0, build: 1.0, skin: 0xd9a77c, shirt: 0xf4f4f0, trousers: 0x2b3440, hat: 'cap', hatColor: 0x2b3440, hair: 0x4a3524 } },
  { id: 'stretch', name: 'Stretch', blurb: 'Seven foot four with a swing arc to match. Enormous power, less touch.', attr: [0.96, 0.5, 0.55, 0.45, 0.6, 0.5],
    look: { height: 1.26, build: 0.84, skin: 0x8a5a3c, shirt: 0xe8762c, trousers: 0x1f4e5f, hat: 'headband', hatColor: 0xe8762c, hair: 0x141210 } },
  { id: 'chef', name: 'Big Chef', blurb: '99 overall. Every attribute maxed out. Runs the clubhouse kitchen between rounds.', attr: [1, 1, 1, 1, 1, 1],
    look: { height: 0.98, build: 1.5, skin: 0xc68a5e, shirt: 0xf3efe4, trousers: 0x5a5f66, hat: 'toque', hatColor: 0xffffff, hair: 0x6a4a32, apron: true, apronColor: 0x2f7d5b, chest: 1, belly: 1, rear: 1, beard: true } },
  { id: 'rookie', name: 'The Rookie', blurb: 'Raw beginner. Pick this if you want it hard.', attr: [0.2, 0.2, 0.2, 0.2, 0.2, 0.2],
    look: { height: 0.94, build: 0.92, skin: 0xf0c8a0, shirt: 0xf2c230, trousers: 0x6a6f3a, hat: 'cap', hatColor: 0xd04a3a, hair: 0xb5651d } },
];
const ATTR_NAMES = ['Power', 'Accuracy', 'Short game', 'Putting', 'Recovery', 'Consistency'];
function golferById(id) { return ROSTER.find((g) => g.id === id) || ROSTER[0]; }
function golferName(g) { const names = store.get('names', {}); return names[g.id] || g.name; }
function overall(g) { return Math.round(50 + 49 * g.attr.reduce((a, b) => a + b, 0) / g.attr.length); }
// The Daily Course is a like-for-like competition, so everyone plays it with the same standard attributes.
function applyGolfer(standard) { const a = standard ? ROSTER[0].attr : golferById(settings.golfer).attr; core.ng_set_attributes(a[0], a[1], a[2], a[3], a[4], a[5]); }

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
let renderer, scene, camera, post = null;
const world = { group: null, heights: null, nx: 0, ny: 0, cell: 2, gx: 0, gy: 0, env: 0 };
const V = (x, y, z) => new THREE.Vector3(x, z, -y);
const UP = new THREE.Vector3(0, 1, 0); // simulation (x right, y downrange, z up) -> three.js

function initRenderer() {
  renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(52, 1, 0.3, 2400);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  world.hemi = new THREE.HemisphereLight(0xe6f1ff, 0x6a7a50, 1.35);
  scene.add(world.hemi);
  post = window.PostFX ? new PostFX(renderer) : null;
  const sun = new THREE.DirectionalLight(0xfff0d0, 2.1);
  sun.castShadow = true;
  sun.shadow.camera.left = -28; sun.shadow.camera.right = 28; sun.shadow.camera.top = 28; sun.shadow.camera.bottom = -28;
  sun.shadow.camera.near = 10; sun.shadow.camera.far = 260;
  sun.shadow.bias = -0.0006; sun.shadow.normalBias = 0.06;
  scene.add(sun, sun.target);
  world.sun = sun;
  applyQuality();
  window.addEventListener('resize', resize);
  resize();
}
const SUN_DIR = new THREE.Vector3(-0.55, 0.8, 0.35).normalize();   // updated per hole from the time of day
// Time of day: sun elevation and azimuth (degrees), sun colour, clear-sky colours, light levels, grade warmth.
const TOD = [
  { name: 'Morning',   el: 17, az: 115, sun: 0xffe3c4, top: 0x5a90d8, hor: 0xdfe8ef, si: 0.85, hi: 0.9,  warm: 0.3 },
  { name: 'Midday',    el: 60, az: 200, sun: 0xfff5e6, top: 0x3f8fe0, hor: 0xbfe0f7, si: 1.0,  hi: 1.0,  warm: 0.0 },
  { name: 'Afternoon', el: 33, az: 250, sun: 0xfff0d8, top: 0x4a8ede, hor: 0xcfe3f2, si: 0.95, hi: 0.95, warm: 0.4 },
  { name: 'Sunset',    el: 7,  az: 285, sun: 0xffa868, top: 0x3d5e9e, hor: 0xf2b886, si: 0.8,  hi: 0.65, warm: 1.4 },
  { name: 'Evening',   el: 3.5, az: 298, sun: 0xff8a58, top: 0x2d4176, hor: 0xd99a74, si: 0.55, hi: 0.5, warm: 1.8 },
];
function todFromSeed(text) { let h = 2166136261; for (let i = 0; i < text.length; i++) { h ^= text.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; } return [0, 1, 1, 2, 2, 3][h % 6]; }
function applyQuality() {
  const cap = [1, 1.5, 2][settings.quality];
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, cap));
  // Real-time shadows near the ball on Balanced and Quality; Performance relies on the painted ones.
  renderer.shadowMap.enabled = settings.quality > 0;
  if (world.sun) {
    const size = settings.quality > 1 ? 2048 : 1024;
    if (world.sun.shadow.mapSize.x !== size) { world.sun.shadow.mapSize.set(size, size); if (world.sun.shadow.map) { world.sun.shadow.map.dispose(); world.sun.shadow.map = null; } }
  }
  scene.traverse((o) => { if (o.material) o.material.needsUpdate = true; });
}
function resize() {
  renderer.setSize(window.innerWidth, window.innerHeight, false);
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  if (post) post.setSize();
}
function draw() {
  const usePost = post && settings.quality > 0;
  const v = document.getElementById('vignette');
  if (v) v.style.display = usePost ? 'none' : 'block';
  if (usePost) post.render(scene, camera, renderer.toneMappingExposure, world.tod ? world.tod.warm : 0);
  else renderer.render(scene, camera);
}

// Tree canopy: a lumpy, smooth-shaded mass with darker underside and interior (baked as vertex colour).
function canopyGeometry(pine) {
  const g = pine ? new THREE.ConeGeometry(1, 1, 12, 6) : new THREE.SphereGeometry(1, 18, 12);
  const pos = g.attributes.position, col = new Float32Array(pos.count * 3);
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
    const n = Math.sin(x * 5.1 + y * 3.7) * Math.sin(z * 4.3 - y * 2.9) * 0.5 + Math.sin(x * 11.0 + z * 9.0) * 0.25;
    const k = pine ? 1 + 0.12 * n * (0.5 - y) : 1 + 0.16 * n;
    if (pine) { pos.setX(i, x * k); pos.setZ(i, z * k); } else pos.setXYZ(i, x * k, y * k, z * k);
    const shade = pine ? 0.55 + 0.45 * (y + 0.5) : 0.5 + 0.5 * (y * 0.5 + 0.5);
    const c = Math.min(1.1, shade * (0.88 + 0.25 * n));
    col[i * 3] = c; col[i * 3 + 1] = c; col[i * 3 + 2] = c;
  }
  g.setAttribute('color', new THREE.BufferAttribute(col, 3));
  g.computeVertexNormals();
  return g;
}

function heightAt(x, y) {
  const w = world;
  if (!w.heights) return 0;
  if (core && w.holeIndex !== undefined) return core.ng_height(w.holeIndex, x, y);   // includes the fine green
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
  Object.assign(world, { heights, nx, ny, cell, gx, gy, env, holeIndex: index });
  world.timeUniforms = [];

  // Trees first: their shadows are painted into the terrain texture.
  const nTrees = core.ng_hole_trees(index);
  const trees = Array.from(buf(nTrees * 5));

  // Terrain texture from the core's surface map.
  const res = 0.5;
  const mapPtr = core.ng_surface_map(index, res);
  const mw = buf(2)[0], mh = buf(2)[1];
  const map = new Uint8Array(core.memory.buffer, mapPtr, mw * mh).slice();
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
      // Edges: a shaded lip around bunkers and a darker bank around water read as depth from a distance.
      if ((s === SURF.Sand || s === SURF.Water) && x > 2 && y > 2 && x < mw - 3 && y < mh - 3) {
        if (map[y * mw + x - 3] !== s || map[y * mw + x + 3] !== s || map[(y - 3) * mw + x] !== s || map[(y + 3) * mw + x] !== s) n -= s === SURF.Sand ? 26 : 18;
      }
      px[i] = c[0] + n; px[i + 1] = c[1] + n; px[i + 2] = c[2] + n; px[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  ctx.fillStyle = 'rgba(10,30,14,0.30)';
  for (let t = 0; t < nTrees; t++) {
    const tx = trees[t * 5], ty = trees[t * 5 + 1], r = trees[t * 5 + 3], h = trees[t * 5 + 4];
    ctx.beginPath();
    ctx.ellipse((tx + h * 0.45 - gx) / res, (ty + h * 0.29 - gy) / res, r * 1.5 / res, r * 1.1 / res, 0, 0, Math.PI * 2);
    ctx.fill();
  }
  const tex = new THREE.CanvasTexture(cv);
  tex.flipY = false;
  tex.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());
  if ('colorSpace' in tex) tex.colorSpace = THREE.SRGBColorSpace;
  const ids = new THREE.DataTexture(new Uint8Array(map), mw, mh, THREE.RedFormat, THREE.UnsignedByteType);
  ids.magFilter = THREE.NearestFilter; ids.minFilter = THREE.NearestFilter; ids.flipY = false; ids.needsUpdate = true;
  const terrainMat = GolfShaders.terrainMaterial(tex, ids);
  world.terrainUniforms = terrainMat.userData.uniforms;

  // Terrain: the 2 m grid everywhere except around the green, where a 0.5 m mesh shows every contour.
  // Both take height and normals from the core, so what you see is exactly what the ball rolls on.
  const W = (nx - 1) * cell, Hh = (ny - 1) * cell;
  const groundNormal = (x, y, e) => {
    const dx = (core.ng_height(index, x + e, y) - core.ng_height(index, x - e, y)) / (2 * e);
    const dy = (core.ng_height(index, x, y + e) - core.ng_height(index, x, y - e)) / (2 * e);
    const l = Math.hypot(dx, dy, 1);
    return [-dx / l, 1 / l, dy / l];             // three.js: y up, z = -sim y
  };
  core.ng_hole_green_patch(index);
  const gp = buf(5);
  const ix0 = Math.max(0, Math.floor((gp[0] - gx) / cell) - 1), iy0 = Math.max(0, Math.floor((gp[1] - gy) / cell) - 1);
  const ix1 = Math.min(nx - 1, Math.ceil((gp[0] + (gp[3] - 1) * gp[2] - gx) / cell) + 1), iy1 = Math.min(ny - 1, Math.ceil((gp[1] + (gp[4] - 1) * gp[2] - gy) / cell) + 1);
  const hasFine = gp[3] > 1 && ix1 > ix0 && iy1 > iy0;
  let minH = 1e9;
  const makeGrid = (cols, rows, x0, y0, step, heightFn, skip) => {
    const pos = new Float32Array(cols * rows * 3), nor = new Float32Array(cols * rows * 3), uv = new Float32Array(cols * rows * 2);
    for (let iy = 0; iy < rows; iy++) for (let ix = 0; ix < cols; ix++) {
      const k = iy * cols + ix, x = x0 + ix * step, y = y0 + iy * step, h = heightFn(ix, iy, x, y);
      pos[k * 3] = x; pos[k * 3 + 1] = h; pos[k * 3 + 2] = -y;
      const n = groundNormal(x, y, 1.0);
      nor[k * 3] = n[0]; nor[k * 3 + 1] = n[1]; nor[k * 3 + 2] = n[2];
      uv[k * 2] = (x - gx) / W; uv[k * 2 + 1] = (y - gy) / Hh;
      if (h < minH) minH = h;
    }
    const idx = [];
    for (let iy = 0; iy < rows - 1; iy++) for (let ix = 0; ix < cols - 1; ix++) {
      if (skip && skip(ix, iy)) continue;
      const a = iy * cols + ix, b = a + 1, c = a + cols, d = c + 1;
      idx.push(a, b, c, b, d, c);
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(cols * rows > 65535 ? new THREE.Uint32BufferAttribute(idx, 1) : new THREE.Uint16BufferAttribute(idx, 1));
    const mesh = new THREE.Mesh(geo, terrainMat);
    mesh.receiveShadow = true;
    mesh.castShadow = settings.quality > 1;
    g.add(mesh);
    return mesh;
  };
  makeGrid(nx, ny, gx, gy, cell, (ix, iy) => heights[iy * nx + ix], hasFine ? (ix, iy) => ix >= ix0 && ix < ix1 && iy >= iy0 && iy < iy1 : null);
  if (hasFine) {
    const sub = Math.max(1, Math.round(cell / gp[2]));
    const step = cell / sub;
    makeGrid((ix1 - ix0) * sub + 1, (iy1 - iy0) * sub + 1, gx + ix0 * cell, gy + iy0 * cell, step, (ix, iy, x, y) => core.ng_height(index, x, y));
  }

  // Surroundings: open sea for coastal courses, otherwise distant ground.
  const far = pal[10];
  const surroundColor = env === 2 ? 0x2f6f9c : (far[0] << 16) | (far[1] << 8) | far[2];
  // Lighting for this time of day, blended toward grey by cloud cover.
  const tod = TOD[game.course && game.course.tod !== undefined ? game.course.tod : 1];
  const cover = [0, 0.3, 0.85, 0.9, 0.1, 0.9][weather] || 0;
  const el = tod.el * Math.PI / 180, az = tod.az * Math.PI / 180;
  SUN_DIR.set(Math.cos(el) * Math.sin(az), Math.sin(el), Math.cos(el) * Math.cos(az)).normalize();
  const mixHex = (x, y, t) => new THREE.Color(x).lerp(new THREE.Color(y), t).getHex();
  const sky0 = [mixHex(tod.top, (SKY[weather] || SKY[0])[0], cover), mixHex(tod.hor, (SKY[weather] || SKY[0])[1], cover), (SKY[weather] || SKY[0])[2]];
  if (weather === 1 || weather === 4) { sky0[0] = mixHex(tod.top, SKY[weather][0], 0.25); }
  world.tod = tod;
  if (world.sun) world.sun.color.set(tod.sun);
  if (world.hemi) { world.hemi.intensity = 1.35 * tod.hi; world.hemi.color.set(mixHex(0xe6f1ff, tod.hor, 0.35)); }
  terrainMat.userData.uniforms.uSunDirW.value.copy(SUN_DIR);
  const sunStrength = ([1.0, 0.8, 0.25, 0.15, 1.0, 0.1][weather] || 1) * (tod.el < 10 ? 1.3 : 1);
  const surround = new THREE.Mesh(new THREE.PlaneGeometry(6000, 6000), env === 2
    ? GolfShaders.waterMaterial({ sunDir: SUN_DIR, sunStrength, skyTop: sky0[0], skyHorizon: sky0[1], fogColor: sky0[1], fogDensity: sky0[2],
                                  cx: 0, cy: 0, rx: 1e6, ry: 1e6, rotDeg: 0, deep: 0x0d3550, shallow: 0x2a7f8f, chop: 1.6 })
    : new THREE.MeshLambertMaterial({ color: surroundColor }));
  if (env === 2) world.timeUniforms.push(surround.material.uniforms.uTime);
  surround.rotation.x = -Math.PI / 2;
  surround.position.set(gx + nx * cell / 2, minH - (env === 2 ? 2.5 : 1.2), -(gy + ny * cell / 2));
  g.add(surround);

  // Ponds: a shaded water surface over each hazard (terrain beneath is carved, so the banks clip it).
  const nWater = core.ng_hole_water(index);
  const ponds = Array.from(buf(nWater * 6));
  for (let k = 0; k < nWater; k++) {
    const [cx, cy, rx, ry, rot, level] = ponds.slice(k * 6, k * 6 + 6);
    const m = GolfShaders.waterMaterial({ sunDir: SUN_DIR, sunStrength, skyTop: sky0[0], skyHorizon: sky0[1], fogColor: sky0[1], fogDensity: sky0[2],
                                          cx, cy, rx, ry, rotDeg: rot });
    const disc = new THREE.Mesh(new THREE.CircleGeometry(1, 72), m);
    disc.rotation.order = 'YXZ';
    disc.rotation.set(-Math.PI / 2, -rot * Math.PI / 180, 0);
    disc.scale.set(rx + 9, ry + 9, 1);
    disc.position.set(cx, level, -cy);
    disc.receiveShadow = false;
    g.add(disc);
    world.timeUniforms.push(m.uniforms.uTime);
  }

  // Grass tufts in the rough, swaying in the wind (Balanced and Quality only).
  const tuftCount = [0, 14000, 42000][settings.quality];
  if (tuftCount > 0) {
    const swayU = { uTime: { value: 0 }, uWind: { value: new THREE.Vector2(0.3, 0) } };
    const tuftMat = GolfShaders.addSway(new THREE.MeshLambertMaterial({ vertexColors: true, side: THREE.DoubleSide }), swayU, 0.22);
    const tufts = new THREE.InstancedMesh(GolfShaders.tuftGeometry(), tuftMat, tuftCount);
    const m4 = new THREE.Matrix4(), q4 = new THREE.Quaternion(), p3 = new THREE.Vector3(), s3 = new THREE.Vector3(), c3 = new THREE.Color();
    const tuftBase = [palette(env)[3], palette(env)[4]];
    let placed = 0, seed = 12345 + index * 977;
    const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
    for (let tries = 0; tries < tuftCount * 6 && placed < tuftCount; tries++) {
      const mx = Math.floor(rnd() * mw), my = Math.floor(rnd() * mh), sid = map[my * mw + mx];
      if (sid !== SURF.Rough && sid !== SURF.DeepRough && sid !== SURF.FirstCut) continue;
      if (sid === SURF.FirstCut && rnd() < 0.7) continue;
      const x = gx + (mx + rnd()) * res, y = gy + (my + rnd()) * res;
      const tall = sid === SURF.DeepRough ? 0.55 + rnd() * 0.35 : sid === SURF.Rough ? 0.3 + rnd() * 0.25 : 0.12 + rnd() * 0.08;
      q4.setFromAxisAngle(UP, rnd() * 6.283);
      p3.set(x, core.ng_height(index, x, y) - 0.02, -y);
      s3.set(tall * (0.8 + rnd() * 0.6), tall, tall * (0.8 + rnd() * 0.6));
      tufts.setMatrixAt(placed, m4.compose(p3, q4, s3));
      const b = tuftBase[sid === SURF.DeepRough ? 1 : 0];
      c3.setRGB(b[0] / 255, b[1] / 255, b[2] / 255, THREE.SRGBColorSpace).offsetHSL((rnd() - 0.5) * 0.05, 0, (rnd() - 0.3) * 0.12);
      tufts.setColorAt(placed, c3);
      placed++;
    }
    tufts.count = placed;
    tufts.instanceMatrix.needsUpdate = true;
    if (tufts.instanceColor) tufts.instanceColor.needsUpdate = true;
    tufts.computeBoundingSphere();
    g.add(tufts);
    world.timeUniforms.push(swayU.uTime);
    world.windUniforms = [swayU.uWind];
  } else world.windUniforms = [];

  // Trees (instanced): two canopy masses per tree with a little colour variation.
  if (nTrees > 0) {
    const pine = env === 3 || env === 1;
    const canopyGeo = canopyGeometry(pine);
    const base = new THREE.Color([0x4a8f42, 0x41783f, 0x55904c, 0x2a6336, 0x858c46, 0x698648][env]);
    const treeU = { uTime: { value: 0 }, uWind: { value: new THREE.Vector2(0.3, 0) } };
    // Visual-only tree belts beyond the course boundary, so holes sit in woodland instead of open ground.
    const belt = [];
    const beltWant = [120, 320, 650][settings.quality] * (env === 5 || env === 2 || env === 4 ? 0.25 : 1);
    let bseed = 777 + index * 131;
    const brnd = () => { bseed = (bseed * 1664525 + 1013904223) >>> 0; return bseed / 4294967296; };
    for (let t = 0; t < beltWant * 8 && belt.length < beltWant; t++) {
      const mx = Math.floor(brnd() * mw), my = Math.floor(brnd() * mh);
      if (map[my * mw + mx] !== SURF.OOB) continue;
      const x = gx + mx * res, y = gy + my * res;
      belt.push([x, y, core.ng_height(index, x, y), 3 + brnd() * 2.5, 10 + brnd() * 9]);
    }
    for (const bt of belt) trees.push(...bt);
    const nAll = nTrees + belt.length;
    const canopy = new THREE.InstancedMesh(canopyGeo, GolfShaders.addSway(new THREE.MeshLambertMaterial({ vertexColors: true }), treeU, 0.05), nAll * 2);
    world.timeUniforms.push(treeU.uTime); world.windUniforms.push(treeU.uWind);
    const trunk = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.2, 0.32, 1, 7), new THREE.MeshLambertMaterial({ color: 0x5b4532 }), nAll);
    canopy.castShadow = true; trunk.castShadow = true;
    const m = new THREE.Matrix4(), s = new THREE.Vector3(), p = new THREE.Vector3(), rot = new THREE.Quaternion(), col = new THREE.Color();
    for (let t = 0; t < nAll; t++) {
      const tx = trees[t * 5], ty = trees[t * 5 + 1], tz = trees[t * 5 + 2], r = trees[t * 5 + 3], h = trees[t * 5 + 4];
      const v = ((t * 2654435761) >>> 0) / 4294967296;           // stable per-tree variation
      rot.setFromAxisAngle(UP, t * 1.7);
      col.copy(base).offsetHSL((v - 0.5) * 0.04, (v - 0.5) * 0.1, (v - 0.5) * 0.09);
      if (pine) {
        p.set(tx, tz + h * 0.46, -ty); s.set(r * 1.05, h * 0.62, r * 1.05); canopy.setMatrixAt(t * 2, m.compose(p, rot, s));
        p.set(tx, tz + h * 0.76, -ty); s.set(r * 0.68, h * 0.48, r * 0.68); canopy.setMatrixAt(t * 2 + 1, m.compose(p, rot, s));
      } else {
        p.set(tx, tz + h * 0.62, -ty); s.set(r * 1.2, h * 0.34, r * 1.2); canopy.setMatrixAt(t * 2, m.compose(p, rot, s));
        p.set(tx + r * 0.45 * Math.cos(t), tz + h * 0.8, -ty + r * 0.45 * Math.sin(t)); s.set(r * 0.8, h * 0.24, r * 0.8); canopy.setMatrixAt(t * 2 + 1, m.compose(p, rot, s));
      }
      canopy.setColorAt(t * 2, col); canopy.setColorAt(t * 2 + 1, col.offsetHSL(0, 0, 0.035));
      p.set(tx, tz + h * 0.25, -ty); s.set(1 + r * 0.12, h * 0.5, 1 + r * 0.12);
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
  const flagGeo = new THREE.PlaneGeometry(0.55, 0.36, 12, 5).translate(0.275, 1.9, 0);
  const flagU = { uTime: { value: 0 } };
  const flagMat = new THREE.MeshStandardMaterial({ color: 0xd8342a, side: THREE.DoubleSide, roughness: 0.8 });
  flagMat.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, flagU);
    sh.vertexShader = 'uniform float uTime;\n' + sh.vertexShader.replace('#include <begin_vertex>', `#include <begin_vertex>
      float fx = max(position.x, 0.0);
      transformed.z += sin(fx * 9.0 - uTime * 7.0) * 0.06 * fx + sin(fx * 4.0 - uTime * 3.1) * 0.03 * fx;
      transformed.y -= 0.05 * fx * fx;`);
  };
  const flag = new THREE.Mesh(flagGeo, flagMat);
  flag.castShadow = true;
  world.timeUniforms.push(flagU.uTime);
  flag.position.copy(pin);
  pole.castShadow = true;
  world.flag = flag; world.pole = pole; world.pin = pin;
  g.add(cup, pole, flag);

  // Tee markers.
  for (const side of [-1, 1]) {
    const mk = new THREE.Mesh(new THREE.SphereGeometry(0.07, 10, 8), new THREE.MeshLambertMaterial({ color: 0x2d62c8 }));
    mk.position.copy(V(info[10] + side * 1.6, info[11], info[12] + 0.07));
    g.add(mk);
  }

  // Sky and fog follow the weather.
  const sky = sky0;
  const skyMesh = new THREE.Mesh(new THREE.SphereGeometry(2200, 32, 16), GolfShaders.skyMaterial(sky[0], sky[1], SUN_DIR, sunStrength));
  skyMesh.renderOrder = -10;
  skyMesh.frustumCulled = false;
  const skyGroup = new THREE.Group();
  skyGroup.add(skyMesh);
  // Clouds: soft sprites on the dome. Count and tint follow the weather.
  const puff = document.createElement('canvas'); puff.width = puff.height = 128;
  const pc = puff.getContext('2d');
  for (let i = 0; i < 9; i++) {
    const cx = 30 + ((i * 53) % 70), cy = 50 + ((i * 31) % 30), r = 22 + ((i * 17) % 18);
    const gr = pc.createRadialGradient(cx, cy, 0, cx, cy, r);
    gr.addColorStop(0, 'rgba(255,255,255,0.85)'); gr.addColorStop(1, 'rgba(255,255,255,0)');
    pc.fillStyle = gr; pc.fillRect(0, 0, 128, 128);
  }
  const puffTex = new THREE.CanvasTexture(puff);
  const cloudCount = [7, 16, 26, 26, 10, 0][weather] || 0, cloudTint = [0xffffff, 0xf4f6f8, 0xc9ced2, 0xaab0b6, 0xffffff, 0xffffff][weather];
  for (let i = 0; i < cloudCount; i++) {
    const az = (i * 2.399) + weather, el = 0.14 + ((i * 0.6180339) % 1) * 0.5, R = 1700;
    const cloud = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshBasicMaterial({ map: puffTex, color: cloudTint, transparent: true, opacity: 0.9, fog: false, depthWrite: false }));
    cloud.position.set(Math.cos(az) * Math.cos(el) * R, Math.sin(el) * R, Math.sin(az) * Math.cos(el) * R);
    const w = 420 + ((i * 97) % 380);
    cloud.scale.set(w, w * 0.42, 1);
    cloud.lookAt(0, 0, 0);
    skyGroup.add(cloud);
  }
  // Distant hills give the horizon some shape (mountains with snow for alpine; none out at sea or on links).
  if (env !== 2 && env !== 5) {
    const segs = 96, ring = new Float32Array((segs + 1) * 2 * 3), rc = new Float32Array((segs + 1) * 2 * 3);
    const tall = env === 3 ? 330 : env === 1 ? 150 : env === 4 ? 120 : 70;
    const lowC = new THREE.Color(env === 4 ? 0x9a7b5a : 0x4c6b4a), highC = new THREE.Color(env === 3 ? 0xf2f5f8 : env === 4 ? 0xb08a62 : 0x5f7d5c);
    for (let i = 0; i <= segs; i++) {
      const a = i / segs * Math.PI * 2, R = 1250;
      const hgt = tall * (0.35 + 0.65 * Math.abs(Math.sin(a * 3.1 + env) * Math.sin(a * 7.3 + 1.7) + 0.35 * Math.sin(a * 17.0)));
      ring.set([Math.cos(a) * R, -30, Math.sin(a) * R, Math.cos(a) * R, hgt, Math.sin(a) * R], i * 6);
      const top = lowC.clone().lerp(highC, Math.min(1, hgt / tall));
      rc.set([lowC.r, lowC.g, lowC.b, top.r, top.g, top.b], i * 6);
    }
    const hi = [];
    for (let i = 0; i < segs; i++) { const a = i * 2; hi.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
    const hg = new THREE.BufferGeometry();
    hg.setAttribute('position', new THREE.BufferAttribute(ring, 3));
    hg.setAttribute('color', new THREE.BufferAttribute(rc, 3));
    hg.setIndex(hi);
    skyGroup.add(new THREE.Mesh(hg, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide })));
  }
  world.sky = skyGroup;
  g.add(skyGroup);
  scene.fog = new THREE.FogExp2(sky[1], sky[2]);
  world.fogDensity = sky[2];
  if (world.sun) world.sun.intensity = ([2.4, 1.9, 0.9, 0.7, 2.3, 0.6][weather] || 2) * tod.si;
  renderer.toneMappingExposure = ([1.05, 1.08, 1.18, 1.2, 1.05, 1.15][weather] || 1.05) * (tod.el < 10 ? 1.12 : 1);
  // Wind for grass, trees and water ripples, in render space.
  const wd = info[21] * Math.PI / 180, ws = Math.min(1.5, 0.25 + info[22] / 6);
  for (const u of world.windUniforms) u.value.set(Math.sin(wd) * ws, -Math.cos(wd) * ws);
  scene.add(g);
  return info;
}

// Persistent objects: ball, shadow, tracer, aim aids, golfer.
const actors = {};
function initActors() {
  actors.ball = new THREE.Mesh(new THREE.SphereGeometry(0.0213, 20, 14), new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.35 }));
  actors.ball.castShadow = true;
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

  const steel = new THREE.MeshLambertMaterial({ color: 0xb8bcc2 });
  actors.club = new THREE.Mesh(new THREE.CylinderGeometry(0.011, 0.011, 1, 6), steel);
  actors.head = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.05, 0.07), new THREE.MeshLambertMaterial({ color: 0x23262b }));
  actors.club.castShadow = true;
  for (const k of ['ball', 'shadow', 'tracer', 'aim', 'read', 'ring']) { actors[k].visible = false; scene.add(actors[k]); }
  buildGolferFigure();
}

// Stylised golfer assembled from primitives, parameterised by the chosen character's look.
function buildGolferFigure() {
  if (actors.rig) actors.rig.dispose(scene);
  actors.rig = new GolferRig(golferById(settings.golfer).look, scene);
  actors.rig.setVisible(false);
}
function clubKind(id) { return id === 0 ? 'driver' : id <= 4 ? 'wood' : id <= 11 ? 'iron' : id <= 15 ? 'wedge' : 'putter'; }
let poseDt = 0;
function poseGolfer(ball, aimDeg, swingAngle, visible) {
  if (!actors.rig) return;
  actors.rig.setVisible(visible);
  // Game swing angles run -3.6 (top) .. +3.3 (finish); a real club turns ~250 degrees back and ~275 through.
  const visual = game.club === PUTTER && !(game.outcome && game.mode === 'flight' && !game.outcome.putt) ? swingAngle : swingAngle < 0 ? swingAngle * 1.25 : swingAngle * 1.45;
  if (visible) actors.rig.pose(ball, aimDeg, visual, clubKind(game.outcome && game.mode === 'flight' ? game.outcome.club : game.club), settings.leftHanded, heightAt, poseDt);
}

// ---------------------------------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------------------------------
const game = {
  mode: 'menu',            // menu | aim | swing | flight | result | holeout | done
  kind: 'round',           // round | range
  daily: null,             // daily id when playing the Daily Course
  forceTod: -1,            // time of day chosen in the Random 9 menu (-1 = from the course code)
  holes: 0, hole: 0, info: null, course: null,
  card: [],                // finished holes
  bag: [], club: 0, shotType: 0, style: 0, curve: 0, traj: 0, spin: 0, aim: 0,
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
    applyGolfer(kind === 'round' && !!game.daily);
    if (kind === 'range') { core.ng_use_range(1); game.holes = 1; game.course = { name: 'Practice Range', par: 0, env: 0, weather: 0, code: '', tod: 2 }; }
    else {
      core.ng_use_range(0);
      const n = generate();
      if (!n) { showMenu(mainMenu, 'That course could not be created. Check the code and try again.'); return; }
      core.ng_course_info();
      const c = buf(12);
      game.holes = n;
      game.course = { par: c[1], wind: c[2], temp: c[4], stimp: c[5], weather: c[6], env: c[8], diff: c[9], tee: c[10], length: c[11],
                      name: cstr(core.ng_course_name()), code: cstr(core.ng_share_code()), weatherName: cstr(core.ng_weather_name()) };
      // Time of day is presentation only. It follows the course code, so a shared course looks the same for everyone.
      game.course.tod = game.forceTod >= 0 ? game.forceTod : todFromSeed(game.course.code);
      game.course.weatherName += ' · ' + TOD[game.course.tod].name;
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
  game.club = rec; game.shotType = buf(1)[0]; game.style = 0; game.curve = 0; game.traj = 0; game.spin = 0; core.ng_set_spin(0);
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
  if (what === 'spin') { game.spin = Math.round(Math.min(1, Math.max(-1, game.spin + d)) * 4) / 4; core.ng_set_spin(game.spin); }
  if (what === 'style') game.style = (game.style + 1) % 4;
  if (what === 'reset') { game.curve = 0; game.traj = 0; game.style = 0; game.shotType = 0; game.spin = 0; core.ng_set_spin(0); }
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
    game.aim += e.movementX * (e.shiftKey ? 0.012 : 0.05) * (game.club === PUTTER ? 0.35 : 1);
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
  // Swing analysis diagram: target line, club path arrow and face line (angles exaggerated 3x to read).
  let diagram = '';
  if (!o.putt) {
    const deg = (v) => Math.max(-30, Math.min(30, v * 3)) * Math.PI / 180;
    const px = Math.sin(deg(o.path)) * 34, py = Math.cos(deg(o.path)) * 34, fx = Math.cos(deg(o.face)) * 22, fy = -Math.sin(deg(o.face)) * 22;
    diagram = '<svg viewBox="-50 -44 100 88" width="120" height="104" style="float:right;margin:0 0 4px 10px">' +
      '<line x1="0" y1="40" x2="0" y2="-40" stroke="rgba(255,255,255,.25)" stroke-dasharray="3 3"/>' +
      '<line x1="' + (-px) + '" y1="' + py + '" x2="' + px + '" y2="' + (-py) + '" stroke="#e8c65a" stroke-width="3" marker-end="url(#ah)"/>' +
      '<line x1="' + (-fx) + '" y1="' + (-fy) + '" x2="' + fx + '" y2="' + fy + '" stroke="#7fe0ff" stroke-width="4" stroke-linecap="round"/>' +
      '<defs><marker id="ah" markerWidth="6" markerHeight="6" refX="3" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6 Z" fill="#e8c65a"/></marker></defs>' +
      '<text x="-48" y="42" font-size="8" fill="#e8c65a">path ' + (o.path >= 0 ? '+' : '') + o.path.toFixed(1) + '°</text>' +
      '<text x="6" y="42" font-size="8" fill="#7fe0ff">face ' + (o.face >= 0 ? '+' : '') + o.face.toFixed(1) + '°</text></svg>';
  }
  const r = $('result');
  r.innerHTML = diagram + '<h2>' + title + '</h2><div class="nums">' + nums + '</div>' + extra + '<ul class="why">' + why + '</ul><div class="adv">' + adv +
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
  updateHud();
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
  showMenu((body) => roundSummaryMenu(body, entry));
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
  const LIES = ['Tee', 'Fairway', 'First Cut', 'Rough', 'Deep Rough', 'Sand', 'Green', 'Fringe', 'Hard Ground', 'Water', 'Out of Bounds'];
  $('hLie').textContent = game.mode === 'flight' && game.outcome ? LIES[game.outcome.from.lie] : LIES[s.lie];
  $('hType').textContent = game.club === PUTTER ? 'Putt' : cstr(core.ng_shot_type_name(game.shotType));
  $('hShape').textContent = game.club === PUTTER ? '—' : shapeText();
  // Strike point: where the club meets the ball. Down = backspin, up = topspin; left/right shows the shape.
  const sp = game.club === PUTTER ? 0 : game.spin;
  $('hSpin').textContent = game.club === PUTTER ? '—' : sp === 0 ? 'Normal' : (sp > 0 ? 'Backspin +' : 'Topspin +') + Math.round(Math.abs(sp) * 100) + '%';
  const dot = $('strikeDot');
  if (dot) { dot.setAttribute('cx', String(20 + game.curve * 9)); dot.setAttribute('cy', String(20 + sp * 11)); $('strike').style.opacity = game.club === PUTTER ? 0.3 : 1; }
  const capture = !pointerLocked && canvas.requestPointerLock && !lockFailed ? '<b style="color:var(--accent)">Click once to capture the mouse</b><br>' : '';
  const modeHelp = capture + (settings.swingMode === 'natural'
    ? '<kbd>Hold left mouse</kbd> pull back, then push through'
    : settings.swingMode === 'classic' ? '<kbd>Click</kbd> start · <kbd>click</kbd> power · <kbd>click</kbd> on the line' : '<kbd>Hold</kbd> to fill power · release to hit');
  $('hudHelp').innerHTML = modeHelp + '<br>Aim <kbd>mouse</kbd>/<kbd>A</kbd><kbd>D</kbd> · Club <kbd>W</kbd><kbd>S</kbd> · Shot <kbd>Q</kbd><kbd>E</kbd><br>' +
    'Draw/Fade <kbd>Z</kbd><kbd>X</kbd> · Low/High <kbd>C</kbd><kbd>V</kbd> · Spin <kbd>↑</kbd><kbd>↓</kbd> · Style <kbd>F</kbd> · Reset <kbd>R</kbd><br>Map <kbd>M</kbd> · Grid <kbd>G</kbd> · Card <kbd>Tab</kbd> · Menu <kbd>Esc</kbd>';
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

function goMenu() { pausedFrom = null; game.mode = 'menu'; clearWorld(); for (const k in actors) if (actors[k] && actors[k].isObject3D) actors[k].visible = false; if (actors.rig) actors.rig.setVisible(false); updateHud(); showMenu(mainMenu); }

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
    () => { game.daily = id; game.forceTod = -1; startCourse('round', () => core.ng_generate_daily(unix)); });
  button(body, 'Play a shared course', 'Enter a course code from a friend', () => showMenu(codeMenu));
  button(body, 'Practice Range', 'Learn the swing with every club; no score', () => { game.daily = null; startCourse('range'); });
  const me = golferById(settings.golfer);
  button(body, 'Golfer · ' + golferName(me) + ' (' + overall(me) + ' OVR)', 'Choose and rename your golfer', () => showMenu(golferMenu));
  button(body, 'Statistics', 'Your saved rounds on this device', () => showMenu(statsMenu));
  button(body, 'Settings', 'Swing mode, difficulty, sensitivity, units, graphics', () => showMenu((b) => settingsMenu(b, mainMenu)));
  button(body, 'How to play', '', () => showMenu(helpMenu));
}

function golferMenu(body) {
  body.appendChild(el('h2', {}, 'Choose your golfer'));
  for (const g of ROSTER) {
    const stats = g.attr.map((v, i) => ATTR_NAMES[i] + ' ' + Math.round(50 + 49 * v)).join(' · ');
    const b = button(body, (g.id === settings.golfer ? '✓ ' : '') + golferName(g) + ' · ' + overall(g) + ' OVR', g.blurb + '<br>' + stats, () => {
      settings.golfer = g.id; saveSettings(); buildGolferFigure(); showMenu(golferMenu);
    });
    if (g.id === settings.golfer) b.style.borderColor = 'var(--accent)';
  }
  const cur = golferById(settings.golfer);
  const l = el('label', {}, '<span>Name</span>');
  const input = el('input', { type: 'text', maxLength: 24, value: golferName(cur) });
  input.addEventListener('keydown', (e) => e.stopPropagation());
  input.addEventListener('change', () => { const names = store.get('names', {}); const v = input.value.trim(); if (v) names[cur.id] = v; else delete names[cur.id]; store.set('names', names); });
  l.appendChild(input); body.appendChild(l);
  body.appendChild(el('p', { className: 'note' }, 'Attributes shrink or grow your mistakes and your clubhead speed; you still have to make the swing. The Daily Course is always played with standard attributes so scores are comparable.'));
  button(body, 'Done', '', () => showMenu(mainMenu), 'primary');
}

const quick = Object.assign({ env: -1, diff: 1, tee: 1, tod: -1 }, store.get('quick', {}));
function randomMenu(body) {
  body.appendChild(el('h2', {}, 'Random 9'));
  const envs = [{ value: -1, label: 'Surprise me' }];
  for (let i = 0; i < 6; i++) envs.push({ value: i, label: cstr(core.ng_environment_name(i)) });
  select(body, 'Environment', envs, quick.env, (v) => { quick.env = Number(v); });
  select(body, 'Course difficulty', COURSE_DIFF, quick.diff, (v) => { quick.diff = Number(v); });
  select(body, 'Tees', TEES, quick.tee, (v) => { quick.tee = Number(v); });
  select(body, 'Time of day', [{ value: -1, label: 'From the course seed' }].concat(TOD.map((t, i) => ({ value: i, label: t.name }))), quick.tod, (v) => { quick.tod = Number(v); });
  body.appendChild(el('p', { className: 'note' }, 'Course difficulty changes the architecture (fairway width, hazards, green speed). Swing difficulty is separate, under Settings.'));
  button(body, 'Generate and play', '', () => {
    store.set('quick', quick);
    const r = new Uint32Array(2);
    if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(r); else { r[0] = Math.random() * 4294967296; r[1] = Math.random() * 4294967296; }
    const env = quick.env < 0 ? r[0] % 6 : quick.env;
    game.daily = null;
    game.forceTod = quick.tod;
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
    game.forceTod = -1;
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
  body.appendChild(el('p', { className: 'note' }, 'Graphics: Performance has no live shadows or grass; Quality adds dense swaying grass and terrain self-shadowing. A change of graphics takes full effect on the next hole.'));
  slider(body, 'Sound volume', 0, 1, 0.05, settings.volume, (v) => Math.round(v * 100) + '%', (v) => { settings.volume = v; saveSettings(); });
  select(body, 'Shot tracer', ['Off', 'On'], settings.tracer ? 1 : 0, (v) => { settings.tracer = v === '1'; saveSettings(); });
  select(body, 'Putting grid', ['Off', 'On'], settings.puttGrid ? 1 : 0, (v) => { settings.puttGrid = v === '1'; saveSettings(); });
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
      if (!game.puttRead) {
        core.ng_putt_read();
        const b = buf(6 + 900), m = b[5], rp = actors.readGeo.attributes.position;
        for (let i = 0; i < m; i++) rp.setXYZ(i, b[6 + i * 3], b[8 + i * 3] + 0.03, -b[7 + i * 3]);
        rp.needsUpdate = true; actors.readGeo.setDrawRange(0, m);
        game.puttRead = { points: m };
      }
      actors.read.visible = game.puttRead.points > 1;
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
    // Putting: behind the ball and offset away from the golfer, so the stance frames the left of the shot and the hole stays clear.
    const back = putting ? 3.6 : 7.4, up = putting ? 1.75 : 2.9, side = putting ? 0.55 * (settings.leftHanded ? -1 : 1) : 0.6;
    const cx = s.x - fx * back + fy * side, cy = s.y - fy * back - fx * side;
    pos = V(cx, cy, Math.max(heightAt(cx, cy) + up, s.z + up * 0.8));
    const ahead = putting ? Math.min(8, s.toPin + 1) : 45;
    const lx = s.x + fx * ahead, ly = s.y + fy * ahead;
    look = V(lx, ly, putting ? heightAt(lx, ly) : (s.z + heightAt(lx, ly)) / 2 + 1.5);
  }
  if (game.debugCam) { pos = game.debugCam(s, fx, fy) [0]; look = game.debugCam(s, fx, fy)[1]; game.camSnap = true; }   // automated screenshots only
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
  if (game.mode === 'menu' || !game.state) { renderer.render(scene, camera); return; }   // menu: no scene to post-process
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
        game.swingAngle = -Math.min(1.15, p) * (game.club === PUTTER ? 1.6 : 3.6);
        setMeter(p, core.ng_swing_live_lateral() * 3 * (settings.leftHanded ? -1 : 1), p > 1.02 ? 'Overswing!' : Math.round(Math.min(p, 1.1) * 100) + '%');
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
  const showGolfer = aiming || (game.mode === 'flight' && game.playT < (game.outcome && game.outcome.putt ? 0.9 : 1.4));
  poseDt = dt;
  const addr = game.outcome && game.mode === 'flight' ? V(game.outcome.from.x, game.outcome.from.y, game.outcome.from.z + 0.0213) : ball;
  poseGolfer(addr, game.aim, game.swingAngle, showGolfer && !game.overhead);
  if (world.flag) { const near = game.state.toPin < 2.5 && game.club === PUTTER && aiming; world.flag.visible = !near; world.pole.visible = !near; }

  if (world.sun) { world.sun.target.position.copy(ball); world.sun.position.copy(ball).addScaledVector(SUN_DIR, 120); }
  if (world.flag && world.flag.visible) {
    const w = game.info[22];
    world.flag.rotation.y = Math.PI / 2 - game.info[21] * Math.PI / 180 + Math.sin(ms * 0.006) * 0.18 * Math.min(1, 0.3 + w / 5);
  }
  if (world.timeUniforms) for (const u of world.timeUniforms) u.value = ms / 1000;
  if (scene.fog && world.fogDensity) scene.fog.density = world.fogDensity * (game.overhead ? 0.2 : 1);
  if (world.terrainUniforms) {
    const gridOn = settings.puttGrid && game.club === PUTTER && game.state.lie === SURF.Green && (game.mode === 'aim' || game.mode === 'swing') && !game.overhead;
    world.terrainUniforms.uGrid.value = gridOn ? 1 : 0;
    world.terrainUniforms.uGridCenter.value.set(game.state.x, game.state.y);
  }
  updateCamera(dt, ball);
  if (!game.skipDraw) draw();   // skipDraw: automated input tests on machines without a GPU
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
    case 'KeyW': cycleClub(-1); break;
    case 'KeyS': cycleClub(1); break;
    case 'ArrowUp': nudge('spin', -0.25); break;
    case 'ArrowDown': nudge('spin', 0.25); break;
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
    case 'KeyG': settings.puttGrid = !settings.puttGrid; saveSettings(); flashBanner(settings.puttGrid ? 'Putting grid on' : 'Putting grid off', '', 900); break;
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
    beginHole(i) { beginHole(i); },
    setTod(t) { game.course.tod = t; beginHole(game.hole); },
    next() { continueAfterShot(); },
  };
}
boot();
})();
