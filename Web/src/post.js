// Post-processing for Balanced and Quality graphics: the scene renders to a multisampled HDR target,
// bright areas (sun, glints, white clothing in sunlight) bloom, then a composite pass applies filmic
// tone mapping, a gentle colour grade and a vignette.
(function () {
'use strict';

const FS_VERT = 'varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }';

class PostFX {
  constructor(renderer) {
    this.r = renderer;
    const hdr = { type: THREE.HalfFloatType, depthBuffer: true };
    this.rt = new THREE.WebGLRenderTarget(4, 4, Object.assign({ samples: 4 }, hdr));
    this.a = new THREE.WebGLRenderTarget(4, 4, { type: THREE.HalfFloatType, depthBuffer: false });
    this.b = new THREE.WebGLRenderTarget(4, 4, { type: THREE.HalfFloatType, depthBuffer: false });
    this.c = new THREE.WebGLRenderTarget(4, 4, { type: THREE.HalfFloatType, depthBuffer: false });
    // Light shafts: march from each pixel toward the sun; bright sky passes, trees and terrain block.
    this.rays = new THREE.ShaderMaterial({
      uniforms: { uTex: { value: null }, uSun: { value: new THREE.Vector2(0.5, 0.5) }, uVis: { value: 0 } },
      vertexShader: FS_VERT,
      fragmentShader: `uniform sampler2D uTex; uniform vec2 uSun; uniform float uVis; varying vec2 vUv;
        void main() {
          if (uVis <= 0.001) { gl_FragColor = vec4(0.0); return; }
          vec2 step = (uSun - vUv) / 56.0;
          vec2 uv = vUv; float decay = 1.0; vec3 acc = vec3(0.0);
          for (int i = 0; i < 56; i++) {
            uv += step;
            vec3 s = texture2D(uTex, clamp(uv, 0.001, 0.999)).rgb;
            float l = max(max(s.r, s.g), s.b);
            acc += s * smoothstep(1.3, 3.5, l) * decay;                 // only the bright sky near the sun casts shafts
            decay *= 0.962;
          }
          gl_FragColor = vec4(acc / 56.0 * uVis, 1.0);
        }`,
      depthTest: false, depthWrite: false,
    });
    this.cam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    this.quad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2));
    this.quad.frustumCulled = false;
    this.scene = new THREE.Scene();
    this.scene.add(this.quad);
    this.bright = new THREE.ShaderMaterial({
      uniforms: { uTex: { value: null }, uThreshold: { value: 0.9 } },
      vertexShader: FS_VERT,
      fragmentShader: `uniform sampler2D uTex; uniform float uThreshold; varying vec2 vUv;
        void main() { vec3 c = texture2D(uTex, vUv).rgb; float l = max(max(c.r, c.g), c.b);
          gl_FragColor = vec4(c * smoothstep(uThreshold, uThreshold * 1.8, l), 1.0); }`,
      depthTest: false, depthWrite: false,
    });
    this.blur = new THREE.ShaderMaterial({
      uniforms: { uTex: { value: null }, uDir: { value: new THREE.Vector2() } },
      vertexShader: FS_VERT,
      fragmentShader: `uniform sampler2D uTex; uniform vec2 uDir; varying vec2 vUv;
        void main() {
          vec3 s = texture2D(uTex, vUv).rgb * 0.227;
          s += (texture2D(uTex, vUv + uDir * 1.385).rgb + texture2D(uTex, vUv - uDir * 1.385).rgb) * 0.316;
          s += (texture2D(uTex, vUv + uDir * 3.231).rgb + texture2D(uTex, vUv - uDir * 3.231).rgb) * 0.070;
          gl_FragColor = vec4(s, 1.0); }`,
      depthTest: false, depthWrite: false,
    });
    this.composite = new THREE.ShaderMaterial({
      uniforms: { uScene: { value: null }, uBloom: { value: null }, uExposure: { value: 1 }, uBloomStrength: { value: 0.35 }, uWarmth: { value: 0 },
                  uRays: { value: null }, uSun: { value: new THREE.Vector2(0.5, 0.5) }, uSunVis: { value: 0 }, uSunColor: { value: new THREE.Color(1, 0.8, 0.5) },
                  uAspect: { value: 1.6 }, uCine: { value: 0 }, uLook: { value: 0 }, uTime: { value: 0 }, uRes: { value: new THREE.Vector2(1280, 720) }, uSharpen: { value: 0.6 } },
      vertexShader: FS_VERT,
      fragmentShader: `uniform sampler2D uScene; uniform sampler2D uBloom; uniform float uExposure; uniform float uBloomStrength; uniform float uWarmth;
        uniform sampler2D uRays; uniform vec2 uSun; uniform float uSunVis; uniform vec3 uSunColor; uniform float uAspect; uniform float uCine;
        uniform float uLook; uniform float uTime; uniform vec2 uRes; uniform float uSharpen;
        float hash12(vec2 p) { p = fract(p * vec2(443.897, 441.423)); p += dot(p, p.yx + 19.19); return fract((p.x + p.y) * p.x); }
        varying vec2 vUv;
        vec3 rrtOdt(vec3 v) { vec3 a = v * (v + 0.0245786) - 0.000090537; vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081; return a / b; }
        vec3 filmic(vec3 c) {
          const mat3 IN = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
          const mat3 OUT = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
          return clamp(OUT * rrtOdt(IN * (c / 0.6)), 0.0, 1.0);
        }
        vec3 toSRGB(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
        void main() {
          vec3 c = texture2D(uScene, vUv).rgb + texture2D(uBloom, vUv).rgb * uBloomStrength;
          // Contrast-adaptive sharpening: restores crisp edges after multisampling and filtering.
          {
            vec2 px = 1.0 / uRes;
            vec3 n = texture2D(uScene, vUv + vec2(0.0, px.y)).rgb, sx = texture2D(uScene, vUv - vec2(0.0, px.y)).rgb;
            vec3 e = texture2D(uScene, vUv + vec2(px.x, 0.0)).rgb, w = texture2D(uScene, vUv - vec2(px.x, 0.0)).rgb;
            vec3 mn = min(min(min(n, sx), min(e, w)), c), mx = max(max(max(n, sx), max(e, w)), c);
            vec3 amt = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, 1e-4), 0.0, 1.0)) * -0.125 * uSharpen;
            c = clamp((c + (n + sx + e + w) * amt) / (1.0 + 4.0 * amt), 0.0, 64.0);
          }
          if (uSunVis > 0.001) {
            // How much of the sun is actually visible (trees in front of it dim the flare).
            float occ = 0.0;
            for (int i = 0; i < 5; i++) {
              vec2 o = vec2(float(i - 2) * 0.006, float((i * 3) % 5 - 2) * 0.006);
              vec3 sc = texture2D(uScene, clamp(uSun + o, 0.001, 0.999)).rgb;
              occ += smoothstep(1.0, 3.0, max(max(sc.r, sc.g), sc.b)) * 0.2;
            }
            vec3 sunC = uSunColor * uSunVis;
            c += texture2D(uRays, vUv).rgb * sunC * (0.35 + 0.55 * uCine);               // god rays
            vec2 sv = (vUv - uSun) * vec2(uAspect, 1.0);
            float sd = length(sv);
            c += sunC * occ * (exp(-sd * sd * 30.0) * 0.28 + exp(-sd * 9.0) * 0.1) * uCine;   // hazy glow around the sun
            c += sunC * occ * exp(-abs(sv.y) * 140.0) * exp(-abs(sv.x) * 3.0) * 0.18 * uCine; // horizontal streak
            // Lens ghosts along the line from the sun through the screen centre, and a soft halo ring.
            vec2 axis = vec2(0.5) - uSun;
            vec3 ghosts = vec3(0.0);
            ghosts += vec3(1.0, 0.75, 0.4) * smoothstep(0.06, 0.02, length((vUv - (uSun + axis * 0.55)) * vec2(uAspect, 1.0)));
            ghosts += vec3(0.6, 0.8, 1.0) * smoothstep(0.035, 0.01, length((vUv - (uSun + axis * 0.85)) * vec2(uAspect, 1.0)));
            ghosts += vec3(0.9, 0.5, 1.0) * smoothstep(0.11, 0.05, length((vUv - (uSun + axis * 1.35)) * vec2(uAspect, 1.0))) * 0.6;
            ghosts += vec3(0.5, 1.0, 0.7) * smoothstep(0.05, 0.015, length((vUv - (uSun + axis * 1.75)) * vec2(uAspect, 1.0)));
            ghosts += vec3(1.0, 0.6, 0.3) * exp(-pow((sd - 0.42) * 22.0, 2.0)) * 0.5;
            c += ghosts * sunC * occ * 0.06 * uCine;
          }
          c = filmic(c * uExposure);
          // Grade: a touch more saturation, warm highlights and cool shadows, soft S-curve.
          float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
          c = mix(vec3(l), c, 1.24);                                     // vivid, but not neon
          c += (vec3(0.02, 0.01, -0.015) * smoothstep(0.4, 1.0, l) + vec3(-0.01, 0.0, 0.012) * (1.0 - smoothstep(0.0, 0.35, l))) * (1.0 + uWarmth);
          // Cinematic: golden highlights, warm amber shadows, a soft lifted black.
          c = mix(c, c * vec3(1.06, 0.99, 0.88) + vec3(0.012, 0.006, 0.0), uCine * 0.7);
          if (uLook > 1.5 && uLook < 2.5) {
            // Broadcast: natural colour. Pull back the saturation boost, cool the shadows a touch.
            float lb = dot(c, vec3(0.2126, 0.7152, 0.0722));
            c = mix(vec3(lb), c, 0.88);
            c += vec3(-0.006, 0.0, 0.01) * (1.0 - smoothstep(0.0, 0.4, lb));
          }

          c = mix(c, c * c * (3.0 - 2.0 * c), 0.25 * uCine);                     // extra contrast keeps the scene readable
          c = mix(c, c * c * (3.0 - 2.0 * c), 0.18);
          vec2 q = vUv - 0.5;
          c *= 1.0 - 0.32 * smoothstep(0.35, 0.95, length(q * vec2(1.25, 1.0)) * 1.25);
          gl_FragColor = vec4(toSRGB(clamp(c, 0.0, 1.0)), 1.0);
        }`,
      depthTest: false, depthWrite: false, toneMapped: false,
    });
    this.size = new THREE.Vector2();
  }

  setSize() {
    const s = this.r.getDrawingBufferSize(this.size);
    this.rt.setSize(s.x, s.y);
    const bw = Math.max(1, Math.floor(s.x / 4)), bh = Math.max(1, Math.floor(s.y / 4));
    this.a.setSize(bw, bh); this.b.setSize(bw, bh); this.c.setSize(bw, bh);
    this.texel = new THREE.Vector2(1 / bw, 1 / bh);
  }

  pass(material, target) {
    this.quad.material = material;
    this.r.setRenderTarget(target);
    this.r.render(this.scene, this.cam);
  }

  render(scene, camera, exposure, warmth, sun, look, time, sharpen) {
    if (!this.texel) this.setSize();
    this.r.setRenderTarget(this.rt);
    this.r.render(scene, camera);
    this.bright.uniforms.uTex.value = this.rt.texture;
    this.pass(this.bright, this.a);
    for (let i = 0; i < 2; i++) {
      const spread = 1 + i * 1.5;
      this.blur.uniforms.uTex.value = this.a.texture; this.blur.uniforms.uDir.value.set(this.texel.x * spread, 0); this.pass(this.blur, this.b);
      this.blur.uniforms.uTex.value = this.b.texture; this.blur.uniforms.uDir.value.set(0, this.texel.y * spread); this.pass(this.blur, this.a);
    }
    const u = this.composite.uniforms;
    const vis = sun ? sun.vis : 0;
    this.rays.uniforms.uTex.value = this.rt.texture; this.rays.uniforms.uVis.value = vis;
    if (sun) this.rays.uniforms.uSun.value.copy(sun.uv);
    this.pass(this.rays, this.c);
    u.uScene.value = this.rt.texture; u.uBloom.value = this.a.texture; u.uExposure.value = exposure; u.uWarmth.value = warmth || 0;
    u.uRays.value = this.c.texture; u.uSunVis.value = vis; u.uCine.value = sun ? sun.cine : 0;
    u.uBloomStrength.value = sun && sun.cine ? 0.6 : 0.35;
    if (sun) { u.uSun.value.copy(sun.uv); u.uSunColor.value.copy(sun.color); }
    u.uAspect.value = this.size.x / Math.max(1, this.size.y);
    u.uLook.value = look || 0; u.uTime.value = time || 0; u.uRes.value.copy(this.size); u.uSharpen.value = sharpen === undefined ? 0.6 : sharpen;
    if (look === 2) u.uBloomStrength.value = 0.22;
    this.pass(this.composite, null);
  }
}

window.PostFX = PostFX;
})();
