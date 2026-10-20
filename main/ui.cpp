// The menus' look (see ui.h).
#include "ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui {
namespace {

int font_h(Gfx& g, const lgfx::IFont* f)
{
    g.setFont(f);
    return g.fontHeight();
}

// ---- icons ----

void icon_back(Gfx& g, int cx, int cy, int s, uint16_t c)
{
    const float r = s > 10 ? 2.0f : 1.5f;
    g.drawWideLine(cx + s / 2, cy - s, cx - s / 2, cy, r, c);
    g.drawWideLine(cx - s / 2, cy, cx + s / 2, cy + s, r, c);
}

void icon_chevron(Gfx& g, int cx, int cy, int s, uint16_t c)
{
    const float r = s > 6 ? 1.5f : 1.0f;
    g.drawWideLine(cx - s / 2, cy - s, cx + s / 2, cy, r, c);
    g.drawWideLine(cx + s / 2, cy, cx - s / 2, cy + s, r, c);
}

void icon_gear(Gfx& g, int cx, int cy, int r, uint16_t c, uint16_t bg)
{
    for (int k = 0; k < 8; k++) {
        const float a = float(k) * 3.14159265f / 4.0f;
        g.fillCircle(cx + int(std::lround(std::cos(a) * r * 0.82f)), cy + int(std::lround(std::sin(a) * r * 0.82f)),
                     r * 3 / 10, c);
    }
    g.fillCircle(cx, cy, r * 7 / 10, c);
    g.fillCircle(cx, cy, r * 3 / 10, bg);
}

// A game controller, filled (connected) or an outline
void icon_pad(Gfx& g, int x, int y, int w, int h, uint16_t c, bool filled, uint16_t bg)
{
    const uint16_t ink = filled ? bg : c;
    if (filled) g.fillRoundRect(x, y, w, h, h / 2, c);
    else g.drawRoundRect(x, y, w, h, h / 2, c);
    const int cy = y + h / 2, dx = x + h / 2 + 1, a = h / 4 > 1 ? h / 4 : 1;
    g.drawFastHLine(dx - a, cy, 2 * a + 1, ink);
    g.drawFastVLine(dx, cy - a, 2 * a + 1, ink);
    g.fillCircle(x + w - h / 2 - 1, cy - a / 2, 1, ink);
    g.fillCircle(x + w - h / 2 + a - 1, cy + a / 2, 1, ink);
}

// A board turned on its side: the game is landscape
void icon_sideways(Gfx& g, int cx, int cy, int s, uint16_t c)
{
    const int w = s * 3 / 2, h = s;
    g.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 2, c);
    g.fillRect(cx + w / 2 - 3, cy - h / 4, 2, h / 2, c);   // the USB end
}

void key(Gfx& g, const Rect& r, uint16_t fill, uint16_t edge)
{
    const int rad = r.h / 4;
    g.fillRoundRect(r.x, r.y, r.w, r.h, rad, fill);
    if (edge != fill) g.drawRoundRect(r.x, r.y, r.w, r.h, rad, edge);
}

// Words that fit `max_w` from `text`, written to `out`; returns the
// characters used (spaces after them skipped by the caller)
int line_fits(Gfx& g, const char* text, int max_w, char* out, int cap)
{
    int fit = 0, i = 0;
    for (;;) {
        int end = i;
        while (text[end] && text[end] != ' ') ++end;
        if (end >= cap) break;
        std::memcpy(out, text, size_t(end));
        out[end] = 0;
        if (g.textWidth(out) > max_w) break;
        fit = end;
        if (!text[end]) break;
        i = end;
        while (text[i] == ' ') ++i;
    }
    if (fit == 0) {     // one word too long: cut it
        fit = 1;
        while (text[fit] && fit < cap - 1) {
            std::memcpy(out, text, size_t(fit + 1));
            out[fit + 1] = 0;
            if (g.textWidth(out) > max_w) break;
            ++fit;
        }
    }
    std::memcpy(out, text, size_t(fit));
    out[fit] = 0;
    return fit;
}

