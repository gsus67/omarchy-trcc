// SPDX-License-Identifier: GPL-3.0-or-later
// Animated backgrounds for the Matrix and Omarchy layouts. State only moves in
// step(), called by the frame loop at 8 fps; draw() never changes it, so
// previews and status renders do not advance the animation.
#pragma once
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QStringDecoder>
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

// A picture as half-block art for LogoScene: each character holds two pixels
// (▀ top, ▄ bottom, █ both), dithered (Floyd–Steinberg) around the image's
// mean brightness. Bright pixels are drawn; transparent ones never are. At
// most 160 columns × 40 rows, keeping the picture's proportions.
inline QStringList imageToBlocks(const QImage &source) {
    if (source.isNull() || source.width() < 1 || source.height() < 1) return {};
    int columns = 160, rows = std::max(1, int(std::ceil(columns * double(source.height()) / source.width() / 2)));
    if (rows > 40) { rows = 40; columns = std::max(1, int(rows * 2.0 * source.width() / source.height())); }
    QImage small = source.convertToFormat(QImage::Format_ARGB32).scaled(columns, rows * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QVector<double> gray(qsizetype(columns) * rows * 2), alpha(qsizetype(columns) * rows * 2);
    double sum = 0;
    for (int y = 0; y < rows * 2; ++y)
        for (int x = 0; x < columns; ++x) {
            QRgb pixel = small.pixel(x, y);
            gray[y * columns + x] = qGray(pixel) / 255.0; alpha[y * columns + x] = qAlpha(pixel) / 255.0;
            sum += gray[y * columns + x];
        }
    double threshold = sum / gray.size();
    QVector<bool> on(gray.size());
    for (int y = 0; y < rows * 2; ++y)
        for (int x = 0; x < columns; ++x) {
            int i = y * columns + x;
            double value = std::clamp(gray[i], 0.0, 1.0);
            on[i] = alpha[i] >= 0.5 && value > threshold;
            double error = value - (value > threshold ? 1.0 : 0.0);
            if (x + 1 < columns) gray[i + 1] += error * 7 / 16;
            if (y + 1 < rows * 2) {
                if (x > 0) gray[i + columns - 1] += error * 3 / 16;
                gray[i + columns] += error * 5 / 16;
                if (x + 1 < columns) gray[i + columns + 1] += error * 1 / 16;
            }
        }
    QStringList lines;
    for (int r = 0; r < rows; ++r) {
        QString line;
        for (int x = 0; x < columns; ++x) {
            bool top = on[(r * 2) * columns + x], bottom = on[(r * 2 + 1) * columns + x];
            line += top && bottom ? QChar(0x2588) : top ? QChar(0x2580) : bottom ? QChar(0x2584) : QChar(' ');
        }
        lines << line;
    }
    return lines;
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
        // Drop the margin every line shares on the left, so the art is centered.
        qsizetype indent = 160;
        for (const QString &line : next)
            if (!line.isEmpty()) { qsizetype spaces = 0; while (spaces < line.size() && line[spaces] == ' ') ++spaces; indent = std::min(indent, spaces); }
        for (QString &line : next) line = line.mid(indent);
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

// The Omarchy screensaver's own engine: runs the system's ttfx (package ttfx,
// part of Omarchy) on our text and reads its frames into a cell grid, so all
// of its effects and colors show on the LCD. Nothing is installed; without
// ttfx the layouts fall back to LogoScene. ttfx gets the text on stdin and a
// fixed argument list (no shell); its output is parsed, never executed.
class TtfxScene {
public:
    static constexpr double holdSeconds = 4, maxSeconds = 25;
    ~TtfxScene() { stop(); }
    static bool available() { static const bool found = !QStandardPaths::findExecutable("ttfx").isEmpty(); return found; }
    bool running() const { return process && process->state() != QProcess::NotRunning; }
    // Starts a random effect on `text` inside a canvas with some room around it.
    void start(const QStringList &text, int fps, int padColumns = 8, int padRows = 4) {
        stop();
        lines = text; textColumns = 1;
        for (const QString &line : text) textColumns = std::max(textColumns, int(line.size()));
        width = textColumns + padColumns; height = int(text.size()) + padRows; homeRow = homeColumn = -1;
        chars = QVector<QChar>(qsizetype(width) * height, ' ');
        colors = QVector<QRgb>(qsizetype(width) * height, 0);
        shownChars = chars; shownColors = colors; fresh = false;
        row = col = savedRow = savedCol = 0; state = Normal; current = 0; decoder = QStringDecoder(QStringDecoder::Utf8);
        elapsed = 0; finishedAt = -1;
        process = std::make_unique<QProcess>();
        process->setProcessChannelMode(QProcess::SeparateChannels);
        process->setStandardErrorFile(QProcess::nullDevice());
        QObject::connect(process.get(), &QProcess::readyReadStandardOutput, [this] { feed(process->readAllStandardOutput()); });
        process->start("ttfx", {"--frame-rate", QString::number(std::clamp(fps, 2, 30)),
                                "--canvas-width", QString::number(width), "--canvas-height", QString::number(height),
                                "--anchor-canvas", "c", "--anchor-text", "c", "--ignore-terminal-dimensions",
                                "--no-eol", "--no-restore-cursor", "--random-effect"});
        process->write(text.join('\n').toUtf8());
        process->closeWriteChannel();
    }
    void stop() {
        if (!process) return;
        process->disconnect();
        if (process->state() != QProcess::NotRunning) { process->kill(); process->waitForFinished(500); }
        process.reset();
    }
    // Advances the cycle: run, hold the final frame, then a new effect.
    // Returns true when the caller should start the next effect.
    bool step(double seconds) {
        elapsed += seconds;
        if (running() && elapsed < maxSeconds) return false;
        if (running()) stop();
        if (finishedAt < 0) { finishedAt = elapsed; commit(); locate(); }
        return elapsed - finishedAt >= holdSeconds;
    }
    bool animating() const { return running(); }
    // True once per complete frame from ttfx, so the LCD only redraws on new frames.
    bool takeFresh() { bool was = fresh; fresh = false; return was; }
    bool started() const { return !chars.isEmpty(); }
    // Parses ttfx output: printable text, newlines, ESC 7/8 (save/restore
    // cursor), CSI n A/B/C/D (moves) and SGR 38;2;r;g;b / 0 (colors).
    void feed(const QByteArray &bytes) {
        const QString text = decoder(bytes);
        for (QChar ch : text) {
            ushort c = ch.unicode();
            if (state == Escape) {
                if (c == '[') { state = Csi; params.clear(); }
                else { if (c == '7') { savedRow = row; savedCol = col; } else if (c == '8') { row = savedRow; col = savedCol; } state = Normal; }
                continue;
            }
            if (state == Csi) {
                if ((c >= '0' && c <= '9') || c == ';' || c == '?') { if (params.size() < 64) params += ch; continue; }
                csi(c); state = Normal; continue;
            }
            if (c == 0x1b) state = Escape;
            else if (c == '\n') { ++row; col = 0; }
            else if (c == '\r') col = 0;
            else if (c >= 0x20) {
                if (row >= 0 && row < height && col >= 0 && col < width) {
                    chars[qsizetype(row) * width + col] = ch;
                    colors[qsizetype(row) * width + col] = ch.isSpace() ? 0 : current;
                }
                ++col;
            }
        }
    }
    // While holding, live text replaces the characters but keeps ttfx's final colors.
    void setLive(const QStringList &text) { if (!running() && text.size() == lines.size()) lines = text; }
    void draw(QPainter &painter, const QRectF &area, const QString &family, const QColor &fallback) const {
        if (shownChars.isEmpty()) return;
        QFont font(family); font.setPixelSize(100);
        double ratio = QFontMetricsF(font).horizontalAdvance('M') / 100.0;
        double size = std::min(area.height() / height / 1.15, area.width() / width / std::max(0.1, ratio));
        font.setPixelSize(std::max(4, int(size)));
        double cell = std::max(1.0, QFontMetricsF(font).horizontalAdvance('M')), lineHeight = font.pixelSize() * 1.15;
        QPointF origin(area.center().x() - cell * width / 2, area.center().y() - lineHeight * height / 2);
        painter.setFont(font);
        painter.setRenderHint(QPainter::Antialiasing, false);
        int top = homeRow, left = homeColumn;
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c) {
                QChar ch = shownChars.value(qsizetype(r) * width + c, ' ');
                QRgb rgb = shownColors.value(qsizetype(r) * width + c, 0);
                // Holding: show the live text at its home cells.
                if (!running() && top >= 0 && r >= top && r - top < lines.size() && c >= left) {
                    const QString &line = lines[r - top];
                    if (c - left < line.size()) ch = line[c - left];
                }
                if (ch.isSpace()) continue;
                QColor color = rgb ? QColor::fromRgb(rgb) : fallback;
                QRectF box(origin.x() + c * cell, origin.y() + r * lineHeight, cell, lineHeight);
                if (drawBlock(painter, box, ch, color)) continue;
                painter.setPen(color);
                painter.drawText(box.adjusted(0, 0, cell * .05, 0), Qt::AlignLeft | Qt::AlignVCenter, QString(ch));
            }
        painter.setRenderHint(QPainter::Antialiasing, true);
    }
    int columnsCount() const { return width; }
    int rowsCount() const { return height; }
    QChar at(int r, int c) const { return chars.value(qsizetype(r) * width + c); }
    QRgb colorAt(int r, int c) const { return colors.value(qsizetype(r) * width + c); }
    // For tests: a grid without a process.
    void prepare(int columns, int rowsWanted) {
        stop(); width = columns; height = rowsWanted; textColumns = columns; lines.clear(); homeRow = homeColumn = -1;
        chars = QVector<QChar>(qsizetype(width) * height, ' '); colors = QVector<QRgb>(qsizetype(width) * height, 0);
        shownChars = chars; shownColors = colors; fresh = false;
        row = col = savedRow = savedCol = 0; state = Normal; current = 0; decoder = QStringDecoder(QStringDecoder::Utf8);
    }
    QChar shownAt(int r, int c) const { return shownChars.value(qsizetype(r) * width + c); }
private:
    enum State { Normal, Escape, Csi };
    std::unique_ptr<QProcess> process;
    QStringList lines;
    // Written as ttfx outputs, and the last complete frame (what is drawn).
    QVector<QChar> chars, shownChars; QVector<QRgb> colors, shownColors;
    bool fresh = false;
    void commit() { shownChars = chars; shownColors = colors; fresh = true; }
    int width = 0, height = 0, textColumns = 1, row = 0, col = 0, savedRow = 0, savedCol = 0;
    State state = Normal; QString params; QRgb current = 0;
    QStringDecoder decoder{QStringDecoder::Utf8};
    double elapsed = 0, finishedAt = -1;
    // Where ttfx left the text (its first line), found in the final frame;
    // -1 when not found, and then live text is not overlaid.
    int homeRow = -1, homeColumn = -1;
    void locate() {
        homeRow = homeColumn = -1;
        if (lines.isEmpty()) return;
        QString first = lines.first();
        while (first.endsWith(' ')) first.chop(1);
        if (first.isEmpty()) return;
        for (int r = 0; r < height; ++r) {
            QString rowText(chars.constData() + qsizetype(r) * width, width);
            int column = rowText.indexOf(first);
            if (column >= 0) { homeRow = r; homeColumn = column; return; }
        }
    }
    void csi(ushort final) {
        QStringList parts = params.split(';');
        int n = std::max(1, parts.value(0).toInt());
        // A terminal never moves the cursor above the first row; ttfx relies on
        // it. Moving up is also where ttfx starts each frame: the one just
        // written is complete, so it becomes the frame on show.
        if (final == 'A') { row = std::max(0, row - n); commit(); } else if (final == 'B') row += n;
        else if (final == 'C') col += n; else if (final == 'D') col = std::max(0, col - n);
        else if (final == 'm') {
            for (int i = 0; i < parts.size(); ++i) {
                int code = parts[i].toInt();
                if (parts[i].isEmpty() || code == 0 || code == 39) current = 0;
                else if (code == 38 && parts.value(i + 1) == "2" && i + 4 < parts.size()) {
                    current = qRgb(std::clamp(parts[i + 2].toInt(), 0, 255), std::clamp(parts[i + 3].toInt(), 0, 255), std::clamp(parts[i + 4].toInt(), 0, 255)) | 0xff000000;
                    i += 4;
                } else if (code == 48 && parts.value(i + 1) == "2") i += 4;
            }
        }
    }
};

}  // namespace scenes
