// CYD Classic Arcade - the "Load ROMs" page's logic (MIT): reads the ROM
// zips the user picked (only their ZIP directories), works out which games
// they make complete, and sends the useful zips to the board over the
// serial protocol in core/usb_import.h. Plain JS, no dependencies: the page
// wires it to Web Serial, tools/host_tests/loader_test.mjs to a fake board.

export const MAX_FILE = 1536 * 1024;       // never send a file over 1.5 MB
export const BAUD = 115200;

// ---- CRC-32 (the ZIP / PNG one) ---------------------------------------------

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

// Continues a CRC: start with 0, feed the data in any number of parts.
export function crc32(bytes, crc = 0) {
  let c = ~crc >>> 0;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return ~c >>> 0;
}

export const hex8 = (n) => (n >>> 0).toString(16).padStart(8, "0");

// ---- ZIP directory ----------------------------------------------------------
// Only the central directory is read: it holds each member's size and CRC,
// which is all the ROM matcher looks at. No unpacking, no ZIP64.

const EOCD_SIG = 0x06054b50, CDH_SIG = 0x02014b50, EOCD_LEN = 22;

async function readRange(blob, start, end) {
  return new Uint8Array(await blob.slice(start, end).arrayBuffer());
}

// The end-of-central-directory record in `tail` (the file's last bytes), or -1.
// It is 22 bytes plus a comment of up to 65535; search back from the end.
function findEocd(tail) {
  const dv = new DataView(tail.buffer, tail.byteOffset, tail.byteLength);
  for (let i = tail.length - EOCD_LEN; i >= 0; i--) {
    if (dv.getUint32(i, true) === EOCD_SIG && i + EOCD_LEN + dv.getUint16(i + 20, true) <= tail.length) return i;
  }
  return -1;
}

