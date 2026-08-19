/* Public Example Groovebox — 4 synth + 4 drum. Original MIT, not AB 9-09. */
(function () {
  const NAMES = ["Syn 1", "Syn 2", "Syn 3", "Syn 4", "Kick", "Snare", "Hat", "Clap"];
  const STEPS = 16;
  const grid = Array.from({ length: 8 }, () => new Uint8Array(STEPS));
  let ctx = null;
  let master = null;
  let playing = false;
  let step = 0;
  let timer = 0;

  const root = document.getElementById("grid");
  NAMES.forEach((name, row) => {
    const el = document.createElement("div");
    el.className = "row";
    const lab = document.createElement("span");
    lab.className = "lab";
    lab.textContent = name;
    el.appendChild(lab);
    for (let s = 0; s < STEPS; s++) {
      const c = document.createElement("button");
      c.type = "button";
      c.className = "cell";
      c.dataset.row = String(row);
      c.dataset.step = String(s);
      c.addEventListener("click", () => {
        grid[row][s] ^= 1;
        c.classList.toggle("on", !!grid[row][s]);
      });
      el.appendChild(c);
    }
    root.appendChild(el);
  });

  function ensureAudio() {
    if (ctx) return ctx;
    ctx = new AudioContext({ latencyHint: "interactive" });
    master = ctx.createGain();
    master.gain.value = 0.55;
    master.connect(ctx.destination);
    return ctx;
  }

  function envGain(dest, t0, peak, decay) {
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.exponentialRampToValueAtTime(peak, t0 + 0.004);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + decay);
    g.connect(dest);
    return g;
  }

  function fireSynth(row, t0) {
    const osc = ctx.createOscillator();
    osc.type = row === 1 ? "square" : row === 2 ? "triangle" : "sawtooth";
    osc.frequency.value = [110, 146.8, 196, 261.6][row];
    const g = envGain(master, t0, 0.22, 0.18);
    osc.connect(g);
    osc.start(t0);
    osc.stop(t0 + 0.2);
  }

  function fireDrum(row, t0) {
    if (row === 4) {
      const osc = ctx.createOscillator();
      osc.type = "sine";
      osc.frequency.setValueAtTime(120, t0);
      osc.frequency.exponentialRampToValueAtTime(42, t0 + 0.08);
      const g = envGain(master, t0, 0.9, 0.28);
      osc.connect(g);
      osc.start(t0);
      osc.stop(t0 + 0.32);
      return;
    }
    const n = 0.2 * ctx.sampleRate;
    const buf = ctx.createBuffer(1, n, ctx.sampleRate);
    const d = buf.getChannelData(0);
    for (let i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
    const src = ctx.createBufferSource();
    src.buffer = buf;
    const bp = ctx.createBiquadFilter();
    bp.type = row === 6 ? "highpass" : "bandpass";
    bp.frequency.value = row === 5 ? 1800 : row === 6 ? 7000 : 1200;
    const decay = row === 6 ? 0.07 : 0.18;
    const g = envGain(master, t0, row === 6 ? 0.35 : 0.55, decay);
    src.connect(bp);
    bp.connect(g);
    src.start(t0);
    src.stop(t0 + decay + 0.02);
  }

  function hit(row, t0) {
    if (row < 4) fireSynth(row, t0);
    else fireDrum(row, t0);
  }

  function tick() {
    const cells = document.querySelectorAll(".cell");
    cells.forEach((c) => c.classList.toggle("play", +c.dataset.step === step));
    if (!ctx) return;
    const t0 = ctx.currentTime;
    for (let r = 0; r < 8; r++) {
      if (grid[r][step]) hit(r, t0);
    }
    step = (step + 1) % STEPS;
  }

  function bpmMs() {
    const bpm = Math.max(60, Math.min(180, +document.getElementById("bpm").value || 120));
    return (60 / bpm) * 250;
  }

  document.getElementById("start").onclick = async () => {
    ensureAudio();
    if (ctx.state === "suspended") await ctx.resume();
    document.getElementById("start").textContent = "Audio on";
  };

  document.getElementById("play").onclick = async () => {
    ensureAudio();
    if (ctx.state === "suspended") await ctx.resume();
    playing = !playing;
    document.getElementById("play").textContent = playing ? "Stop" : "Play";
    if (playing) {
      step = 0;
      tick();
      timer = setInterval(tick, bpmMs());
    } else {
      clearInterval(timer);
      document.querySelectorAll(".cell").forEach((c) => c.classList.remove("play"));
    }
  };

  window.addEventListener("keydown", (e) => {
    if (e.repeat) return;
    const k = e.key;
    if (k >= "1" && k <= "8") {
      ensureAudio();
      if (ctx.state === "suspended") ctx.resume();
      hit(k.charCodeAt(0) - 49, ctx.currentTime);
    }
  });
})();
