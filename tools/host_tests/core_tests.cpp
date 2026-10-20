// Host tests for core/: the ROM store on a fake NOR flash, and the USB
// loading protocol driven the way the web page drives it.
#include "../../core/log_pack.h"
#include "../../core/log_ring.h"
#include "../../core/rom_store.h"
#include "../../core/usb_import.h"
#include "../../emu/rom/crc32.h"
#include "../../emu/util/inflate.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const char* what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL: %s\n", what); }
}

// NOR flash: erase sets a sector to 0xFF, a write can only clear bits.
class FakeFlash : public rom_store::Flash {
public:
    explicit FakeFlash(uint32_t size) : mem(size, 0x5A) {}     // starts dirty, like used flash
    uint32_t size() const override { return uint32_t(mem.size()); }
    bool read(uint32_t off, void* out, uint32_t len) override
    {
        if (off + uint64_t(len) > mem.size()) return false;
        std::memcpy(out, mem.data() + off, len);
        return true;
    }
    bool erase_sector(uint32_t off) override
    {
        if (off % rom_store::kSector || off + rom_store::kSector > mem.size()) return false;
        std::memset(mem.data() + off, 0xFF, rom_store::kSector);
        ++erases;
        return true;
    }
    bool write(uint32_t off, const void* data, uint32_t len) override
    {
        if (fail_writes_after >= 0 && writes++ >= fail_writes_after) return false;
        if (off + uint64_t(len) > mem.size()) return false;
        const uint8_t* d = static_cast<const uint8_t*>(data);
        for (uint32_t i = 0; i < len; ++i) mem[off + i] &= d[i];
        return true;
    }
    const uint8_t* map() override { return mem.data(); }

    std::vector<uint8_t> mem;
    int erases = 0;
    int writes = 0;
    int fail_writes_after = -1;
};

std::vector<uint8_t> pattern(uint32_t n, uint32_t seed)
{
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; ++i) { seed = seed * 1103515245u + 12345u; v[i] = uint8_t(seed >> 16); }
    return v;
}

bool store_file(rom_store::Store& s, const char* name, const std::vector<uint8_t>& d, uint32_t piece = 1000)
{
    if (!s.begin(name, uint32_t(d.size()), crc32::of(d.data(), d.size()))) return false;
    for (uint32_t at = 0; at < d.size(); at += piece) {
        const uint32_t n = uint32_t(d.size()) - at < piece ? uint32_t(d.size()) - at : piece;
        if (!s.add(d.data() + at, n)) return false;
    }
    return s.finish();
}

int find(rom_store::Store& s, const char* name)
{
    for (int i = 0; i < s.count(); ++i)
        if (s.live(i) && std::strcmp(s.entry(i).name, name) == 0) return i;
    return -1;
}

bool same(rom_store::Store& s, const char* name, const std::vector<uint8_t>& d)
{
    const int i = find(s, name);
    return i >= 0 && s.entry(i).size == d.size() && std::memcmp(s.data(i), d.data(), d.size()) == 0;
}

