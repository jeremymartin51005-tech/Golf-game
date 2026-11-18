// Articulated golfer: a jointed body (pelvis, two-segment spine, neck, head, arms, legs, feet) posed
// every frame by golf-swing kinematics: hip and shoulder turn, spine tilt, wrist hinge, weight shift,
// two-bone IK for arms and legs, and a trail heel that releases in the follow-through.
// Rendering only. All proportions scale with the character's height and build.
(function () {
'use strict';

const UP = new THREE.Vector3(0, 1, 0);
const tmpM = new THREE.Matrix4();
const tmpQ = new THREE.Quaternion();
const V3 = () => new THREE.Vector3();

// Tapered limb with rounded ends, unit length along +Y, radii r0 (start) -> r1 (end).
function limbGeometry(r0, r1, segs) {
  const pts = [];
  const n = 6;
  for (let i = 0; i <= n; i++) { const a = -Math.PI / 2 + (i / n) * Math.PI / 2; pts.push(new THREE.Vector2(Math.cos(a) * r0, (Math.sin(a) + 1) * r0 * 0.6)); }
  for (let i = 1; i < 8; i++) { const t = i / 8; const r = r0 + (r1 - r0) * t + Math.sin(t * Math.PI) * (r0 + r1) * 0.06; pts.push(new THREE.Vector2(r, r0 * 0.6 + t * (1 - 0.6 * (r0 + r1)))); }
  for (let i = 0; i <= n; i++) { const a = (i / n) * Math.PI / 2; pts.push(new THREE.Vector2(Math.cos(a) * r1, 1 - 0.6 * r1 + Math.sin(a) * r1 * 0.6)); }
  return new THREE.LatheGeometry(pts, segs || 22);
}
// Torso shell from waist (y=0) to base of neck (y=1); radius in units of half shoulder width.
function torsoGeometry(profile) {
  const pts = profile.map(([r, y]) => new THREE.Vector2(r, y));
  return new THREE.LatheGeometry(pts, 36);
}
function place(mesh, a, b) {          // orient a unit +Y mesh from point a to point b
  const d = V3().subVectors(b, a);
  const len = d.length();
  mesh.position.copy(a);
  mesh.quaternion.setFromUnitVectors(UP, d.multiplyScalar(1 / Math.max(len, 1e-6)));
  mesh.scale.set(1, len, 1);
}
function basis(mesh, origin, x, y, z, sx, sy, sz) {   // place a mesh with an explicit frame and scale
  tmpM.makeBasis(x.clone().multiplyScalar(sx), y.clone().multiplyScalar(sy), z.clone().multiplyScalar(sz));
  tmpM.setPosition(origin);
  mesh.matrixAutoUpdate = false;
  mesh.matrix.copy(tmpM);
}
function rotateAbout(v, axis, angle) { return v.clone().applyAxisAngle(axis, angle); }
function ik(root, target, l1, l2, pole) {
  const d = V3().subVectors(target, root);
  let dist = d.length();
  const dir = d.clone().multiplyScalar(1 / Math.max(dist, 1e-6));
  dist = Math.min(dist, l1 + l2 - 1e-4);
  const a = (l1 * l1 + dist * dist - l2 * l2) / (2 * dist);
  const h = Math.sqrt(Math.max(l1 * l1 - a * a, 0));
  const pd = pole.clone().sub(dir.clone().multiplyScalar(pole.dot(dir))).normalize();
  const elbow = root.clone().addScaledVector(dir, a).addScaledVector(pd, h);
  const end = root.clone().addScaledVector(dir, dist);
  return { elbow, end };
}
const smooth = (e0, e1, x) => { const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); };

// Classic kitchen check: a small two-tone houndstooth-like grid, tiled along the trouser legs.
function checkTexture(base) {
  const cv = document.createElement('canvas'); cv.width = cv.height = 64;
  const g = cv.getContext('2d');
  const c = new THREE.Color(base);
  g.fillStyle = '#e9e9e4'; g.fillRect(0, 0, 64, 64);
  g.fillStyle = '#' + c.getHexString();
  for (let y = 0; y < 64; y += 16) for (let x = 0; x < 64; x += 16) { if (((x + y) / 16) % 2 === 0) g.fillRect(x, y, 16, 16); g.fillRect(x + 6, y, 4, 16); }
  const t = new THREE.CanvasTexture(cv);
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(4, 6);
  if ('colorSpace' in t) t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

class GolferRig {
  constructor(look, scene) {
    this.look = look;
    this.group = new THREE.Group();
    scene.add(this.group);
    const L = look, b = L.build;
    // Physical materials: fabrics get a soft sheen at grazing angles, skin a warm sheen that reads as
    // light scattering under the surface.
    const std = (c, r, extra) => new THREE.MeshPhysicalMaterial(Object.assign({ color: c, roughness: r === undefined ? 0.8 : r, metalness: 0,
      sheen: (r === undefined ? 0.8 : r) > 0.6 ? 0.6 : 0.25, sheenRoughness: 0.6, sheenColor: new THREE.Color(c).lerp(new THREE.Color(0xffffff), 0.5) }, extra || {}));
    this.mat = {
      skin: std(L.skin, 0.55, { emissive: new THREE.Color(L.skin).multiplyScalar(0.06) }),
      shirt: std(L.shirt, 0.85), trousers: std(L.trousers, 0.8, L.trouserCheck ? { map: checkTexture(L.trousers) } : undefined), shoe: std(L.shoe || 0xf4f4f2, 0.5), sole: std(0x2a2a2a, 0.9),
      hair: std(L.hair, 0.9), hat: std(L.hatColor, 0.75), glove: std(0xfafafa, 0.7), belt: std(0x1d1d1f, 0.5),
      eye: std(0x1b1b1b, 0.3), steel: std(0xc9ccd1, 0.25, { metalness: 0.9 }), grip: std(0x1a1a1a, 0.8), head: std(0x2b2e33, 0.35, { metalness: 0.7 }),
      apron: std(L.apronColor || 0xf7f7f2, 0.85),
    };
    const add = (geo, mat) => { const m = new THREE.Mesh(geo, mat); m.castShadow = true; this.group.add(m); return m; };
    const sph = (r) => new THREE.SphereGeometry(r, 20, 14);
    const p = this.parts = {};
    // Torso: lower (abdomen, follows the hips) and upper (chest, follows the shoulders).
    p.abdomen = add(torsoGeometry([[0.0, 0.0], [0.80, 0.0], [0.86, 0.12], [0.84, 0.45], [0.88, 0.85], [0.9, 1.0], [0.0, 1.0]]), this.mat.shirt);
    p.chest = add(torsoGeometry([[0.0, 0.0], [0.9, 0.0], [0.97, 0.25], [1.0, 0.55], [0.92, 0.8], [0.62, 0.94], [0.3, 1.0], [0.0, 1.0]]), this.mat.shirt);
    p.pelvis = add(sph(1), this.mat.trousers);
    p.belt = add(new THREE.TorusGeometry(1, 0.06, 8, 28), this.mat.belt);
    p.collar = add(new THREE.TorusGeometry(1, 0.28, 8, 20), this.mat.shirt);
    p.neck = add(limbGeometry(0.055, 0.05), this.mat.skin);
    // Head with features.
    p.head = new THREE.Group(); this.group.add(p.head);
    const hm = (geo, mat, x, y, z, sx, sy, sz) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); if (sx) m.scale.set(sx, sy, sz); m.castShadow = true; p.head.add(m); return m; };
    hm(sph(0.1), this.mat.skin, 0, 0, 0, 0.92, 1.12, 1.0);                       // skull and face (local +Z = facing)
    hm(sph(0.06), this.mat.skin, 0, -0.07, 0.03, 0.95, 0.75, 1.0);                // jaw
    hm(new THREE.ConeGeometry(0.016, 0.04, 8), this.mat.skin, 0, -0.005, 0.105).rotation.x = Math.PI / 2; // nose
    for (const s of [-1, 1]) {
      hm(sph(0.022), this.mat.skin, s * 0.092, 0.0, -0.005, 0.5, 1, 0.8);           // ears
      hm(sph(0.011), this.mat.eye, s * 0.035, 0.028, 0.088);                        // eyes
      hm(new THREE.BoxGeometry(0.03, 0.006, 0.01), this.mat.hair, s * 0.035, 0.05, 0.092); // brows
    }
    hm(new THREE.BoxGeometry(0.035, 0.006, 0.01), std(0x9a5a4e, 0.6), 0, -0.05, 0.097); // mouth
    hm(sph(0.104), this.mat.hair, 0, 0.022, -0.012, 0.95, 1.05, 1.0);               // hair
    if (L.beard) hm(sph(0.07), this.mat.hair, 0, -0.07, 0.035, 1.05, 0.8, 0.95);
    if (L.hat === 'cap') {
      hm(new THREE.SphereGeometry(0.108, 20, 10, 0, Math.PI * 2, 0, Math.PI / 2), this.mat.hat, 0, 0.035, -0.005, 1, 0.85, 1.05);
      hm(new THREE.CylinderGeometry(0.075, 0.075, 0.008, 20, 1, false, -Math.PI / 2, Math.PI), this.mat.hat, 0, 0.04, 0.085, 1, 1, 0.9);
    } else if (L.hat === 'toque') {
      // Pleated chef's toque: a band, a fluted body, and a soft puffed crown.
      hm(new THREE.CylinderGeometry(0.106, 0.108, 0.05, 28), this.mat.hat, 0, 0.07, -0.005);
      const body = new THREE.CylinderGeometry(0.115, 0.104, 0.16, 48, 1, true);
      const bp = body.attributes.position;
      for (let i = 0; i < bp.count; i++) { const a = Math.atan2(bp.getZ(i), bp.getX(i)); const k = 1 + 0.06 * Math.abs(Math.sin(a * 8)); bp.setX(i, bp.getX(i) * k); bp.setZ(i, bp.getZ(i) * k); }
      body.computeVertexNormals();
      hm(body, this.mat.hat, 0, 0.17, -0.005);
      hm(sph(0.15), this.mat.hat, 0, 0.27, -0.005, 1, 0.55, 1);
    } else if (L.hat === 'headband') {
      hm(new THREE.CylinderGeometry(0.106, 0.106, 0.028, 22, 1, true), this.mat.hat, 0, 0.045, -0.006);
    }
    // Arms (lead = L for a right-hander), legs, feet.
    for (const s of ['L', 'R']) {
      p['sleeve' + s] = add(limbGeometry(0.062 * b, 0.052 * b), this.mat.shirt);
      p['upper' + s] = add(limbGeometry(0.048 * b, 0.04 * b), this.mat.skin);
      p['fore' + s] = add(limbGeometry(0.04 * b, 0.03), this.mat.skin);
      p['hand' + s] = add(sph(0.042), s === 'L' ? this.mat.glove : this.mat.skin);
      p['thigh' + s] = add(limbGeometry(0.088 * b, 0.06 * Math.sqrt(b)), this.mat.trousers);
      p['shin' + s] = add(limbGeometry(0.06 * Math.sqrt(b), 0.045), this.mat.trousers);
      p['shoe' + s] = add(new THREE.CapsuleGeometry(0.045, 0.17, 6, 12), this.mat.shoe);
      p['sole' + s] = add(new THREE.BoxGeometry(0.1, 0.018, 0.27), this.mat.sole);
    }
    // Character extras.
    if (L.chest) { p.chestL2 = add(sph(1), this.mat.shirt); p.chestR2 = add(sph(1), this.mat.shirt); }
    if (L.belly) p.belly = add(sph(1), L.apron ? this.mat.apron : this.mat.shirt);
    if (L.rear) { p.rearL = add(sph(1), this.mat.trousers); p.rearR = add(sph(1), this.mat.trousers); p.rearC = add(sph(1), this.mat.trousers); }
    if (L.chefJacket) {
      this.mat.button = std(0x2a2a2a, 0.4);
      p.buttons = [];
      for (let i = 0; i < 6; i++) p.buttons.push(add(sph(0.011), this.mat.button));
    }
    if (L.neckerchief) { this.mat.kerchief = std(L.neckerchief, 0.8); p.kerchief = add(new THREE.TorusGeometry(1, 0.35, 8, 20), this.mat.kerchief); p.knot = add(sph(1), this.mat.kerchief); }
    if (L.apron) { p.tieL = add(sph(1), this.mat.apron); p.tieR = add(sph(1), this.mat.apron); }
    if (L.apron) p.apron = add(sph(1), this.mat.apron);
    // Club.
    p.shaft = add(new THREE.CylinderGeometry(0.0065, 0.0045, 1, 8).translate(0, 0.5, 0), this.mat.steel);
    p.grip = add(new THREE.CylinderGeometry(0.012, 0.01, 1, 10).translate(0, 0.5, 0), this.mat.grip);
    p.clubHead = add(new THREE.BoxGeometry(1, 1, 1), this.mat.head);
    this.club = 'iron';
    this.time = 0;
    this.jiggle = {};            // per-part spring followers for secondary motion
  }

  dispose(scene) {
    scene.remove(this.group);
    this.group.traverse((o) => { if (o.geometry) o.geometry.dispose(); });
    for (const k in this.mat) this.mat[k].dispose();
  }

  setVisible(v) { this.group.visible = v; }

  // Underdamped spring follower: returns how far the jiggling part trails its rest position.
  spring(key, target, dt, stiffness, damping, maxOffset) {
    let j = this.jiggle[key];
    if (!j || !dt || dt > 0.1) { j = this.jiggle[key] = { p: target.clone(), v: new THREE.Vector3() }; return new THREE.Vector3(); }
    const steps = Math.max(1, Math.ceil(dt / 0.004));
    const h = dt / steps;
    for (let i = 0; i < steps; i++) {
      const acc = target.clone().sub(j.p).multiplyScalar(stiffness).addScaledVector(j.v, -damping);
      j.v.addScaledVector(acc, h);
      j.p.addScaledVector(j.v, h);
    }
    const off = j.p.clone().sub(target);
    j.last = off.length();
    if (off.length() > maxOffset) { off.setLength(maxOffset); j.p.copy(target).add(off); }
    return off;
  }

  // ball: Vector3 at the ball's centre. aimDeg: sim heading. theta: club rotation in the swing plane
  // (negative = backswing, 0 = impact, positive = follow-through). kind: 'driver' | 'wood' | 'iron' | 'wedge' | 'putter'.
  pose(ball, aimDeg, theta, kind, leftHanded, groundAt, dtIn) {
    const dt = dtIn || 0;
    this.time += dt || 0;
    const L = this.look, H = 1.8 * L.height, b = L.build, P = this.parts;
    const lh = leftHanded ? -1 : 1;
    const a = aimDeg * Math.PI / 180;
    const A = new THREE.Vector3(Math.sin(a), 0, -Math.cos(a));                 // toward the target
    const F = new THREE.Vector3().crossVectors(A, UP).multiplyScalar(lh);        // golfer -> ball
    const putting = kind === 'putter';
    const club = { driver: [1.14, 47, 0.13, 0.46], wood: [1.07, 52, 0.08, 0.42], iron: [0.95, 61, 0.03, 0.36], wedge: [0.89, 63, 0.0, 0.33], putter: [0.86, 72, 0.02, 0.3] }[kind] || [0.95, 61, 0.03, 0.36];
    const [clubLen, lieDeg, ballFwd, stanceW] = club;

    const back = putting ? Math.min(1, -theta / 0.9) : Math.min(1.15, Math.max(0, -theta / 4.5));
    const thru = putting ? Math.min(1, Math.max(0, theta / 0.9)) : Math.min(1, Math.max(0, theta / 4.8));
    const idle = Math.sin(this.time * 1.4) * 0.004;

    // Feet and stance.
    const ground = ball.clone(); ground.y = groundAt(ball.x, -ball.z);
    const lieA = lieDeg * Math.PI / 180;
    const shaft0 = V3().addScaledVector(F, -Math.cos(lieA)).addScaledVector(UP, Math.sin(lieA)).addScaledVector(A, putting ? 0.0 : 0.08).normalize();
    const hands0 = ground.clone().addScaledVector(shaft0, clubLen);
    const standoff = Math.max(0.3, V3().subVectors(hands0, ground).dot(F) * -1) + (putting ? 0.22 : 0.27) * H / 1.8;
    const mid = ground.clone().addScaledVector(F, -standoff).addScaledVector(A, -ballFwd * lh * 0 - ballFwd);
    const w = stanceW * H / 1.8 * (0.85 + 0.15 * b);
    const leadSide = A.clone();                                                  // lead foot is toward the target
    const footLead = mid.clone().addScaledVector(leadSide, w / 2), footTrail = mid.clone().addScaledVector(leadSide, -w / 2);
    footLead.y = groundAt(footLead.x, -footLead.z); footTrail.y = groundAt(footTrail.x, -footTrail.z);

    // Body turn and posture through the swing.
    const turnS = (putting ? 0.18 : 1.62) * back - (putting ? 0.2 : 1.85) * thru;          // shoulders, radians, + = away from target
    const turnH = (putting ? 0.02 : 0.75) * back - (putting ? 0.02 : (0.7 * smooth(0, 0.18, thru) + 0.95 * thru)) ;
    const bend = (putting ? 0.75 : 0.63) - (putting ? 0 : 0.5 * smooth(0.2, 1, thru));       // forward spine tilt
    const sideBend = (putting ? 0.03 : 0.14) * (1 - thru) - 0.12 * thru;                     // trail shoulder lower at address
    const shift = (-0.03 * back + 0.11 * thru) * H / 1.8;
    const hipH = (putting ? 0.5 : 0.495) * H + idle;
    const pelvis = mid.clone().addScaledVector(F, -0.07 * H / 1.8).addScaledVector(A, shift);
    pelvis.y = (footLead.y + footTrail.y) / 2 + hipH;

    // Spine frame. Start upright, bend toward the ball, then side-bend in the plane of the target line.
    const trailAway = A.clone().multiplyScalar(-1);
    let spine = UP.clone().applyAxisAngle(A.clone().cross(UP).normalize().multiplyScalar(-lh), 0).normalize();
    spine = UP.clone().multiplyScalar(Math.cos(bend)).addScaledVector(F, Math.sin(bend)).normalize();
    spine = spine.clone().multiplyScalar(Math.cos(sideBend)).addScaledVector(trailAway, Math.sin(sideBend) * 0.6).normalize();
    const Ls = 0.3 * H;
    const midSpine = pelvis.clone().addScaledVector(spine, Ls * 0.45);
    const neckBase = pelvis.clone().addScaledVector(spine, Ls);
    const lineAt = (turn) => {       // shoulder/hip line (lead side) rotated about the spine
      const base = A.clone().sub(spine.clone().multiplyScalar(A.dot(spine))).normalize();
      return base.applyAxisAngle(spine, turn * lh);
    };
    const S = lineAt(turnS), Hl = lineAt(turnH);
    const halfS = 0.118 * H * (0.9 + 0.1 * b), halfH = 0.09 * H * (0.8 + 0.2 * b);
    const shoulderL = neckBase.clone().addScaledVector(S, halfS).addScaledVector(spine, -0.035 * H);
    const shoulderR = neckBase.clone().addScaledVector(S, -halfS).addScaledVector(spine, -0.035 * H);
    const hipL = pelvis.clone().addScaledVector(Hl, halfH), hipR = pelvis.clone().addScaledVector(Hl, -halfH);

    // Hands and club in the swing plane.
    const hub = pelvis.clone().addScaledVector(spine, Ls * 0.82);
    let Np = V3().crossVectors(A, V3().subVectors(hub, ground)).normalize();
    const probe = V3().subVectors(hands0, hub).applyAxisAngle(Np, -0.1).add(hub);
    if (V3().subVectors(probe, hands0).dot(A) > 0) Np.multiplyScalar(-1);       // backswing moves the hands away from the target
    const armRot = putting ? theta : theta * 0.52;                 // the rest of the club's rotation is wrist hinge
    const handsGoal = V3().subVectors(hands0, hub).applyAxisAngle(Np, -armRot).add(hub);
    // Hands lift and set as the shoulders turn (a little extra height at the top and finish).
    handsGoal.addScaledVector(UP, (putting ? 0 : 0.1 * H / 1.8) * (smooth(0.5, 1, back) + smooth(0.5, 1, thru)));
    const clubDir = V3().subVectors(ground, hands0).normalize().applyAxisAngle(Np, -theta);

    // Arms: two-bone IK to the grip (lead hand on top, trail hand below it).
    const lu = 0.172 * H, lf = 0.158 * H;
    const leadShoulder = lh > 0 ? shoulderL : shoulderR, trailShoulder = lh > 0 ? shoulderR : shoulderL;
    const pole = UP.clone().multiplyScalar(-1).addScaledVector(F, -0.4);
    const leadArm = ik(leadShoulder, handsGoal, lu, lf + 0.04 * H, pole);
    const handLead = leadArm.end;
    const handTrail = handLead.clone().addScaledVector(clubDir, 0.085);
    const trailArm = ik(trailShoulder, handTrail, lu, lf + 0.04 * H, pole.clone().addScaledVector(A, -0.6 * lh));
    const headPos = handLead.clone().addScaledVector(clubDir, clubLen);

    // Legs: knees flex toward the ball; the trail heel lifts and the trail knee drives in the finish.
    const ankleLead = footLead.clone().addScaledVector(UP, 0.075), ankleTrail = footTrail.clone().addScaledVector(UP, 0.075 + 0.08 * thru).addScaledVector(A, 0.06 * thru);
    const legL = 0.25 * H;
    const kneePole = F.clone().addScaledVector(UP, 0.2);
    const leadHip = lh > 0 ? hipL : hipR, trailHip = lh > 0 ? hipR : hipL;
    const legLead = ik(leadHip, ankleLead, legL, legL, kneePole);
    const legTrail = ik(trailHip, ankleTrail, legL, legL, kneePole.clone().addScaledVector(A, 0.9 * thru));

    // ---- Place meshes -------------------------------------------------------------------------------
    const side = (s, lead, trail) => (lh > 0 ? (s === 'L' ? lead : trail) : (s === 'L' ? trail : lead));
    const fwd = (line) => V3().crossVectors(line, spine).multiplyScalar(lh).normalize();    // chest-facing direction (toward the ball at address)
    const Zs = fwd(S), Zh = fwd(Hl);
    const chestBase = pelvis.clone().addScaledVector(spine, Ls * 0.42);
    basis(P.abdomen, pelvis.clone().addScaledVector(spine, -0.02 * H), Hl.clone().lerp(S, 0.4).normalize(), spine, Zh.clone().lerp(Zs, 0.4).normalize(), halfH * 1.05, Ls * 0.47, halfH * 0.78 * b);
    basis(P.chest, chestBase, S, spine, Zs, halfS * 0.95, Ls * 0.62, halfS * 0.62 * (0.8 + 0.2 * b));
    basis(P.pelvis, pelvis.clone().addScaledVector(spine, -0.03 * H), Hl, spine, Zh, halfH * 1.15, 0.085 * H, halfH * 0.85 * b);
    basis(P.belt, pelvis.clone().addScaledVector(spine, 0.012 * H), Hl, Zh, spine, halfH * 1.04, halfH * 0.8 * b, 0.25);
    basis(P.collar, neckBase.clone().addScaledVector(spine, -0.005 * H), S, Zs, spine, 0.062, 0.058, 0.3);
    const neckTop = neckBase.clone().addScaledVector(spine, 0.045 * H).addScaledVector(UP, 0.02 * H);
    place(P.neck, neckBase.clone().addScaledVector(spine, -0.01 * H), neckTop);
    // Head: eyes on the ball until after impact, then the head releases toward the target.
    const look = putting ? ground.clone() : ground.clone().lerp(ground.clone().addScaledVector(A, 40).add(V3().set(0, 6, 0)), smooth(0.15, 0.6, thru));
    const headC = neckTop.clone().addScaledVector(UP, 0.07 * H / 1.8 * 1.0).addScaledVector(F, 0.01);
    P.head.position.copy(headC);
    P.head.scale.setScalar(H / 1.8 * 1.05);
    tmpM.lookAt(look, headC, UP);           // three's lookAt points -Z at the target; the face is +Z
    P.head.quaternion.setFromRotationMatrix(tmpM);              // Matrix4.lookAt(eye=look, target=head): +Z points at what he is looking at
    for (const s of ['L', 'R']) {
      const sh = side(s, leadShoulder, trailShoulder), arm = side(s, leadArm, trailArm), hand = side(s, handLead, handTrail);
      place(P['sleeve' + s], sh, sh.clone().lerp(arm.elbow, 0.48));
      place(P['upper' + s], sh, arm.elbow);
      place(P['fore' + s], arm.elbow, hand);
      P['hand' + s].position.copy(hand);
      P['hand' + s].scale.set(1, 1.15, 0.9);
      const hip = side(s, leadHip, trailHip), leg = side(s, legLead, legTrail), ankle = side(s, ankleLead, ankleTrail), foot = side(s, footLead, footTrail);
      place(P['thigh' + s], hip, leg.elbow);
      place(P['shin' + s], leg.elbow, ankle);
      // Shoes point at the ball line, lead foot flared toward the target; trail shoe rolls onto its toe.
      const flare = (s === 'L') === (lh > 0) ? 0.35 : 0.12;
      const toe = F.clone().applyAxisAngle(UP, flare * (A.dot(V3().crossVectors(UP, F)) > 0 ? 1 : -1) * ((s === 'L') === (lh > 0) ? -1 : 1)).normalize();
      const isTrail = (s === 'L') !== (lh > 0);
      const pitch = isTrail ? 0.6 * thru : 0;
      const sideV = V3().crossVectors(UP, toe).normalize();
      const toeDir = toe.clone().applyAxisAngle(sideV, -pitch);
      const shoeC = foot.clone().addScaledVector(UP, 0.05 + (isTrail ? 0.07 * thru : 0)).addScaledVector(toeDir, 0.06);
      const upV = V3().crossVectors(toeDir, sideV).normalize().multiplyScalar(-1);
      basis(P['shoe' + s], shoeC, sideV, toeDir, upV, 1, 1, 0.82);
      basis(P['sole' + s], shoeC.clone().addScaledVector(upV, -0.035), sideV, upV, toeDir, 1, 1, 1);
    }
    // Character extras ride on the torso frames.
    if (P.chestL2) {
      for (const [m, s] of [[P.chestL2, 1], [P.chestR2, -1]]) {
        const c = pelvis.clone().addScaledVector(spine, Ls * 0.74).addScaledVector(S, s * halfS * 0.42).addScaledVector(Zs, halfS * 0.5 * b);
        basis(m, c, S, spine, Zs, 0.085 * b, 0.08 * b, 0.07 * b);
      }
    }
    const bellyK = L.belly === undefined ? 0 : L.belly;
    if (P.belly) basis(P.belly, pelvis.clone().addScaledVector(spine, Ls * 0.3).addScaledVector(Zh, halfH * (0.45 + 0.17 * bellyK) * b), Hl, spine, Zh, halfH * (0.8 + 0.15 * bellyK) * b, Ls * 0.3, halfH * (0.45 + 0.15 * bellyK) * b);
    if (P.rearL) {
      // Fuller, rounder seat: two lobes set high on the hips and projecting back, with a centre fill so
      // the silhouette reads as one smooth curve from the waist to the thighs.
      const k = L.rear;
      for (const [m, sgn, key] of [[P.rearL, 1, 'L'], [P.rearR, -1, 'R']]) {
        const c = pelvis.clone().addScaledVector(spine, -0.02 * H).addScaledVector(Hl, sgn * halfH * 0.48).addScaledVector(Zh, -halfH * (0.55 + 0.22 * k) * b);
        // Secondary motion: each lobe lags behind the hips on an underdamped spring, so the hip turn
        // and weight shift of the swing set it wobbling, and it settles a moment after the body stops.
        const off = this.spring(key, c, dt, 210, 7.5, 0.07);
        const squash = 1 + Math.min(0.12, off.length() * 2.2);
        basis(m, c.clone().add(off), Hl, spine, Zh, 0.075 * (1 + 0.32 * k) * b * squash, 0.085 * (1 + 0.28 * k) * b / squash, 0.07 * (1 + 0.4 * k) * b * squash);
      }
      basis(P.rearC, pelvis.clone().addScaledVector(spine, 0.005 * H).addScaledVector(Zh, -halfH * (0.42 + 0.18 * k) * b), Hl, spine, Zh, halfH * 0.95 * b, 0.075 * (1 + 0.2 * k) * b, 0.06 * (1 + 0.35 * k) * b);
    }
    if (P.buttons) {
      // Double-breasted jacket: two vertical rows of buttons on the chest.
      for (let i = 0; i < 6; i++) {
        const row = i % 2 === 0 ? 1 : -1, level = Math.floor(i / 2);
        const c = pelvis.clone().addScaledVector(spine, Ls * (0.6 + 0.11 * level)).addScaledVector(S, row * halfS * 0.32).addScaledVector(Zs, halfS * 0.58 * (0.8 + 0.2 * b) + 0.004);
        P.buttons[i].position.copy(c);
      }
    }
    if (P.kerchief) {
      basis(P.kerchief, neckBase.clone().addScaledVector(spine, 0.01 * H), S, Zs, spine, 0.058, 0.055, 0.35);
      basis(P.knot, neckBase.clone().addScaledVector(Zs, 0.06).addScaledVector(spine, -0.005 * H), S, spine, Zs, 0.03, 0.035, 0.022);
    }
    if (P.tieL) for (const [m, sgn] of [[P.tieL, 1], [P.tieR, -1]]) {
      basis(m, pelvis.clone().addScaledVector(spine, 0.015 * H).addScaledVector(Hl, sgn * halfH * 0.95 * b).addScaledVector(Zh, -halfH * 0.3), Hl, spine, Zh, 0.02, 0.012, 0.03);
    }
    if (P.apron) basis(P.apron, pelvis.clone().addScaledVector(spine, Ls * 0.15).addScaledVector(Zh, halfH * (P.belly ? 1.15 : 0.82) * b), Hl, spine, Zh, halfH * 0.9, Ls * 0.42, 0.02);

    // Club: grip, shaft, head (driver, iron, putter shapes).
    place(P.grip, handLead.clone().addScaledVector(clubDir, -0.03), handLead.clone().addScaledVector(clubDir, 0.25));
    place(P.shaft, handLead, headPos);
    const faceOut = V3().crossVectors(clubDir, Np).normalize();
    const hz = kind === 'driver' || kind === 'wood' ? [0.11, 0.05, 0.09] : kind === 'putter' ? [0.11, 0.025, 0.035] : [0.075, 0.042, 0.02];
    const hc = headPos.clone().addScaledVector(clubDir, hz[1] * 0.3).addScaledVector(faceOut, -hz[2] * 0.2);
    const toeV = V3().crossVectors(faceOut, clubDir).normalize();
    basis(P.clubHead, hc, toeV.clone().multiplyScalar(-1), clubDir, faceOut, hz[0], hz[1], hz[2]);
    if (this.club !== kind) {
      this.club = kind;
      P.clubHead.geometry.dispose();
      P.clubHead.geometry = kind === 'driver' || kind === 'wood' ? new THREE.SphereGeometry(0.5, 18, 12) : new THREE.BoxGeometry(1, 1, 1);
    }
  }
}

window.GolferRig = GolferRig;
})();
