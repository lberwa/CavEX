(function () {
  "use strict";

  const canvas = document.getElementById("view");
  const status = document.getElementById("status");
  const rendererStatus = document.getElementById("renderer");
  /* This viewer is a diagnostic tool; antialiasing costs a lot on software
   * WebGL and is not useful for the block grid. */
  const gl = canvas.getContext("webgl", { antialias: false, alpha: false,
    powerPreference: "high-performance" });
  if (!gl) {
    status.textContent = "WebGL is not supported by this browser.";
    return;
  }

  const vertexSource = `
    attribute vec3 a_position;
    attribute vec3 a_color;
    uniform mat4 u_matrix;
    varying vec3 v_color;
    void main() { gl_Position = u_matrix * vec4(a_position, 1.0); v_color = a_color; }
  `;
  const fragmentSource = `
    precision mediump float;
    varying vec3 v_color;
    void main() { gl_FragColor = vec4(v_color, 1.0); }
  `;

  function shader(type, source) {
    const result = gl.createShader(type);
    gl.shaderSource(result, source);
    gl.compileShader(result);
    if (!gl.getShaderParameter(result, gl.COMPILE_STATUS))
      throw new Error(gl.getShaderInfoLog(result));
    return result;
  }
  const program = gl.createProgram();
  gl.attachShader(program, shader(gl.VERTEX_SHADER, vertexSource));
  gl.attachShader(program, shader(gl.FRAGMENT_SHADER, fragmentSource));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error("WebGL link failed");
  gl.useProgram(program);
  const positionAttribute = gl.getAttribLocation(program, "a_position");
  const colorAttribute = gl.getAttribLocation(program, "a_color");
  const matrixUniform = gl.getUniformLocation(program, "u_matrix");
  const positionBuffer = gl.createBuffer();
  const colorBuffer = gl.createBuffer();
  const outlinePositionBuffer = gl.createBuffer();
  const outlineColorBuffer = gl.createBuffer();

  const state = { files: [], index: 0, map: null, vertices: 0, outlineVertices: 0, yaw: 0, pitch: -0.35, distance: 25,
    cameraReady: false,
    position: [0, 0, 0], target: [0, 0, 0], eye: [0, 0, 0], keys: Object.create(null), dirty: true, raf: 0, timer: 0, last: performance.now() };
  const colors = {
    wand: [0.43, 0.45, 0.49], begehbar: [0.22, 0.58, 0.32], creeper: [0.32, 0.95, 0.48],
    spieler: [0.94, 0.22, 0.28], pfad: [1.0, 0.72, 0.10], default: [0.30, 0.42, 0.62]
  };

  function perspective(fov, aspect, near, far) {
    const f = 1 / Math.tan(fov / 2), nf = 1 / (near - far);
    return [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
  }
  function multiply(a, b) {
    const out = new Array(16).fill(0);
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++)
      for (let k = 0; k < 4; k++) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return out;
  }
  function lookAt(eye, center, up) {
    let z = [eye[0]-center[0], eye[1]-center[1], eye[2]-center[2]];
    let zl = Math.hypot(...z); z = z.map(v => v / zl);
    let x = [up[1]*z[2]-up[2]*z[1], up[2]*z[0]-up[0]*z[2], up[0]*z[1]-up[1]*z[0]];
    let xl = Math.hypot(...x); x = x.map(v => v / xl);
    const y = [z[1]*x[2]-z[2]*x[1], z[2]*x[0]-z[0]*x[2], z[0]*x[1]-z[1]*x[0]];
    return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
      -(x[0]*eye[0]+x[1]*eye[1]+x[2]*eye[2]), -(y[0]*eye[0]+y[1]*eye[1]+y[2]*eye[2]),
      -(z[0]*eye[0]+z[1]*eye[1]+z[2]*eye[2]), 1];
  }
  function cube(vertices, colorsOut, x, y, z, color, alpha, occupied, topOnly) {
    const p = [[x,y,z],[x+1,y,z],[x+1,y+1,z],[x,y+1,z],[x,y,z+1],[x+1,y,z+1],[x+1,y+1,z+1],[x,y+1,z+1]];
    const faces = [[0,1,2,0,2,3],[1,5,6,1,6,2],[5,4,7,5,7,6],[4,0,3,4,3,7],[3,2,6,3,6,7],[4,5,1,4,1,0]];
    const neighbours = [[0,0,-1],[1,0,0],[0,0,1],[-1,0,0],[0,1,0],[0,-1,0]];
    /* Walkable cells are floors, not solid blocks. Rendering their four side
     * faces and bottom face made large maps needlessly expensive. */
    const firstFace = topOnly ? 4 : 0;
    for (let f = firstFace; f < faces.length; f++) {
      const n = neighbours[f];
      if (occupied.has(`${x+n[0]},${y+n[1]},${z+n[2]}`)) continue;
      for (const i of faces[f]) { vertices.push(p[i][0], p[i][1], p[i][2]); colorsOut.push(color[0], color[1], color[2] * alpha); }
    }
  }
  function sphere(vertices, colorsOut, x, y, z, radius, color) {
    const rings = 4, segments = 8;
    for (let r = 0; r < rings; r++) {
      const a0 = Math.PI * r / rings - Math.PI / 2;
      const a1 = Math.PI * (r + 1) / rings - Math.PI / 2;
      for (let s = 0; s < segments; s++) {
        const b0 = Math.PI * 2 * s / segments, b1 = Math.PI * 2 * (s + 1) / segments;
        const p = (a, b) => [x + radius * Math.cos(a) * Math.cos(b), y + radius * Math.sin(a), z + radius * Math.cos(a) * Math.sin(b)];
        const q0 = p(a0, b0), q1 = p(a0, b1), q2 = p(a1, b1), q3 = p(a1, b0);
        for (const q of [q0, q1, q2, q0, q2, q3]) {
          vertices.push(q[0], q[1], q[2]); colorsOut.push(color[0], color[1], color[2]);
        }
      }
    }
  }
  function outlineCube(vertices, colorsOut, x, y, z, occupied, color) {
    const p = [[x,y,z],[x+1,y,z],[x+1,y+1,z],[x,y+1,z],[x,y,z+1],[x+1,y,z+1],[x+1,y+1,z+1],[x,y+1,z+1]];
    const faces = [[0,1,2,3],[1,5,6,2],[5,4,7,6],[4,0,3,7],[3,2,6,7],[4,5,1,0]];
    const neighbours = [[0,0,-1],[1,0,0],[0,0,1],[-1,0,0],[0,1,0],[0,-1,0]];
    for (let f = 0; f < faces.length; f++) {
      const n = neighbours[f];
      if (occupied.has(`${x+n[0]},${y+n[1]},${z+n[2]}`)) continue;
      const face = faces[f];
      for (let i = 0; i < 4; i++) {
        const a = p[face[i]], b = p[face[(i + 1) % 4]];
        vertices.push(a[0], a[1], a[2], b[0], b[1], b[2]);
        colorsOut.push(color[0], color[1], color[2], color[0], color[1], color[2]);
      }
    }
  }
  function buildGeometry(data) {
    const vertices = [], colorData = [], outlines = [], outlineColors = [], path = new Set();
    const occupied = new Set();
    for (const cell of (data.zellen || [])) {
      if (cell.type !== "begehbar") occupied.add(`${cell.x},${cell.y},${cell.z}`);
    }
    for (const point of (data.pfad || [])) if (point && point.x !== undefined)
      path.add(`${point.x},${point.y},${point.z}`);
    for (const cell of (data.zellen || [])) {
      const key = `${cell.x},${cell.y},${cell.z}`;
      if (cell.type === "begehbar") {
        const floorKey = `${cell.x},${cell.y - 1},${cell.z}`;
        if (!occupied.has(floorKey)) {
          cube(vertices, colorData, cell.x, cell.y - 1, cell.z, colors.default, 0.92, occupied, false);
          outlineCube(outlines, outlineColors, cell.x, cell.y - 1, cell.z, occupied, [0.02, 0.02, 0.02]);
          occupied.add(floorKey);
        }
        sphere(vertices, colorData, cell.x + .5, cell.y + .56, cell.z + .5,
          path.has(key) ? .30 : .22, path.has(key) ? colors.pfad : colors.begehbar);
      } else {
        const color = colors[cell.type] || colors.default;
        cube(vertices, colorData, cell.x, cell.y, cell.z, color, 0.92, occupied, false);
        outlineCube(outlines, outlineColors, cell.x, cell.y, cell.z, occupied, [0.02, 0.02, 0.02]);
      }
    }
    if (data.creeper) cube(vertices, colorData, data.creeper.x - .25, data.creeper.y, data.creeper.z - .25, colors.creeper, 1, new Set());
    if (data.spieler) cube(vertices, colorData, data.spieler.x - .25, data.spieler.y, data.spieler.z - .25, colors.spieler, 1, new Set());
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, colorBuffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(colorData), gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, outlinePositionBuffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(outlines), gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, outlineColorBuffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(outlineColors), gl.STATIC_DRAW);
    state.vertices = vertices.length / 3;
    state.outlineVertices = outlines.length / 3;
    const center = data.spieler || data.creeper || { x: 0, y: 0, z: 0 };
    if (!state.cameraReady) {
      state.target = [center.x, center.y, center.z];
      state.distance = Math.max(12, Math.min(80, Math.cbrt((data.zellen || []).length) * 3));
      const initialForward = [Math.sin(state.yaw) * Math.cos(state.pitch), Math.sin(state.pitch), Math.cos(state.yaw) * Math.cos(state.pitch)];
      state.position = [center.x - initialForward[0] * state.distance, center.y - initialForward[1] * state.distance, center.z - initialForward[2] * state.distance];
      state.cameraReady = true;
    }
    state.dirty = true;
    requestRender();
  }
  async function load(index) {
    if (!state.files.length) return;
    state.index = (index + state.files.length) % state.files.length;
    const name = state.files[state.index];
    status.textContent = `Loading ${name} …`;
    const response = await fetch(`/api/map/${encodeURIComponent(name)}`);
    state.map = await response.json(); buildGeometry(state.map);
    status.textContent = `${name} (${state.index + 1}/${state.files.length}) · ${(state.map.zellen || []).length} cells · ${state.vertices / 36} cubes`;
  }
  async function init() { state.files = await (await fetch("/api/list")).json(); await load(0); }

  function resize() {
    /* Keep the diagnostic view responsive even when WebGL falls back to
     * SwiftShader. The canvas still fills the window; only its internal
     * render resolution is reduced. */
    const scale = Math.min(devicePixelRatio || 1, 1.0) * 0.72;
    let w = Math.max(320, Math.floor(innerWidth * scale));
    let h = Math.max(240, Math.floor(innerHeight * scale));
    const maxPixels = 1200000;
    if (w * h > maxPixels) {
      const factor = Math.sqrt(maxPixels / (w * h));
      w = Math.floor(w * factor); h = Math.floor(h * factor);
    }
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w; canvas.height = h; gl.viewport(0, 0, w, h); state.dirty = true;
    }
  }
  function requestRender() {
    if (state.raf || state.timer) return;
    const moving = state.keys.KeyW || state.keys.KeyS || state.keys.KeyA || state.keys.KeyD || state.keys.Space || state.keys.ShiftLeft || state.keys.ShiftRight;
    if (moving) {
      /* 30 FPS is enough for this diagnostic viewer and avoids saturating a
       * CPU when the browser falls back to software WebGL. */
      state.timer = setTimeout(() => { state.timer = 0; state.raf = requestAnimationFrame(frame); }, 33);
    } else state.raf = requestAnimationFrame(frame);
  }
  function frame(now) {
    state.raf = 0; resize(); const dt = Math.min(.05, (now - state.last) / 1000); state.last = now;
    /* W/S follow the horizontal view direction. Looking up or down must not
     * make forward movement climb or descend. */
    const viewForward = [Math.sin(state.yaw) * Math.cos(state.pitch), Math.sin(state.pitch), Math.cos(state.yaw) * Math.cos(state.pitch)];
    const forward = [Math.sin(state.yaw), 0, Math.cos(state.yaw)];
    const right = [Math.cos(state.yaw), 0, -Math.sin(state.yaw)];
    const speed = (state.keys.KeyQ ? 30 : 10) * dt;
    const move = (v, s) => { state.position[0] += v[0] * s; state.position[1] += v[1] * s; state.position[2] += v[2] * s; };
    if (state.keys.KeyW) move(forward, speed); if (state.keys.KeyS) move(forward, -speed);
    /* A is left and D is right. */
    if (state.keys.KeyA) move(right, speed); if (state.keys.KeyD) move(right, -speed);
    if (state.keys.Space) state.position[1] += speed;
    if (state.keys.ShiftLeft || state.keys.ShiftRight) state.position[1] -= speed;
    state.eye = state.position.slice();
    state.target = [state.position[0] + viewForward[0], state.position[1] + viewForward[1], state.position[2] + viewForward[2]];
    const moving = state.keys.KeyW || state.keys.KeyS || state.keys.KeyA || state.keys.KeyD || state.keys.Space || state.keys.ShiftLeft || state.keys.ShiftRight;
    if (moving) state.dirty = true;
    if (state.dirty) { state.dirty = false; gl.enable(gl.DEPTH_TEST); gl.clearColor(.06, .08, .12, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (state.vertices) { const matrix = multiply(perspective(Math.PI/3, canvas.width/canvas.height, .05, 2000), lookAt(state.eye, state.target, [0,1,0])); gl.uniformMatrix4fv(matrixUniform, false, new Float32Array(matrix));
      gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer); gl.enableVertexAttribArray(positionAttribute); gl.vertexAttribPointer(positionAttribute, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, colorBuffer); gl.enableVertexAttribArray(colorAttribute); gl.vertexAttribPointer(colorAttribute, 3, gl.FLOAT, false, 0, 0); gl.drawArrays(gl.TRIANGLES, 0, state.vertices); }
    if (state.outlineVertices) {
      gl.bindBuffer(gl.ARRAY_BUFFER, outlinePositionBuffer);
      gl.vertexAttribPointer(positionAttribute, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, outlineColorBuffer);
      gl.vertexAttribPointer(colorAttribute, 3, gl.FLOAT, false, 0, 0);
      gl.drawArrays(gl.LINES, 0, state.outlineVertices);
    }
    }
    if (moving || state.dirty) requestRender();
  }
  addEventListener("keydown", e => { state.keys[e.code] = true; state.dirty = true; requestRender(); if (e.code === "KeyM") load(state.index + 1); if (e.code === "KeyN") load(state.index - 1); });
  addEventListener("keyup", e => { state.keys[e.code] = false; });
  canvas.addEventListener("wheel", e => { const forward = [Math.sin(state.yaw) * Math.cos(state.pitch), Math.sin(state.pitch), Math.cos(state.yaw) * Math.cos(state.pitch)]; const amount = e.deltaY * .05; state.position[0] += forward[0] * amount; state.position[1] += forward[1] * amount; state.position[2] += forward[2] * amount; state.dirty = true; requestRender(); e.preventDefault(); }, { passive: false });
  canvas.addEventListener("click", () => canvas.requestPointerLock && canvas.requestPointerLock());
  document.addEventListener("mousemove", e => { if (document.pointerLockElement !== canvas) return; state.yaw -= e.movementX * .003; state.pitch = Math.max(-1.5, Math.min(1.5, state.pitch - e.movementY * .003)); state.dirty = true; requestRender(); });
  const rendererInfo = gl.getExtension("WEBGL_debug_renderer_info");
  const rendererName = rendererInfo ? gl.getParameter(rendererInfo.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
  rendererStatus.textContent = `WebGL: ${rendererName}`;
  init().catch(err => { status.textContent = "Fehler: " + err.message; });
  requestRender();
}());