void test_store()
{
    FakeFlash f(64 * 1024);
    rom_store::Store s;
    s.open(f);
    check(s.count() == 0, "blank flash = empty store");
    check(s.free_bytes() == 60 * 1024, "free space after the directory");

    const auto a = pattern(5000, 1), b = pattern(7001, 2), c = pattern(3, 3);
    check(store_file(s, "pacman.zip", a), "store a file");
    check(store_file(s, "invaders.zip", b, 7001), "store in one piece");
    check(store_file(s, "x.zip", c, 1), "store byte by byte");
    check(same(s, "pacman.zip", a) && same(s, "invaders.zip", b) && same(s, "x.zip", c), "files read back");

    rom_store::Store s2;
    s2.open(f);
    check(s2.count() == 3 && same(s2, "invaders.zip", b), "the directory survives a restart");

    // Storing a name again hides the old copy.
    const auto a2 = pattern(4000, 9);
    check(store_file(s2, "pacman.zip", a2), "store again");
    check(same(s2, "pacman.zip", a2), "the new copy wins");
    int live = 0;
    for (int i = 0; i < s2.count(); ++i) live += s2.live(i);
    check(live == 3, "one live copy per name");

    // A bad CRC or a short file leaves the store as it was.
    const uint32_t free_before = s2.free_bytes();
    check(s2.begin("bad.zip", 100, 0x12345678), "begin a file");
    const auto junk = pattern(100, 4);
    check(s2.add(junk.data(), 100), "add its bytes");
    check(!s2.finish(), "a wrong CRC is refused");
    check(find(s2, "bad.zip") < 0 && s2.free_bytes() == free_before, "nothing kept");
    check(s2.begin("short.zip", 100, crc32::of(junk.data(), 100)) && s2.add(junk.data(), 50) && !s2.finish(),
          "a short file is refused");
    check(!s2.begin("bad name.zip", 10, 0) && !s2.begin("", 10, 0), "bad names are refused");
    check(!s2.begin("huge.zip", 1 << 20, 0), "too big is refused");

    // A file cut off half way (power loss) mustn't spoil the next one.
    const auto d = pattern(3000, 5);
    check(s2.begin("cut.zip", 3000, crc32::of(d.data(), 3000)) && s2.add(d.data(), 1500), "half a file");
    rom_store::Store s3;
    s3.open(f);
    const auto e = pattern(6000, 6);
    check(store_file(s3, "after.zip", e), "store after a cut-off file");
    check(same(s3, "after.zip", e) && find(s3, "cut.zip") < 0, "the cut-off file is gone, the next is whole");

    // Clear, then the old bytes everywhere must be erased again as needed.
    check(s3.clear(), "clear");
    check(s3.count() == 0, "clear empties the store");
    const auto big = pattern(40000, 7);
    check(store_file(s3, "big.zip", big, 2048), "store over the old data");
    check(same(s3, "big.zip", big), "it reads back after clear");
    rom_store::Store s4;
    s4.open(f);
    check(s4.count() == 1 && same(s4, "big.zip", big), "and after a restart");

    // A failing write ends the file without changing the directory.
    f.fail_writes_after = f.writes;
    const auto g = pattern(500, 8);
    check(!store_file(s4, "fail.zip", g), "a failing write is reported");
    f.fail_writes_after = -1;
    rom_store::Store s5;
    s5.open(f);
    check(s5.count() == 1 && same(s5, "big.zip", big), "the store is unchanged");

    // A damaged directory = an empty store.
    f.mem[0] = 'X';
    rom_store::Store s6;
    s6.open(f);
    check(s6.count() == 0, "a damaged directory reads as empty");
}

// ---- protocol ---------------------------------------------------------------

struct Board {
    FakeFlash flash{256 * 1024};
    rom_store::Store store;
    usb_import::Session session;
    std::vector<std::string> out;
    uint32_t now = 0;

    Board()
    {
        store.open(flash);
        session.init(store, &Board::send, &Board::games, this, "v9.9.9");
    }
    static void send(void* ctx, const char* line) { static_cast<Board*>(ctx)->out.push_back(line); }
    static void games(void* ctx, char* buf, size_t cap)
    {
        Board* b = static_cast<Board*>(ctx);
        std::snprintf(buf, cap, "%d", b->store.count());
    }
    void say(const std::string& s) { feed(s + "\n"); }
    void feed(const std::string& s) { session.feed(reinterpret_cast<const uint8_t*>(s.data()), s.size(), now); }
    void bytes(const uint8_t* d, size_t n) { session.feed(d, n, now); }
    std::string last() const { return out.empty() ? "" : out.back(); }
};

// The page's way of sending a file: PUT, wait for READY, chunks with ACKs.
bool page_put(Board& b, const char* name, const std::vector<uint8_t>& d, uint32_t crc, size_t feed_piece = 300)
{
    char line[128];
    std::snprintf(line, sizeof line, "CYDA PUT %s %u %08x", name, unsigned(d.size()), unsigned(crc));
    b.say(line);
    if (b.last().rfind("CYDA READY ", 0) != 0) return false;
    const size_t chunk = std::strtoul(b.last().c_str() + 11, nullptr, 10);
    for (size_t at = 0; at < d.size(); at += chunk) {
        const size_t n = d.size() - at < chunk ? d.size() - at : chunk;
        for (size_t k = 0; k < n; k += feed_piece)          // serial arrives in bits
            b.bytes(d.data() + at + k, n - k < feed_piece ? n - k : feed_piece);
        char want[32];
        std::snprintf(want, sizeof want, "CYDA ACK %u", unsigned(at + n));
        bool acked = false;
        for (const std::string& o : b.out) acked |= o == want;
        if (!acked) return false;
    }
    return true;
}

