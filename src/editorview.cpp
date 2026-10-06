// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "editorview.h"

#include <QClipboard>
#include <QDrag>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QMimeData>
#include <QNativeGestureEvent>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QStyleHints>
#include <QtMath>

// A line longer than this is laid out in pieces, and always wrapped.
static constexpr int segmentLength = 1024;
static constexpr qreal topPadding = 6;
static constexpr qreal bottomPadding = 12;
// The caret stops blinking after this long without typing, so an idle window
// does not keep waking up.
static constexpr int blinkDuration = 10000;

void VisualLineIndex::build(const std::vector<int> &counts)
{
    m_size = int(counts.size());
    m_tree.assign(m_size + 1, 0);
    m_total = 0;
    for (int i = 0; i < m_size; ++i) {
        m_total += counts[i];
        m_tree[i + 1] += counts[i];
        const int parent = (i + 1) + ((i + 1) & -(i + 1));
        if (parent <= m_size)
            m_tree[parent] += m_tree[i + 1];
    }
}

void VisualLineIndex::add(int line, int delta)
{
    m_total += delta;
    for (int i = line + 1; i <= m_size; i += i & -i)
        m_tree[i] += delta;
}

qint64 VisualLineIndex::before(int line) const
{
    qint64 sum = 0;
    for (int i = qMin(line, m_size); i > 0; i -= i & -i)
        sum += m_tree[i];
    return sum;
}

int VisualLineIndex::lineAt(qint64 visual, int *offset) const
{
    if (m_size == 0) {
        *offset = 0;
        return 0;
    }
    if (visual >= m_total) {
        const int last = m_size - 1;
        *offset = int(qMax<qint64>(0, m_total - before(last) - 1));
        return last;
    }
    visual = qMax<qint64>(0, visual);
    int position = 0;
    qint64 remaining = visual;
    int step = 1;
    while (step * 2 <= m_size)
        step *= 2;
    for (; step > 0; step /= 2) {
        const int next = position + step;
        if (next <= m_size && m_tree[next] <= remaining) {
            position = next;
            remaining -= m_tree[next];
        }
    }
    *offset = int(remaining);
    return position;
}

// The scene graph side: the nodes kept from frame to frame.
class EditorNode : public QSGTransformNode
{
public:
    // Transform nodes, not plain ones: the software renderer looks up a new
    // node's position through its parent, and keeps none for plain nodes.
    QSGTransformNode *underlay = nullptr;
    QSGTransformNode *textArea = nullptr;
    QSGRectangleNode *gutterBackground = nullptr;
    QSGTransformNode *text = nullptr;
    QSGTransformNode *overlay = nullptr;
    QSGTransformNode *gutter = nullptr;
    // Keyed by line id and segment index.
    std::unordered_map<quint64, std::pair<QSGTextNode *, quint64>> lines;
    // Keyed by line * 2, plus one for the current line.
    std::unordered_map<qint64, QSGTextNode *> numbers;
    QList<QSGRectangleNode *> rects;
    int usedRects = 0;
};

static int digitCount(int value)
{
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

EditorView::EditorView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlags(ItemHasContents | ItemIsFocusScope | ItemAcceptsInputMethod | ItemAcceptsDrops);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);
    setCursor(Qt::IBeamCursor);
    setClip(false);

    m_exactTimer.setSingleShot(true);
    m_exactTimer.setInterval(0);
    connect(&m_exactTimer, &QTimer::timeout, this, [this] {
        // Exact wrapped heights of lines out of view, a slice at a time, so
        // the scroll bar settles without a pause on a large file.
        if (!m_document || !m_wordWrap || width() <= 0)
            return;
        QElapsedTimer timer;
        timer.start();
        ensureIndex();
        int anchorOffset = 0;
        const int anchor = m_index.lineAt(qint64((m_contentY - topPadding) / m_lineHeight), &anchorOffset);
        qint64 shift = 0;
        const int count = int(m_lines.size());
        int checked = 0;
        while (m_exactCursor < count) {
            LineInfo &info = m_lines[m_exactCursor];
            if (!info.exact) {
                // Laid out as when shown, then dropped; very long lines stay estimated.
                int lines = info.visualLines;
                if (m_document->buffer().line(m_exactCursor).size() <= segmentLength * 8) {
                    LineCache scratch;
                    buildLayout(m_exactCursor, &scratch);
                    lines = scratch.visualLines;
                }
                lines = qMax(1, lines);
                if (lines != info.visualLines) {
                    if (m_exactCursor < anchor)
                        shift += lines - info.visualLines;
                    setLineVisualCount(m_exactCursor, lines);
                }
                info.exact = true;
            }
            ++m_exactCursor;
            if ((++checked & 31) == 0 && timer.elapsed() >= 3)
                break;
        }
        if (shift != 0)
            applyScroll(m_contentX, m_contentY + shift * m_lineHeight);
        if (m_exactCursor < count)
            m_exactTimer.start();
        polish();
    });

    m_primaryTimer.setSingleShot(true);
    m_primaryTimer.setInterval(150);
    connect(&m_primaryTimer, &QTimer::timeout, this, &EditorView::updatePrimarySelection);

    m_font = QFont(QStringLiteral("monospace"));
    updateTheme();
    updateMetrics();
}

EditorView::~EditorView() = default;

void EditorView::setDocument(Document *document)
{
    if (m_document == document)
        return;
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    m_preedit.clear();
    m_preferredX = -1;
    if (m_document) {
        connect(m_document, &Document::linesChanged, this, &EditorView::onLinesChanged);
        connect(m_document, &Document::syntaxChanged, this, &EditorView::onSyntaxChanged);
        connect(m_document, &Document::cursorChanged, this, &EditorView::onCursorChanged);
        connect(m_document, &Document::revealCursor, this, [this] { ensureCursorVisible(); });
        connect(m_document, &Document::indentationChanged, this, [this] { invalidateLayouts(true); });
        connect(m_document, &Document::languageChanged, this, [this] { invalidateLayouts(false); });
        connect(m_document, &Document::matchesChanged, this, &QQuickItem::polish);
        connect(m_document, &Document::searchChanged, this, &QQuickItem::polish);
        connect(m_document, &Document::lockedChanged, this, &QQuickItem::polish);
        connect(m_document, &QObject::destroyed, this, [this] { setDocument(nullptr); });
    }
    invalidateLayouts(true);
    emit documentChanged();
}

void EditorView::setFont(const QFont &font)
{
    if (m_font == font)
        return;
    m_font = font;
    updateMetrics();
    emit fontChanged();
}

void EditorView::setWordWrap(bool wrap)
{
    if (m_wordWrap == wrap)
        return;
    const qreal top = topLine();
    m_wordWrap = wrap;
    invalidateLayouts(true);
    scrollToLine(top);
    emit optionsChanged();
}

void EditorView::setLineNumbers(bool show)
{
    if (m_lineNumbers == show)
        return;
    m_lineNumbers = show;
    invalidateLayouts(true);
    emit metricsChanged();
    emit optionsChanged();
}

void EditorView::setHighlightCurrentLine(bool highlight)
{
    if (m_highlightCurrentLine == highlight)
        return;
    m_highlightCurrentLine = highlight;
    polish();
    emit optionsChanged();
}

void EditorView::setShowWhitespace(bool show)
{
    if (m_showWhitespace == show)
        return;
    m_showWhitespace = show;
    polish();
    emit optionsChanged();
}

void EditorView::setMatchBrackets(bool match)
{
    if (m_matchBrackets == match)
        return;
    m_matchBrackets = match;
    polish();
    emit optionsChanged();
}

void EditorView::setAutoIndent(bool indent)
{
    if (m_autoIndent == indent)
        return;
    m_autoIndent = indent;
    emit optionsChanged();
}

void EditorView::setOverwriteMode(bool overwrite)
{
    if (m_overwrite == overwrite)
        return;
    m_overwrite = overwrite;
    polish();
    emit overwriteModeChanged();
}

void EditorView::setColorScheme(const QString &scheme)
{
    if (m_scheme == scheme)
        return;
    m_scheme = scheme;
    updateTheme();
}

void EditorView::setDarkMode(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    updateTheme();
}

void EditorView::setAccentColor(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    updateTheme();
}

void EditorView::setSidePadding(qreal padding)
{
    if (qFuzzyCompare(m_sidePadding, padding))
        return;
    m_sidePadding = padding;
    invalidateLayouts(true);
    emit optionsChanged();
}

