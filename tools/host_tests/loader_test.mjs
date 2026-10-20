// Tests web/roms/loader.js (the "Load ROMs" page's logic) end to end against
// tools/host_tests/fake_board (core/usb_import + core/rom_store over stdio).
//
//   node loader_test.mjs --board <fake_board> --games <games.json>
//                        --fake <synthetic pacman.zip> [--roms <dir with MAME zips>]
// --roms is for local runs with real sets (never in CI, never committed).
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const L = await import(pathToFileURL(path.join(here, "../../web/roms/loader.js")).href);

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const games = JSON.parse(fs.readFileSync(args.games, "utf8"));

let pass = 0, fail = 0;
function check(ok, what) {
  if (ok) pass++;
  else { fail++; console.log("FAIL: " + what); }
}

// ---- a tiny ZIP writer (stored members) for test files ----------------------
// `lie` = {crc, size} written into the directory instead of the real ones, so
// recognition can be tested with made-up "ROMs" (only the directory is read).
function makeZip(members, comment = "") {
  const locals = [], centrals = [];
  let offset = 0;
  for (const m of members) {
    const name = Buffer.from(m.name, "latin1");
    const data = Buffer.from(m.data);
    const crc = m.lie ? m.lie.crc : L.crc32(data);
    const size = m.lie ? m.lie.size : data.length;
    const lh = Buffer.alloc(30);
    lh.writeUInt32LE(0x04034b50, 0); lh.writeUInt16LE(10, 4);
    lh.writeUInt32LE(crc, 14); lh.writeUInt32LE(data.length, 18); lh.writeUInt32LE(size, 22);
    lh.writeUInt16LE(name.length, 26);
    const ch = Buffer.alloc(46);
    ch.writeUInt32LE(0x02014b50, 0); ch.writeUInt16LE(20, 4); ch.writeUInt16LE(10, 6);
    ch.writeUInt32LE(crc, 16); ch.writeUInt32LE(data.length, 20); ch.writeUInt32LE(size, 24);
    ch.writeUInt16LE(name.length, 28); ch.writeUInt32LE(offset, 42);
    locals.push(lh, name, data);
    centrals.push(ch, name);
    offset += 30 + name.length + data.length;
  }
  const cd = Buffer.concat(centrals);
  const c = Buffer.from(comment, "latin1");
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(members.length, 8); end.writeUInt16LE(members.length, 10);
  end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(offset, 16); end.writeUInt16LE(c.length, 20);
  return Buffer.concat([...locals, cd, end, c]);
}

const romOf = (id, name) => games.find((g) => g.id === id).roms.find((r) => r.name === name);
const lieFor = (r) => ({ crc: parseInt(r.crc, 16), size: r.size });
function rnd(n, seed) {
  const b = Buffer.alloc(n);
  for (let i = 0; i < n; i++) { seed = (seed * 1103515245 + 12345) >>> 0; b[i] = seed >>> 16; }
  return b;
}

// A zip with two of Pac-Man's chips (by its directory) and an archive comment.
const halfPac = makeZip([
  { name: "pacman.6e", data: rnd(100, 1), lie: lieFor(romOf("pacman", "pacman.6e")) },
  { name: "pacman.6f", data: rnd(100, 2), lie: lieFor(romOf("pacman", "pacman.6f")) },
], "a comment, so the end record isn't the last 22 bytes");
const junk = makeZip([{ name: "readme.txt", data: Buffer.from("not a ROM") }]);
const notZip = rnd(5000, 3);
const bigPac = makeZip([
  { name: "pacman.6e", data: rnd(100, 4), lie: lieFor(romOf("pacman", "pacman.6e")) },
  { name: "filler.bin", data: rnd(1600 * 1024, 5) },
]);
const fakePac = fs.readFileSync(args.fake);

const zipInfo = async (name, buf) => ({ name, size: buf.length, entries: await L.readZipDirectory(new Blob([buf])) });

// ---- 1. plain parts ----------------------------------------------------------
check(L.hex8(L.crc32(Buffer.from("123456789"))) === "cbf43926", "CRC-32 check value");
check(L.crc32(Buffer.from("6789"), L.crc32(Buffer.from("12345"))) === 0xcbf43926, "CRC-32 in parts");
check(L.sanitizeName("Pac Man (Midway).ZIP") === "Pac_Man__Midway_.zip", "names: odd characters");
check(L.sanitizeName("folder/pacman.zip") === "pacman.zip", "names: folders dropped");
check(L.sanitizeName("a".repeat(60) + ".zip").length === 31, "names: at most 31 characters");
{
  const e = await L.readZipDirectory(new Blob([halfPac]));
  check(e && e.length === 2 && e[0].name === "pacman.6e", "ZIP directory with a comment");
  check(await L.readZipDirectory(new Blob([notZip])) === null, "not a zip");
  const f = await L.readZipDirectory(new Blob([fakePac]));
  check(f && f.length === 9, "the synthetic Pac-Man zip (deflated, written by Python)");
}

