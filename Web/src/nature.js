// Detailed scenery built procedurally: leafy trees made of many foliage clusters, spectators with
// proper limbs, and rock edging for ponds. Rendering only; golf physics never sees any of this.
(function () {
'use strict';

const UP = new THREE.Vector3(0, 1, 0);

// Merge several geometries (each with an optional transform) into one indexed geometry.
function merge(parts) {
  const pos = [], nor = [], uv = [], idx = [];
  for (const [geo0, mtx] of parts) {
    const geo = geo0.index ? geo0 : geo0;
    const g = geo.clone();
    if (mtx) g.applyMatrix4(mtx);
    const base = pos.length / 3;
    const p = g.attributes.position, n = g.attributes.normal, t = g.attributes.uv;
    for (let i = 0; i < p.count; i++) {
      pos.push(p.getX(i), p.getY(i), p.getZ(i));
      nor.push(n ? n.getX(i) : 0, n ? n.getY(i) : 1, n ? n.getZ(i) : 0);
      uv.push(t ? t.getX(i) : 0, t ? t.getY(i) : 0);
    }
    if (g.index) for (let i = 0; i < g.index.count; i++) idx.push(g.index.getX(i) + base);
    else for (let i = 0; i < p.count; i++) idx.push(base + i);
    g.dispose();
  }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  out.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
  out.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  out.setIndex(idx);
  return out;
}

function rng(seed) { let s = seed >>> 0; return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; }

// ---- Foliage ------------------------------------------------------------------------------------
// A square of hundreds of individual leaves (or needle sprays) with transparent gaps, used on
// crossed cards. Many cards per tree give a ragged, see-through crown instead of a solid blob.
const texCache = {};
function foliageTexture(kind) {
  if (texCache[kind]) return texCache[kind];
  const S = 256, cv = document.createElement('canvas');
  cv.width = cv.height = S;
  const g = cv.getContext('2d'), r = rng(kind === 'pine' ? 7 : 3);
  const greens = kind === 'pine'
    ? ['#1f4a2a', '#2a5d33', '#33703c', '#244f2e', '#3d7d44']
    : ['#2f6b2c', '#3b7f33', '#4b9140', '#5ea34c', '#2a5e28', '#6fb055', '#3f8a3a'];
  const n = kind === 'pine' ? 300 : 430;
  for (let i = 0; i < n; i++) {
    // Denser in the middle, ragged at the edge.
    const a = r() * Math.PI * 2, d = Math.pow(r(), 0.9) * S * 0.46;
    if (d > S * 0.3 && r() < 0.45) continue;                       // thin, broken edge: no hard outline
    const x = S / 2 + Math.cos(a) * d, y = S / 2 + Math.sin(a) * d;
    g.fillStyle = greens[Math.floor(r() * greens.length)];
    g.save(); g.translate(x, y); g.rotate(r() * Math.PI * 2);
    if (kind === 'pine') {
      g.strokeStyle = g.fillStyle; g.lineWidth = 1.6;
      for (let k = 0; k < 7; k++) { g.beginPath(); g.moveTo(0, 0); g.lineTo(-8 + k * 2.6, -14 + Math.abs(k - 3) * 2); g.stroke(); }
    } else {
      const w = 4 + r() * 4, h = 8 + r() * 7;
      g.beginPath(); g.ellipse(0, 0, w, h, 0, 0, Math.PI * 2); g.fill();
      g.fillStyle = 'rgba(255,255,230,0.12)'; g.beginPath(); g.ellipse(-w * 0.25, -h * 0.2, w * 0.4, h * 0.5, 0, 0, Math.PI * 2); g.fill();
    }
    g.restore();
  }
  const t = new THREE.CanvasTexture(cv);
  if ('colorSpace' in t) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  texCache[kind] = t;
  return t;
}

// Three crossed cards with normals bent outward, so a cluster lights like a soft rounded mass.
function clumpGeometry() {
  const parts = [];
  for (let k = 0; k < 3; k++) parts.push([new THREE.PlaneGeometry(1, 1), new THREE.Matrix4().makeRotationY(k * Math.PI / 3)]);
  const g = merge(parts);
  const p = g.attributes.position, n = g.attributes.normal;
  for (let i = 0; i < p.count; i++) {
    const v = new THREE.Vector3(p.getX(i), p.getY(i) + 0.35, p.getZ(i)).normalize();
    n.setXYZ(i, v.x, v.y, v.z);
  }
  return g;
}

// Builds every tree on a hole. trees: flat array of [x, y(sim), z, crownRadius, height, ...].
// lifted(t): whether tree t is a tall background tree with a high crown.
function buildTrees(trees, count, opts) {
  const group = new THREE.Group();
  const pine = !!opts.pine;
  const tex = foliageTexture(pine ? 'pine' : 'leaf');
  // alphaToCoverage: with multisampling, leaf edges are antialiased instead of jagged cut-outs.
  const leafMat = new THREE.MeshLambertMaterial({ map: tex, alphaTest: 0.42, alphaToCoverage: true, side: THREE.DoubleSide });
  if (opts.sway) opts.sway(leafMat);
  const r = rng(opts.seed || 1);
  const clumps = [], trunks = [], branches = [];
  for (let t = 0; t < count; t++) {
    const tx = trees[t * 5], ty = trees[t * 5 + 1], tz = trees[t * 5 + 2], cr = trees[t * 5 + 3], h = trees[t * 5 + 4];
    const lifted = opts.lifted && opts.lifted(t);
    const shade = 0.85 + r() * 0.3;
    if (pine) {
      // Conifer: stacked whorls of needle clusters, narrowing to a point.
      const tiers = 7;
      for (let k = 0; k < tiers; k++) {
        const f = k / (tiers - 1), y = tz + h * (0.28 + 0.68 * f), rad = cr * 1.15 * (1 - f * 0.85);
        const per = Math.max(2, Math.round(7 - f * 5));
        for (let j = 0; j < per; j++) {
          const a = j / per * 6.283 + r() * 0.6;
          clumps.push([tx + Math.cos(a) * rad * 0.55, y, ty + Math.sin(a) * rad * 0.55, rad * 1.1, (0.75 + 0.25 * f) * shade]);
        }
      }
      trunks.push([tx, tz, ty, h * 0.95, 0.16 + cr * 0.04]);
    } else {
      // Broadleaf: a crown of overlapping leaf clusters on an ellipsoid shell, with a few limbs.
      const liftK = lifted ? 0.56 + r() * 0.2 : 0.6;
      const crownY = tz + h * liftK, ry = h * (lifted ? 0.24 + r() * 0.08 : 0.3), rx = cr * (lifted ? 1.4 + r() * 0.5 : 1.25);
      const n = lifted ? 30 : 24;
      for (let j = 0; j < n; j++) {
        const u = r() * 2 - 1, a = r() * 6.283, sr = Math.sqrt(1 - u * u);
        const k = 0.55 + 0.45 * r();
        const x = tx + Math.cos(a) * sr * rx * k, z = ty + Math.sin(a) * sr * rx * k, y = crownY + u * ry * k;
        const lower = (y - crownY) / ry;                          // underside and inside are darker
        clumps.push([x, y, z, cr * (0.55 + r() * 0.4), (0.72 + 0.22 * (lower * 0.5 + 0.5) + 0.12 * k) * shade]);
      }
      trunks.push([tx, tz, ty, h * liftK, 0.2 + cr * 0.05]);
      for (let b = 0; b < 4; b++) {
        const a = b * 1.57 + r();
        branches.push([tx, tz + h * liftK * 0.75, ty, tx + Math.cos(a) * rx * 0.7, crownY + ry * 0.1, ty + Math.sin(a) * rx * 0.7]);
      }
    }
  }
  const leaves = new THREE.InstancedMesh(clumpGeometry(), leafMat, clumps.length);
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), c = new THREE.Color();
  const eul = new THREE.Euler();
  clumps.forEach(([x, y, z, s, k], i) => {
    q.setFromEuler(eul.set((r() - 0.5) * 0.9, (i * 2.399) % 6.283, (r() - 0.5) * 0.9));
    leaves.setMatrixAt(i, m.compose(new THREE.Vector3(x, y, -z), q, new THREE.Vector3(s, s * 0.9, s)));
    leaves.setColorAt(i, c.setRGB(k * (0.95 + r() * 0.1), k, k * (0.9 + r() * 0.1)));
  });
  leaves.castShadow = true;
  leaves.computeBoundingSphere();
  group.add(leaves);
  const barkMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.95 });
  const trunkMesh = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.7, 1, 1, 8).translate(0, 0.5, 0), barkMat, trunks.length);
  const barks = [0x6e5c4a, 0x7a6a58, 0x5f4f40, 0x857463, 0x6b6156];
  trunks.forEach(([x, z0, y, len, rad], i) => {
    q.setFromAxisAngle(new THREE.Vector3(r() - 0.5, 0, r() - 0.5).normalize(), r() * 0.06);   // slight natural lean
    trunkMesh.setMatrixAt(i, m.compose(new THREE.Vector3(x, z0 - 0.1, -y), q, new THREE.Vector3(rad, len, rad)));
    trunkMesh.setColorAt(i, c.set(barks[Math.floor(r() * barks.length)]));
  });
  trunkMesh.castShadow = true;
  group.add(trunkMesh);
  if (branches.length) {
    const limb = new THREE.InstancedMesh(new THREE.CylinderGeometry(0.5, 1, 1, 5).translate(0, 0.5, 0), barkMat, branches.length);
    branches.forEach(([x0, y0, z0, x1, y1, z1], i) => {
      const a = new THREE.Vector3(x0, y0, -z0), b = new THREE.Vector3(x1, y1, -z1), d = b.clone().sub(a), len = d.length();
      q.setFromUnitVectors(UP, d.normalize());
      limb.setMatrixAt(i, m.compose(a, q, new THREE.Vector3(0.09, len, 0.09)));
      limb.setColorAt(i, c.set(0x6e5c4a));
    });
    limb.castShadow = true;
    group.add(limb);
  }
  return group;
}