// Wrapped text from (x, y), at most max_w wide; returns the height used.
// draw = false only measures. Stops at y_end.
int wrap(Gfx& g, const char* text, int x, int y, int max_w, uint16_t color, bool draw, int y_end = 1 << 30,
         bool center = false)
{
    const int lh = g.fontHeight() + 3;
    char buf[160];
    int used = 0;
    const char* p = text;
    g.setTextDatum(center ? textdatum_t::top_center : textdatum_t::top_left);
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (y + used + lh > y_end) break;
        const int n = line_fits(g, p, max_w, buf, sizeof buf);
        if (draw) {
            g.setTextColor(color);
            g.drawString(buf, center ? x + max_w / 2 : x, y + used);
        }
        used += lh;
        p += n;
    }
    return used;
}

Rect row_rect(const Metrics& m, int slot)
{
    Rect r;
    r.x = m.margin;
    r.w = m.w - 2 * m.margin;
    r.y = m.list_y + slot * m.row_h + m.gap / 2;
    r.h = m.row_h - m.gap;
    return r;
}

Rect back_rect(const Metrics& m) { return {0, 0, m.header_h + m.header_h / 3, m.header_h}; }
Rect gear_rect(const Metrics& m) { return {m.w - m.header_h - m.header_h / 3, 0, m.header_h + m.header_h / 3, m.header_h}; }

Rect pager_key(const Metrics& m, bool next)
{
    const int y = m.list_y + m.list_h;
    const int w = m.w / 3;
    Rect r;
    r.y = y + m.gap;
    r.h = m.pager_h - 2 * m.gap;
    r.w = w;
    r.x = next ? m.w - m.margin - w : m.margin;
    return r;
}

// The right-hand widget's area of a row, and the stepper's keys
struct WidgetBox {
    int w = 0;
    Rect minus, plus;
};

WidgetBox widget_box(const Metrics& m, const Rect& r, Widget w)
{
    WidgetBox b;
    const int pad = m.big ? 12 : 8;
    switch (w) {
    case Widget::Chevron:
        b.w = m.big ? 28 : 22;
        break;
    case Widget::Switch:
        b.w = (m.big ? 56 : 44) + pad;
        break;
    case Widget::Sideways:
        b.w = m.big ? 34 : 26;
        break;
    case Widget::Value:
        b.w = (m.big ? 70 : 54) + pad;
        break;
    case Widget::Stepper: {
        const int k = r.h - (m.big ? 16 : 12);
        const int vw = m.big ? 62 : 48;
        b.w = 2 * k + vw + pad;
        b.plus = {r.x + r.w - pad - k, r.y + (r.h - k) / 2, k, k};
        b.minus = {b.plus.x - vw - k, b.plus.y, k, k};
        break;
    }
    case Widget::None:
        break;
    }
    return b;
}

void draw_widget(Gfx& g, const Metrics& m, const Rect& r, const Item& it, const WidgetBox& b, uint16_t row_bg)
{
    const int pad = m.big ? 12 : 8;
    const int cy = r.y + r.h / 2;
    switch (it.widget) {
    case Widget::Chevron:
        icon_chevron(g, r.x + r.w - pad - (m.big ? 6 : 4), cy, m.big ? 7 : 5, it.muted ? kMuted : kSub);
        break;
    case Widget::Sideways:
        icon_sideways(g, r.x + r.w - pad - (m.big ? 10 : 8), cy, m.big ? 14 : 10, it.muted ? kMuted : kSub);
        break;
    case Widget::Switch: {
        const int sw = m.big ? 56 : 44, sh = m.big ? 30 : 24;
        const int x = r.x + r.w - pad - sw, y = cy - sh / 2;
        g.fillRoundRect(x, y, sw, sh, sh / 2, it.on ? kTeal : kOff);
        const int kr = sh / 2 - 3;
        g.fillCircle(it.on ? x + sw - sh / 2 : x + sh / 2, cy, kr, kText);
        break;
    }
    case Widget::Value:
        g.setFont(m.body);
        g.setTextDatum(textdatum_t::middle_right);
        g.setTextColor(it.muted ? kMuted : kGold);
        g.drawString(it.value, r.x + r.w - pad, cy);
        break;
    case Widget::Stepper: {
        for (int k = 0; k < 2; k++) {
            const Rect& kr = k ? b.plus : b.minus;
            key(g, kr, kCardHi, kCardHi);
            const int s = kr.w / 4;
            const int kx = kr.x + kr.w / 2, ky = kr.y + kr.h / 2;
            g.fillRect(kx - s, ky - 1, 2 * s + 1, 3, kText);
            if (k) g.fillRect(kx - 1, ky - s, 3, 2 * s + 1, kText);
        }
        g.setFont(m.body);
        g.setTextDatum(textdatum_t::middle_center);
        g.setTextColor(kGold);
        g.drawString(it.value, (b.minus.x + b.minus.w + b.plus.x) / 2, cy);
        break;
    }
    case Widget::None:
        break;
    }
    (void)row_bg;
}