// ---- 2. which games the picked zips make -------------------------------------
{
  const r = L.recognise(games, [await zipInfo("pacman.zip", halfPac), await zipInfo("junk.zip", junk),
                                await zipInfo("notzip.zip", notZip), await zipInfo("big.zip", bigPac),
                                await zipInfo("other.zip", halfPac)]);
  const pac = r.games.find((g) => g.id === "pacman"), inv = r.games.find((g) => g.id === "invaders");
  check(pac.state === "incomplete" && pac.missing.length === 7 && pac.needParent, "half a set: incomplete, parent suggested");
  check(inv.state === "none", "a game not picked");
  const z = Object.fromEntries(r.zips.map((x) => [x.name, x]));
  check(z["pacman.zip"].use && z["pacman.zip"].known === 2, "an incomplete game's own zip is used");
  check(!z["other.zip"].use && z["other.zip"].why === "not needed", "another zip holding its chips isn't");
  check(!z["junk.zip"].use && z["junk.zip"].why === "nothing known", "a zip with nothing known isn't");
  check(!z["notzip.zip"].use && z["notzip.zip"].why === "not a zip", "a file that isn't a zip isn't");
  check(!z["big.zip"].use && z["big.zip"].why === "too big", "over 1.5 MB isn't");

  const r2 = L.recognise(games, [await zipInfo("pacman.zip", fakePac), await zipInfo("half.zip", halfPac)]);
  const pac2 = r2.games.find((g) => g.id === "pacman");
  check(pac2.state === "complete" && pac2.from.join() === "pacman.zip", "complete: only the zips needed");
  check(!r2.zips[1].use && r2.zips[1].why === "not needed", "a zip whose chips are elsewhere isn't sent");
  // A clone's split zip + its parent's: both needed.
  const fakeEntries = await L.readZipDirectory(new Blob([fakePac]));
  const r3 = L.recognise(games, [{ name: "pacman.zip", size: 999, entries: fakeEntries.slice(0, 4) },
                                 { name: "puckman.zip", size: 999, entries: fakeEntries.slice(3) },
                                 { name: "galaga.zip", size: 999, entries: fakeEntries.slice(8) }]);
  check(r3.games[0].state === "complete" && r3.games[0].from.join() === "pacman.zip,puckman.zip" && !r3.zips[2].use,
        "a clone from its zip and its parent's: " + JSON.stringify(r3.games[0].from));
}
{
  // A game with variants (Donkey Kong: dkong or dkongo program ROMs): either
  // set's ROMs make it complete; missing ROMs are named for the closest set.
  const dk = games.find((g) => g.id === "dkong");
  check(dk && dk.variants && dk.variants.join() === "dkong,dkongo", "games.json lists dkong's variants");
  const forSet = (v) => dk.roms.filter((r) => !r.variant || r.variant === v)
    .map((r, i) => ({ name: r.name, data: rnd(10, 20 + i), lie: lieFor(r) }));
  const dkongo = makeZip(forSet("dkongo"));
  const r = L.recognise(games, [await zipInfo("dkongo.zip", dkongo)]);
  const g = r.games.find((x) => x.id === "dkong");
  check(g.state === "complete" && g.from.join() === "dkongo.zip", "a variant's own zip completes the game");
  const noProgram = makeZip(forSet("dkongo").filter((m) => m.name !== "tkg3c.5k"));
  const r2 = L.recognise(games, [await zipInfo("dkongo.zip", noProgram)]);
  const g2 = r2.games.find((x) => x.id === "dkong");
  check(g2.state === "incomplete" && g2.missing.join() === "tkg3c.5k", "missing: the closest variant's: " + g2.missing.join());
}
if (args.roms && fs.existsSync(args.roms)) {
  const picked = [];
  for (const f of fs.readdirSync(args.roms).filter((n) => n.endsWith(".zip")))
    picked.push(await zipInfo(f, fs.readFileSync(path.join(args.roms, f))));
  const r = L.recognise(games, picked);
  console.log("real sets: " + r.games.map((g) => `${g.id} ${g.state}`).join(", ") +
              "; used: " + r.zips.filter((z) => z.use).map((z) => z.name).join(" "));
  for (const g of r.games)      // every game whose own zip (or a variant's) is there
    if (picked.some((z) => z.name === `${g.id}.zip` ||
                           (games.find((x) => x.id === g.id).variants || []).some((v) => z.name === `${v}.zip`)))
      check(g.state === "complete", `real set ${g.id} complete`);
}