void EditorView::updateTheme()
{
    m_theme = SyntaxTheme::theme(m_scheme, m_dark, m_accent);
    m_formats.clear();
    for (int i = 0; i < int(SyntaxStyle::Count); ++i) {
        QTextCharFormat format;
        format.setForeground(m_theme.styles[i]);
        const SyntaxStyle style = SyntaxStyle(i);
        if (style == SyntaxStyle::Comment || style == SyntaxStyle::Emphasis)
            format.setFontItalic(true);
        if (style == SyntaxStyle::Heading || style == SyntaxStyle::Strong || style == SyntaxStyle::Section
                || style == SyntaxStyle::DiffHeader || style == SyntaxStyle::Error)
            format.setFontWeight(QFont::Bold);
        if (style == SyntaxStyle::Link)
            format.setFontUnderline(true);
        m_formats.append(format);
    }
    m_themeDirty = true;
    invalidateLayouts(false);
    emit themeChanged();
}

void EditorView::updateMetrics()
{
    m_numberLayouts.clear();
    m_themeDirty = true;
    if (window())
        m_dpr = window()->effectiveDevicePixelRatio();
    const QFontMetricsF metrics(m_font);
    const qreal height = metrics.height();
    // Whole device pixels, so every line starts on the pixel grid.
    m_lineHeight = qCeil(height * 1.4 * m_dpr) / m_dpr;
    m_lineInset = qRound((m_lineHeight - height) / 2 * m_dpr) / m_dpr;
    m_ascent = metrics.ascent();
    m_charWidth = metrics.horizontalAdvance(QLatin1Char('M'));
    m_digitWidth = metrics.horizontalAdvance(QLatin1Char('0'));
    invalidateLayouts(true);
    emit metricsChanged();
}

qreal EditorView::gutterWidth() const
{
    if (!m_lineNumbers)
        return 0;
    const int lines = m_document ? m_document->lineCount() : 1;
    const qreal width = m_sidePadding + qMax(2, digitCount(lines)) * m_digitWidth + m_sidePadding;
    return qCeil(width * m_dpr) / m_dpr;
}

qreal EditorView::textLeft() const
{
    return m_lineNumbers ? gutterWidth() : qRound(m_sidePadding * m_dpr) / m_dpr;
}

qreal EditorView::wrapWidth() const
{
    return qMax<qreal>(m_charWidth * 4, width() - textLeft() - m_sidePadding);
}

int EditorView::displayColumns(const QString &text) const
{
    const int tabWidth = m_document ? m_document->tabWidth() : 4;
    int columns = 0;
    const QChar *data = text.constData();
    const int size = int(text.size());
    for (int i = 0; i < size; ++i) {
        const char16_t c = data[i].unicode();
        if (c == u'\t')
            columns += tabWidth - columns % tabWidth;
        else if (c < 0x1100)
            ++columns;
        else if (QChar::isLowSurrogate(c))
            ++columns;
        else
            // East Asian wide characters take two cells.
            columns += (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3)
                    || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60)
                    || (c >= 0xFFE0 && c <= 0xFFE6) ? 2 : 1;
    }
    return columns;
}

int EditorView::estimateVisualLines(const QString &text, int columns) const
{
    const bool wraps = m_wordWrap || text.size() > segmentLength;
    if (!wraps)
        return 1;
    const qreal available = wrapWidth();
    return qMax(1, int(qCeil(columns * m_charWidth / available)));
}

void EditorView::invalidateLayouts(bool estimates)
{
    // The plan points into the caches; it is rebuilt in the next polish.
    m_plan.clear();
    m_caches.clear();
    m_staleCaches.clear();
    m_widestLaidOut = 0;
    if (estimates) {
        m_lines.clear();
        if (m_document) {
            const TextBuffer &buffer = m_document->buffer();
            m_lines.resize(buffer.lineCount());
            m_maxColumns = 0;
            for (int i = 0; i < buffer.lineCount(); ++i) {
                LineInfo &info = m_lines[i];
                info.id = m_nextId++;
                info.columns = displayColumns(buffer.line(i));
                info.visualLines = estimateVisualLines(buffer.line(i), info.columns);
                info.exact = !m_wordWrap && buffer.line(i).size() <= segmentLength;
                m_maxColumns = qMax(m_maxColumns, info.columns);
            }
        }
        m_maxColumnsDirty = false;
        m_indexDirty = true;
        m_exactCursor = 0;
        if (m_wordWrap)
            m_exactTimer.start();
    }
    polish();
}

void EditorView::ensureIndex()
{
    if (!m_indexDirty)
        return;
    std::vector<int> counts(m_lines.size());
    for (size_t i = 0; i < m_lines.size(); ++i)
        counts[i] = m_lines[i].visualLines;
    m_index.build(counts);
    m_indexDirty = false;
}

void EditorView::setLineVisualCount(int line, int count)
{
    LineInfo &info = m_lines[line];
    if (info.visualLines == count)
        return;
    if (!m_indexDirty)
        m_index.add(line, count - info.visualLines);
    info.visualLines = count;
}

void EditorView::recountWidth()
{
    if (!m_maxColumnsDirty)
        return;
    m_maxColumns = 0;
    for (const LineInfo &info : m_lines)
        m_maxColumns = qMax(m_maxColumns, info.columns);
    m_maxColumnsDirty = false;
}

void EditorView::onLinesChanged(int line, int removed, int added)
{
    if (!m_document)
        return;
    m_plan.clear();
    const TextBuffer &buffer = m_document->buffer();
    if (removed > int(m_lines.size()) - line) {
        invalidateLayouts(true);
        return;
    }
    const int oldMax = m_maxColumns;
    const int kept = qMin(removed, added);
    for (int i = line; i < line + removed; ++i) {
        auto cached = m_caches.find(m_lines[i].id);
        if (cached != m_caches.end()) {
            if (i < line + kept && buffer.line(i).size() > segmentLength && cached->second->plain)
                m_staleCaches[m_lines[i].id] = std::move(cached->second);
            m_caches.erase(cached);
        }
        // Only losing the widest line needs a recount.
        if (m_lines[i].columns >= oldMax && (i >= line + kept || displayColumns(buffer.line(i)) < oldMax))
            m_maxColumnsDirty = true;
    }

    // Lines edited in place keep their id, so their node is reused.
    for (int i = line; i < line + kept; ++i) {
        LineInfo &info = m_lines[i];
        info.columns = displayColumns(buffer.line(i));
        const int estimate = estimateVisualLines(buffer.line(i), info.columns);
        setLineVisualCount(i, estimate);
        info.exact = !m_wordWrap && buffer.line(i).size() <= segmentLength;
        m_maxColumns = qMax(m_maxColumns, info.columns);
    }
    if (removed > added) {
        m_lines.erase(m_lines.begin() + line + added, m_lines.begin() + line + removed);
        m_indexDirty = true;
    } else if (added > removed) {
        std::vector<LineInfo> fresh(added - removed);
        for (int k = 0; k < int(fresh.size()); ++k) {
            const int i = line + removed + k;
            LineInfo &info = fresh[k];
            info.id = m_nextId++;
            info.columns = displayColumns(buffer.line(i));
            info.visualLines = estimateVisualLines(buffer.line(i), info.columns);
            info.exact = !m_wordWrap && buffer.line(i).size() <= segmentLength;
            m_maxColumns = qMax(m_maxColumns, info.columns);
        }
        m_lines.insert(m_lines.begin() + line + removed, fresh.begin(), fresh.end());
        m_indexDirty = true;
    }
    if (m_wordWrap) {
        m_exactCursor = qMin(m_exactCursor, line);
        m_exactTimer.start();
    }
    // The gutter widens when the line count gains a digit.
    if (m_lineNumbers && digitCount(int(m_lines.size())) != digitCount(int(m_lines.size()) - added + removed)) {
        invalidateLayouts(true);
        emit metricsChanged();
    }
    polish();
}

void EditorView::onSyntaxChanged(int first, int last)
{
    m_plan.clear();
    for (int i = qMax(0, first); i <= last && i < int(m_lines.size()); ++i) {
        auto it = m_caches.find(m_lines[i].id);
        if (it != m_caches.end())
            m_caches.erase(it);
    }
    polish();
}

void EditorView::onCursorChanged()
{
    resetBlink();
    m_primaryTimer.start();
    polish();
    emit cursorRectangleChanged();
    if (hasActiveFocus())
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImSurroundingText | Qt::ImCursorPosition
                                               | Qt::ImAnchorPosition | Qt::ImCurrentSelection);
}

EditorView::LineCache *EditorView::ensureLayout(int line)
{
    LineInfo &info = m_lines[line];
    auto it = m_caches.find(info.id);
    if (it != m_caches.end())
        return it->second.get();
    auto cache = std::make_unique<LineCache>();
    auto stale = m_staleCaches.find(info.id);
    if (stale != m_staleCaches.end()) {
        buildLayout(line, cache.get(), stale->second.get());
        m_staleCaches.erase(stale);
    } else {
        buildLayout(line, cache.get());
    }
    LineCache *result = cache.get();
    m_caches.emplace(info.id, std::move(cache));
    info.exact = true;
    setLineVisualCount(line, result->visualLines);
    return result;
}