void header(Gfx& g, const Metrics& m, const char* title, bool back, bool gear)
{
    g.fillRect(0, 0, m.w, m.header_h, kBar);
    g.drawFastHLine(0, m.header_h - 1, m.w, kCardHi);
    int x = m.margin + 4;
    if (back) {
        const Rect b = back_rect(m);
        icon_back(g, b.x + b.w / 2 - 2, m.header_h / 2, m.big ? 9 : 7, kText);
        x = b.x + b.w;
    }
    int right = m.w - m.margin;
    if (gear) {
        const Rect r = gear_rect(m);
        icon_gear(g, r.x + r.w / 2, m.header_h / 2, m.big ? 13 : 10, kSub, kBar);
        right = r.x;
    }
    g.setFont(m.title);
    char buf[48];
    fit(g, title, right - x - 4, buf, sizeof buf);
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(kGold);
    g.drawString(buf, x, m.header_h / 2);
}

void pager(Gfx& g, const Metrics& m, int page, int pages)
{
    const int y = m.list_y + m.list_h;
    g.fillRect(0, y, m.w, m.pager_h, kBg);
    if (pages <= 1) return;
    for (int k = 0; k < 2; k++) {
        const Rect r = pager_key(m, k == 1);
        const bool on = k ? page < pages - 1 : page > 0;
        key(g, r, on ? kCard : kBg, on ? kCardHi : kOff);
        const int s = r.h / 4, cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        const uint16_t c = on ? kText : kOff;
        if (k) g.fillTriangle(cx - s / 2, cy - s, cx - s / 2, cy + s, cx + s, cy, c);
        else g.fillTriangle(cx + s / 2, cy - s, cx + s / 2, cy + s, cx - s, cy, c);
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d / %d", page + 1 > 99 ? 99 : page + 1, pages > 99 ? 99 : pages);
    g.setFont(m.sub);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(kSub);
    g.drawString(buf, m.w / 2, y + m.pager_h / 2);
}

}  // namespace

Metrics metrics(Gfx& g)
{
    Metrics m{};
    m.w = g.width();
    m.h = g.height();
    m.big = m.w >= 320;
    if (m.big) {
        m.margin = 10;
        m.header_h = 44;
        m.pager_h = 46;
        m.news_h = 18;
        m.pads_h = 22;
        m.gap = 6;
        m.title = &fonts::FreeSansBold12pt7b;
        m.body = &fonts::FreeSansBold12pt7b;
        m.sub = &fonts::DejaVu12;
        m.tiny = &fonts::DejaVu12;
        m.text = &fonts::DejaVu18;
    } else {
        m.margin = 6;
        m.header_h = 34;
        m.pager_h = 36;
        m.news_h = 13;
        m.pads_h = 16;
        m.gap = 4;
        m.title = &fonts::FreeSansBold9pt7b;
        m.body = &fonts::FreeSansBold9pt7b;
        m.sub = &fonts::DejaVu9;
        m.tiny = &fonts::DejaVu9;
        m.text = &fonts::DejaVu12;
    }
    m.list_y = m.header_h + m.gap / 2;
    m.list_h = m.h - m.list_y - m.pager_h - m.news_h - m.pads_h;
    // Two lines of text and some air
    const int min_row = font_h(g, m.body) + font_h(g, m.sub) + (m.big ? 14 : 10) + m.gap;
    m.rows = m.list_h / min_row;
    if (m.rows < 1) m.rows = 1;
    m.row_h = m.list_h / m.rows;
    if (m.row_h > min_row + 10) m.row_h = min_row + 10;
    return m;
}

