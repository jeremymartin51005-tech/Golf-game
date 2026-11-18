// Next-Gen Golf shaders (three.js r160, WebGL2). Pure rendering: nothing here affects the golf.
//  - terrain: per-surface detail (grass clumps, sand grain, mown texture), micro-relief bump shading,
//             smoother greens, and an optional slope-tinted putting grid
//  - water:   animated waves, Fresnel sky reflection, sun glint, shallow edges and shoreline foam
//  - sky:     atmospheric gradient with horizon haze, sun disc and glow
//  - grass:   instanced tufts that sway with the wind
(function () {
'use strict';

const NOISE = `
float ngHash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float ngNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
  return mix(mix(ngHash(i), ngHash(i + vec2(1.0, 0.0)), u.x), mix(ngHash(i + vec2(0.0, 1.0)), ngHash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float ngFbm(vec2 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 4; i++) { s += a * ngNoise(p); p = p * 2.03 + vec2(17.1, 9.3); a *= 0.5; }
  return s;
}
`;

// ---------------------------------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------------------------------
function terrainMaterial(map, surfaceIds) {
  const uniforms = {
    uSurf: { value: surfaceIds },
    uGrid: { value: 0 },
    uGridCenter: { value: new THREE.Vector2() },
    uSunDirW: { value: new THREE.Vector3(-0.55, 0.8, 0.35).normalize() },
    uStripeDir: { value: new THREE.Vector2(0, 1) },
    uTexel: { value: new THREE.Vector2(1 / 1024, 1 / 1024) },
  };
  const mat = new THREE.MeshStandardMaterial({ map, roughness: 0.93, metalness: 0 });
  mat.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, uniforms);
    sh.vertexShader = 'varying vec3 vNgWorld;\nvarying vec3 vNgNormal;\n' + sh.vertexShader.replace('#include <worldpos_vertex>', `#include <worldpos_vertex>
      vNgWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;
      vNgNormal = normalize(mat3(modelMatrix) * objectNormal);`);
    sh.fragmentShader = `uniform sampler2D uSurf;
uniform float uGrid;
uniform vec2 uGridCenter;
uniform vec3 uSunDirW;
uniform vec2 uStripeDir;
uniform vec2 uTexel;
varying vec3 vNgWorld;
varying vec3 vNgNormal;
${NOISE}
` + sh.fragmentShader
      .replace('#include <map_fragment>', `#include <map_fragment>
      float ngSurf = floor(texture2D(uSurf, vMapUv).r * 255.0 + 0.5);
      vec2 ngWp = vec2(vNgWorld.x, -vNgWorld.z);
      float ngDist = length(vNgWorld - cameraPosition);
      float ngFine = 1.0 - smoothstep(25.0, 90.0, ngDist);
      bool ngGreen = ngSurf == 6.0 || ngSurf == 7.0;
      bool ngSand = ngSurf == 5.0;
      bool ngRough = ngSurf == 3.0 || ngSurf == 4.0 || ngSurf == 10.0;
      bool ngFair = ngSurf == 0.0 || ngSurf == 1.0 || ngSurf == 2.0;
      // Soft surface weights from four taps, so mowing patterns and shading blend smoothly across the
      // edge between surfaces instead of stepping along the texel grid.
      float wStripe = 0.0, wCut = 0.0, wGreenT = 0.0, wCollar = 0.0;
      for (int ti = 0; ti < 4; ti++) {
        vec2 o = vec2(ti == 0 || ti == 2 ? -0.5 : 0.5, ti < 2 ? -0.5 : 0.5) * uTexel;
        float sid = floor(texture2D(uSurf, vMapUv + o).r * 255.0 + 0.5);
        wStripe += (sid == 0.0 || sid == 1.0) ? 0.25 : 0.0;
        wCut += sid == 2.0 ? 0.25 : 0.0;
        wGreenT += sid == 6.0 ? 0.25 : 0.0;
        wCollar += sid == 7.0 ? 0.25 : 0.0;
      }
      float wGreenAll = wGreenT + wCollar, wFairAll = wStripe + wCut;
      {
        float n1 = ngNoise(ngWp * 0.9), n2 = ngFbm(ngWp * 0.12), n3 = ngNoise(ngWp * 6.0);
        vec3 c = diffuseColor.rgb;
        if (ngSand) {
          c *= 0.93 + 0.13 * n2 + 0.10 * (n3 - 0.5) * ngFine;
        } else if (ngGreen) {
          c *= 0.95 + 0.08 * n2 + 0.035 * (n3 - 0.5) * ngFine;
        } else if (ngRough) {
          float clump = ngFbm(ngWp * 0.45);
          c *= 0.76 + 0.40 * clump;
          c = mix(c, c * vec3(1.20, 1.12, 0.74), smoothstep(0.62, 0.86, clump) * 0.6);   // seed heads
          c *= 1.0 + 0.12 * (n3 - 0.5) * ngFine;
        } else if (ngFair) {
          c *= 0.92 + 0.14 * n2 + 0.06 * (n1 - 0.5) + 0.05 * (n3 - 0.5) * ngFine;
        }
        vec2 viewXZ = normalize(vec2(vNgWorld.x - cameraPosition.x, -(vNgWorld.z - cameraPosition.z)) + 1e-4);
        if (wStripe > 0.0) {
          // Mowing stripes down the hole with a soft cross-cut; blades laid in opposite directions,
          // so light and dark swap with the view. Antialiased at any distance.
          float across = dot(ngWp, vec2(uStripeDir.y, -uStripeDir.x)) / 7.0;
          float w = fwidth(across) * 1.5 + 0.02;
          float band = smoothstep(0.5 - w, 0.5 + w, abs(fract(across) - 0.5) * 2.0);
          float cross = smoothstep(0.5 - w, 0.5 + w, abs(fract(dot(ngWp, uStripeDir) / 9.0) - 0.5) * 2.0);
          float facing = clamp(dot(viewXZ, uStripeDir) * 1.4, -1.0, 1.0);
          float s1 = mix(1.10, 0.90, band), s2 = mix(0.92, 1.08, band);
          c *= mix(1.0, mix(s2, s1, facing * 0.5 + 0.5) * mix(1.025, 0.975, cross), wStripe);
        }
        c *= mix(1.0, 1.06, wCut);                                     // first cut: a lighter ring around the fairway
        if (wGreenAll > 0.0) {
          // Tournament green: smooth and bright with a faint double-cut sheen that shifts with the view;
          // the collar around it is a slightly longer, darker ring.
          float acrossG = dot(ngWp, vec2(uStripeDir.y, -uStripeDir.x)) / 3.5;
          float wq = fwidth(acrossG) * 1.5 + 0.03;
          float bandG = smoothstep(0.5 - wq, 0.5 + wq, abs(fract(acrossG) - 0.5) * 2.0);
          c *= mix(1.0, 1.0 + (bandG - 0.5) * 0.045 * dot(viewXZ, uStripeDir), wGreenT);
          c *= mix(1.0, 0.86, wCollar);
        }
        // Relief shading: exaggerate how the sun falls across slopes so a 2% green contour or a
        // fairway swale is readable from address, the way TV coverage and good course art show it.
        {
          float lit = dot(normalize(vNgNormal), uSunDirW) - uSunDirW.y;
          float side = dot(normalize(vNgNormal).xz, vec2(0.7071, 0.7071));           // a soft second light
          float k = 1.6 + wGreenAll * 6.4 + wFairAll * 2.4 + (ngSand ? 1.6 : 0.0);
          c *= clamp(1.0 + (lit + 0.5 * side) * k, 0.62, 1.38);
        }
        // Steep banks dry out a little.
        float ngSlope = sqrt(max(0.0, 1.0 - vNgNormal.y * vNgNormal.y)) / max(vNgNormal.y, 0.05);
        if (ngRough || ngFair) c = mix(c, c * vec3(1.08, 1.0, 0.78), smoothstep(0.12, 0.35, ngSlope) * 0.5);
        // Putting grid: 1 m squares, tinted by how hard the ground falls (white flat, amber 2%, red 4%+).
        if (uGrid > 0.5 && ngGreen) {
          vec2 gl = abs(fract(ngWp - 0.5) - 0.5) / fwidth(ngWp);
          float line = 1.0 - min(min(gl.x, gl.y), 1.0);
          line *= 1.0 - smoothstep(16.0, 26.0, length(ngWp - uGridCenter));
          vec3 tint = mix(vec3(1.0), vec3(1.0, 0.72, 0.2), smoothstep(0.008, 0.022, ngSlope));
          tint = mix(tint, vec3(1.0, 0.25, 0.15), smoothstep(0.03, 0.045, ngSlope));
          c = mix(c, tint, line * 0.55);
        }
        diffuseColor.rgb = c;
      }`)
      .replace('#include <roughnessmap_fragment>', `#include <roughnessmap_fragment>
      if (ngGreen) roughnessFactor = 0.68;
      if (ngSand) roughnessFactor = 1.0;`)
      .replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>
      {
        // Micro-relief: tufted rough, rippled sand, a hint of grain on fairways and greens.
        float amp = ngRough ? 0.05 : ngSand ? 0.02 : ngGreen ? 0.003 : 0.012;
        amp *= 1.0 - smoothstep(20.0, 70.0, ngDist);
        if (amp > 0.0) {
          float hh = (ngRough ? ngFbm(ngWp * 3.5) : ngNoise(ngWp * (ngSand ? 9.0 : 5.0))) * amp;
          vec3 sx = dFdx(-vViewPosition), sy = dFdy(-vViewPosition);
          float dhx = dFdx(hh), dhy = dFdy(hh);
          vec3 r1 = cross(sy, normal), r2 = cross(normal, sx);
          float det = dot(sx, r1);
          normal = normalize(abs(det) * normal - sign(det) * (dhx * r1 + dhy * r2));
        }
      }`);
  };
  mat.userData.uniforms = uniforms;
  return mat;
}

// ---------------------------------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------------------------------
function skyMaterial(top, horizon, sunDir, sunStrength, cover, cloudTint) {
  return new THREE.ShaderMaterial({
    uniforms: {
      uTop: { value: new THREE.Color(top) },
      uHorizon: { value: new THREE.Color(horizon) },
      uSunDir: { value: sunDir.clone() },
      uSun: { value: sunStrength },
      uTime: { value: 0 },
      uCover: { value: cover === undefined ? 0.35 : cover },
      uCloud: { value: new THREE.Color(cloudTint === undefined ? 0xffffff : cloudTint) },
      uGlow: { value: 0 },
    },
    vertexShader: `varying vec3 vDir;
      void main() {
        vDir = normalize(position);
        vec4 p = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        gl_Position = p.xyww;
      }`,
    fragmentShader: `uniform vec3 uTop; uniform vec3 uHorizon; uniform vec3 uSunDir; uniform float uSun;
      uniform float uTime; uniform float uCover; uniform vec3 uCloud; uniform float uGlow;
      varying vec3 vDir;
      ${NOISE}
      float cloudDensity(vec2 p) {
        float n = ngFbm(p) * 0.65 + ngFbm(p * 2.7 + 11.3) * 0.35;
        return smoothstep(1.0 - uCover - 0.18, 1.0 - uCover + 0.22, n);
      }
      void main() {
        vec3 d = normalize(vDir);
        float y = max(d.y, 0.0);
        vec3 col = mix(uHorizon, uTop, pow(y, 0.45));
        col = mix(col, uHorizon * 1.04, exp(-y * 14.0) * 0.45);                  // horizon haze
        float mu = max(dot(d, normalize(uSunDir)), 0.0);
        vec3 sunCol = vec3(1.0, 0.92, 0.78);
        col += sunCol * uSun * (pow(mu, 1200.0) * 14.0 + pow(mu, 60.0) * 0.35 + pow(mu, 6.0) * 0.12);
        col += sunCol * uSun * uGlow * (pow(mu, 380.0) * 5.0 + pow(mu, 30.0) * 0.32 + pow(mu, 4.0) * 0.1);   // golden-hour haze
        // Clouds on a flat layer overhead, drifting slowly. Sunlit tops, darker undersides, fading at the horizon.
        if (d.y > 0.02 && uCover > 0.01) {
          vec2 p = d.xz / (d.y + 0.12) * 1.6 + vec2(uTime * 0.012, uTime * 0.004);
          float dens = cloudDensity(p);
          float shade = cloudDensity(p + normalize(uSunDir.xz + 1e-4) * 0.18);   // denser toward the sun = shadowed underside
          vec3 lit = mix(uCloud, uCloud * vec3(0.72, 0.75, 0.82), shade * 0.8);
          lit += sunCol * uSun * pow(mu, 8.0) * 0.5 * (1.0 - shade);             // silver lining near the sun
          float fade = smoothstep(0.02, 0.22, d.y);
          col = mix(col, lit, dens * fade * 0.92);
        }
        gl_FragColor = vec4(col, 1.0);
        #include <tonemapping_fragment>
        #include <colorspace_fragment>
      }`,
    side: THREE.BackSide,
    depthWrite: false,
    fog: false,
  });
}

// ---------------------------------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------------------------------
function waterMaterial(opts) {
  const mat = new THREE.ShaderMaterial({
    uniforms: {
      uTime: { value: 0 },
      uSunDir: { value: opts.sunDir.clone() },
      uSun: { value: opts.sunStrength },
      uSkyTop: { value: new THREE.Color(opts.skyTop) },
      uSkyHorizon: { value: new THREE.Color(opts.skyHorizon) },
      uDeep: { value: new THREE.Color(opts.deep || 0x0b5a7a) },
      uShallow: { value: new THREE.Color(opts.shallow || 0x35b8b0) },
      uEllipse: { value: new THREE.Vector4(opts.cx, opts.cy, opts.rx, opts.ry) },
      uRot: { value: opts.rotDeg * Math.PI / 180 },
      uWind: { value: new THREE.Vector2(opts.windX || 0.5, opts.windY || 0.3) },
      uFogColor: { value: new THREE.Color(opts.fogColor) },
      uFogDensity: { value: opts.fogDensity },
      uChop: { value: opts.chop || 1 },
    },
    vertexShader: `varying vec3 vWorld;
      void main() {
        vec4 w = modelMatrix * vec4(position, 1.0);
        vWorld = w.xyz;
        gl_Position = projectionMatrix * viewMatrix * w;
      }`,
    fragmentShader: `uniform float uTime; uniform vec3 uSunDir; uniform float uSun;
      uniform vec3 uSkyTop; uniform vec3 uSkyHorizon; uniform vec3 uDeep; uniform vec3 uShallow;
      uniform vec4 uEllipse; uniform float uRot; uniform vec2 uWind; uniform vec3 uFogColor; uniform float uFogDensity; uniform float uChop;
      varying vec3 vWorld;
      ${NOISE}
      float waves(vec2 p) {
        float t = uTime;
        return 0.55 * ngNoise(p * 0.32 + uWind * t * 0.10)
             + 0.30 * ngNoise(p * 0.85 - uWind.yx * t * 0.17 + 3.1)
             + 0.15 * ngNoise(p * 2.3 + vec2(t * 0.31, -t * 0.19) + 7.7);
      }
      void main() {
        vec2 wp = vec2(vWorld.x, -vWorld.z);
        float e = 0.06;
        float h0 = waves(wp), hx = waves(wp + vec2(e, 0.0)), hy = waves(wp + vec2(0.0, e));
        float k = 0.10 * uChop;
        vec3 n = normalize(vec3(-(hx - h0) / e * k, 1.0, (hy - h0) / e * k));
        vec3 V = normalize(cameraPosition - vWorld);
        float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
        vec3 R = reflect(-V, n);
        vec3 sky = mix(uSkyHorizon, uSkyTop, pow(max(R.y, 0.0), 0.45));
        float sdot = max(dot(R, normalize(uSunDir)), 0.0);
        float spec = pow(sdot, 380.0) * 6.0 * uSun + pow(sdot, 60.0) * 0.22 * uSun;
        // Glitter: tiny bright wave facets catching the sun along its path across the water.
        float glint = step(0.95, ngNoise(wp * 7.0 + uTime * vec2(0.9, -0.6))) * pow(sdot, 14.0) * 1.6 * uSun;
        spec += glint;
        // Distance from the shore, in ellipse units (matches the core's hazard shape).
        vec2 d = wp - uEllipse.xy;
        float cr = cos(uRot), sr = sin(uRot);
        float u = dot(d, vec2(cr, -sr)) / uEllipse.z, v = dot(d, vec2(sr, cr)) / uEllipse.w;
        float kk = length(vec2(u, v));
        float shore = smoothstep(0.70, 1.02, kk);
        vec3 body = mix(uDeep, uShallow, shore * 0.85);
        vec3 col = mix(body, sky, fres * (1.0 - 0.4 * shore)) + vec3(1.0, 0.95, 0.85) * spec;
        float foam = smoothstep(0.95, 1.0, kk) * (1.0 - smoothstep(1.0, 1.07, kk));
        foam *= 0.55 + 0.45 * sin(kk * 70.0 - uTime * 1.6 + ngNoise(wp * 1.7) * 6.0);
        col = mix(col, vec3(0.93, 0.96, 0.95), clamp(foam, 0.0, 1.0) * 0.55);
        float fd = length(vWorld - cameraPosition);
        col = mix(col, uFogColor, 1.0 - exp(-uFogDensity * uFogDensity * fd * fd));
        gl_FragColor = vec4(col, 1.0);
        #include <tonemapping_fragment>
        #include <colorspace_fragment>
      }`,
  });
  return mat;
}

// ---------------------------------------------------------------------------------------------------
// Grass tufts and swaying foliage
// ---------------------------------------------------------------------------------------------------
function tuftGeometry() {
  const pos = [], col = [];
  for (let b = 0; b < 7; b++) {
    const a = b * 2.39996, r = 0.03 + 0.09 * ((b * 0.618) % 1);
    const bx = Math.cos(a) * r, bz = Math.sin(a) * r;
    const lean = 0.18 + 0.2 * ((b * 0.377) % 1), la = a + 0.6;
    const w = 0.022, h = 0.7 + 0.3 * ((b * 0.731) % 1);
    const px = -Math.sin(a) * w, pz = Math.cos(a) * w;
    pos.push(bx - px, 0, bz - pz, bx + px, 0, bz + pz, bx + Math.cos(la) * lean, h, bz + Math.sin(la) * lean);
    col.push(0.42, 0.45, 0.32, 0.42, 0.45, 0.32, 1.0, 1.0, 0.86);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.computeVertexNormals();
  return g;
}

// Adds wind sway to any instanced Lambert material. Movement grows with height above the base.
function addSway(mat, uniforms, amount, push) {
  mat.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, uniforms);
    const pushCode = push ? `
      #ifdef USE_INSTANCING
      {
        // Blades bend away from the ball and from the golfer's feet (world space, turned into the tuft's frame).
        vec2 base = vec2(instanceMatrix[3][0], instanceMatrix[3][2]);
        vec2 push = vec2(0.0);
        vec2 db = base - uBall.xz; float rb = length(db);
        push += (rb > 1e-3 ? db / rb : vec2(1.0, 0.0)) * smoothstep(0.45, 0.0, rb) * 1.3;
        vec2 df = base - uFeet.xz; float rf = length(df);
        push += (rf > 1e-3 ? df / rf : vec2(1.0, 0.0)) * smoothstep(0.75, 0.1, rf) * 1.0;
        vec3 local = transpose(mat3(instanceMatrix)) * vec3(push.x, 0.0, push.y);
        transformed.xz += local.xz * max(position.y, 0.0) * 0.9;
        transformed.y -= length(push) * max(position.y, 0.0) * 0.35;
      }
      #endif` : '';
    sh.vertexShader = 'uniform float uTime;\nuniform vec2 uWind;\n' + (push ? 'uniform vec3 uBall;\nuniform vec3 uFeet;\n' : '') + sh.vertexShader.replace('#include <begin_vertex>', `#include <begin_vertex>${pushCode}
      #ifdef USE_INSTANCING
        vec2 ngBase = vec2(instanceMatrix[3][0], instanceMatrix[3][2]);
      #else
        vec2 ngBase = vec2(0.0);
      #endif
      float ngSway = sin(uTime * 1.7 + ngBase.x * 0.31 + ngBase.y * 0.27) * 0.6 + sin(uTime * 3.3 + ngBase.x * 0.9) * 0.25 + 0.55;
      transformed.xz += uWind * ngSway * max(position.y, 0.0) * max(position.y, 0.0) * ${amount.toFixed(4)};`);
  };
  return mat;
}

window.GolfShaders = { terrainMaterial, skyMaterial, waterMaterial, tuftGeometry, addSway };
})();