void test_protocol()
{
    Board b;
    b.say("log");                                   // not ours: ignored
    check(b.out.empty(), "other lines are ignored");
    b.say("CYDA HELLO");
    check(b.last().rfind("CYDA HELLO v9.9.9 ", 0) == 0 && b.last().size() > 20, "HELLO answers");

    const auto z = pattern(5000, 11);
    const uint32_t crc = crc32::of(z.data(), z.size());
    check(page_put(b, "pacman.zip", z, crc), "PUT with chunks and ACKs");
    check(b.last() == "CYDA OK pacman.zip", "PUT ends OK");
    check(same(b.store, "pacman.zip", z), "the file is in the store");

    b.out.clear();
    check(page_put(b, "bad.zip", z, crc ^ 1, 4096) && b.last() == "CYDA ERR crc", "a wrong CRC is reported");
    check(find(b.store, "bad.zip") < 0, "and not kept");

    b.say("CYDA PUT nope 10");
    check(b.last() == "CYDA ERR bad request", "a PUT without a CRC is refused");
    b.say("CYDA PUT big.zip 99999999 0");
    check(b.last().rfind("CYDA ERR no room", 0) == 0, "too big is refused");

    // Bytes that stop coming: dropped after the timeout, lines work again.
    b.say("CYDA PUT slow.zip 100 0");
    check(b.last().rfind("CYDA READY", 0) == 0, "slow PUT starts");
    b.bytes(z.data(), 10);
    b.now += usb_import::kTimeoutMs - 1;
    b.session.tick(b.now);
    check(b.session.receiving(), "still waiting just before the timeout");
    b.now += 2;
    b.session.tick(b.now);
    check(!b.session.receiving() && b.last() == "CYDA ERR timeout", "timeout ends the PUT");
    b.say("CYDA LIST");
    const size_t n = b.out.size();
    check(n >= 2 && b.out[n - 1] == "CYDA END" && b.out[n - 2].rfind("CYDA FILE pacman.zip 5000 ", 0) == 0,
          "LIST shows the stored file");

    b.say("CYDA DONE");
    check(b.last() == "CYDA GAMES 1" && b.session.take_done() && !b.session.take_done(), "DONE reports once");

    b.say("CYDA CLEAR");
    check(b.last() == "CYDA OK" && b.store.count() == 0, "CLEAR empties the store");

    b.say("CYDA HELLO\r");                          // Windows line ends
    check(b.last().rfind("CYDA HELLO", 0) == 0, "CR LF works");
    b.say(std::string(500, 'x'));
    b.say("CYDA HELLO");
    check(b.last().rfind("CYDA HELLO", 0) == 0, "an overlong line is skipped");
    b.say("CYDA FOO");
    check(b.last() == "CYDA ERR unknown", "unknown commands are answered");

    // LOG: every line of the board's log, a newline in one sent as a space,
    // then LOGEND with the count; with no log, just LOGEND 0.
    b.out.clear();
    b.say("CYDA LOG");
    check(b.out.size() == 1 && b.last() == "CYDA LOGEND 0", "LOG with no log");
    b.session.set_log([](void*, int i, char* out, size_t cap) {
        static const char* const kLines[] = {"first", "", "two\nparts"};
        if (i >= 3) return false;
        std::snprintf(out, cap, "%s", kLines[i]);
        return true;
    });
    b.out.clear();
    b.say("CYDA LOG");
    check(b.out.size() == 4 && b.out[0] == "CYDA LOG first" && b.out[1] == "CYDA LOG " &&
          b.out[2] == "CYDA LOG two parts" && b.out[3] == "CYDA LOGEND 3", "LOG sends the log");
}

} // namespace

void test_log_ring()
{
    static log_ring::Ring<64> r;
    r.reset(0);
    check(r.sane() && r.count() == 0, "log: empty");
    char out[80];
    check(!r.line(0, out, sizeof out), "log: nothing to read");
    r.put("one");
    r.put("two");
    r.put("three");
    check(r.count() == 3, "log: three lines");
    check(r.line(0, out, sizeof out) && std::strcmp(out, "three") == 0, "log: newest first");
    check(r.line(2, out, sizeof out) && std::strcmp(out, "one") == 0, "log: oldest last");
    check(!r.line(3, out, sizeof out), "log: past the end");
    // Fill past the size: old lines go whole, the newest stay readable
    char text[32];
    for (int i = 0; i < 40; ++i) {
        std::snprintf(text, sizeof text, "line %d", i);
        r.put(text);
        bool ok = r.line(0, out, sizeof out) && std::strcmp(out, text) == 0;
        int n = r.count();
        for (int k = 0; k < n; ++k) {
            char want[32];
            std::snprintf(want, sizeof want, "line %d", i - k);
            if (i - k >= 0) ok = ok && r.line(k, out, sizeof out) && std::strcmp(out, want) == 0;
        }
        if (!ok) {
            check(false, "log: wraps keeping whole lines");
            return;
        }
    }
    check(r.used <= 64 && r.count() >= 6, "log: wraps keeping whole lines");
    // Cut to the buffer given
    check(r.line(0, out, 4) && std::strcmp(out, "lin") == 0, "log: cut to the caller's buffer");
    // A line longer than the ring is cut, never overruns
    char big[100];
    std::memset(big, 'x', sizeof big - 1);
    big[99] = 0;
    r.put(big);
    check(r.count() == 1 && r.line(0, out, sizeof out) && std::strlen(out) == 63, "log: a huge line is cut");
}