void EditorView::buildLayout(int line, LineCache *cache, LineCache *previous)
{
    const QString &text = m_document->buffer().line(line);
    const bool wraps = m_wordWrap || text.size() > segmentLength;
    const qreal available = wrapWidth();

    QList<SyntaxSpan> spans;
    SyntaxStates &states = m_document->syntaxStates();
    if (m_document->isHighlighting() && text.size() <= Document::maximumHighlightedLine) {
        Syntax::highlight(states.language(), text, states.before(line), &spans);
        cache->provisionalSyntax = line > states.validLines();
    }

    const TextPosition cursor = m_document->cursor();
    const bool preeditHere = !m_preedit.isEmpty() && cursor.line == line;
    cache->preeditColumn = preeditHere ? cursor.column : -1;
    cache->plain = spans.isEmpty() && !preeditHere;
    // Segments of a long line that an edit left as they were keep their layouts.
    QHash<QString, Segment *> reusable;
    if (previous && cache->plain && previous->plain) {
        for (Segment &old : previous->segments)
            reusable.insert(old.layout->text(), &old);
    }

    QTextOption option;
    option.setWrapMode(wraps ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    option.setTabStopDistance(m_document->tabWidth() * QFontMetricsF(m_font).horizontalAdvance(QLatin1Char(' ')));

    int start = 0;
    int visual = 0;
    qreal hanging = 0;
    cache->width = 0;
    cache->segments.clear();
    do {
        int length = int(text.size()) - start;
        if (length > segmentLength) {
            // Pieces end where the text itself says, not at a fixed offset, so
            // an edit early in a long line leaves the pieces after it alone:
            // after a separator whose preceding characters hash to a marker.
            length = segmentLength;
            int fallback = -1;
            for (int i = start + segmentLength / 4; i < start + segmentLength; ++i) {
                const QChar c = text.at(i);
                if (c != QLatin1Char(' ') && c != QLatin1Char(';') && c != QLatin1Char(',') && c != QLatin1Char('}')
                        && c != QLatin1Char(')') && c != QLatin1Char('>') && c != QLatin1Char('\t'))
                    continue;
                fallback = i;
                if (qHash(QStringView(text).mid(i - 6, 6)) % 8 == 0) {
                    length = i - start + 1;
                    fallback = -1;
                    break;
                }
            }
            if (fallback >= 0)
                length = fallback - start + 1;
            if (text.at(start + length - 1).isHighSurrogate())
                --length;
        }

        Segment segment;
        segment.start = start;
        segment.length = length;
        segment.firstVisual = visual;
        if (Segment *old = reusable.isEmpty() ? nullptr : reusable.value(text.mid(start, length))) {
            if (old->layout) {
                segment.layout = std::move(old->layout);
                segment.version = old->version;
                visual += segment.layout->lineCount();
                cache->width = qMax(cache->width, segment.layout->maximumWidth());
                cache->segments.push_back(std::move(segment));
                start += length;
                continue;
            }
        }
        segment.version = m_nextVersion++;
        segment.layout = std::make_unique<QTextLayout>(text.mid(start, length), m_font);
        QTextLayout *layout = segment.layout.get();
        layout->setTextOption(option);
        layout->setCacheEnabled(true);

        QList<QTextLayout::FormatRange> formats;
        const bool preeditInSegment = preeditHere && cursor.column >= start
                && (cursor.column < start + length || start + length == text.size());
        const int preeditAt = cursor.column - start;
        auto shifted = [&](int position) {
            return preeditInSegment && position >= preeditAt ? position + int(m_preedit.size()) : position;
        };
        for (const SyntaxSpan &span : std::as_const(spans)) {
            const int from = qMax(span.start, start);
            const int to = qMin(span.start + span.length, start + length);
            if (from >= to || span.style == SyntaxStyle::Normal)
                continue;
            formats.append({shifted(from - start), shifted(to - start) - shifted(from - start),
                            m_formats.at(int(span.style))});
        }
        if (preeditInSegment) {
            layout->setPreeditArea(preeditAt, m_preedit);
            QTextCharFormat underline;
            underline.setFontUnderline(true);
            formats.append({preeditAt, int(m_preedit.size()), underline});
            for (const QInputMethodEvent::Attribute &attribute : std::as_const(m_preeditAttributes)) {
                if (attribute.type != QInputMethodEvent::TextFormat)
                    continue;
                const QTextCharFormat format = qvariant_cast<QTextFormat>(attribute.value).toCharFormat();
                if (format.isValid())
                    formats.append({preeditAt + attribute.start, attribute.length, format});
            }
        }
        layout->setFormats(formats);

        layout->beginLayout();
        while (true) {
            QTextLine textLine = layout->createLine();
            if (!textLine.isValid())
                break;
            // Wrapped lines continue at the line's own indentation.
            const qreal indent = visual > 0 ? hanging : 0;
            textLine.setLineWidth(wraps ? available - indent : 1e7);
            // Relative to the segment, so a reused one can move down the line.
            textLine.setPosition(QPointF(indent, (visual - segment.firstVisual) * m_lineHeight + m_lineInset));
            if (visual == 0 && wraps && m_wordWrap && textLine.textLength() < length) {
                int leading = 0;
                while (leading < textLine.textLength() && text.at(start + leading).isSpace())
                    ++leading;
                hanging = qMin(textLine.cursorToX(leading), available / 2);
            }
            ++visual;
        }
        layout->endLayout();
        cache->width = qMax(cache->width, layout->maximumWidth());
        cache->segments.push_back(std::move(segment));
        start += length;
    } while (start < text.size());

    cache->visualLines = qMax(1, visual);
    cache->version = m_nextVersion++;
    if (!wraps)
        m_widestLaidOut = qMax(m_widestLaidOut, cache->width);
}

void EditorView::dropFarCaches(int first, int last)
{
    const size_t keep = size_t(qMax(200, (last - first + 1) * 3));
    if (m_caches.size() <= keep)
        return;
    const int margin = last - first + 1;
    std::unordered_map<quint32, int> wanted;
    for (int i = qMax(0, first - margin); i <= last + margin && i < int(m_lines.size()); ++i)
        wanted.emplace(m_lines[i].id, i);
    for (auto it = m_caches.begin(); it != m_caches.end();) {
        if (wanted.find(it->first) == wanted.end())
            it = m_caches.erase(it);
        else
            ++it;
    }
}

qreal EditorView::lineTop(int line) const
{
    return topPadding + m_index.before(line) * m_lineHeight;
}

qreal EditorView::topLine() const
{
    if (m_lines.empty() || m_indexDirty)
        return 0;
    int offset = 0;
    const qreal visual = (m_contentY - topPadding) / m_lineHeight;
    const int line = m_index.lineAt(qint64(qMax<qreal>(0, visual)), &offset);
    return line + qBound<qreal>(0, (visual - m_index.before(line)) / m_lines[line].visualLines, 0.999);
}

void EditorView::scrollToLine(qreal line)
{
    if (m_lines.empty())
        return;
    ensureIndex();
    const int index = qBound(0, int(line), int(m_lines.size()) - 1);
    const qreal fraction = line - index;
    const qreal y = lineTop(index) + fraction * m_lines[index].visualLines * m_lineHeight - topPadding;
    applyScroll(m_contentX, qMax<qreal>(0, y));
}

void EditorView::applyScroll(qreal x, qreal y)
{
    const qreal maxY = qMax<qreal>(0, m_contentHeight - height());
    const qreal maxX = qMax<qreal>(0, m_contentWidth - width());
    x = qBound<qreal>(0, x, maxX);
    y = qBound<qreal>(-m_occludedHeight, y, qMax(maxY, y > 0 ? y : 0));
    if (!qFuzzyCompare(x + 1, m_contentX + 1)) {
        m_contentX = x;
        emit contentXChanged();
    }
    if (!qFuzzyCompare(y + 1, m_contentY + 1)) {
        m_contentY = y;
        emit contentYChanged();
    }
    emit scrollRequested(x, y);
    polish();
}

void EditorView::setContentX(qreal x)
{
    if (qFuzzyCompare(x + 1, m_contentX + 1))
        return;
    m_contentX = x;
    emit contentXChanged();
    polish();
}

void EditorView::setContentY(qreal y)
{
    if (qFuzzyCompare(y + 1, m_contentY + 1))
        return;
    m_contentY = y;
    emit contentYChanged();
    polish();
}

bool EditorView::locate(const TextPosition &position, LineCache **cacheOut, const Segment **segmentOut,
                        QTextLine *textLineOut, int *visualOut)
{
    if (!m_document || m_lines.empty())
        return false;
    const TextPosition p = m_document->buffer().clamp(position);
    LineCache *cache = ensureLayout(p.line);
    const Segment *segment = &cache->segments.front();
    for (const Segment &s : cache->segments) {
        if (p.column >= s.start)
            segment = &s;
    }
    int column = p.column - segment->start;
    if (cache->preeditColumn >= 0 && p.column > cache->preeditColumn)
        column += int(m_preedit.size());
    QTextLine textLine = segment->layout->lineForTextPosition(column);
    if (!textLine.isValid())
        textLine = segment->layout->lineAt(segment->layout->lineCount() - 1);
    *cacheOut = cache;
    *segmentOut = segment;
    *textLineOut = textLine;
    *visualOut = segment->firstVisual + textLine.lineNumber();
    return textLine.isValid();
}

QRectF EditorView::caretRect(const TextPosition &position)
{
    LineCache *cache;
    const Segment *segment;
    QTextLine textLine;
    int visual;
    if (!locate(position, &cache, &segment, &textLine, &visual))
        return QRectF();
    const TextPosition p = m_document->buffer().clamp(position);
    int column = p.column - segment->start;
    if (cache->preeditColumn >= 0 && p.column >= cache->preeditColumn)
        column += p.column == cache->preeditColumn ? m_preeditCursor : int(m_preedit.size());
    const qreal x = textLine.cursorToX(column);
    const qreal width = qCeil(1.5 * m_dpr) / m_dpr;
    return QRectF(x, lineTop(p.line) + visual * m_lineHeight, width, m_lineHeight);
}

QRectF EditorView::cursorRectangle() const
{
    const QRectF r = m_caret;
    return r;
}

TextPosition EditorView::hitTest(const QPointF &point)
{
    if (!m_document || m_lines.empty())
        return {};
    ensureIndex();
    const qreal y = point.y() + m_contentY - topPadding;
    if (y < 0)
        return {0, 0};
    int offset = 0;
    const int line = m_index.lineAt(qint64(y / m_lineHeight), &offset);
    if (y >= m_index.total() * m_lineHeight)
        return m_document->buffer().end();
    LineCache *cache = ensureLayout(line);
    for (const Segment &segment : cache->segments) {
        const int lines = segment.layout->lineCount();
        if (offset >= segment.firstVisual + lines && &segment != &cache->segments.back())
            continue;
        QTextLine textLine = segment.layout->lineAt(qBound(0, offset - segment.firstVisual, lines - 1));
        const qreal x = point.x() - textLeft() + m_contentX;
        int column = textLine.xToCursor(x);
        // A click past the end of a wrapped line lands before its last space, on that line.
        if (textLine.textStart() + textLine.textLength() < segment.length && column >= textLine.textStart() + textLine.textLength())
            column = textLine.textStart() + textLine.textLength() - 1;
        return m_document->buffer().clamp({line, segment.start + column});
    }
    return {line, 0};
}

QList<QRectF> EditorView::rangeRects(int line, LineCache *cache, int from, int to, bool toLineEnd)
{
    QList<QRectF> rects;
    const qreal top = lineTop(line);
    const qreal left = textLeft() - m_contentX;
    const qreal right = width() + m_contentX;
    for (const Segment &segment : cache->segments) {
        const int segmentEnd = segment.start + segment.length;
        if (to < segment.start || from > segmentEnd)
            continue;
        const bool lastSegment = &segment == &cache->segments.back();
        for (int i = 0; i < segment.layout->lineCount(); ++i) {
            QTextLine textLine = segment.layout->lineAt(i);
            const int lineStart = segment.start + textLine.textStart();
            const int lineEnd = lineStart + textLine.textLength();
            const bool lastLine = lastSegment && i == segment.layout->lineCount() - 1;
            if (to < lineStart || from > lineEnd || (from == lineEnd && !lastLine && from != to))
                continue;
            const int a = qMax(from, lineStart) - segment.start;
            const int b = qMin(to, lineEnd) - segment.start;
            qreal x1 = textLine.cursorToX(a);
            qreal x2 = textLine.cursorToX(b);
            if (x2 < x1)
                std::swap(x1, x2);
            // A selection running on to the next line reaches the right edge.
            if ((toLineEnd && lastLine) || (to > lineEnd && !lastLine))
                x2 = right;
            else if (toLineEnd && to >= lineEnd && lastLine)
                x2 = right;
            const qreal y = top + (segment.firstVisual + i) * m_lineHeight - m_contentY;
            rects.append(QRectF(left + x1, y, qMax<qreal>(x2 - x1, 0), m_lineHeight));
        }
    }
    return rects;
}

void EditorView::addWhitespaceMarks(int line, LineCache *cache)
{
    // A dot for each space and a line for each tab, on top of the text.
    static constexpr int limit = 20000;
    const QString &text = m_document->buffer().line(line);
    const qreal top = lineTop(line) - m_contentY;
    const qreal left = textLeft() - m_contentX;
    const qreal dot = qMax<qreal>(1, qRound(2 * m_dpr)) / m_dpr;
    const qreal hairline = qMax<qreal>(1, qRound(m_dpr)) / m_dpr;
    for (const Segment &segment : cache->segments) {
        for (int l = 0; l < segment.layout->lineCount() && m_whitespaceRects.size() < limit; ++l) {
            const QTextLine textLine = segment.layout->lineAt(l);
            const qreal y = top + (segment.firstVisual + l) * m_lineHeight;
            if (y + m_lineHeight < 0 || y > height())
                continue;
            const qreal middle = y + m_lineHeight / 2;
            const int end = textLine.textStart() + textLine.textLength();
            for (int i = textLine.textStart(); i < end; ++i) {
                const QChar c = text.at(segment.start + i);
                if (c != QLatin1Char(' ') && c != QLatin1Char('\t'))
                    continue;
                const qreal x1 = left + textLine.cursorToX(i);
                const qreal x2 = left + textLine.cursorToX(i + 1);
                if (c == QLatin1Char(' '))
                    m_whitespaceRects.append(QRectF((x1 + x2 - dot) / 2, middle - dot / 2, dot, dot));
                else
                    m_whitespaceRects.append(QRectF(x1 + m_charWidth / 4, middle, qMax<qreal>(0, x2 - x1 - m_charWidth / 2), hairline));
            }
        }
    }
}

void EditorView::updatePolish()
{
    if (window() && !qFuzzyCompare(window()->effectiveDevicePixelRatio(), m_dpr))
        updateMetrics();

    m_plan.clear();
    m_selectionRects.clear();
    m_matchRects.clear();
    m_currentMatchRects.clear();
    m_bracketRects.clear();
    m_whitespaceRects.clear();
    m_currentLineRect = QRectF();
    m_caret = QRectF();
    m_currentLineNumber = -1;

    if (!m_document || m_lines.empty() || width() <= 0 || height() <= 0) {
        update();
        return;
    }
    ensureIndex();

    // Lines laid out for the first time may wrap differently than estimated;
    // the line at the top keeps its place on screen.
    int anchorOffset = 0;
    const int anchor = m_index.lineAt(qint64(qMax<qreal>(0, m_contentY - topPadding) / m_lineHeight), &anchorOffset);
    qreal anchorTop = lineTop(anchor);
    for (int pass = 0; pass < 4; ++pass) {
        m_plan.clear();
        int offset = 0;
        int line = m_index.lineAt(qint64(qMax<qreal>(0, m_contentY - topPadding) / m_lineHeight), &offset);
        while (line < int(m_lines.size())) {
            LineCache *cache = ensureLayout(line);
            const qreal top = lineTop(line);
            if (top >= m_contentY + height())
                break;
            m_plan.push_back({line, m_lines[line].id, top, cache});
            ++line;
        }
        const qreal newTop = lineTop(anchor);
        if (qFuzzyCompare(newTop + 1, anchorTop + 1))
            break;
        m_contentY += newTop - anchorTop;
        anchorTop = newTop;
        emit contentYChanged();
        emit scrollRequested(m_contentX, m_contentY);
    }

    const int firstLine = m_plan.empty() ? 0 : m_plan.front().line;
    const int lastLine = m_plan.empty() ? 0 : m_plan.back().line;
    m_document->requestHighlighting(lastLine + 50);
    dropFarCaches(firstLine, lastLine);

    // Content size.
    recountWidth();
    const qreal contentHeight = topPadding + m_index.total() * m_lineHeight + bottomPadding;
    const qreal contentWidth = m_wordWrap
            ? width()
            : textLeft() + qMax(m_maxColumns * m_charWidth, m_widestLaidOut) + m_sidePadding + m_charWidth;
    if (!qFuzzyCompare(contentHeight, m_contentHeight) || !qFuzzyCompare(contentWidth, m_contentWidth)) {
        m_contentHeight = contentHeight;
        m_contentWidth = contentWidth;
        emit contentSizeChanged();
    }

    const TextBuffer &buffer = m_document->buffer();
    const TextRange selection = m_document->selection();
    const TextPosition cursor = m_document->cursor();

    for (const PlannedLine &planned : m_plan) {
        const int line = planned.line;
        if (m_showWhitespace)
            addWhitespaceMarks(line, planned.cache);
        if (!selection.isEmpty() && line >= selection.start.line && line <= selection.end.line) {
            const int from = line == selection.start.line ? selection.start.column : 0;
            const int to = line == selection.end.line ? selection.end.column : int(buffer.line(line).size());
            m_selectionRects += rangeRects(line, planned.cache, from, to, line < selection.end.line);
        }
        const Searcher &searcher = m_document->searcher();
        if (searcher.isValid() && buffer.line(line).size() < 100000) {
            const TextRange scope = m_document->searchScope();
            if (scope.isEmpty() || (line >= scope.start.line && line <= scope.end.line)) {
                const int lower = !scope.isEmpty() && line == scope.start.line ? scope.start.column : 0;
                const int upper = !scope.isEmpty() && line == scope.end.line ? scope.end.column : -1;
                for (const TextRange &match : searcher.matchesInLine(buffer.line(line), line, lower, upper)) {
                    const QList<QRectF> rects = rangeRects(line, planned.cache, match.start.column, match.end.column, false);
                    if (match == selection)
                        m_currentMatchRects += rects;
                    else
                        m_matchRects += rects;
                }
            }
        }
    }

    if (m_matchBrackets && selection.isEmpty() && m_document->isHighlighting()) {
        const TextRange brackets = m_document->matchingBracket(cursor);
        if (brackets.start.line >= 0) {
            for (const TextPosition &p : {brackets.start, brackets.end}) {
                if (p.line < firstLine || p.line > lastLine)
                    continue;
                LineCache *cache = ensureLayout(p.line);
                m_bracketRects += rangeRects(p.line, cache, p.column, p.column + 1, false);
            }
        }
    }

    if (cursor.line >= firstLine && cursor.line <= lastLine) {
        const QRectF caret = caretRect(cursor);
        if (!caret.isNull()) {
            m_caret = caret.translated(textLeft() - m_contentX, -m_contentY);
            if (m_overwrite && selection.isEmpty() && m_preedit.isEmpty())
                m_caret.setWidth(qMax(m_caret.width(), m_charWidth));
        }
        if (m_highlightCurrentLine && selection.isEmpty()) {
            const qreal top = lineTop(cursor.line) - m_contentY;
            m_currentLineRect = QRectF(0, top, width(), m_lines[cursor.line].visualLines * m_lineHeight);
        }
    }
    m_currentLineNumber = cursor.line;
    // Number layouts are made here, on the GUI thread, in the editor font.
    if (m_lineNumbers) {
        std::unordered_map<int, std::unique_ptr<QTextLayout>> numbers;
        for (const PlannedLine &planned : m_plan) {
            auto existing = m_numberLayouts.find(planned.line);
            if (existing != m_numberLayouts.end()) {
                numbers.emplace(planned.line, std::move(existing->second));
                continue;
            }
            auto layout = std::make_unique<QTextLayout>(QString::number(planned.line + 1), m_font);
            layout->setCacheEnabled(true);
            layout->beginLayout();
            QTextLine textLine = layout->createLine();
            textLine.setLineWidth(1e6);
            textLine.setPosition(QPointF(0, m_lineInset));
            layout->endLayout();
            numbers.emplace(planned.line, std::move(layout));
        }
        m_numberLayouts = std::move(numbers);
    } else {
        m_numberLayouts.clear();
    }
    emit cursorRectangleChanged();
    update();
}

QSGNode *EditorView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<EditorNode *>(oldNode);
    QQuickWindow *win = window();
    if (!win)
        return oldNode;
    if (!node) {
        node = new EditorNode;
        node->underlay = new QSGTransformNode;
        node->textArea = new QSGTransformNode;
        // Text scrolled sideways goes under the gutter, which covers it.
        node->gutterBackground = win->createRectangleNode();
        node->text = new QSGTransformNode;
        node->overlay = new QSGTransformNode;
        node->gutter = new QSGTransformNode;
        node->appendChildNode(node->underlay);
        node->appendChildNode(node->textArea);
        node->textArea->appendChildNode(node->text);
        node->textArea->appendChildNode(node->overlay);
        node->appendChildNode(node->gutterBackground);
        node->appendChildNode(node->gutter);
    }
    if (m_themeDirty) {
        // Every line is rebuilt with the new colours.
        for (auto &entry : node->lines)
            delete entry.second.first;
        node->lines.clear();
        for (auto &entry : node->numbers)
            delete entry.second;
        node->numbers.clear();
        m_themeDirty = false;
    }

    const qreal left = textLeft();
    node->gutterBackground->setRect(m_wordWrap || m_contentX <= 0 ? QRectF() : QRectF(0, 0, left, height()));
    node->gutterBackground->setColor(m_theme.background);

    // Rectangles are reused in order; spare ones are taken out.
    node->usedRects = 0;
    auto rect = [&](QSGNode *parent, const QRectF &r, const QColor &color) {
        QSGRectangleNode *n;
        if (node->usedRects < node->rects.size()) {
            n = node->rects.at(node->usedRects);
            if (n->parent() != parent) {
                if (n->parent())
                    n->parent()->removeChildNode(n);
                parent->appendChildNode(n);
            }
        } else {
            n = win->createRectangleNode();
            node->rects.append(n);
            parent->appendChildNode(n);
        }
        ++node->usedRects;
        n->setRect(r);
        n->setColor(color);
    };
    auto snapped = [this](const QRectF &r) {
        return QRectF(qRound(r.x() * m_dpr) / m_dpr, qRound(r.y() * m_dpr) / m_dpr,
                      qRound(r.width() * m_dpr) / m_dpr, qRound(r.height() * m_dpr) / m_dpr);
    };

    if (!m_currentLineRect.isNull())
        rect(node->underlay, snapped(m_currentLineRect), m_theme.currentLine);
    for (const QRectF &r : std::as_const(m_matchRects))
        rect(node->underlay, snapped(r), m_theme.match);
    for (const QRectF &r : std::as_const(m_currentMatchRects))
        rect(node->underlay, snapped(r), m_theme.currentMatch);
    for (const QRectF &r : std::as_const(m_selectionRects))
        rect(node->underlay, snapped(r), m_theme.selection);
    for (const QRectF &r : std::as_const(m_bracketRects))
        rect(node->underlay, snapped(r), m_theme.bracket);
    for (const QRectF &r : std::as_const(m_whitespaceRects))
        rect(node->underlay, snapped(r), m_theme.whitespace);

    // Text: one node per segment in view, kept while its layout is unchanged.
    std::unordered_map<quint64, bool> seen;
    const qreal textX = qRound((left - m_contentX) * m_dpr) / m_dpr;
    for (const PlannedLine &planned : m_plan) {
        for (size_t index = 0; index < planned.cache->segments.size(); ++index) {
            const Segment &segment = planned.cache->segments[index];
            const qreal top = planned.top + segment.firstVisual * m_lineHeight;
            const qreal bottom = top + segment.layout->lineCount() * m_lineHeight;
            if (bottom < m_contentY || top > m_contentY + height())
                continue;
            const quint64 key = (quint64(planned.id) << 16) | quint64(index);
            seen.emplace(key, true);
            auto it = node->lines.find(key);
            QSGTextNode *textNode = nullptr;
            if (it != node->lines.end() && it->second.second == segment.version) {
                textNode = it->second.first;
            } else {
                if (it != node->lines.end()) {
                    textNode = it->second.first;
                    textNode->clear();
                } else {
                    textNode = win->createTextNode();
                    textNode->setRenderType(QSGTextNode::NativeRendering);
                    node->text->appendChildNode(textNode);
                }
                textNode->setColor(m_theme.text);
                textNode->addTextLayout(QPointF(0, 0), segment.layout.get());
                node->lines[key] = {textNode, segment.version};
            }
            QMatrix4x4 matrix;
            matrix.translate(textX, qRound((top - m_contentY) * m_dpr) / m_dpr);
            textNode->setMatrix(matrix);
        }
    }
    for (auto it = node->lines.begin(); it != node->lines.end();) {
        if (seen.find(it->first) == seen.end()) {
            delete it->second.first;
            it = node->lines.erase(it);
        } else {
            ++it;
        }
    }

    const bool showCaret = hasActiveFocus() && m_caretVisible && !m_caret.isNull() && win->isActive();
    if (showCaret)
        rect(node->overlay, snapped(m_caret), m_overwrite && m_caret.width() > 3 ? m_theme.selection : m_theme.text);
    if (!m_dropCaret.isNull())
        rect(node->overlay, snapped(m_dropCaret), m_theme.text);

    // Line numbers, right-aligned in the gutter; a node is remade when its
    // line becomes or stops being the current one.
    std::unordered_map<qint64, bool> numbersSeen;
    if (m_lineNumbers) {
        const qreal right = gutterWidth() - m_sidePadding;
        for (const PlannedLine &planned : m_plan) {
            auto layout = m_numberLayouts.find(planned.line);
            if (layout == m_numberLayouts.end())
                continue;
            const bool current = planned.line == m_currentLineNumber;
            const qint64 key = qint64(planned.line) * 2 + (current ? 1 : 0);
            numbersSeen.emplace(key, true);
            auto it = node->numbers.find(key);
            QSGTextNode *numberNode;
            if (it == node->numbers.end()) {
                numberNode = win->createTextNode();
                numberNode->setRenderType(QSGTextNode::NativeRendering);
                numberNode->setColor(current ? m_theme.text : m_theme.gutterText);
                numberNode->addTextLayout(QPointF(0, 0), layout->second.get());
                node->gutter->appendChildNode(numberNode);
                node->numbers.emplace(key, numberNode);
            } else {
                numberNode = it->second;
            }
            const qreal width = layout->second->lineAt(0).naturalTextWidth();
            QMatrix4x4 matrix;
            matrix.translate(qRound((right - width) * m_dpr) / m_dpr, qRound((planned.top - m_contentY) * m_dpr) / m_dpr);
            numberNode->setMatrix(matrix);
        }
    }
    for (auto it = node->numbers.begin(); it != node->numbers.end();) {
        if (numbersSeen.find(it->first) == numbersSeen.end()) {
            delete it->second;
            it = node->numbers.erase(it);
        } else {
            ++it;
        }
    }

    while (node->rects.size() > node->usedRects)
        delete node->rects.takeLast();
    return node;
}

