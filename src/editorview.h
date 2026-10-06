// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EDITORVIEW_H
#define EDITORVIEW_H

#include "document.h"
#include "syntaxtheme.h"

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QTextLayout>
#include <QTimer>

#include <memory>
#include <unordered_map>
#include <vector>

class QSGNode;
class QSGTextNode;

// Prefix sums of the visual line counts, for mapping between document lines
// and their vertical positions in O(log n).
class VisualLineIndex
{
public:
    void build(const std::vector<int> &counts);
    void add(int line, int delta);
    // Visual lines before `line`.
    qint64 before(int line) const;
    qint64 total() const { return m_total; }
    // The line containing visual line `visual`; `offset` is its index within it.
    int lineAt(qint64 visual, int *offset) const;
    int size() const { return m_size; }

private:
    std::vector<qint64> m_tree;
    int m_size = 0;
    qint64 m_total = 0;
};

// Draws a Document and edits it: only the lines in view are laid out and
// turned into scene graph nodes, so a file of any size scrolls and types as
// fast as a short one. Scrolling is driven from outside through contentX and
// contentY (a Flickable with FishUI's touchpad physics); the view asks for
// scroll changes with scrollRequested().
class EditorView : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(bool wordWrap READ wordWrap WRITE setWordWrap NOTIFY optionsChanged)
    Q_PROPERTY(bool lineNumbers READ lineNumbers WRITE setLineNumbers NOTIFY optionsChanged)
    Q_PROPERTY(bool highlightCurrentLine READ highlightCurrentLine WRITE setHighlightCurrentLine NOTIFY optionsChanged)
    Q_PROPERTY(bool showWhitespace READ showWhitespace WRITE setShowWhitespace NOTIFY optionsChanged)
    Q_PROPERTY(bool matchBrackets READ matchBrackets WRITE setMatchBrackets NOTIFY optionsChanged)
    Q_PROPERTY(bool autoIndent READ autoIndent WRITE setAutoIndent NOTIFY optionsChanged)
    Q_PROPERTY(bool overwriteMode READ overwriteMode WRITE setOverwriteMode NOTIFY overwriteModeChanged)
    Q_PROPERTY(QString colorScheme READ colorScheme WRITE setColorScheme NOTIFY themeChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY themeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(qreal sidePadding READ sidePadding WRITE setSidePadding NOTIFY optionsChanged)

    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY contentXChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight NOTIFY metricsChanged)
    Q_PROPERTY(qreal gutterWidth READ gutterWidth NOTIFY metricsChanged)
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle NOTIFY cursorRectangleChanged)
    // A panel floating over the top right of the view, from occludedLeft to the
    // right edge and occludedHeight down: the cursor is revealed clear of it,
    // scrolling above the first line when it has to.
    Q_PROPERTY(qreal occludedLeft MEMBER m_occludedLeft NOTIFY occlusionChanged)
    Q_PROPERTY(qreal occludedHeight MEMBER m_occludedHeight NOTIFY occlusionChanged)

public:
    explicit EditorView(QQuickItem *parent = nullptr);
    ~EditorView() override;

    Document *document() const { return m_document; }
    void setDocument(Document *document);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    bool wordWrap() const { return m_wordWrap; }
    void setWordWrap(bool wrap);
    bool lineNumbers() const { return m_lineNumbers; }
    void setLineNumbers(bool show);
    bool highlightCurrentLine() const { return m_highlightCurrentLine; }
    void setHighlightCurrentLine(bool highlight);
    bool showWhitespace() const { return m_showWhitespace; }
    void setShowWhitespace(bool show);
    bool matchBrackets() const { return m_matchBrackets; }
    void setMatchBrackets(bool match);
    bool autoIndent() const { return m_autoIndent; }
    void setAutoIndent(bool indent);
    bool overwriteMode() const { return m_overwrite; }
    void setOverwriteMode(bool overwrite);
    QString colorScheme() const { return m_scheme; }
    void setColorScheme(const QString &scheme);
    bool darkMode() const { return m_dark; }
    void setDarkMode(bool dark);
    QColor accentColor() const { return m_accent; }
    void setAccentColor(const QColor &color);
    QColor backgroundColor() const { return m_theme.background; }
    qreal sidePadding() const { return m_sidePadding; }
    void setSidePadding(qreal padding);

    qreal contentX() const { return m_contentX; }
    void setContentX(qreal x);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y);
    qreal contentWidth() const { return m_contentWidth; }
    qreal contentHeight() const { return m_contentHeight; }
    qreal lineHeight() const { return m_lineHeight; }
    qreal gutterWidth() const;
    QRectF cursorRectangle() const;

    Q_INVOKABLE void cut();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void ensureCursorVisible(bool center = false);
    // The text position under a point of this item, as {line, column}.
    Q_INVOKABLE QPoint positionAt(qreal x, qreal y);
    // Scrolls so `line` (from 0) is at the top, e.g. when a session is restored.
    Q_INVOKABLE void scrollToLine(qreal line);
    Q_INVOKABLE qreal topLine() const;