// ---- Spectators ---------------------------------------------------------------------------------
let personGeo = null;
function personGeometries() {
  if (personGeo) return personGeo;
  const M = (x, y, z, rx, ry, rz) => new THREE.Matrix4().compose(new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(rx || 0, ry || 0, rz || 0)), new THREE.Vector3(1, 1, 1));
  personGeo = {
    legs: merge([[new THREE.CapsuleGeometry(0.075, 0.72, 4, 8), M(-0.1, 0.47, 0)], [new THREE.CapsuleGeometry(0.075, 0.72, 4, 8), M(0.1, 0.47, 0)],
                 [new THREE.BoxGeometry(0.13, 0.07, 0.26), M(-0.1, 0.04, 0.05)], [new THREE.BoxGeometry(0.13, 0.07, 0.26), M(0.1, 0.04, 0.05)]]),
    torso: merge([[new THREE.CapsuleGeometry(0.19, 0.38, 4, 12), M(0, 1.15, 0)], [new THREE.CapsuleGeometry(0.06, 0.48, 4, 8), M(-0.25, 1.12, 0, 0, 0, 0.08)],
                  [new THREE.CapsuleGeometry(0.06, 0.48, 4, 8), M(0.25, 1.12, 0, 0, 0, -0.08)]]),
    skin: merge([[new THREE.SphereGeometry(0.105, 14, 10), M(0, 1.6, 0)], [new THREE.CylinderGeometry(0.05, 0.055, 0.1, 8), M(0, 1.48, 0)],
                 [new THREE.SphereGeometry(0.045, 8, 6), M(-0.27, 0.82, 0)], [new THREE.SphereGeometry(0.045, 8, 6), M(0.27, 0.82, 0)]]),
    hair: merge([[new THREE.SphereGeometry(0.11, 14, 8, 0, Math.PI * 2, 0, Math.PI * 0.55), M(0, 1.62, -0.01)]]),
    cap: merge([[new THREE.SphereGeometry(0.113, 14, 8, 0, Math.PI * 2, 0, Math.PI / 2), M(0, 1.63, 0)], [new THREE.CylinderGeometry(0.08, 0.08, 0.01, 14, 1, false, -Math.PI / 2, Math.PI), M(0, 1.64, 0.09)]]),
  };
  return personGeo;
}
// people: [x, y(sim), z, faceAngle, seated]
function buildCrowd(people, seed, shadows) {
  const geo = personGeometries(), n = people.length, r = rng(seed);
  const mat = () => new THREE.MeshStandardMaterial({ roughness: 0.85 });
  const meshes = { legs: new THREE.InstancedMesh(geo.legs, mat(), n), torso: new THREE.InstancedMesh(geo.torso, mat(), n),
                   skin: new THREE.InstancedMesh(geo.skin, mat(), n), hair: new THREE.InstancedMesh(geo.hair, mat(), n), cap: new THREE.InstancedMesh(geo.cap, mat(), n) };
  const shirts = [0xffffff, 0x1d4e89, 0xc0392b, 0x8ecae6, 0x2a9d8f, 0xf4a261, 0xc9184a, 0x6c757d, 0xf1faee, 0x3a5a40, 0xffb4c6, 0x264653, 0xe9c46a, 0x9b5de5];
  const bottoms = [0x2b2d42, 0xd6c7a1, 0x495057, 0x1b263b, 0xe9e4d8, 0x6b705c, 0x3d405b];
  const skins = [0xf1c27d, 0xc68642, 0x8d5524, 0xe0ac69, 0xffdbac, 0x5c3a21, 0xd9a066];
  const hairs = [0x1b1b1b, 0x3b2a20, 0x6a4a32, 0xb08850, 0x8a8a8a, 0xd8c08a];
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), c = new THREE.Color(), hide = new THREE.Vector3(0.0001, 0.0001, 0.0001);
  people.forEach(([x, y, z, face, seated], i) => {
    const tall = 0.92 + r() * 0.16, wide = 0.9 + r() * 0.25;
    q.setFromAxisAngle(UP, face + (r() - 0.5) * 0.5);
    const pos = new THREE.Vector3(x, z - (seated ? 0.45 : 0), -y), sc = new THREE.Vector3(wide, tall, wide);
    m.compose(pos, q, sc);
    for (const k of ['legs', 'torso', 'skin']) meshes[k].setMatrixAt(i, m);
    const capOn = r() < 0.4;
    meshes.cap.setMatrixAt(i, capOn ? m : m.clone().compose(pos, q, hide));
    meshes.hair.setMatrixAt(i, capOn ? m.clone().compose(pos, q, hide) : m);
    meshes.legs.setColorAt(i, c.set(bottoms[Math.floor(r() * bottoms.length)]));
    meshes.torso.setColorAt(i, c.set(shirts[Math.floor(r() * shirts.length)]));
    meshes.skin.setColorAt(i, c.set(skins[Math.floor(r() * skins.length)]));
    meshes.hair.setColorAt(i, c.set(hairs[Math.floor(r() * hairs.length)]));
    meshes.cap.setColorAt(i, c.set(shirts[Math.floor(r() * shirts.length)]));
  });
  const g = new THREE.Group();
  for (const k in meshes) { meshes[k].castShadow = !!shadows; meshes[k].computeBoundingSphere(); g.add(meshes[k]); }
  return g;
}