int page_count(const Metrics& m, int items) { return items <= 0 ? 1 : (items + m.rows - 1) / m.rows; }
int page_of(const Metrics& m, int index) { return index < 0 ? 0 : index / m.rows; }

void fit(Gfx& g, const char* text, int max_w, char* out, int cap)
{
    std::snprintf(out, size_t(cap), "%s", text);
    if (g.textWidth(out) <= max_w) return;
    int n = int(std::strlen(out));
    while (n > 0) {
        --n;
        while (n > 0 && out[n - 1] == ' ') --n;
        std::snprintf(out + n, size_t(cap - n), "...");
        if (g.textWidth(out) <= max_w) return;
    }
}

void draw_row(Gfx& g, const ListPage& p, int index)
{
    const Metrics m = metrics(g);
    if (index < 0 || index >= p.count || page_of(m, index) != p.page) return;
    const Item& it = p.items[index];
    const Rect r = row_rect(m, index - p.page * m.rows);
    g.fillRect(r.x, r.y - m.gap / 2, r.w, r.h + m.gap, kBg);
    const bool pressed = index == p.pressed;
    const bool focus = index == p.focus;
    const uint16_t fill = pressed ? kCardHi : kCard;
    const int rad = m.big ? 10 : 7;
    g.fillRoundRect(r.x, r.y, r.w, r.h, rad, fill);
    if (focus) {
        g.drawRoundRect(r.x, r.y, r.w, r.h, rad, kGold);
        g.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, rad - 1, kGold);
    }
    const WidgetBox b = widget_box(m, r, it.widget);
    draw_widget(g, m, r, it, b, fill);
    const int tx = r.x + (m.big ? 14 : 10);
    const int tw = r.w - (tx - r.x) - b.w - 6;
    char buf[64];
    const int th = font_h(g, m.body);
    const int sh = it.sub[0] ? font_h(g, m.sub) : 0;
    const int space = it.sub[0] ? (m.big ? 4 : 2) : 0;
    const int ty = r.y + (r.h - th - sh - space) / 2;
    g.setFont(m.body);
    fit(g, it.title, tw, buf, sizeof buf);
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(it.muted ? kMuted : focus ? kGold : kText);
    g.drawString(buf, tx, ty);
    if (it.sub[0]) {
        g.setFont(m.sub);
        fit(g, it.sub, tw, buf, sizeof buf);
        g.setTextColor(it.muted ? kMuted : kSub);
        g.drawString(buf, tx, ty + th + space);
    }
}