void EditorView::releaseResources()
{
    m_themeDirty = true;
}

void EditorView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() != oldGeometry.width()) {
        if (m_wordWrap) {
            const qreal top = topLine();
            invalidateLayouts(true);
            scrollToLine(top);
        } else {
            polish();
        }
    } else if (newGeometry.height() != oldGeometry.height()) {
        polish();
    }
}

void EditorView::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange && value.window) {
        disconnect(m_windowConnection);
        m_windowConnection = connect(value.window, &QQuickWindow::activeChanged, this, [this] {
            resetBlink();
            update();
        });
        updateMetrics();
    }
    QQuickItem::itemChange(change, value);
}

void EditorView::resetBlink()
{
    m_caretVisible = true;
    m_blinkStarted.start();
    const int flash = QGuiApplication::styleHints()->cursorFlashTime();
    if (flash > 0 && hasActiveFocus())
        m_blinkTimer.start(flash / 2, this);
    else
        m_blinkTimer.stop();
    update();
}

void EditorView::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blinkTimer.timerId()) {
        if (m_blinkStarted.elapsed() > blinkDuration) {
            m_blinkTimer.stop();
            m_caretVisible = true;
        } else {
            m_caretVisible = !m_caretVisible;
        }
        update();
        return;
    }
    if (event->timerId() == m_autoScrollTimer.timerId()) {
        // Dragging a selection past an edge scrolls, faster the further out.
        qreal dy = 0;
        qreal dx = 0;
        if (m_lastMouse.y() < 0)
            dy = m_lastMouse.y();
        else if (m_lastMouse.y() > height())
            dy = m_lastMouse.y() - height();
        if (!m_wordWrap) {
            if (m_lastMouse.x() < textLeft())
                dx = m_lastMouse.x() - textLeft();
            else if (m_lastMouse.x() > width())
                dx = m_lastMouse.x() - width();
        }
        if (dx == 0 && dy == 0) {
            m_autoScrollTimer.stop();
            return;
        }
        applyScroll(m_contentX + dx / 2, m_contentY + dy / 2);
        selectByUnit(hitTest(m_lastMouse));
        return;
    }
    QQuickItem::timerEvent(event);
}