// ---- 3. sending, against the fake board ---------------------------------------
function board(...flags) {
  const proc = spawn(args.board, flags, { stdio: ["pipe", "pipe", "inherit"] });
  const q = new L.LineQueue();
  proc.stdout.on("data", (d) => q.push(new Uint8Array(d)));
  proc.stdout.on("end", () => q.close());
  const write = (b) => new Promise((res, rej) => proc.stdin.write(b, (e) => (e ? rej(e) : res())));
  const link = {
    sendLine: (t) => write(t + "\n"),
    sendBytes: (b) => write(Buffer.from(b)),
    readLine: (ms) => q.readLine(ms),
    get closed() { return q.closed; },
  };
  return { proc, link, end: () => { proc.stdin.end(); proc.kill(); } };
}
const file = (name, buf) => ({ name, blob: new Blob([buf]) });
const realZip = (n) => args.roms && fs.existsSync(path.join(args.roms, n)) ? fs.readFileSync(path.join(args.roms, n)) : null;

{
  // The board resets when the port opens: the first HELLOs are lost.
  const b = board("--boot-ms", "2500");
  const logs = [], states = [];
  const pac = realZip("pacman.zip") || fakePac;
  const inv = realZip("invaders.zip");
  const files = [file("pacman.zip", pac), file("half.zip", halfPac), ...(inv ? [file("invaders.zip", inv)] : [])];
  let boardInfo = null;
  const t0 = Date.now();
  const r = await L.transfer(b.link, files, {
    onLog: (l) => logs.push(l), onBoard: (x) => { boardInfo = x; },
    onFile: (i, s) => states.push([i, s.state, s.sent, s.size]),
  });
  console.log(`sent ${files.length} files (${files.reduce((s, f) => s + f.blob.size, 0)} bytes) in ${Date.now() - t0} ms`);
  check(boardInfo && boardInfo.version === "v0.2.0-fake" && boardInfo.free === 0x1B0000 - 4096, "HELLO after a reset: version, free bytes");
  check(r.results.every((x) => x.state === "ok"), "every file stored: " + JSON.stringify(r.results));
  check(r.games.includes("pacman") && (!inv || r.games.includes("invaders")), "GAMES names the complete sets: " + r.games.join(" "));
  check(logs.some((l) => l.includes("heap")) && logs.every((l) => !l.startsWith("CYDA ")), "log lines kept apart");
  const last = states.filter((s) => s[0] === 0).pop();
  check(last[1] === "ok" && last[2] === pac.length, "progress ends at the file's size");

  // Again, without Replace: the same files are skipped (LIST).
  const r2 = await L.transfer(b.link, [file("pacman.zip", pac)], {});
  check(r2.results[0].state === "skipped" && r2.games.includes("pacman"), "a file the board has is skipped");

  // A byte spoiled on the way: that file fails (crc), the next one still goes.
  let putting = "";
  const spoil = { ...b.link, get closed() { return b.link.closed; },
    sendLine: (t) => { putting = t.startsWith("CYDA PUT ") ? t.split(" ")[2] : putting; return b.link.sendLine(t); },
    sendBytes: (d) => {
      if (putting === "bad.zip") { d = Uint8Array.from(d); d[0] ^= 0x55; putting = ""; }
      return b.link.sendBytes(d);
    } };
  const r3 = await L.transfer(spoil, [file("bad.zip", rnd(5000, 7)), file("good.zip", rnd(3000, 8))], {});
  check(r3.results[0].state === "error" && r3.results[0].error === "crc", "a spoiled file is reported: " + JSON.stringify(r3.results[0]));
  check(r3.results[1].state === "ok", "and the next file is stored");

  // Replace: CLEAR first; only half a Pac-Man is left, so no games.
  let free = 0;
  const r4 = await L.transfer(b.link, [file("half.zip", halfPac)], { replace: true, onBoard: (x) => { free = x.free; } });
  check(r4.results[0].state === "ok" && r4.games.length === 0, "Replace clears the board: " + r4.games.join(" "));
  check(free === 0x1B0000 - 4096, "free space after CLEAR");

  // Download Log: the log's lines as they are, the empty one kept, the
  // board's other output left out.
  const lg = await L.readLog(b.link, {});
  check(lg.version === "v0.2.0-fake" &&
        lg.text === "CYD Classic Arcade log\n\n[1 0.5s] Start v0.2.0-fake on a fake board\n", "the log comes back: " + JSON.stringify(lg.text));
  b.end();
}
{
  // Download Log from the board just reset by the port opening
  const b = board("--boot-ms", "1500");
  const lg = await L.readLog(b.link, {});
  check(lg.text.startsWith("CYD Classic Arcade log\n"), "the log after a reset");
  b.end();
}
{
  // Not enough room: nothing is sent.
  const b = board("--flash-kb", "8");
  let err = null;
  try { await L.transfer(b.link, [file("pacman.zip", fakePac)], {}); } catch (e) { err = e; }
  check(err && err.code === "no-room" && err.free === 4096 && err.need >= fakePac.length, "no room is reported before sending");
  b.end();
}
{
  // Not our firmware: no answer.
  const b = board("--mute");
  let err = null;
  try { await L.transfer(b.link, [file("pacman.zip", fakePac)], { helloTimeoutMs: 2000 }); } catch (e) { err = e; }
  check(err && err.code === "no-answer" && /USB Transfer/.test(err.message), "no answer is reported");
  b.end();
}

console.log(`loader tests: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