// Members of a ZIP file (a Blob / File): [{name, size, crc}], or null when it
// isn't a ZIP we can read. Reads only the end of the file and its directory.
export async function readZipDirectory(blob) {
  const size = blob.size;
  if (size < EOCD_LEN) return null;
  // Most zips have no comment: the record is the last 22 bytes.
  let tailStart = size - EOCD_LEN;
  let tail = await readRange(blob, tailStart, size);
  let at = findEocd(tail);
  if (at < 0) {
    tailStart = Math.max(0, size - (EOCD_LEN + 65535));
    tail = await readRange(blob, tailStart, size);
    at = findEocd(tail);
    if (at < 0) return null;
  }
  const dv = new DataView(tail.buffer, tail.byteOffset, tail.byteLength);
  const count = dv.getUint16(at + 10, true);
  const cdSize = dv.getUint32(at + 12, true);
  const cdOffset = dv.getUint32(at + 16, true);
  if (count === 0xFFFF || cdOffset === 0xFFFFFFFF || cdOffset + cdSize > tailStart + at) return null;
  const cd = await readRange(blob, cdOffset, cdOffset + cdSize);
  const cv = new DataView(cd.buffer, cd.byteOffset, cd.byteLength);
  const entries = [];
  let p = 0;
  for (let i = 0; i < count; i++) {
    if (p + 46 > cd.length || cv.getUint32(p, true) !== CDH_SIG) return null;
    const nameLen = cv.getUint16(p + 28, true);
    const extraLen = cv.getUint16(p + 30, true);
    const commentLen = cv.getUint16(p + 32, true);
    if (p + 46 + nameLen > cd.length) return null;
    let name = "";
    for (let k = 0; k < nameLen; k++) name += String.fromCharCode(cd[p + 46 + k]);
    entries.push({ name, crc: cv.getUint32(p + 16, true), size: cv.getUint32(p + 24, true) });
    p += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}

// ---- Which games the picked zips make --------------------------------------

const key = (size, crc) => `${size}:${crc >>> 0}`;

// The board's file name: 1..31 of A-Z a-z 0-9 . _ - , ending in .zip.
export function sanitizeName(name) {
  let base = String(name).split(/[\\/]/).pop().replace(/\.zip$/i, "");
  base = base.replace(/[^A-Za-z0-9._-]/g, "_") || "roms";
  return base.slice(0, 27) + ".zip";
}

// games: games.json (list of {id, title, parent, roms:[{name, size, crc, optional}]}).
// zips:  [{name, size, entries}] (entries from readZipDirectory, null = unreadable).
// Returns {games, zips}:
//   games[i] = {id, title, parent, state: "complete" | "incomplete" | "none",
//               missing: [rom names], from: [zip names sent for it], needParent: bool}
//   zips[i]  = {...the zip, use: bool, known: number of known ROMs in it,
//               why: "" | "not a zip" | "nothing known" | "too big" | "not needed"}
// A game's ROMs are looked for in every zip (a clone set's zip holds only
// what differs from its parent's). Only the zips a game needs are sent: for
// a complete game, as few as cover it (its own and its parent's first; games
// share chips, e.g. Namco's sound PROM, so other sets' zips often hold one);
// for an incomplete one, its own and its parent's (the rest may come later).
export function recognise(games, zips) {
  const wanted = new Set();                 // size:crc of every known ROM
  for (const g of games) for (const r of g.roms) wanted.add(key(r.size, parseInt(r.crc, 16)));

  const outZips = zips.map((z) => {
    if (!z.entries) return { ...z, use: false, why: "not a zip", known: 0 };
    const keys = new Set(z.entries.map((e) => key(e.size, e.crc)));
    const known = [...keys].filter((k) => wanted.has(k)).length;
    if (!known) return { ...z, use: false, why: "nothing known", known, keys };
    if (z.size > MAX_FILE) return { ...z, use: false, why: "too big", known, keys };
    return { ...z, use: false, why: "not needed", known, keys };
  });
  const usable = outZips.filter((z) => z.keys && z.why === "not needed");
  const lower = (z) => z.name.toLowerCase().split(/[\\/]/).pop();
  const pickedNames = new Set(zips.map(lower));

  const outGames = games.map((g) => {
    const own = new Set([`${g.id}.zip`, ...(g.parent ? [`${g.parent}.zip`] : [])]);
    const rank = (z) => (lower(z) === `${g.id}.zip` ? 0 : own.has(lower(z)) ? 1 : 2);
    const need = g.roms.filter((r) => !r.optional).map((r) => ({ name: r.name, k: key(r.size, parseInt(r.crc, 16)) }));
    const missing = need.filter((r) => !usable.some((z) => z.keys.has(r.k))).map((r) => r.name);
    const found = need.length - missing.length;
    const state = missing.length === 0 ? "complete" : found ? "incomplete" : "none";
    const from = [];
    if (state === "complete") {
      // Greedy cover: own zips first, then the zip adding the most chips.
      let left = need.map((r) => r.k);
      while (left.length) {
        let best = null, bestGain = 0;
        for (const z of usable) {
          const gain = left.filter((k) => z.keys.has(k)).length;
          if (gain && (!best || rank(z) < rank(best) || (rank(z) === rank(best) && gain > bestGain))) { best = z; bestGain = gain; }
        }
        from.push(best);
        left = left.filter((k) => !best.keys.has(k));
      }
    } else if (state === "incomplete") {
      for (const z of usable) if (rank(z) < 2 && need.some((r) => z.keys.has(r.k))) from.push(z);
    }
    for (const z of from) { z.use = true; z.why = ""; }
    const needParent = state === "incomplete" && !!g.parent && !pickedNames.has(`${g.parent}.zip`);
    return { id: g.id, title: g.title, parent: g.parent || null, state, missing,
             from: from.map((z) => z.name).sort(), needParent };
  });
  for (const z of outZips) delete z.keys;
  return { games: outGames, zips: outZips };
}

// ---- Lines from the board ---------------------------------------------------

// Collects the board's bytes into lines; readLine() waits for the next one.
// Links use it on their incoming side.
export class LineQueue {
  constructor() {
    this.decoder = new TextDecoder();
    this.partial = "";
    this.lines = [];
    this.waiters = [];
    this.closed = false;
  }
  push(chunk) {
    this.partial += typeof chunk === "string" ? chunk : this.decoder.decode(chunk, { stream: true });
    let nl;
    while ((nl = this.partial.indexOf("\n")) >= 0) {
      const line = this.partial.slice(0, nl).replace(/\r$/, "");
      this.partial = this.partial.slice(nl + 1);
      const w = this.waiters.shift();
      if (w) w(line); else this.lines.push(line);
    }
  }
  close() {
    this.closed = true;
    for (const w of this.waiters.splice(0)) w(null);
  }
  // The next line, or null after `ms` (or once closed).
  readLine(ms) {
    if (this.lines.length) return Promise.resolve(this.lines.shift());
    if (this.closed) return Promise.resolve(null);
    return new Promise((resolve) => {
      let timer;
      const w = (line) => { clearTimeout(timer); resolve(line); };
      timer = setTimeout(() => {
        const i = this.waiters.indexOf(w);
        if (i >= 0) this.waiters.splice(i, 1);
        resolve(null);
      }, ms);
      this.waiters.push(w);
    });
  }
}

// ---- Sending ----------------------------------------------------------------

export class TransferError extends Error {
  constructor(code, message, extra = {}) {
    super(message);
    this.code = code;          // "no-answer" | "timeout" | "no-room" | "clear-failed" | "closed"
    Object.assign(this, extra);
  }
}

const align4 = (n) => (n + 3) & ~3;

// Sends the zips over `link` = {sendLine(text), sendBytes(Uint8Array),
// readLine(ms) -> the next line or null, closed (true once the port is
// gone)}, following core/usb_import.h.
//   files: [{name (already sanitised), blob}]
//   opts:  replace (CLEAR first), onLog(line), onStatus(text), onBoard({version, free, files}),
//          onFile(index, {state: "checking" | "sending" | "ok" | "skipped" | "error", sent, size, error}),
//          helloTimeoutMs (20000), stepTimeoutMs (10000)
// Returns {version, free, results: [{name, state, error}], games: [ids]};
// throws TransferError when the board doesn't answer or there isn't room.
export async function transfer(link, files, opts = {}) {
  const onLog = opts.onLog || (() => {});
  const onStatus = opts.onStatus || (() => {});
  const onFile = opts.onFile || (() => {});
  const helloTimeout = opts.helloTimeoutMs ?? 20000;
  const stepTimeout = opts.stepTimeoutMs ?? 10000;

  // The next line of ours ("CYDA ..." without the prefix). Log lines go to
  // onLog; answers to extra HELLOs (sent while the board was starting) are skipped.
  async function next(ms = stepTimeout, keepHello = false) {
    const until = Date.now() + ms;
    for (;;) {
      const left = until - Date.now();
      const line = left > 0 ? await link.readLine(left) : null;
      if (line === null) {
        if (link.closed) throw new TransferError("closed", "The board was disconnected.");
        throw new TransferError("timeout", "The board stopped answering. Unplug it, plug it back in and try again.");
      }
      if (!line.startsWith("CYDA ")) { onLog(line); continue; }
      const msg = line.slice(5);
      if (!keepHello && msg.startsWith("HELLO ")) continue;
      return msg;
    }
  }

  // Each file's bytes and CRC first (the files stay on this computer until sent).
  const prepared = [];
  for (let i = 0; i < files.length; i++) {
    onFile(i, { state: "checking", sent: 0, size: files[i].blob.size });
    const bytes = new Uint8Array(await files[i].blob.arrayBuffer());
    prepared.push({ name: files[i].name, bytes, crc: crc32(bytes) });
  }

  // HELLO: opening the port may have reset the board, so ask again until it
  // answers. After four tries, keep quiet for 6 s: a board left half way
  // through a file (bytes mode) takes our HELLOs as file bytes until it gives
  // up on that file (5 s without bytes).
  onStatus("Waiting for the board…");
  const start = Date.now();
  const askAt = [0, 1500, 3000, 4500, 11000, 12500, 14000, 15500, 17000, 18500];
  let hello = null, asked = 0;
  while (!hello) {
    const t = Date.now() - start;
    if (t >= helloTimeout) break;
    if (asked < askAt.length && t >= askAt[asked]) { await link.sendLine("CYDA HELLO"); asked++; continue; }
    const nextAsk = asked < askAt.length ? askAt[asked] : helloTimeout;
    const line = await link.readLine(Math.max(20, Math.min(nextAsk, helloTimeout) - t));
    if (line === null) { if (link.closed) break; continue; }
    if (line.startsWith("CYDA HELLO ")) hello = line;
    else if (!line.startsWith("CYDA ")) onLog(line);
  }
  if (!hello) throw new TransferError("no-answer", "No answer - is CYD Classic Arcade v0.2.0 or newer on the board?");
  const parseHello = (h) => {
    const [, , version, free, count] = h.split(" ");
    return { version, free: Number(free) || 0, files: Number(count) || 0 };
  };
  const onBoard = opts.onBoard || (() => {});
  let board = parseHello(hello);
  onBoard(board);

  // What's on the board already: CLEAR it, or LIST it to skip files it has.
  const onBoardNow = new Set();
  if (opts.replace) {
    onStatus("Clearing the board's ROMs…");
    await link.sendLine("CYDA CLEAR");
    const r = await next();
    if (r !== "OK") throw new TransferError("clear-failed", `The board couldn't clear its ROMs (${r}).`);
    await link.sendLine("CYDA HELLO");
    board = parseHello("CYDA " + (await next(stepTimeout, true)));
    onBoard(board);
  } else {
    await link.sendLine("CYDA LIST");
    for (;;) {
      const r = await next();
      if (r === "END") break;
      const m = r.match(/^FILE (\S+) (\d+) ([0-9a-fA-F]+)$/);
      if (m) onBoardNow.add(`${m[1]} ${m[2]} ${parseInt(m[3], 16)}`);
    }
  }

  const results = prepared.map((f) => ({ name: f.name, state: "waiting", error: "" }));
  const todo = [];
  prepared.forEach((f, i) => {
    if (onBoardNow.has(`${f.name} ${f.bytes.length} ${f.crc}`)) {
      results[i].state = "skipped";
      onFile(i, { state: "skipped", sent: f.bytes.length, size: f.bytes.length });
    } else todo.push(i);
  });
  const need = todo.filter((i) => prepared[i].bytes.length <= MAX_FILE)
                   .reduce((s, i) => s + align4(prepared[i].bytes.length), 0);
  if (need > board.free) {
    throw new TransferError("no-room", "Not enough room on the board.", { need, free: board.free });
  }

  for (const i of todo) {
    const f = prepared[i];
    const size = f.bytes.length;
    const fail = (error) => { results[i].state = "error"; results[i].error = error; onFile(i, { state: "error", sent: 0, size, error }); };
    if (size > MAX_FILE) { fail("too big"); continue; }
    onStatus(`Sending ${f.name}…`);
    onFile(i, { state: "sending", sent: 0, size });
    await link.sendLine(`CYDA PUT ${f.name} ${size} ${hex8(f.crc)}`);
    const r = await next();
    const ready = r.match(/^READY (\d+)$/);
    if (!ready) { fail(r.replace(/^ERR /, "")); continue; }
    const chunk = Math.max(1, Number(ready[1]));
    let sent = 0, error = "";
    while (sent < size) {
      const n = Math.min(chunk, size - sent);
      await link.sendBytes(f.bytes.subarray(sent, sent + n));
      sent += n;
      const a = await next();
      if (a !== `ACK ${sent}`) { error = a.replace(/^ERR /, ""); break; }
      onFile(i, { state: "sending", sent, size });
    }
    if (!error) {
      const end = await next();
      if (end !== `OK ${f.name}`) error = end.replace(/^ERR /, "");
    }
    if (error) { fail(error); continue; }
    results[i].state = "ok";
    onFile(i, { state: "ok", sent: size, size });
  }

  onStatus("Asking the board which games it has…");
  await link.sendLine("CYDA DONE");
  let g;
  do g = await next(); while (!g.startsWith("GAMES"));
  const ids = g.slice(5).trim().split(/\s+/).filter(Boolean);
  return { version: board.version, free: board.free, results, games: ids };
}