void EditorView::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    resetBlink();
}

void EditorView::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    m_blinkTimer.stop();
    update();
}

void EditorView::moveCursor(const TextPosition &position, bool select, bool keepPreferredX)
{
    if (!m_document)
        return;
    if (!keepPreferredX)
        m_preferredX = -1;
    m_document->setCursor(position, select ? m_document->anchor() : position);
    ensureCursorVisible();
}

TextPosition EditorView::moveVertically(const TextPosition &from, int lines)
{
    LineCache *cache;
    const Segment *segment;
    QTextLine textLine;
    int visual;
    if (!locate(from, &cache, &segment, &textLine, &visual))
        return from;
    if (m_preferredX < 0)
        m_preferredX = caretRect(from).x();
    ensureIndex();
    const qint64 target = m_index.before(from.line) + visual + lines;
    if (target < 0)
        return {0, 0};
    if (target >= m_index.total())
        return m_document->buffer().end();
    int offset = 0;
    const int line = m_index.lineAt(target, &offset);
    LineCache *targetCache = ensureLayout(line);
    for (const Segment &s : targetCache->segments) {
        const int count = s.layout->lineCount();
        if (offset >= s.firstVisual + count && &s != &targetCache->segments.back())
            continue;
        QTextLine targetLine = s.layout->lineAt(qBound(0, offset - s.firstVisual, count - 1));
        int column = targetLine.xToCursor(m_preferredX);
        if (column >= targetLine.textStart() + targetLine.textLength()
                && targetLine.textStart() + targetLine.textLength() < s.length)
            column = targetLine.textStart() + targetLine.textLength() - 1;
        return {line, s.start + column};
    }
    return {line, 0};
}

