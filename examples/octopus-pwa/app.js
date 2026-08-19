/* Public Octopus PWA — original MIT demo, not product firmware. */
(function () {
  const SCALES = [
    { name: "Major", deg: [0, 2, 4, 5, 7, 9, 11, 12] },
    { name: "Minor", deg: [0, 2, 3, 5, 7, 8, 10, 12] },
    { name: "Pentatonic", deg: [0, 2, 4, 7, 9, 12, 14, 16] },
    { name: "Dorian", deg: [0, 2, 3, 5, 7, 9, 10, 12] },
    { name: "Mixolydian", deg: [0, 2, 4, 5, 7, 9, 10, 12] },
    { name: "Blues", deg: [0, 3, 5, 6, 7, 10, 12, 15] },
    { name: "Whole", deg: [0, 2, 4, 6, 8, 10, 12, 14] },
    { name: "Chromatic", deg: [0, 1, 2, 3, 4, 5, 6, 7] },
  ];
  const PRESETS = [
    { name: "Sine", wave: 0, attack: 0.04, release: 0.22, cutoff: 0.85, level: 0.8 },
    { name: "Triangle", wave: 1, attack: 0.008, release: 0.16, cutoff: 0.8, level: 0.82 },
    { name: "Saw", wave: 2, attack: 0.006, release: 0.14, cutoff: 0.55, level: 0.7 },
    { name: "Square", wave: 3, attack: 0.006, release: 0.18, cutoff: 0.45, level: 0.68 },
    { name: "Pulse", wave: 4, attack: 0.004, release: 0.12, cutoff: 0.5, level: 0.66 },
    { name: "Organ", wave: 5, attack: 0.02, release: 0.28, cutoff: 0.7, level: 0.75 },
    { name: "Bell", wave: 6, attack: 0.002, release: 0.65, cutoff: 0.9, level: 0.72 },
    { name: "Noise", wave: 7, attack: 0.001, release: 0.08, cutoff: 0.4, level: 0.55 },
  ];
  const FX = ["Bypass", "Low-pass", "Delay", "Chorus", "Distort", "Tremolo", "Crush", "Room"];

  const gates = new Uint8Array(8);
  const phase = new Float32Array(8);
  const env = new Float32Array(8);
  const lpf = new Float32Array(8);
  const delay = new Float32Array(4096);
  let dw = 0, fxLfo = 0, roomA = 0, roomB = 0, fxLp = 0, vibLfo = 0;
  let ctx = null, node = null, midiOut = null;

  function midiHz(m) { return 440 * Math.pow(2, (m - 69) / 12); }
  function lerpWt(wave, p) {
    const table = window.WT8[wave & 7];
    const n = window.WT8_LEN;
    let x = p % n; if (x < 0) x += n;
    const i0 = x | 0;
    const i1 = i0 + 1 < n ? i0 + 1 : 0;
    const f = x - i0;
    return ((table[i0] + (table[i1] - table[i0]) * f) / 32768);
  }
  function clamp(n, lo, hi) { return n < lo ? lo : n > hi ? hi : n; }

  function fxProcess(kind, x, amt, fs) {
    const a = clamp(amt, 0, 1);
    switch (kind) {
      case 1:
        fxLp += (0.04 + a * 0.9) * (x - fxLp);
        return fxLp;
      case 2: {
        const d = 800 + (a * 2800) | 0;
        let r = dw - d; if (r < 0) r += 4096;
        const tap = delay[r];
        const y = x + tap * (0.15 + a * 0.45);
        delay[dw] = x + tap * 0.25;
        dw = dw + 1 < 4096 ? dw + 1 : 0;
        return clamp(y, -1, 1);
      }
      case 3: {
        fxLfo += 0.8 / fs; if (fxLfo > 1) fxLfo -= 1;
        const mod = 0.5 + 0.5 * Math.sin(fxLfo * 2 * Math.PI);
        const d = 90 + (mod * (40 + a * 80)) | 0;
        let r = dw - d; if (r < 0) r += 4096;
        const y = x + delay[r] * (0.3 + a * 0.4);
        delay[dw] = x;
        dw = dw + 1 < 4096 ? dw + 1 : 0;
        return clamp(y, -1, 1);
      }
      case 4: {
        const t = Math.tanh(x * (1 + a * 8));
        return x * (1 - a) + t * a;
      }
      case 5: {
        fxLfo += (2 + a * 8) / fs; if (fxLfo > 1) fxLfo -= 1;
        const m = 0.5 + 0.5 * Math.sin(fxLfo * 2 * Math.PI);
        return x * (1 - a + a * m);
      }
      case 6: {
        const steps = 4 + (1 - a) * 28;
        return Math.floor(x * steps + 0.5) / steps;
      }
      case 7: {
        roomA = roomA * (0.82 - a * 0.1) + x * 0.25;
        roomB = roomB * (0.88 - a * 0.08) + roomA * 0.35;
        return clamp(x + (roomA + roomB) * (0.2 + a * 0.45), -1, 1);
      }
      default:
        return x;
    }
  }

  function render(out, fs) {
    const scale = SCALES[document.getElementById("scale").selectedIndex & 7];
    const pr = PRESETS[document.getElementById("preset").selectedIndex & 7];
    const kind = document.getElementById("fx").selectedIndex & 7;
    const amt = document.getElementById("fxAmt").value / 100;
    const cc1 = document.getElementById("dbeam").value | 0;
    const atk = 1 / Math.max(0.0005, pr.attack) / fs;
    const rel = 1 / Math.max(0.0005, pr.release) / fs;
    const cut = clamp(pr.cutoff, 0.02, 0.98);
    for (let n = 0; n < out.length; n++) {
      let mix = 0;
      vibLfo += 5 / fs; if (vibLfo > 1) vibLfo -= 1;
      const vib = (cc1 / 127) * 0.03 * Math.sin(vibLfo * 2 * Math.PI);
      for (let i = 0; i < 8; i++) {
        if (gates[i]) {
          env[i] += (1 - env[i]) * atk;
          if (env[i] > 1) env[i] = 1;
        } else {
          env[i] -= env[i] * rel;
          if (env[i] < 1e-5) env[i] = 0;
        }
        if (env[i] <= 0) continue;
        const hz = midiHz(60 + scale.deg[i]) * (1 + vib);
        phase[i] += hz * (window.WT8_LEN / fs);
        while (phase[i] >= window.WT8_LEN) phase[i] -= window.WT8_LEN;
        const x = lerpWt(pr.wave, phase[i]);
        lpf[i] += cut * (x - lpf[i]);
        mix += lpf[i] * env[i];
      }
      out[n] = fxProcess(kind, clamp(mix * pr.level * 0.35, -1, 1), amt, fs);
    }
  }

  function fillSelect(id, names) {
    const el = document.getElementById(id);
    el.innerHTML = names.map((n) => `<option>${n}</option>`).join("");
  }
  fillSelect("scale", SCALES.map((s) => s.name));
  fillSelect("preset", PRESETS.map((p) => p.name));
  fillSelect("fx", FX);

  function setCc1(v) {
    const cc1 = v | 0;
    document.getElementById("cc1out").textContent = "CC1 " + cc1;
    if (midiOut) midiOut.send([0xB0, 0x01, cc1]);
  }
  document.getElementById("dbeam").addEventListener("input", (e) => setCc1(e.target.value));
  setCc1(document.getElementById("dbeam").value);

  function setGate(i, on) {
    gates[i] = on ? 1 : 0;
    document.querySelectorAll(".beam")[i].classList.toggle("on", !!on);
  }

  document.querySelectorAll(".beam").forEach((el) => {
    const i = +el.dataset.i;
    el.addEventListener("pointerdown", (e) => { e.preventDefault(); el.setPointerCapture(e.pointerId); setGate(i, 1); });
    el.addEventListener("pointerup", () => setGate(i, 0));
    el.addEventListener("pointercancel", () => setGate(i, 0));
    el.addEventListener("pointerleave", (e) => { if (e.buttons === 0) setGate(i, 0); });
  });
  window.addEventListener("keydown", (e) => {
    const k = e.key;
    if (k >= "1" && k <= "8") setGate(k.charCodeAt(0) - 49, 1);
  });
  window.addEventListener("keyup", (e) => {
    const k = e.key;
    if (k >= "1" && k <= "8") setGate(k.charCodeAt(0) - 49, 0);
  });

  document.getElementById("start").addEventListener("click", async () => {
    if (!ctx) {
      ctx = new AudioContext({ sampleRate: 44100 });
      node = ctx.createScriptProcessor(512, 0, 1);
      node.onaudioprocess = (ev) => {
        if (ctx.state !== "running") return;
        render(ev.outputBuffer.getChannelData(0), ctx.sampleRate);
      };
      node.connect(ctx.destination);
    }
    if (ctx.state === "suspended") await ctx.resume();
    document.getElementById("start").textContent = "Audio on";
    if (navigator.requestMIDIAccess) {
      try {
        const midi = await navigator.requestMIDIAccess();
        midiOut = midi.outputs.values().next().value || null;
      } catch (_) { midiOut = null; }
    }
  });
})();