void draw_footer(Gfx& g, const Footer& f)
{
    const Metrics m = metrics(g);
    const int ny = m.h - m.pads_h - m.news_h;
    g.fillRect(0, ny, m.w, m.news_h, kBg);
    char buf[96];
    g.setFont(m.tiny);
    if (f.news && f.news[0]) {
        fit(g, f.news, m.w - 2 * m.margin, buf, sizeof buf);
        g.setTextDatum(textdatum_t::middle_center);
        g.setTextColor(kGold);
        g.drawString(buf, m.w / 2, ny + m.news_h / 2);
    }
    const int py = m.h - m.pads_h;
    g.fillRect(0, py, m.w, m.pads_h, kBar);
    const int ih = m.big ? 13 : 10, iw = ih * 2;
    const int cy = py + m.pads_h / 2;
    g.setTextDatum(textdatum_t::middle_left);
    const int n = f.pads[0].connected + f.pads[1].connected;
    if (n == 0) {
        icon_pad(g, m.margin, cy - ih / 2, iw, ih, kMuted, false, kBar);
        fit(g, "No controller: put one in pairing mode", m.w - 2 * m.margin - iw - 6, buf, sizeof buf);
        g.setTextColor(kSub);
        g.drawString(buf, m.margin + iw + 6, cy);
        return;
    }
    // Both: half the line each. One: its name gets the room, the other
    // slot is just an outline at the right end.
    for (int i = 0; i < 2; i++) {
        const Pad& p = f.pads[i];
        int x, room;
        if (n == 2) {
            x = i * (m.w / 2) + m.margin;
            room = m.w / 2 - m.margin - iw - 10;
        } else if (p.connected) {
            x = m.margin;
            room = m.w - 3 * m.margin - 2 * iw - 12;
        } else {
            icon_pad(g, m.w - m.margin - iw, cy - ih / 2, iw, ih, kMuted, false, kBar);
            continue;
        }
        icon_pad(g, x, cy - ih / 2, iw, ih, kTeal, true, kBar);
        char label[48];
        std::snprintf(label, sizeof label, "%d %s", i + 1, p.name);
        fit(g, label, room, buf, sizeof buf);
        g.setTextColor(kText);
        g.drawString(buf, x + iw + 5, cy);
    }
}

void draw_list(Gfx& g, const ListPage& p, const Footer& f)
{
    const Metrics m = metrics(g);
    g.fillRect(0, m.header_h, m.w, m.h - m.header_h, kBg);
    header(g, m, p.title, p.back, p.gear);
    for (int i = p.page * m.rows; i < p.count && i < (p.page + 1) * m.rows; i++) draw_row(g, p, i);
    if (p.count == 0 && p.empty[0]) {
        g.setFont(m.sub);
        wrap(g, p.empty, m.margin + 8, m.list_y + 12, m.w - 2 * m.margin - 16, kSub, true);
    }
    pager(g, m, p.page, page_count(m, p.count));
    draw_footer(g, f);
}

Target hit_list(Gfx& g, const ListPage& p, int x, int y)
{
    const Metrics m = metrics(g);
    Target t;
    if (y < m.header_h) {
        if (p.back && back_rect(m).contains(x, y)) t.hit = Hit::Back;
        else if (p.gear && gear_rect(m).contains(x, y)) t.hit = Hit::Gear;
        return t;
    }
    const int pages = page_count(m, p.count);
    if (pages > 1) {
        // the pager keys, with a few pixels to spare all round
        for (int k = 0; k < 2; k++) {
            Rect r = pager_key(m, k == 1);
            r.y -= m.gap;
            r.h += 2 * m.gap;
            if (r.contains(x, y)) {
                t.hit = k ? Hit::Next : Hit::Prev;
                return t;
            }
        }
    }
    if (y < m.list_y || y >= m.list_y + m.list_h) return t;
    const int slot = (y - m.list_y) / m.row_h;
    const int index = p.page * m.rows + slot;
    if (slot >= m.rows || index >= p.count) return t;
    t.hit = Hit::Row;
    t.index = index;
    if (p.items[index].widget == Widget::Stepper) {
        const Rect r = row_rect(m, slot);
        const WidgetBox b = widget_box(m, r, Widget::Stepper);
        // keys a little bigger than drawn
        Rect mi = b.minus, pl = b.plus;
        const int e = m.gap;
        mi.x -= e; mi.y -= e; mi.w += 2 * e; mi.h += 2 * e;
        pl.x -= e; pl.y -= e; pl.w += 2 * e; pl.h += 2 * e;
        if (mi.contains(x, y)) t.hit = Hit::Minus;
        else if (pl.contains(x, y)) t.hit = Hit::Plus;
    }
    return t;
}