TextPosition EditorView::visualLineEdge(const TextPosition &from, bool end)
{
    LineCache *cache;
    const Segment *segment;
    QTextLine textLine;
    int visual;
    if (!locate(from, &cache, &segment, &textLine, &visual))
        return from;
    const QString &text = m_document->buffer().line(from.line);
    const int lineStart = segment->start + textLine.textStart();
    int lineEnd = lineStart + textLine.textLength();
    const bool lastVisual = segment == &cache->segments.back() && textLine.lineNumber() == segment->layout->lineCount() - 1;
    if (end) {
        // A wrapped line ends before the space it wrapped at.
        if (!lastVisual && lineEnd > lineStart && text.at(lineEnd - 1).isSpace())
            --lineEnd;
        return {from.line, lastVisual ? int(text.size()) : lineEnd};
    }
    if (lineStart > 0)
        return {from.line, lineStart};
    // Home goes to the first non-blank character, then to the start.
    int indent = 0;
    while (indent < text.size() && text.at(indent).isSpace())
        ++indent;
    return {from.line, from.column == indent ? 0 : indent};
}

void EditorView::ensureCursorVisible(bool center)
{
    if (!m_document || m_lines.empty() || height() <= 0)
        return;
    ensureIndex();
    const QRectF caret = caretRect(m_document->cursor());
    if (caret.isNull())
        return;
    qreal y = m_contentY;
    qreal x = m_contentX;
    const qreal margin = qMin(m_lineHeight * 2, height() / 4);
    // The cursor and, on its line, the rest of the selection; under the
    // floating panel they count as hidden.
    QRectF target = caret;
    const TextPosition anchor = m_document->anchor();
    if (anchor.line == m_document->cursor().line && anchor != m_document->cursor())
        target = target.united(caretRect(anchor));
    const bool underPanel = m_occludedHeight > 0
            && target.right() + textLeft() - m_contentX > m_occludedLeft;
    const qreal top = m_contentY + (underPanel ? m_occludedHeight : 0);
    if (center && (target.top() < top || target.bottom() > m_contentY + height())) {
        y = target.center().y() - height() / 3;
    } else if (target.top() - margin < top) {
        y = target.top() - margin - (underPanel ? m_occludedHeight : 0);
    } else if (target.bottom() + margin > m_contentY + height()) {
        y = target.bottom() + margin - height();
    }
    if (underPanel)
        y = qMax(y, -m_occludedHeight);
    else
        y = qMax<qreal>(y, 0);
    if (!m_wordWrap) {
        const qreal area = width() - textLeft() - m_sidePadding;
        const qreal hMargin = qMin(m_charWidth * 4, area / 4);
        if (caret.left() - hMargin < m_contentX)
            x = caret.left() - hMargin;
        else if (caret.right() + hMargin > m_contentX + area)
            x = caret.right() + hMargin - area;
    }
    // The content may have grown with the edit that moved the cursor.
    const qreal contentHeight = topPadding + m_index.total() * m_lineHeight + bottomPadding;
    if (contentHeight > m_contentHeight) {
        m_contentHeight = contentHeight;
        emit contentSizeChanged();
    }
    if (!m_wordWrap && caret.right() + textLeft() + m_sidePadding + m_charWidth > m_contentWidth) {
        m_contentWidth = caret.right() + textLeft() + m_sidePadding + m_charWidth;
        emit contentSizeChanged();
    }
    if (!qFuzzyCompare(y + 1, m_contentY + 1) || !qFuzzyCompare(x + 1, m_contentX + 1))
        applyScroll(qMax<qreal>(0, x), y);
}

