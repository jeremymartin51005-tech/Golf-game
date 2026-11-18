#!/usr/bin/env python3
"""Assembles the single-file playable build: dist/NextGenGolf.html.

Everything (three.js, the WebAssembly core, the game) is inlined so the file runs by double-clicking it,
with no server and no network. Run ./build_wasm.sh first.
"""
import base64, pathlib
root = pathlib.Path(__file__).resolve().parent
html = (root / 'src/index.template.html').read_text()
three = (root / 'vendor/three.min.js').read_text()
game = (root / 'src/shaders.js').read_text() + '\n' + (root / 'src/game.js').read_text()
wasm = base64.b64encode((root / 'build/golfcore.wasm').read_bytes()).decode()
for name, text in (('three', three), ('game', game)):
    assert '</script' not in text.lower(), name + ' contains a closing script tag'
extra = '\n'.join((root / 'src' / f).read_text() for f in ('post.js', 'golfer.js', 'nature.js'))
assert '</script' not in extra.lower()
html = html.replace('/*__THREE__*/', three).replace('/*__EXTRA__*/', extra).replace('/*__WASM__*/', wasm).replace('/*__GAME__*/', game)
(root / 'dist').mkdir(exist_ok=True)
out = root / 'dist/NextGenGolf.html'
out.write_text(html)
print(out, round(out.stat().st_size / 1024), 'KB')