namespace {

// Where a text page's lines go, and how tall a line is
struct TextBox {
    int x, w, y0, y_end;
};

TextBox text_box(const Metrics& m, bool paged)
{
    TextBox b;
    b.x = m.margin + 6;
    b.w = m.w - 2 * b.x;
    b.y0 = m.list_y + (m.big ? 10 : 6);
    b.y_end = paged ? m.list_y + m.list_h : m.h - m.pads_h - m.news_h;
    return b;
}

// One line (draw = false only measures); returns its height
int text_line(Gfx& g, const Metrics& m, const TextPage& p, const char* s, const TextBox& b, int y, bool draw)
{
    const lgfx::IFont* body = p.small ? m.sub : m.text;
    if (!s[0]) {
        g.setFont(body);
        return g.fontHeight() / 2;
    }
    if (s[0] == '#') {
        g.setFont(m.body);
        return wrap(g, s + 1, b.x, y, b.w, kGold, draw, b.y_end) + 2;
    }
    g.setFont(body);
    return wrap(g, s, b.x, y, b.w, kText, draw, b.y_end);
}

}  // namespace

int text_pages(Gfx& g, const TextPage& p, int* starts, int cap)
{
    const Metrics m = metrics(g);
    const TextBox b = text_box(m, true);
    int n = 0;
    if (cap > 0) starts[n++] = 0;
    int y = b.y0;
    for (int i = 0; i < p.count; i++) {
        TextBox big = b;
        big.y_end = 1 << 30;    // measure whole
        const int h = text_line(g, m, p, p.lines[i], big, 0, false);
        if (y + h > b.y_end && y > b.y0) {
            if (n >= cap) break;
            starts[n++] = i;
            y = b.y0;
        }
        y += h;
    }
    return n < 1 ? 1 : n;
}

void draw_text(Gfx& g, const TextPage& p, const Footer& f, const int* starts, int pages)
{
    const Metrics m = metrics(g);
    g.fillRect(0, m.header_h, m.w, m.h - m.header_h, kBg);
    header(g, m, p.title, true, false);
    const TextBox b = text_box(m, p.paged);
    const int first = p.paged && starts ? starts[p.page] : 0;
    const int last = p.paged && starts && p.page + 1 < pages ? starts[p.page + 1] : p.count;
    int y = b.y0;
    for (int i = first; i < last && y < b.y_end; i++) y += text_line(g, m, p, p.lines[i], b, y, true);
    if (p.paged) pager(g, m, p.page, pages);
    draw_footer(g, f);
}

void draw_header(Gfx& g, const char* title, bool back, bool gear) { header(g, metrics(g), title, back, gear); }

namespace {

// The QR page's lines under the code: their height, or drawn from y
int qr_lines(Gfx& g, const Metrics& m, const QrPage& p, int y, bool draw)
{
    const int x = m.margin + 6, w = m.w - 2 * x;
    int h = 0;
    g.setFont(m.sub);
    if (p.note[0]) h += wrap(g, p.note, x, y + h, w, kSub, draw, 1 << 30, true);
    g.setFont(m.tiny);
    if (p.caption[0]) h += (h ? m.gap : 0) + wrap(g, p.caption, x, y + h + (h ? m.gap : 0), w, kText, draw, 1 << 30, true);
    return h;
}

} // namespace

int qr_room(Gfx& g, const QrPage& p)
{
    const Metrics m = metrics(g);
    const int top = m.list_y + m.gap, bottom = m.h - m.pads_h - m.news_h - m.gap;
    const int h = bottom - top - qr_lines(g, m, p, 0, false) - m.gap;
    const int w = m.w - 2 * m.margin;
    return h < w ? h : w;
}

void draw_qr(Gfx& g, const QrPage& p, const Footer& f)
{
    const Metrics m = metrics(g);
    g.fillRect(0, m.header_h, m.w, m.h - m.header_h, kBg);
    header(g, m, p.title, true, false);
    int y = m.list_y + m.gap;
    if (p.n > 0 && p.dark) {
        const int side = p.n + 4;
        const int px = qr_room(g, p) / side;
        if (px >= 1) {
            const int s = side * px, x0 = (m.w - s) / 2;
            g.fillRect(x0, y, s, s, uint16_t(0xFFFF));
            for (int r = 0; r < p.n; ++r)
                for (int c = 0; c < p.n;) {
                    if (!p.dark(p.ctx, c, r)) { ++c; continue; }
                    int e = c;
                    while (e < p.n && p.dark(p.ctx, e, r)) ++e;
                    g.fillRect(x0 + (2 + c) * px, y + (2 + r) * px, (e - c) * px, px, uint16_t(0x0000));
                    c = e;
                }
            y += s + m.gap;
        }
    }
    qr_lines(g, m, p, y, true);
    draw_footer(g, f);
}

