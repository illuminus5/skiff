// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The FM-1's panel drawn in SVG, shared by the browser emulator (web/emu), the Learn page (web/learn) and (2.7) the
// editor's Screen tab: drawings of the same controls, WIDE after the unit's own layout, TALL for a phone held upright,
// SIDE (the Screen tab's) with the screen large and the controls beside it. A classic script (it loads
// from file:// too), as window.PANEL. Colours come from the page's CSS: --body --tray --part --edge --label, and the
// classes face btn / face key / slit.
(function () {
  "use strict";
  const NS = "http://www.w3.org/2000/svg";
  // panel.c's labels (bit = label) and encoder roles
  const B = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, "OCT-": 12, "OCT+": 13 };
  const EN = { SELECT: 0, ALGORITHM: 1, PRESETS: 2, "KNOB 1": 3, "KNOB 2": 4, "KNOB 3": 5, "KNOB 4": 6 };
  // the 27 keys F3..G5: key k = k semitones above F3
  const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
  const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
  const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];      // the white key each black one follows
  const ROW1 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
  const KNOBS = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"];
  const LAYOUTS = {
    wide: {
      w: 1000, h: 624, screen: [292, 44, 256],
      knobs: [[105, 100, 24], [215, 100, 24], [105, 205, 24], [215, 205, 24], [610, 100, 22], [714, 100, 22], [818, 100, 22], [922, 100, 22]],
      trays: [[50, 250, 220, 50], [568, 160, 400, 140]],
      oct: [[60, 258, 90, 34], [170, 258, 90, 34]],
      grid: [582, 174, 54, 48, 63, 62], keybed: [36, 332, 928, 256],
    },
    // (2.7) the editor's Screen tab: the screen as large as the drawing's height, the controls beside it
    side: {
      w: 1600, h: 900, screen: [24, 24, 852],
      knobs: [[1492, 236, 34], [988, 236, 34], [1156, 236, 34], [1324, 236, 34], [988, 100, 34], [1156, 100, 34], [1324, 100, 34], [1492, 100, 34]],
      trays: [[904, 300, 672, 176], [904, 492, 300, 66]],
      oct: [[918, 502, 130, 46], [1062, 502, 130, 46]],
      grid: [918, 314, 98, 66, 108, 82], keybed: [904, 574, 672, 302],
    },
    tall: {
      w: 600, h: 1070, screen: [130, 104, 340],
      knobs: [[66, 64, 24], [186, 64, 24], [414, 64, 24], [534, 64, 24], [90, 512, 26], [230, 512, 26], [370, 512, 26], [510, 512, 26]],
      trays: [[14, 572, 572, 146], [150, 728, 300, 62]],
      oct: [[164, 737, 128, 44], [308, 737, 128, 44]],
      grid: [24, 582, 86, 60, 94, 70], keybed: [14, 800, 572, 256],
    },
  };
  const SANS = "-apple-system, 'Helvetica Neue', Arial, sans-serif";
  const KEYNAMES = ["F", "F♯", "G", "G♯", "A", "A♯", "B", "C", "C♯", "D", "D♯", "E"];
  const keyName = (k) => KEYNAMES[k % 12] + (3 + Math.floor((k + 5) / 12));

  function el(tag, attrs, parent) {
    const e = document.createElementNS(NS, tag);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    parent.appendChild(e);
    return e;
  }
  function label(parent, x, y, s, size, font = SANS, ink = "var(--label)") {
    const t = el("text", { x, y, "text-anchor": "middle", "font-family": font,
                           "font-size": size, "font-weight": 600, fill: ink, "letter-spacing": ".04em" }, parent);
    t.textContent = s;
    return t;
  }

  /* draw layout `name` into svg (emptied first). Returns {L, screen: [x, y, size] (the 240 x 240 panel inside its
   * bezel, in svg units), knobs: [{g, ind, x, y, name, role}], buttons: [{g, bit, name}], keys: [{g, name}] (index =
   * semitone from F3)}. opts.knobFont: the face of the knobs' names */
  function draw(svg, name, opts = {}) {
    const L = LAYOUTS[name], knobs = [], buttons = [], keys = [];
    svg.innerHTML = "";
    svg.setAttribute("viewBox", `0 0 ${L.w} ${L.h}`);
    el("rect", { x: 4, y: 4, width: L.w - 8, height: L.h - 8, rx: 40, fill: "var(--body)" }, svg);
    for (const [x, y, w, h] of L.trays) el("rect", { x, y, width: w, height: h, rx: 14, fill: "var(--tray)" }, svg);
    const [sx, sy, ss] = L.screen;
    el("rect", { x: sx, y: sy, width: ss, height: ss, rx: 18, fill: "#0b0b0c", class: "bezel" }, svg);
    const inset = ss / 32;                                         // the 240 x 240 panel inside its bezel
    KNOBS.forEach((n, i) => {
      const [x, y, r] = L.knobs[i], g = el("g", { "data-ctrl": n.replace(" ", "") }, svg);
      el("circle", { cx: x, cy: y, r: r + 5, fill: "var(--tray)" }, g);
      el("circle", { cx: x, cy: y, r, fill: "var(--part)", stroke: "var(--edge)", "stroke-width": 2, class: "face knob" }, g);
      const ind = n === "MASTER" ? el("line", { x1: x, y1: y - r + 5, x2: x, y2: y - r + 14, stroke: "var(--label)", "stroke-width": 3, "stroke-linecap": "round" }, g) : null;
      label(g, x, y - r - 12, n, 16, opts.knobFont || SANS);
      el("circle", { cx: x, cy: y, r: r + 12, fill: "transparent" }, g);
      knobs.push({ g, ind, x, y, name: n, role: n === "MASTER" ? "master" : EN[n] });
    });
    const button = ([x, y, w, h], n, sub) => {
      const g = el("g", { "data-ctrl": n }, svg);
      el("rect", { x, y, width: w, height: h, rx: 8, fill: "var(--part)", stroke: "var(--edge)", "stroke-width": 1.5, class: "face btn" }, g);
      const ink = n === "REC" ? "#ff453a" : undefined;
      if (sub) { label(g, x + w / 2, y + h / 2 - 2, n, 10.8, SANS, ink); label(g, x + w / 2, y + h / 2 + 11, sub, 9.9); }
      else label(g, x + w / 2, y + h / 2 + 4.5, n, 12.6, SANS, ink);
      buttons.push({ g, bit: B[n], name: sub ? `${n} / ${sub}` : n });
    };
    button(L.oct[0], "OCT-");
    button(L.oct[1], "OCT+");
    const [gx, gy, gw, gh, dx, dy] = L.grid;
    for (let i = 0; i < 6; i++) {
      button([gx + i * dx, gy, gw, gh], ROW1[i]);
      button([gx + i * dx, gy + dy, gw, gh], ROW2[i], ROW2[i] === "PLAY" ? "STOP" : "");
    }
    const [x0, y0, w, h] = L.keybed;
    el("rect", { x: x0, y: y0, width: w, height: h, rx: 18, fill: "var(--tray)" }, svg);
    // the keys: two rows of the same black keys, each with a slit down its middle that lights
    const pitch = (w - 24) / 16, kw = pitch * 0.78, kh = h * 0.425, sw = Math.max(3, kw * 0.14);
    const key = (cx, cy, k) => {
      const g = el("g", { "data-ctrl": "KEY" + k }, svg), y = cy - kh / 2;
      el("rect", { x: cx - kw / 2, y, width: kw, height: kh, rx: kw / 2, class: "face key" }, g);
      el("rect", { x: cx - sw / 2, y: y + kw * 0.4, width: sw, height: kh - kw * 0.8, rx: sw / 2, class: "slit" }, g);
      el("rect", { x: cx - pitch / 2, y: cy - h * 0.24, width: pitch, height: h * 0.48, fill: "transparent" }, g);   // its target
      keys[k] = { g, name: keyName(k) };
    };
    WHITE.forEach((k, i) => key(x0 + 12 + (i + 0.5) * pitch, y0 + h * 0.75, k));
    BLACK_AFTER.forEach((a, j) => key(x0 + 12 + (a + 1) * pitch, y0 + h * 0.25, BLACK[j]));
    return { L, screen: [sx + inset, sy + inset, ss - 2 * inset], knobs, buttons, keys };
  }

  window.PANEL = { B, EN, PLAY_GREEN: 14, WHITE, BLACK, BLACK_AFTER, ROW1, ROW2, KNOBS, LAYOUTS, keyName, draw };
})();