signals:
    void documentChanged();
    void fontChanged();
    void optionsChanged();
    void overwriteModeChanged();
    void themeChanged();
    void contentXChanged();
    void contentYChanged();
    void contentSizeChanged();
    void metricsChanged();
    void cursorRectangleChanged();
    void scrollRequested(qreal x, qreal y);
    void contextMenuRequested(qreal x, qreal y);
    void zoomRequested(int step);
    void filesDropped(const QList<QUrl> &urls);
    void occlusionChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void releaseResources() override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    struct Segment
    {
        int start = 0;
        int length = 0;
        int firstVisual = 0;
        // Kept when the segment's layout is reused, so its node is too.
        quint64 version = 0;
        std::unique_ptr<QTextLayout> layout;
    };

    // The laid-out form of a line in or near view.
    struct LineCache
    {
        std::vector<Segment> segments;
        int visualLines = 1;
        qreal width = 0;
        bool provisionalSyntax = false;
        // No colours or input method text: segments can be reused as they are.
        bool plain = true;
        // Bumped whenever the layouts are rebuilt, so nodes know to refresh.
        quint64 version = 0;
        int preeditColumn = -1;
    };

    struct LineInfo
    {
        quint32 id = 0;
        int visualLines = 1;
        int columns = 0;
        bool exact = false;
    };

    // One frame's worth of what to draw, computed in polish.
    struct PlannedLine
    {
        int line;
        quint32 id;
        qreal top;
        LineCache *cache;
    };

    void onLinesChanged(int line, int removed, int added);
    void onSyntaxChanged(int first, int last);
    void onCursorChanged();
    void invalidateLayouts(bool estimates);
    void updateMetrics();
    void updateTheme();

    int estimateVisualLines(const QString &text, int columns) const;
    int displayColumns(const QString &text) const;
    qreal wrapWidth() const;
    qreal textLeft() const;
    LineCache *ensureLayout(int line);
    void buildLayout(int line, LineCache *cache, LineCache *previous = nullptr);
    void setLineVisualCount(int line, int count);
    void ensureIndex();
    void recountWidth();
    void dropFarCaches(int first, int last);

    qreal lineTop(int line) const;
    // Where the visual line containing `position` is, and its QTextLine.
    bool locate(const TextPosition &position, LineCache **cache, const Segment **segment, QTextLine *textLine,
                int *visual);
    QRectF caretRect(const TextPosition &position);
    TextPosition hitTest(const QPointF &point);
    QList<QRectF> rangeRects(int line, LineCache *cache, int from, int to, bool toLineEnd);
    void addWhitespaceMarks(int line, LineCache *cache);

    void moveCursor(const TextPosition &position, bool select, bool keepPreferredX = false);
    TextPosition moveVertically(const TextPosition &from, int lines);
    TextPosition visualLineEdge(const TextPosition &from, bool end);
    void selectByUnit(const TextPosition &position);
    void startAutoScroll();
    void updatePrimarySelection();
    void resetBlink();
    void applyScroll(qreal x, qreal y);
    void typeText(const QString &text);
    void startTextDrag();

    QPointer<Document> m_document;
    QFont m_font;
    bool m_wordWrap = true;
    bool m_lineNumbers = true;
    bool m_highlightCurrentLine = false;
    bool m_showWhitespace = false;
    bool m_matchBrackets = true;
    bool m_autoIndent = true;
    bool m_overwrite = false;
    QString m_scheme = QStringLiteral("default");
    bool m_dark = false;
    QColor m_accent = QColor(0x3385FF);
    SyntaxTheme m_theme;
    QList<QTextCharFormat> m_formats;
    qreal m_sidePadding = 12;
    qreal m_occludedLeft = 0;
    qreal m_occludedHeight = 0;

    qreal m_contentX = 0;
    qreal m_contentY = 0;
    qreal m_contentWidth = 0;
    qreal m_contentHeight = 0;

    qreal m_dpr = 1;
    qreal m_lineHeight = 20;
    qreal m_charWidth = 8;
    qreal m_digitWidth = 8;
    qreal m_ascent = 14;
    qreal m_lineInset = 3;
    int m_maxColumns = 0;
    bool m_maxColumnsDirty = false;
    qreal m_widestLaidOut = 0;

    std::vector<LineInfo> m_lines;
    quint32 m_nextId = 1;
    VisualLineIndex m_index;
    bool m_indexDirty = true;
    std::unordered_map<quint32, std::unique_ptr<LineCache>> m_caches;
    // Long lines just edited: their unchanged segments are reused.
    std::unordered_map<quint32, std::unique_ptr<LineCache>> m_staleCaches;
    quint64 m_nextVersion = 1;
    QTimer m_exactTimer;
    int m_exactCursor = 0;

    std::vector<PlannedLine> m_plan;
    std::unordered_map<int, std::unique_ptr<QTextLayout>> m_numberLayouts;
    QList<QRectF> m_selectionRects;
    QList<QRectF> m_matchRects;
    QList<QRectF> m_currentMatchRects;
    QList<QRectF> m_bracketRects;
    QList<QRectF> m_whitespaceRects;
    QRectF m_currentLineRect;
    QRectF m_caret;
    QRectF m_dropCaret;
    int m_currentLineNumber = -1;
    bool m_themeDirty = true;

    // The x the cursor keeps while moving up and down.
    qreal m_preferredX = -1;
    QString m_preedit;
    QList<QInputMethodEvent::Attribute> m_preeditAttributes;
    int m_preeditCursor = 0;

    bool m_caretVisible = true;
    QBasicTimer m_blinkTimer;
    QElapsedTimer m_blinkStarted;
    QBasicTimer m_autoScrollTimer;
    QPointF m_lastMouse;
    bool m_selecting = false;
    int m_selectUnit = 0;
    TextRange m_unitAnchor;
    QElapsedTimer m_lastClick;
    QPointF m_lastClickPos;
    int m_clickCount = 0;
    bool m_maybeDrag = false;
    QPointF m_pressPos;
    bool m_dragging = false;
    QTimer m_primaryTimer;
    QMetaObject::Connection m_windowConnection;
};

#endif