Target hit_text(Gfx& g, int x, int y, bool paged)
{
    const Metrics m = metrics(g);
    Target t;
    if (back_rect(m).contains(x, y)) t.hit = Hit::Back;
    if (paged && t.hit == Hit::None)
        for (int k = 0; k < 2; k++) {
            Rect r = pager_key(m, k == 1);
            r.y -= m.gap;
            r.h += 2 * m.gap;
            if (r.contains(x, y)) t.hit = k ? Hit::Next : Hit::Prev;
        }
    return t;
}

void message(Gfx& g, const char* title, const char* body, uint16_t title_color)
{
    const Metrics m = metrics(g);
    const int x = m.margin * 2, w = m.w - 4 * m.margin;
    const int pad = m.big ? 16 : 10;
    g.setFont(m.text);
    const int bh = wrap(g, body, 0, 0, w - 2 * pad, kText, false);
    const int th = font_h(g, m.body);
    const int h = pad + th + (body[0] ? pad / 2 + bh : 0) + pad;
    const int y = (m.h - h) / 2;
    g.fillRoundRect(x, y, w, h, m.big ? 12 : 8, kCard);
    g.drawRoundRect(x, y, w, h, m.big ? 12 : 8, title_color);
    g.setFont(m.body);
    g.setTextDatum(textdatum_t::top_center);
    g.setTextColor(title_color);
    char buf[64];
    fit(g, title, w - 2 * pad, buf, sizeof buf);
    g.drawString(buf, x + w / 2, y + pad);
    g.setFont(m.text);
    wrap(g, body, x + pad, y + pad + th + pad / 2, w - 2 * pad, kText, true, 1 << 30, true);
}

void loading(Gfx& g, const char* game, const char* note, int done, int total, bool full)
{
    const Metrics m = metrics(g);
    const int w = m.w * 3 / 4 < 280 ? m.w - 4 * m.margin : m.w * 3 / 4;
    const int x = (m.w - w) / 2;
    const int pad = m.big ? 16 : 10;
    const int th = font_h(g, m.sub), gh = font_h(g, m.body), nh = note[0] ? font_h(g, m.sub) + pad / 2 : 0;
    const int bar_h = m.big ? 12 : 8;
    const int h = pad + th + 4 + gh + pad + bar_h + nh + pad;
    const int y = (m.h - h) / 2;
    const int bar_y = y + pad + th + 4 + gh + pad;
    const int bar_x = x + pad, bar_w = w - 2 * pad;
    if (full) {
        g.fillScreen(kBg);
        g.fillRoundRect(x, y, w, h, m.big ? 12 : 8, kCard);
        g.setTextDatum(textdatum_t::top_center);
        g.setFont(m.sub);
        g.setTextColor(kSub);
        g.drawString("Loading", m.w / 2, y + pad);
        g.setFont(m.body);
        g.setTextColor(kGold);
        char buf[48];
        fit(g, game, w - 2 * pad, buf, sizeof buf);
        g.drawString(buf, m.w / 2, y + pad + th + 4);
        if (note[0]) {
            g.setFont(m.sub);
            g.setTextColor(kText);
            fit(g, note, w - 2 * pad, buf, sizeof buf);
            g.drawString(buf, m.w / 2, bar_y + bar_h + pad / 2);
        }
        g.fillRoundRect(bar_x, bar_y, bar_w, bar_h, bar_h / 2, kOff);
    }
    const int fill = total > 0 ? bar_w * (done < total ? done : total) / total : bar_w;
    if (fill >= bar_h) g.fillRoundRect(bar_x, bar_y, fill, bar_h, bar_h / 2, kGold);
}

}  // namespace ui