// ---- Pond rock edging --------------------------------------------------------------------------
function buildRockEdge(cx, cy, rx, ry, rotDeg, heightAt, seed) {
  const r = rng(seed), rocks = [];
  const rot = rotDeg * Math.PI / 180;
  const per = 2 * Math.PI * Math.sqrt((rx * rx + ry * ry) / 2);
  const n = Math.min(900, Math.floor(per / 0.55));
  for (let i = 0; i < n; i++) {
    const a = i / n * 6.283 + r() * 0.01;
    for (let row = 0; row < 2; row++) {
      const k = 1.0 + row * 0.035 + (r() - 0.5) * 0.02;
      const u = Math.cos(a) * rx * k, v = Math.sin(a) * ry * k;
      // Same frame as the core's Ellipse: major axis along heading rotDeg.
      const x = cx + u * Math.cos(rot) + v * Math.sin(rot), y = cy - u * Math.sin(rot) + v * Math.cos(rot);
      rocks.push([x, y, heightAt(x, y), 0.25 + r() * 0.3, r()]);
    }
  }
  const mesh = new THREE.InstancedMesh(new THREE.DodecahedronGeometry(1, 0), new THREE.MeshStandardMaterial({ color: 0x9a9893, roughness: 0.9, flatShading: true }), rocks.length);
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), c = new THREE.Color(), e = new THREE.Euler();
  rocks.forEach(([x, y, z, s, v], i) => {
    q.setFromEuler(e.set(v * 3, v * 7, v * 5));
    mesh.setMatrixAt(i, m.compose(new THREE.Vector3(x, z - s * 0.25, -y), q, new THREE.Vector3(s * 1.2, s * 0.7, s)));
    const g = 0.75 + v * 0.35;
    mesh.setColorAt(i, c.setRGB(g, g * 0.98, g * 0.95));
  });
  mesh.castShadow = true; mesh.receiveShadow = true;
  mesh.computeBoundingSphere();
  return mesh;
}

window.GolfNature = { buildTrees, buildCrowd, buildRockEdge, merge };
})();
