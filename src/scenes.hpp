// SPDX-License-Identifier: GPL-3.0-or-later
// Animated backgrounds for the Matrix and Omarchy layouts. State only moves in
// step(), called by the frame loop at 8 fps; draw() never changes it, so
// previews and status renders do not advance the animation.
#pragma once
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRandomGenerator>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace scenes {

// Plain ASCII so every monospace font has the glyphs (no tofu boxes).
static const QString glyphs = QStringLiteral("0123456789ABCDEFXZ$+-*/=<>:.|#%&@");

inline QChar glyph(quint32 seed) { return glyphs[int(seed % quint32(glyphs.size()))]; }
inline double ease(double x) { x = std::clamp(x, 0.0, 1.0); return 1 - std::pow(1 - x, 3); }
inline QColor mix(const QColor &a, const QColor &b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

// Block elements (U+2580–U+259F) drawn as rectangles, like terminals do, so
// art made of them is solid whatever the font. False for any other character.
inline bool drawBlock(QPainter &painter, const QRectF &cell, QChar ch, const QColor &color) {
    ushort code = ch.unicode();
    if (code < 0x2580 || code > 0x259F) return false;
    double x = cell.x(), y = cell.y(), w = cell.width() + 0.6, h = cell.height() + 0.6;
    auto fill = [&](double fx, double fy, double fw, double fh, double alpha = 1) {
        QColor c = color; c.setAlphaF(c.alphaF() * alpha);
        painter.fillRect(QRectF(x + fx * w, y + fy * h, fw * w, fh * h), c);
    };
    if (code == 0x2580) fill(0, 0, 1, .5);
    else if (code <= 0x2588) fill(0, 1 - (code - 0x2580) / 8.0, 1, (code - 0x2580) / 8.0);      // ▁..█
    else if (code <= 0x258F) fill(0, 0, (0x2590 - code) / 8.0, 1);                               // ▉..▏
    else if (code == 0x2590) fill(.5, 0, .5, 1);
    else if (code <= 0x2593) fill(0, 0, 1, 1, (code - 0x2590) / 4.0);                            // ░▒▓
    else if (code == 0x2594) fill(0, 0, 1, 1 / 8.0);
    else if (code == 0x2595) fill(7 / 8.0, 0, 1 / 8.0, 1);
    else {
        // Quadrants ▖..▟ as bits: upper-left 1, upper-right 2, lower-left 4, lower-right 8.
        static const int quadrants[] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};
        int bits = quadrants[code - 0x2596];
        if (bits & 1) fill(0, 0, .5, .5);
        if (bits & 2) fill(.5, 0, .5, .5);
        if (bits & 4) fill(0, .5, .5, .5);
        if (bits & 8) fill(.5, .5, .5, .5);
    }
    return true;
}

// Falling glyph columns across the whole 1280×480 screen.
class MatrixRain {
public:
    static constexpr int columns = 64, rows = 20, trail = 14;
    static constexpr double cellWidth = 20, cellHeight = 24;
    MatrixRain() {
        for (int i = 0; i < columns; ++i) {
            for (int r = 0; r < rows; ++r) cells[i][r] = glyph(random());
            reset(i, true);
        }
        // Start mid-fall, so the first frame and previews are already full.
        for (int i = 0; i < 40; ++i) step();
    }
    void step() {
        for (int i = 0; i < columns; ++i) {
            heads[i] += speeds[i];
            if (heads[i] - trail > rows) reset(i, false);
            cells[i][random() % rows] = glyph(random());
        }
    }
    void draw(QPainter &painter, const QString &family, const QColor &head, const QColor &tail) const {
        QFont font(family); font.setPixelSize(20); font.setBold(true); painter.setFont(font);
        for (int i = 0; i < columns; ++i)
            for (int k = 0; k < trail; ++k) {
                int row = int(heads[i]) - k;
                if (row < 0 || row >= rows) continue;
                QColor color = k == 0 ? head : tail;
                if (k) color.setAlphaF(0.9 * (1 - double(k) / trail));
                painter.setPen(color);
                painter.drawText(QRectF(i * cellWidth, row * cellHeight, cellWidth, cellHeight), Qt::AlignCenter, QString(cells[i][row]));
            }
    }
private:
    double heads[columns]{}, speeds[columns]{};
    QChar cells[columns][rows];
    static quint32 random() { return QRandomGenerator::global()->generate(); }
    void reset(int i, bool anywhere) {
        heads[i] = anywhere ? -double(random() % (rows * 2)) : -double(random() % rows);
        speeds[i] = 0.35 + (random() % 100) / 100.0 * 0.65;
    }
};

// The Omarchy screensaver idea: the branding art assembles with a different
// effect each cycle (decrypt, rain, wipe, scatter, beams), then holds.
class LogoScene {
public:
    enum Effect { Decrypt, Rain, Wipe, Scatter, Beams, EffectCount };
    static constexpr double revealSeconds = 4, holdSeconds = 4;
    QStringList lines{QStringLiteral("omarchy")};
    // Plain text from the user's own branding file: at most 16 KiB, 40 lines
    // of 160 columns; control characters are dropped. Missing or empty keeps
    // the previous art.
    void load(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        QStringList next;
        for (QString line : QString::fromUtf8(file.read(16384)).split('\n')) {
            line.removeIf([](QChar c) { return c.category() == QChar::Other_Control || c.category() == QChar::Other_Format; });
            while (line.endsWith(' ')) line.chop(1);
            next << line.left(160);
        }
        while (!next.isEmpty() && next.first().isEmpty()) next.removeFirst();
        while (!next.isEmpty() && next.last().isEmpty()) next.removeLast();
        if (!next.isEmpty()) { lines = next.mid(0, 40); reroll(); }
    }
    void setSource(const QString &path) { source = path; load(path); }
    // Live text (metrics) instead of a file: keeps the current effect running.
    void setText(const QStringList &text) { if (!text.isEmpty()) lines = text; }
    void step(double seconds) {
        elapsed += seconds;
        if (elapsed < revealSeconds + holdSeconds) return;
        elapsed = 0;
        effect = Effect((effect + 1 + QRandomGenerator::global()->bounded(EffectCount - 1)) % EffectCount);
        if (!source.isEmpty()) load(source);
        reroll();
    }
    // settled: the finished logo (theme previews).
    void draw(QPainter &painter, const QRectF &area, const QString &family, const QColor &foreground,
              const QColor &accent, const QColor &muted, bool settled) const {
        int columns = 1;
        for (const QString &line : lines) columns = std::max(columns, int(line.size()));
        QFont font(family); font.setPixelSize(100);
        QFontMetricsF metrics(font);
        double ratio = metrics.horizontalAdvance('M') / 100.0;
        double size = std::min(area.height() / lines.size() / 1.15, area.width() / columns / std::max(0.1, ratio));
        font.setPixelSize(std::max(4, int(size)));
        double cell = std::max(1.0, QFontMetricsF(font).horizontalAdvance('M')), lineHeight = font.pixelSize() * 1.15;
        QPointF origin(area.center().x() - cell * columns / 2, area.center().y() - lineHeight * lines.size() / 2);
        painter.setFont(font);
        double t = settled ? revealSeconds + holdSeconds : elapsed;
        // Block rectangles must not be antialiased, or neighbours show seams.
        painter.setRenderHint(QPainter::Antialiasing, false);
        double span = columns + lines.size();
        for (int r = 0; r < lines.size(); ++r)
            for (int c = 0; c < lines[r].size(); ++c) {
                QChar ch = lines[r][c];
                if (ch.isSpace()) continue;
                int index = r * 160 + c;
                double s = seed(index, 0), s2 = seed(index, 1);
                QPointF target(origin.x() + c * cell, origin.y() + r * lineHeight), at = target;
                QColor color = foreground; QChar shown = ch; bool visible = true;
                switch (effect) {
                case Decrypt: {
                    double reveal = 0.4 + s * (revealSeconds - 1);
                    if (t < reveal) { shown = glyph(quint32(index * 7919 + int(t * 12) * 104729)); color = mix(muted, accent, t / reveal); }
                    else color = mix(accent, foreground, (t - reveal) / 0.6);
                    break;
                }
                case Rain: {
                    double progress = ease((t - (double(c) / columns * 1.6 + s * 0.8)) / 1.2);
                    visible = t > double(c) / columns * 1.6 + s * 0.8;
                    at.setY(target.y() - (1 - progress) * (target.y() + lineHeight));
                    color = mix(accent, foreground, progress);
                    break;
                }
                case Wipe: {
                    double edge = (c + r) - t / (revealSeconds - 0.5) * span;
                    visible = edge <= 0;
                    color = mix(accent, foreground, -edge / 8);
                    break;
                }
                case Scatter: {
                    double progress = ease((t - s * 0.8) / 2.4);
                    QPointF from(s2 * 1280, seed(index, 2) * 480);
                    at = from + (target - from) * progress;
                    color = mix(muted, foreground, progress);
                    break;
                }
                case Beams: {
                    double beam = (t / (revealSeconds - 0.5) * 1.3 - double(r) / lines.size() * 0.3) * (columns + 12) - 6;
                    double distance = c - beam;
                    color = distance > 6 ? muted : distance > -6 ? mix(accent, foreground, -distance / 6) : foreground;
                    if (distance > 6) color.setAlphaF(0.45);
                    break;
                }
                default: break;
                }
                if (!visible) continue;
                QRectF box(at.x(), at.y(), cell, lineHeight);
                if (drawBlock(painter, box, shown, color)) continue;
                painter.setPen(color);
                painter.drawText(box.adjusted(0, 0, cell * .05, 0), Qt::AlignLeft | Qt::AlignVCenter, QString(shown));
            }
        painter.setRenderHint(QPainter::Antialiasing, true);
    }
    Effect currentEffect() const { return effect; }
    // True while the art is assembling; false while it holds still.
    bool animating() const { return elapsed < revealSeconds; }
private:
    QString source;
    Effect effect = Decrypt;
    double elapsed = 0;
    quint32 salt = 1;
    void reroll() { salt = QRandomGenerator::global()->generate() | 1; }
    // Stable per-character randomness in [0, 1) for this cycle.
    double seed(int index, int lane) const {
        quint32 x = quint32(index) * 2654435761u ^ (salt + quint32(lane) * 0x9e3779b9u);
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
        return (x & 0xffffff) / double(0x1000000);
    }
};

}  // namespace scenes