QPoint EditorView::positionAt(qreal x, qreal y)
{
    const TextPosition p = hitTest(QPointF(x, y));
    return QPoint(p.column, p.line);
}

void EditorView::typeText(const QString &text)
{
    if (!m_document)
        return;
    m_preferredX = -1;
    m_document->insertText(text, m_overwrite);
}

void EditorView::cut()
{
    if (!m_document || m_document->isLocked())
        return;
    copy();
    if (m_document->hasSelection())
        m_document->deleteSelection();
    else
        m_document->deleteLines();
}

void EditorView::copy()
{
    if (!m_document)
        return;
    // Without a selection, the whole line.
    const QString text = m_document->hasSelection()
            ? m_document->selectedText()
            : m_document->buffer().line(m_document->cursor().line) + QLatin1Char('\n');
    QGuiApplication::clipboard()->setText(text);
}

void EditorView::paste()
{
    if (!m_document)
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.isEmpty())
        typeText(text);
}

void EditorView::updatePrimarySelection()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!m_document || !m_document->hasSelection() || !clipboard->supportsSelection())
        return;
    // A whole large file is not copied on every selection change.
    if (m_document->selectionLength() > 4 * 1024 * 1024)
        return;
    clipboard->setText(m_document->selectedText(), QClipboard::Selection);
}

static bool isNavigationKey(int key)
{
    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        return true;
    default:
        return false;
    }
}

bool EditorView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        // Keys that edit or move take precedence over application shortcuts.
        auto *key = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
        const bool ctrl = mods & Qt::ControlModifier;
        const bool alt = mods & Qt::AltModifier;
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo) || key->matches(QKeySequence::Cut)
                || key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Paste)
                || key->matches(QKeySequence::SelectAll)
                || (key->key() == Qt::Key_Z && mods == (Qt::ControlModifier | Qt::ShiftModifier))
                || (isNavigationKey(key->key()) && !(ctrl && (key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown))
                    && !(alt && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)))
                || key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete
                || ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) && !ctrl)
                || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Insert
                || (!ctrl && !alt && !key->text().isEmpty() && key->text().at(0).isPrint())) {
            event->accept();
            return true;
        }
    } else if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        static qreal pinch = 0;
        if (gesture->gestureType() == Qt::BeginNativeGesture)
            pinch = 0;
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            pinch += gesture->value();
            if (qAbs(pinch) > 0.15) {
                emit zoomRequested(pinch > 0 ? 1 : -1);
                pinch = 0;
            }
            event->accept();
            return true;
        }
    }
    return QQuickItem::event(event);
}

void EditorView::keyPressEvent(QKeyEvent *event)
{
    if (!m_document) {
        event->ignore();
        return;
    }
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool alt = mods & Qt::AltModifier;
    const TextPosition cursor = m_document->cursor();
    const TextRange selection = m_document->selection();
    const TextBuffer &buffer = m_document->buffer();
    resetBlink();

    if (event->matches(QKeySequence::Undo)) {
        m_document->undo();
    } else if (event->matches(QKeySequence::Redo)
               || (event->key() == Qt::Key_Z && mods == (Qt::ControlModifier | Qt::ShiftModifier))) {
        m_document->redo();
    } else if (event->matches(QKeySequence::Cut)) {
        cut();
    } else if (event->matches(QKeySequence::Copy)) {
        copy();
    } else if (event->matches(QKeySequence::Paste)) {
        paste();
    } else if (event->matches(QKeySequence::SelectAll)) {
        m_document->selectAll();
    } else {
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right: {
            const bool forward = event->key() == Qt::Key_Right;
            if (alt) {
                event->ignore();
                return;
            }
            if (!shift && !selection.isEmpty() && !ctrl) {
                moveCursor(forward ? selection.end : selection.start, false);
                break;
            }
            TextPosition target;
            if (ctrl) {
                target = m_document->wordBoundary(cursor, forward);
            } else if (forward) {
                const QString &line = buffer.line(cursor.line);
                if (cursor.column < line.size()) {
                    int column = cursor.column + 1;
                    if (column < line.size() && line.at(column).isLowSurrogate())
                        ++column;
                    target = {cursor.line, column};
                } else {
                    target = cursor.line + 1 < buffer.lineCount() ? TextPosition{cursor.line + 1, 0} : cursor;
                }
            } else {
                if (cursor.column > 0) {
                    const QString &line = buffer.line(cursor.line);
                    int column = cursor.column - 1;
                    if (column > 0 && line.at(column).isLowSurrogate())
                        --column;
                    target = {cursor.line, column};
                } else {
                    target = cursor.line > 0 ? TextPosition{cursor.line - 1, int(buffer.line(cursor.line - 1).size())}
                                             : cursor;
                }
            }
            moveCursor(target, shift);
            break;
        }
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const bool up = event->key() == Qt::Key_Up;
            if (alt && !shift && !ctrl) {
                m_document->moveLines(up);
                break;
            }
            if (ctrl) {
                // Scrolls by a line without moving the cursor.
                applyScroll(m_contentX, m_contentY + (up ? -m_lineHeight : m_lineHeight));
                break;
            }
            if (!shift && !selection.isEmpty()) {
                moveCursor(up ? selection.start : selection.end, false);
                break;
            }
            moveCursor(moveVertically(cursor, up ? -1 : 1), shift, true);
            break;
        }
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            if (ctrl) {
                event->ignore();
                return;
            }
            const int lines = qMax(1, int(height() / m_lineHeight) - 1);
            const qreal delta = (event->key() == Qt::Key_PageUp ? -lines : lines) * m_lineHeight;
            const TextPosition target = moveVertically(cursor, event->key() == Qt::Key_PageUp ? -lines : lines);
            applyScroll(m_contentX, m_contentY + delta);
            moveCursor(target, shift, true);
            break;
        }
        case Qt::Key_Home:
            moveCursor(ctrl ? TextPosition{0, 0} : visualLineEdge(cursor, false), shift);
            break;
        case Qt::Key_End:
            moveCursor(ctrl ? buffer.end() : visualLineEdge(cursor, true), shift);
            break;
        case Qt::Key_Backspace:
            m_preferredX = -1;
            m_document->deleteBackward(ctrl);
            break;
        case Qt::Key_Delete:
            m_preferredX = -1;
            if (shift && !ctrl && m_document->hasSelection())
                cut();
            else
                m_document->deleteForward(ctrl);
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            m_preferredX = -1;
            m_document->insertNewline(m_autoIndent);
            break;
        case Qt::Key_Tab:
        case Qt::Key_Backtab: {
            if (ctrl) {
                event->ignore();
                return;
            }
            const bool unindent = event->key() == Qt::Key_Backtab || shift;
            if (unindent || selection.start.line != selection.end.line) {
                m_document->indentSelection(unindent);
            } else if (m_document->insertSpaces()) {
                const int width = m_document->indentWidth();
                const int column = m_document->visualColumn() - 1;
                typeText(QString(width - column % width, QLatin1Char(' ')));
            } else {
                typeText(QStringLiteral("\t"));
            }
            break;
        }
        case Qt::Key_Insert:
            if (mods == Qt::NoModifier)
                setOverwriteMode(!m_overwrite);
            else if (mods == Qt::ShiftModifier)
                paste();
            else if (mods == Qt::ControlModifier)
                copy();
            break;
        default: {
            const QString text = event->text();
            if (!ctrl && !alt && !text.isEmpty() && text.at(0).isPrint()) {
                typeText(text);
                break;
            }
            event->ignore();
            return;
        }
        }
    }
    event->accept();
}