// Send Log's packing: DEFLATE that our own inflate reads back, base43 in the
// QR alphanumeric set without space or '%', and the whole packet.
void test_log_pack()
{
    std::vector<uint16_t> tables(logpack::kTablesBytes / 2);
    std::string text;
    for (int k = 0; k < 60; ++k) {
        char line[96];
        std::snprintf(line, sizeof line, "[3 %d.%ds] galaga: start; area 92 KB (60 + 32), %d KB free\n", k, k % 10,
                      30 + k % 7);
        text += line;
    }
    std::vector<uint8_t> z(text.size() + 64), back(text.size() + 16);
    const size_t zn = logpack::deflate(reinterpret_cast<const uint8_t*>(text.data()), text.size(), z.data(),
                                       z.size(), tables.data());
    check(zn > 0 && zn * 4 < text.size(), "log pack: a log shrinks to under a quarter");
    const long bn = inflate::raw(z.data(), zn, back.data(), back.size());
    check(bn == long(text.size()) && std::memcmp(back.data(), text.data(), text.size()) == 0,
          "log pack: inflates back to the log");
    for (const char* t : {"", "a", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "abcabcabcabcx"}) {
        uint8_t o[256], b[256];
        const size_t on = logpack::deflate(reinterpret_cast<const uint8_t*>(t), std::strlen(t), o, sizeof o,
                                           tables.data());
        const long n = on ? inflate::raw(o, on, b, sizeof b) : -1;
        check(n == long(std::strlen(t)) && std::memcmp(b, t, size_t(n)) == 0, "log pack: short texts");
    }
    check(logpack::deflate(reinterpret_cast<const uint8_t*>(text.data()), text.size(), z.data(), 8,
                           tables.data()) == 0, "log pack: too small an output = 0");
    // Base43: 2 bytes -> 3 characters
    const uint8_t raw[5] = {0xFF, 0xFF, 0x00, 0x01, 0x80};
    char enc[16];
    check(logpack::base43(raw, 5, enc, sizeof enc) == 8, "log pack: base43 length");
    bool ok = true;
    for (const char* c = enc; *c; ++c) ok = ok && std::strchr(logpack::kAlphabet, *c) && *c != ' ' && *c != '%';
    check(ok, "log pack: base43 characters");
    auto val = [](char c) { return int(std::strchr(logpack::kAlphabet, c) - logpack::kAlphabet); };
    uint8_t dec[5];
    int dn = 0;
    for (int k = 0; k + 2 < 8; k += 3) {
        const int v = val(enc[k]) + 43 * val(enc[k + 1]) + 1849 * val(enc[k + 2]);
        dec[dn++] = uint8_t(v >> 8);
        dec[dn++] = uint8_t(v);
    }
    dec[dn++] = uint8_t(val(enc[6]) + 43 * val(enc[7]));
    check(dn == 5 && std::memcmp(dec, raw, 5) == 0, "log pack: base43 decodes");
    check(logpack::base43(raw, 5, enc, 8) == 0, "log pack: base43 too small = 0");
    // The packet: format byte first
    std::vector<uint8_t> work(text.size() + 64);
    std::vector<char> out(text.size() * 2);
    const size_t pn = logpack::pack(text.c_str(), text.size(), work.data(), work.size(), out.data(), out.size(),
                                    tables.data());
    check(pn > 0 && val(out[0]) + 43 * val(out[1]) + 1849 * val(out[2]) == logpack::kFormat * 256 + work[1],
          "log pack: packet starts with the format byte");
}

int main()
{
    test_log_ring();
    test_log_pack();
    test_store();
    test_protocol();
    std::printf("core tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
