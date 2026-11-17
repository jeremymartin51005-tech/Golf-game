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
      uniforms: { uScene: { value: null }, uBloom: { value: null }, uExposure: { value: 1 }, uBloomStrength: { value: 0.35 }, uWarmth: { value: 0 } },
      vertexShader: FS_VERT,
      fragmentShader: `uniform sampler2D uScene; uniform sampler2D uBloom; uniform float uExposure; uniform float uBloomStrength; uniform float uWarmth;
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
          c = filmic(c * uExposure);
          // Grade: a touch more saturation, warm highlights and cool shadows, soft S-curve.
          float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
          c = mix(vec3(l), c, 1.1);
          c += (vec3(0.02, 0.01, -0.015) * smoothstep(0.4, 1.0, l) + vec3(-0.01, 0.0, 0.012) * (1.0 - smoothstep(0.0, 0.35, l))) * (1.0 + uWarmth);
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
    this.a.setSize(bw, bh); this.b.setSize(bw, bh);
    this.texel = new THREE.Vector2(1 / bw, 1 / bh);
  }

  pass(material, target) {
    this.quad.material = material;
    this.r.setRenderTarget(target);
    this.r.render(this.scene, this.cam);
  }

  render(scene, camera, exposure, warmth) {
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
    u.uScene.value = this.rt.texture; u.uBloom.value = this.a.texture; u.uExposure.value = exposure; u.uWarmth.value = warmth || 0;
    this.pass(this.composite, null);
  }
}

window.PostFX = PostFX;
})();