void EditorView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_document || m_document->isLocked()) {
        event->ignore();
        return;
    }
    if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
        if (event->replacementLength() > 0 && !m_document->hasSelection()) {
            const TextPosition cursor = m_document->cursor();
            const int start = qMax(0, cursor.column + event->replacementStart());
            m_document->setCursor({cursor.line, start + event->replacementLength()}, {cursor.line, start});
        }
        typeText(event->commitString());
    }
    const QString preedit = event->preeditString();
    m_preeditAttributes.clear();
    m_preeditCursor = int(preedit.size());
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
        if (attribute.type == QInputMethodEvent::Cursor)
            m_preeditCursor = attribute.start;
        else if (attribute.type == QInputMethodEvent::TextFormat)
            m_preeditAttributes.append(attribute);
    }
    if (preedit != m_preedit || !preedit.isEmpty()) {
        // Typing over a selection with an input method replaces it first.
        if (!preedit.isEmpty() && m_document->hasSelection())
            m_document->deleteSelection();
        m_preedit = preedit;
        const int line = m_document->cursor().line;
        if (line < int(m_lines.size())) {
            m_plan.clear();
            m_caches.erase(m_lines[line].id);
        }
        polish();
    }
    event->accept();
    emit cursorRectangleChanged();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (!m_document)
        return QQuickItem::inputMethodQuery(query);
    const TextPosition cursor = m_document->cursor();
    const TextPosition anchor = m_document->anchor();
    switch (query) {
    case Qt::ImEnabled:
        return !m_document->isLocked();
    case Qt::ImHints:
        return int(Qt::ImhMultiLine | Qt::ImhNoPredictiveText);
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle:
        return m_caret;
    case Qt::ImFont:
        return m_font;
    case Qt::ImCursorPosition:
        return cursor.column;
    case Qt::ImAnchorPosition:
        return anchor.line == cursor.line ? anchor.column : cursor.column;
    case Qt::ImSurroundingText:
        return m_document->buffer().line(cursor.line);
    case Qt::ImCurrentSelection:
        return m_document->selectedText();
    case Qt::ImReadOnly:
        return m_document->isLocked();
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void EditorView::selectByUnit(const TextPosition &position)
{
    if (!m_document)
        return;
    if (m_selectUnit == 0) {
        m_document->setCursor(position, m_document->anchor());
        return;
    }
    TextRange unit;
    if (m_selectUnit == 1) {
        unit = m_document->wordAt(position);
    } else {
        const int last = m_document->lineCount() - 1;
        unit = position.line < last ? TextRange{{position.line, 0}, {position.line + 1, 0}}
                                    : TextRange{{position.line, 0}, m_document->buffer().end()};
    }
    if (unit.start < m_unitAnchor.start)
        m_document->setCursor(unit.start, m_unitAnchor.end);
    else
        m_document->setCursor(qMax(unit.end, m_unitAnchor.end), m_unitAnchor.start);
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (!m_document) {
        event->ignore();
        return;
    }
    const QPointF pos = event->position();
    const TextPosition hit = hitTest(pos);
    m_preferredX = -1;

    if (event->button() == Qt::RightButton) {
        const TextRange selection = m_document->selection();
        if (selection.isEmpty() || hit < selection.start || hit > selection.end)
            m_document->setCursor(hit);
        emit contextMenuRequested(pos.x(), pos.y());
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard->supportsSelection() && !m_document->isLocked()) {
            const QString text = clipboard->text(QClipboard::Selection);
            if (!text.isEmpty()) {
                m_document->setCursor(hit);
                typeText(text);
            }
        }
        event->accept();
        return;
    }

    const int interval = QGuiApplication::styleHints()->mouseDoubleClickInterval();
    const qreal distance = QGuiApplication::styleHints()->mouseDoubleClickDistance();
    if (m_lastClick.isValid() && m_lastClick.elapsed() < interval
            && QLineF(pos, m_lastClickPos).length() <= distance)
        m_clickCount = m_clickCount % 3 + 1;
    else
        m_clickCount = 1;
    m_lastClick.start();
    m_lastClickPos = pos;
    m_pressPos = pos;
    m_lastMouse = pos;

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const TextRange selection = m_document->selection();
    const bool inGutter = m_lineNumbers && pos.x() < gutterWidth();

    if (inGutter) {
        m_selectUnit = 2;
        const int last = m_document->lineCount() - 1;
        m_unitAnchor = hit.line < last ? TextRange{{hit.line, 0}, {hit.line + 1, 0}}
                                       : TextRange{{hit.line, 0}, m_document->buffer().end()};
        if (shift && !selection.isEmpty())
            m_unitAnchor = {selection.start, selection.start};
        selectByUnit(hit);
        m_selecting = true;
    } else if (m_clickCount == 1 && !shift && !selection.isEmpty() && hit >= selection.start && hit <= selection.end
               && !m_document->isLocked()) {
        // Pressing on the selection may start dragging it.
        m_maybeDrag = true;
    } else if (m_clickCount == 1) {
        m_selectUnit = 0;
        m_document->setCursor(hit, shift ? m_document->anchor() : hit);
        m_selecting = true;
    } else {
        m_selectUnit = m_clickCount == 2 ? 1 : 2;
        selectByUnit(hit);
        const TextRange now = m_document->selection();
        m_unitAnchor = now;
        m_selecting = true;
    }
    event->accept();
}

void EditorView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Counted in mousePressEvent, which also sees triple clicks.
    mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    m_lastMouse = pos;
    if (m_maybeDrag) {
        if (QLineF(pos, m_pressPos).length() >= QGuiApplication::styleHints()->startDragDistance()) {
            m_maybeDrag = false;
            startTextDrag();
        }
        return;
    }
    if (!m_selecting)
        return;
    selectByUnit(hitTest(pos));
    ensureCursorVisible();
    if (pos.y() < 0 || pos.y() > height() || (!m_wordWrap && (pos.x() < textLeft() || pos.x() > width())))
        m_autoScrollTimer.start(30, this);
    else
        m_autoScrollTimer.stop();
}

void EditorView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_maybeDrag && m_document) {
        m_document->setCursor(hitTest(event->position()));
        m_maybeDrag = false;
    }
    m_selecting = false;
    m_autoScrollTimer.stop();
    updatePrimarySelection();
}

void EditorView::hoverMoveEvent(QHoverEvent *event)
{
    const bool gutter = m_lineNumbers && event->position().x() < gutterWidth();
    const Qt::CursorShape shape = gutter ? Qt::ArrowCursor : Qt::IBeamCursor;
    if (cursor().shape() != shape)
        setCursor(shape);
    QQuickItem::hoverMoveEvent(event);
}

void EditorView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            emit zoomRequested(delta > 0 ? 1 : -1);
        event->accept();
        return;
    }
    // The Flickable underneath scrolls.
    event->ignore();
}

void EditorView::startTextDrag()
{
    if (!m_document)
        return;
    const TextRange range = m_document->selection();
    auto *mime = new QMimeData;
    mime->setText(m_document->selectedText());
    QDrag drag(this);
    drag.setMimeData(mime);
    m_dragging = true;
    const Qt::DropAction action = drag.exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
    const bool moved = m_dragging && action == Qt::MoveAction;
    m_dragging = false;
    // Dropped into another view: this one gives the text up.
    if (moved && m_document && !m_document->isLocked()) {
        m_document->setCursor(range.end, range.start);
        m_document->deleteSelection();
    }
}

void EditorView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasText() && !event->mimeData()->hasUrls() && m_document && !m_document->isLocked()) {
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

void EditorView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    const TextPosition position = hitTest(event->position());
    const QRectF caret = caretRect(position);
    m_dropCaret = caret.isNull() ? QRectF() : caret.translated(textLeft() - m_contentX, -m_contentY);
    const bool copy = event->modifiers() & Qt::ControlModifier || event->source() != this;
    event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
    update();
}

void EditorView::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropCaret = QRectF();
    update();
}

void EditorView::dropEvent(QDropEvent *event)
{
    m_dropCaret = QRectF();
    update();
    if (!m_document)
        return;
    const QMimeData *mime = event->mimeData();
    if (mime->hasUrls()) {
        emit filesDropped(mime->urls());
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    if (!mime->hasText() || m_document->isLocked()) {
        event->ignore();
        return;
    }
    const QString text = mime->text();
    TextPosition position = hitTest(event->position());
    const TextBuffer &buffer = m_document->buffer();
    const bool fromSelf = event->source() == this && m_dragging;
    const bool move = fromSelf && !(event->modifiers() & Qt::ControlModifier);

    if (move) {
        const TextRange range = m_document->selection();
        if (position >= range.start && position <= range.end) {
            event->ignore();
            m_dragging = false;
            return;
        }
        const qint64 at = buffer.offset(position);
        const qint64 start = buffer.offset(range.start);
        const qint64 end = buffer.offset(range.end);
        m_document->beginEdit();
        if (at > end) {
            m_document->replace(range, QString(), UndoGroup::Other, {range.start, range.start});
            position = m_document->buffer().position(at - (end - start));
        } else {
            m_document->replace({position, position}, text, UndoGroup::Other, {position, position});
            const TextRange shifted{m_document->buffer().position(start + text.size()),
                                    m_document->buffer().position(end + text.size())};
            m_document->replace(shifted, QString(), UndoGroup::Other, {position, position});
            const TextPosition endAt = TextBuffer::endOf(position, text);
            m_document->setCursor(endAt, position);
            m_document->endEdit();
            m_dragging = false;
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
        const TextPosition endAt = TextBuffer::endOf(position, text);
        m_document->replace({position, position}, text, UndoGroup::Other, {endAt, position});
        m_document->endEdit();
        // Handled here, so the drag source does not delete it again.
        m_dragging = false;
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    const TextPosition endAt = TextBuffer::endOf(position, text);
    m_document->setCursor(position);
    m_document->replace({position, position}, text, UndoGroup::Other, {endAt, position});
    event->acceptProposedAction();
    forceActiveFocus();
}
