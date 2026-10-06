// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "document.h"
#include "fileoptions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

struct LoadResult
{
    DecodedText decoded;
    int error = Document::NoNotice;
    QString errorString;
    QDateTime modified;
    qint64 size = 0;
    bool cancelled = false;
};

static std::function<void(const QString &)> s_recentFilesHook;

void Document::setRecentFilesHook(std::function<void(const QString &)> hook)
{
    s_recentFilesHook = std::move(hook);
}

static LoadResult readFile(const QString &path, const QByteArray &encoding,
                           std::shared_ptr<std::atomic<int>> progress, std::shared_ptr<std::atomic<bool>> cancel)
{
    LoadResult result;
    QFile file(path);
    const QFileInfo info(path);
    if (!info.exists()) {
        result.error = Document::LoadFailed;
        result.errorString = QCoreApplication::translate("Document", "The file does not exist.");
        return result;
    }
    if (info.isDir()) {
        result.error = Document::LoadFailed;
        result.errorString = QCoreApplication::translate("Document", "This is a folder.");
        return result;
    }
    if (info.size() > Document::maximumFileSize) {
        result.error = Document::TooLarge;
        return result;
    }
    errno = 0;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.error() == QFileDevice::PermissionsError || errno == EACCES
                ? Document::PermissionDenied : Document::LoadFailed;
        result.errorString = file.errorString();
        return result;
    }
    result.modified = info.lastModified();
    result.size = info.size();

    QByteArray data;
    const qint64 expected = qMax<qint64>(1, info.size());
    data.reserve(info.size());
    static constexpr qint64 chunk = 1024 * 1024;
    // Sizes of special files (e.g. in /proc) are 0; they are read to the end anyway.
    while (true) {
        if (cancel->load()) {
            result.cancelled = true;
            return result;
        }
        const QByteArray piece = file.read(chunk);
        if (piece.isEmpty())
            break;
        data += piece;
        if (data.size() > Document::maximumFileSize) {
            result.error = Document::TooLarge;
            return result;
        }
        progress->store(int(qMin<qint64>(900, data.size() * 900 / expected)));
    }
    if (file.error() != QFileDevice::NoError) {
        result.error = Document::LoadFailed;
        result.errorString = file.errorString();
        return result;
    }

    result.decoded = TextCodec::decode(data, encoding);
    progress->store(1000);
    return result;
}

void SyntaxStates::reset(const Language *language, int lineCount)
{
    m_language = language;
    m_end = QList<Syntax::State>(lineCount, 0);
    m_valid = 0;
    m_changedEnd = -1;
    m_resume = -1;
}

void SyntaxStates::linesChanged(int line, int removed, int added)
{
    const int oldValid = m_valid;
    if (removed > added)
        m_end.remove(line + added, removed - added);
    else if (added > removed)
        m_end.insert(line + removed, added - removed, 0);
    if (line >= oldValid)
        return;

    m_valid = line;
    m_changedEnd = line + added - 1;
    const int resume = oldValid >= line + removed ? oldValid + added - removed : -1;
    m_resume = qMax(m_resume >= line + removed ? m_resume + added - removed : -1, resume);
}

Syntax::State SyntaxStates::before(int line) const
{
    if (line <= 0 || line - 1 >= m_end.size())
        return 0;
    return m_end.at(line - 1);
}

int SyntaxStates::advance(const TextBuffer &buffer, int line, qint64 budgetMs, int *last)
{
    QElapsedTimer timer;
    timer.start();
    const int target = qMin(line, buffer.lineCount() - 1);
    int first = -1;
    QList<SyntaxSpan> spans;
    while (m_valid <= target) {
        const int i = m_valid;
        const QString &text = buffer.line(i);
        spans.clear();
        const Syntax::State entering = before(i);
        const Syntax::State state = text.size() > Document::maximumHighlightedLine
                ? entering : Syntax::highlight(m_language, text, entering, &spans);
        const Syntax::State old = m_end.at(i);
        m_end[i] = state;
        m_valid = i + 1;
        if (state != old && i + 1 < buffer.lineCount()) {
            if (first < 0)
                first = i + 1;
            if (last)
                *last = i + 1;
        }
        if (m_resume > 0 && i >= m_changedEnd && state == old) {
            m_valid = qMin<int>(m_resume, int(m_end.size()));
            m_resume = -1;
        }
        if ((i & 63) == 0 && timer.elapsed() >= budgetMs)
            break;
    }
    return first;
}

Document::Document(QObject *parent, const QString &draftId)
    : QObject(parent)
    , m_draftId(draftId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : draftId)
{
    m_states.reset(nullptr, 1);

    m_highlightTimer.setInterval(0);
    m_highlightTimer.setSingleShot(true);
    connect(&m_highlightTimer, &QTimer::timeout, this, &Document::highlightSlice);

    // Drafts are written a moment after typing stops, never to the file itself.
    m_draftTimer.setInterval(3000);
    m_draftTimer.setSingleShot(true);
    connect(&m_draftTimer, &QTimer::timeout, this, &Document::writeDraftAsync);

    m_diskCheckTimer.setInterval(200);
    m_diskCheckTimer.setSingleShot(true);
    connect(&m_diskCheckTimer, &QTimer::timeout, this, &Document::checkDisk);

    m_matchTimer.setInterval(60);
    m_matchTimer.setSingleShot(true);
    connect(&m_matchTimer, &QTimer::timeout, this, &Document::computeMatches);

    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        if (!m_loadProgressShared)
            return;
        const qreal progress = m_loadProgressShared->load() / 1000.0;
        if (!qFuzzyCompare(progress, m_loadProgress)) {
            m_loadProgress = progress;
            emit loadProgressChanged();
        }
    });
}

Document::~Document()
{
    if (m_loadCancel)
        m_loadCancel->store(true);
    m_draftWrite.waitForFinished();
}

QString Document::draftDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1String("/cutefish-text-editor/drafts");
}

void Document::setDraftId(const QString &id)
{
    if (id.isEmpty() || id == m_draftId)
        return;
    m_draftId = id;
    emit draftIdChanged();
}

QString Document::draftPath() const
{
    return draftDirectory() + QLatin1Char('/') + m_draftId + QLatin1String(".txt");
}

QString Document::title() const
{
    if (!m_filePath.isEmpty())
        return fileName();
    // Named after its first line until it is saved.
    const QString first = QStringView(m_buffer.line(0)).left(200).toString().simplified();
    if (first.isEmpty())
        return tr("Untitled");
    return first.size() > 40 ? first.left(39) + QStringLiteral("…") : first;
}

QString Document::fileName() const
{
    return QFileInfo(m_filePath).fileName();
}

QString Document::folder() const
{
    if (m_filePath.isEmpty())
        return QString();
    QString dir = QFileInfo(m_filePath).absolutePath();
    const QString home = QDir::homePath();
    if (dir == home || dir.startsWith(home + QLatin1Char('/')))
        dir.replace(0, home.size(), QStringLiteral("~"));
    return dir;
}

void Document::setFilePath(const QString &path)
{
    const QString absolute = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    if (m_filePath == absolute)
        return;
    m_filePath = absolute;
    watchFile();
    emit filePathChanged();
    emit titleChanged();
}

void Document::watchFile()
{
    delete m_watcher;
    m_watcher = nullptr;
    if (m_filePath.isEmpty())
        return;
    m_watcher = new QFileSystemWatcher(this);
    // The folder too: an editor that saves by renaming a new file over the
    // old one leaves a watch on the old file behind.
    m_watcher->addPath(QFileInfo(m_filePath).absolutePath());
    if (QFileInfo::exists(m_filePath))
        m_watcher->addPath(m_filePath);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_diskCheckTimer.start(); });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_diskCheckTimer.start(); });
}

void Document::updateWritable()
{
    bool writable = true;
    if (!m_filePath.isEmpty()) {
        const QFileInfo info(m_filePath);
        writable = info.exists() ? info.isWritable() : QFileInfo(info.absolutePath()).isWritable();
    }
    if (writable != m_writable) {
        m_writable = writable;
        emit writableChanged();
    }
}

void Document::load(const QString &path, const QString &encoding)
{
    cancelLoading();
    setFilePath(path);
    m_loading = true;
    m_loadProgress = 0;
    emit loadingChanged();
    emit loadProgressChanged();

    m_loadProgressShared = std::make_shared<std::atomic<int>>(0);
    m_loadCancel = std::make_shared<std::atomic<bool>>(false);
    m_loadWatcher = new QFutureWatcher<LoadResult>(this);
    QFutureWatcher<LoadResult> *watcher = m_loadWatcher;
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher != m_loadWatcher)
            return;
        m_loadWatcher = nullptr;
        const LoadResult result = watcher->result();
        if (!result.cancelled)
            finishLoad(result);
    });
    watcher->setFuture(QtConcurrent::run(readFile, m_filePath, encoding.toLatin1(), m_loadProgressShared,
                                         m_loadCancel));
    m_progressTimer.start();
}

bool Document::waitForLoaded(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (m_loading && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_loading;
}

void Document::cancelLoading()
{
    if (!m_loading)
        return;
    if (m_loadCancel)
        m_loadCancel->store(true);
    m_loadWatcher = nullptr;
    m_progressTimer.stop();
    m_loading = false;
    m_reloading = false;
    emit loadingChanged();
}

void Document::reload()
{
    if (m_filePath.isEmpty())
        return;
    m_reloading = true;
    m_reloadCursor = m_cursor;
    load(m_filePath, QString::fromLatin1(m_encoding));
}

void Document::reloadWithEncoding(const QString &encoding)
{
    if (m_filePath.isEmpty()) {
        setEncoding(encoding);
        return;
    }
    m_reloading = true;
    m_reloadCursor = m_cursor;
    load(m_filePath, encoding);
}

void Document::finishLoad(const LoadResult &result)
{
    m_progressTimer.stop();
    m_loading = false;
    const bool reloading = m_reloading;
    m_reloading = false;

    if (result.error != NoNotice) {
        emit loadingChanged();
        // Unsaved text of a file that can no longer be read still comes back.
        if (m_restoreDraftAfterLoad) {
            m_restoreDraftAfterLoad = false;
            if (restoreDraft()) {
                m_diskDiffers = true;
                updateModified();
            }
        }
        setNotice(result.error == LoadFailed && !QFileInfo::exists(m_filePath) && m_modified ? Deleted : Notice(result.error),
                  result.errorString);
        return;
    }

    const int oldCount = m_buffer.lineCount();
    const DecodedText &decoded = result.decoded;
    m_buffer.setLines(decoded.lines);
    // Views follow the buffer before anything else can make them look at it.
    m_states.reset(nullptr, m_buffer.lineCount());
    m_cursor = m_anchor = reloading ? m_buffer.clamp(m_reloadCursor) : TextPosition();
    emit linesChanged(0, oldCount, m_buffer.lineCount());
    m_undo.clear();
    m_formatModified = false;
    m_diskDiffers = false;
    m_ignoreDisk = false;
    m_diskModified = result.modified;
    m_diskSize = result.size;
    m_encoding = decoded.encoding;
    m_bom = decoded.bom;
    m_lineEnding = decoded.lineEnding;

    const bool large = result.size > largeFileSize;
    if (large != m_large) {
        m_large = large;
        emit largeChanged();
    }
    m_highlightingEnabled = !large;
    if (!m_languageChosen)
        m_language = Syntax::detect(m_filePath, m_buffer.line(0));
    m_states.reset(Syntax::language(m_language), m_buffer.lineCount());

    if (!m_indentationFromFile || !reloading)
        applyEditorConfig();

    if (m_locked != decoded.binary) {
        m_locked = decoded.binary;
        emit lockedChanged();
    }

    updateWritable();
    watchFile();

    emit loadingChanged();
    emit encodingChanged();
    emit lineEndingChanged();
    emit languageChanged();
    emit titleChanged();
    emit undoChanged();
    emit cursorChanged();
    emit contentsChanged();
    updateModified();
    updateMatches();

    m_malformed = decoded.malformed;
    m_loadedEncoding = decoded.encoding;
    if (decoded.binary)
        setNotice(Binary);
    else if (decoded.malformed)
        setNotice(Malformed, encoding());
    else if (large && m_states.language())
        setNotice(LargeFile);
    else if (!m_writable)
        setNotice(ReadOnly);
    else if (decoded.mixedLineEndings)
        setNotice(MixedLineEndings);
    else
        setNotice(NoNotice);

    if (m_restoreDraftAfterLoad) {
        m_restoreDraftAfterLoad = false;
        restoreDraft();
    } else if (!reloading) {
        removeDraft();
    }

    if (!reloading && s_recentFilesHook)
        s_recentFilesHook(m_filePath);
    emit loaded();
}

void Document::applyEditorConfig()
{
    const EditorConfig config = m_filePath.isEmpty() ? EditorConfig() : EditorConfig::forFile(m_filePath);
    m_indentationFromFile = false;
    if (config.insertSpaces || config.indentSize) {
        m_insertSpaces = config.insertSpaces.value_or(m_insertSpaces);
        m_indentWidth = config.indentSize.value_or(m_indentWidth);
        m_tabWidth = config.tabWidth.value_or(m_insertSpaces ? m_tabWidth : m_indentWidth);
        m_indentationFromFile = true;
    } else if (const auto detected = FileOptions::detectIndentation(m_buffer.lines())) {
        m_insertSpaces = detected->insertSpaces;
        if (detected->insertSpaces)
            m_indentWidth = detected->width;
        else
            m_indentWidth = m_tabWidth;
        m_indentationFromFile = true;
    }
    m_trimTrailingWhitespace = config.trimTrailingWhitespace.value_or(false);
    m_finalNewline = config.insertFinalNewline;
    emit indentationChanged();
}

void Document::setIndentationDefaults(bool insertSpaces, int width)
{
    if (m_indentationFromFile)
        return;
    m_insertSpaces = insertSpaces;
    m_tabWidth = width;
    m_indentWidth = width;
    emit indentationChanged();
}

void Document::setNotice(Notice notice, const QString &detail)
{
    if (m_notice == notice && m_noticeDetail == detail)
        return;
    m_notice = notice;
    m_noticeDetail = detail;
    emit noticeChanged();
}

void Document::dismissNotice()
{
    setNotice(NoNotice);
}

void Document::keepLocalVersion()
{
    // The disk copy is no longer what the buffer started from.
    m_ignoreDisk = true;
    m_diskDiffers = true;
    const QFileInfo info(m_filePath);
    m_diskModified = info.lastModified();
    m_diskSize = info.size();
    updateModified();
    setNotice(NoNotice);
}

void Document::setLocked(bool locked)
{
    if (m_locked == locked)
        return;
    m_locked = locked;
    emit lockedChanged();
    if (!locked && m_notice == Binary)
        setNotice(NoNotice);
}

void Document::updateModified()
{
    const bool modified = !m_undo.isClean() || m_formatModified || m_diskDiffers;
    if (modified == m_modified)
        return;
    m_modified = modified;
    emit modifiedChanged();
}

void Document::setEncoding(const QString &encoding)
{
    const QByteArray name = encoding.toLatin1();
    if (name == m_encoding || !TextCodec::isSupported(name))
        return;
    m_encoding = name;
    if (!m_encoding.startsWith("UTF-"))
        m_bom = false;
    m_formatModified = true;
    emit encodingChanged();
    updateModified();
    scheduleDraft();
}

void Document::setBom(bool bom)
{
    if (m_bom == bom || (bom && !m_encoding.startsWith("UTF-")))
        return;
    m_bom = bom;
    m_formatModified = true;
    emit encodingChanged();
    updateModified();
}

void Document::setLineEnding(int lineEnding)
{
    if (lineEnding < 0 || lineEnding > int(LineEnding::ClassicMac) || LineEnding(lineEnding) == m_lineEnding)
        return;
    m_lineEnding = LineEnding(lineEnding);
    m_formatModified = true;
    emit lineEndingChanged();
    updateModified();
    if (m_notice == MixedLineEndings)
        setNotice(NoNotice);
}

QString Document::languageName() const
{
    return Syntax::name(m_language);
}

void Document::setLanguage(const QString &language)
{
    m_languageChosen = true;
    const bool enabling = !m_highlightingEnabled;
    m_highlightingEnabled = true;
    if (m_language == language && !enabling)
        return;
    m_language = language;
    m_states.reset(Syntax::language(language), m_buffer.lineCount());
    emit languageChanged();
    emit syntaxChanged(0, m_buffer.lineCount() - 1);
    if (m_notice == LargeFile)
        setNotice(NoNotice);
}

void Document::detectLanguage()
{
    if (m_languageChosen)
        return;
    const QString detected = Syntax::detect(m_filePath, m_buffer.line(0));
    if (detected == m_language)
        return;
    m_language = detected;
    m_states.reset(Syntax::language(detected), m_buffer.lineCount());
    emit languageChanged();
    emit syntaxChanged(0, m_buffer.lineCount() - 1);
}

void Document::setInsertSpaces(bool insertSpaces)
{
    if (m_insertSpaces == insertSpaces)
        return;
    m_insertSpaces = insertSpaces;
    m_indentationFromFile = true;
    emit indentationChanged();
}

void Document::setTabWidth(int width)
{
    width = qBound(1, width, 16);
    if (m_tabWidth == width)
        return;
    m_tabWidth = width;
    m_indentationFromFile = true;
    emit indentationChanged();
}

void Document::setIndentWidth(int width)
{
    width = qBound(1, width, 16);
    if (m_indentWidth == width)
        return;
    m_indentWidth = width;
    m_indentationFromFile = true;
    emit indentationChanged();
}

void Document::requestHighlighting(int line)
{
    if (!isHighlighting())
        return;
    m_highlightTarget = qMin(line, m_buffer.lineCount() - 1);
    if (m_states.validLines() <= m_highlightTarget && !m_highlightTimer.isActive())
        m_highlightTimer.start();
}

void Document::highlightSlice()
{
    if (!isHighlighting())
        return;
    int last = -1;
    const int first = m_states.advance(m_buffer, m_highlightTarget, 6, &last);
    if (first >= 0)
        emit syntaxChanged(first, last);
    if (m_states.validLines() <= m_highlightTarget)
        m_highlightTimer.start();
}

void Document::beginEdit()
{
    m_undo.begin({m_cursor, m_anchor});
}

void Document::endEdit()
{
    m_undo.end({m_cursor, m_anchor});
    emit undoChanged();
}

void Document::applyStep(const EditStep &step, bool reverse)
{
    const QString &remove = reverse ? step.inserted : step.removed;
    const QString &insert = reverse ? step.removed : step.inserted;
    const TextRange range{step.at, TextBuffer::endOf(step.at, remove)};
    m_buffer.remove(range);
    const TextPosition end = m_buffer.insert(step.at, insert);
    const int removedLines = range.end.line - range.start.line + 1;
    const int addedLines = end.line - step.at.line + 1;
    m_states.linesChanged(step.at.line, removedLines, addedLines);
    emit linesChanged(step.at.line, removedLines, addedLines);
    if (step.at.line == 0) {
        emit titleChanged();
        if (m_filePath.isEmpty())
            detectLanguage();
    }
}

void Document::replace(const TextRange &range, const QString &text, UndoGroup::Kind kind, const Selection &after)
{
    if (m_locked || m_loading)
        return;
    const TextRange r{m_buffer.clamp(range.normalized().start), m_buffer.clamp(range.normalized().end)};
    const QString removed = m_buffer.text(r);
    if (removed.isEmpty() && text.isEmpty()) {
        setCursor(after.cursor, after.anchor);
        return;
    }
    const EditStep step{r.start, removed, text};
    m_undo.record(step, kind, {m_cursor, m_anchor}, after, QDateTime::currentMSecsSinceEpoch());
    applyStep(step, false);

    m_cursor = m_buffer.clamp(after.cursor);
    m_anchor = m_buffer.clamp(after.anchor);
    emit cursorChanged();
    emit undoChanged();
    emit contentsChanged();
    updateModified();
    scheduleDraft();
    updateMatches();
    emit revealCursor();
}

void Document::undo()
{
    if (!m_undo.canUndo() || m_locked)
        return;
    const UndoGroup group = m_undo.undo();
    for (auto it = group.steps.crbegin(); it != group.steps.crend(); ++it)
        applyStep(*it, true);
    m_cursor = m_buffer.clamp(group.before.cursor);
    m_anchor = m_buffer.clamp(group.before.anchor);
    emit cursorChanged();
    emit undoChanged();
    emit contentsChanged();
    updateModified();
    scheduleDraft();
    updateMatches();
    emit revealCursor();
}

void Document::redo()
{
    if (!m_undo.canRedo() || m_locked)
        return;
    const UndoGroup group = m_undo.redo();
    for (const EditStep &step : group.steps)
        applyStep(step, false);
    m_cursor = m_buffer.clamp(group.after.cursor);
    m_anchor = m_buffer.clamp(group.after.anchor);
    emit cursorChanged();
    emit undoChanged();
    emit contentsChanged();
    updateModified();
    scheduleDraft();
    updateMatches();
    emit revealCursor();
}

void Document::setText(const QString &text)
{
    const TextPosition end = TextBuffer::endOf({0, 0}, text);
    replace({{0, 0}, m_buffer.end()}, text, UndoGroup::Other, {end, end});
}

void Document::setCursor(const TextPosition &cursor, const TextPosition &anchor)
{
    const TextPosition c = m_buffer.clamp(cursor);
    const TextPosition a = m_buffer.clamp(anchor);
    if (c == m_cursor && a == m_anchor)
        return;
    m_cursor = c;
    m_anchor = a;
    m_undo.seal();
    emit cursorChanged();
    updateMatches();
}

int Document::visualColumn() const
{
    const QString &line = m_buffer.line(m_cursor.line);
    int column = 0;
    for (int i = 0; i < m_cursor.column && i < line.size(); ++i) {
        if (line.at(i) == QLatin1Char('\t'))
            column += m_tabWidth - column % m_tabWidth;
        else if (!line.at(i).isLowSurrogate())
            ++column;
    }
    return column + 1;
}

int Document::selectionLength() const
{
    if (!hasSelection())
        return 0;
    const TextRange r = selection();
    return int(m_buffer.offset(r.end) - m_buffer.offset(r.start));
}

QString Document::selectedText() const
{
    return m_buffer.text(selection());
}

void Document::selectAll()
{
    setCursor(m_buffer.end(), {0, 0});
}

void Document::goToLine(int line, int column)
{
    const int index = qBound(0, line - 1, m_buffer.lineCount() - 1);
    setCursor({index, qMax(0, column - 1)});
    emit revealCursor();
}

void Document::insertText(const QString &input, bool overwrite)
{
    QString text = input;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    TextRange range = selection();
    if (overwrite && range.isEmpty() && !text.contains(QLatin1Char('\n'))) {
        const QString &line = m_buffer.line(m_cursor.line);
        range.end.column = qMin<int>(line.size(), m_cursor.column + int(text.size()));
    }
    const TextPosition end = TextBuffer::endOf(range.start, text);
    const bool typed = text.size() <= 2 && !text.contains(QLatin1Char('\n'));
    replace(range, text, typed ? UndoGroup::Typing : UndoGroup::Other, {end, end});
}

void Document::insertNewline(bool autoIndent)
{
    QString text = QStringLiteral("\n");
    if (autoIndent) {
        const QString &line = m_buffer.line(selection().start.line);
        int indent = 0;
        while (indent < line.size() && indent < selection().start.column
               && (line.at(indent) == QLatin1Char(' ') || line.at(indent) == QLatin1Char('\t')))
            ++indent;
        text += line.left(indent);
    }
    const TextRange range = selection();
    const TextPosition end = TextBuffer::endOf(range.start, text);
    replace(range, text, UndoGroup::Other, {end, end});
}

static bool isWordCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

TextPosition Document::wordBoundary(const TextPosition &from, bool forward) const
{
    TextPosition p = m_buffer.clamp(from);
    const QString &line = m_buffer.line(p.line);
    if (forward) {
        if (p.column >= line.size())
            return p.line + 1 < m_buffer.lineCount() ? TextPosition{p.line + 1, 0} : p;
        int i = p.column;
        while (i < line.size() && line.at(i).isSpace())
            ++i;
        if (i < line.size() && isWordCharacter(line.at(i))) {
            while (i < line.size() && isWordCharacter(line.at(i)))
                ++i;
        } else {
            while (i < line.size() && !isWordCharacter(line.at(i)) && !line.at(i).isSpace())
                ++i;
        }
        return {p.line, i};
    }

    if (p.column == 0)
        return p.line > 0 ? TextPosition{p.line - 1, int(m_buffer.line(p.line - 1).size())} : p;
    int i = p.column;
    while (i > 0 && line.at(i - 1).isSpace())
        --i;
    if (i > 0 && isWordCharacter(line.at(i - 1))) {
        while (i > 0 && isWordCharacter(line.at(i - 1)))
            --i;
    } else {
        while (i > 0 && !isWordCharacter(line.at(i - 1)) && !line.at(i - 1).isSpace())
            --i;
    }
    return {p.line, i};
}

TextRange Document::wordAt(const TextPosition &position) const
{
    const TextPosition p = m_buffer.clamp(position);
    const QString &line = m_buffer.line(p.line);
    int start = p.column;
    int end = p.column;
    auto same = [&](QChar c, QChar reference) {
        if (isWordCharacter(reference))
            return isWordCharacter(c);
        if (reference.isSpace())
            return c.isSpace();
        return !isWordCharacter(c) && !c.isSpace();
    };
    const QChar reference = p.column < line.size() ? line.at(p.column)
                                                   : (p.column > 0 ? line.at(p.column - 1) : QChar());
    if (reference.isNull())
        return {p, p};
    while (start > 0 && same(line.at(start - 1), reference))
        --start;
    while (end < line.size() && same(line.at(end), reference))
        ++end;
    return {{p.line, start}, {p.line, end}};
}

void Document::deleteBackward(bool word)
{
    if (hasSelection()) {
        deleteSelection();
        return;
    }
    TextPosition start;
    if (word) {
        start = wordBoundary(m_cursor, false);
    } else if (m_cursor.column > 0) {
        const QString &line = m_buffer.line(m_cursor.line);
        int column = m_cursor.column - 1;
        if (column > 0 && line.at(column).isLowSurrogate() && line.at(column - 1).isHighSurrogate())
            --column;
        // In leading spaces, Backspace goes back to the previous indentation stop.
        if (m_insertSpaces && line.at(column) == QLatin1Char(' ')) {
            bool leading = true;
            for (int i = 0; i < m_cursor.column; ++i) {
                if (line.at(i) != QLatin1Char(' ')) {
                    leading = false;
                    break;
                }
            }
            if (leading) {
                const int stop = ((m_cursor.column - 1) / m_indentWidth) * m_indentWidth;
                column = stop;
            }
        }
        start = {m_cursor.line, column};
    } else if (m_cursor.line > 0) {
        start = {m_cursor.line - 1, int(m_buffer.line(m_cursor.line - 1).size())};
    } else {
        return;
    }
    replace({start, m_cursor}, QString(), UndoGroup::Backspace, {start, start});
}

void Document::deleteForward(bool word)
{
    if (hasSelection()) {
        deleteSelection();
        return;
    }
    TextPosition end;
    const QString &line = m_buffer.line(m_cursor.line);
    if (word) {
        end = wordBoundary(m_cursor, true);
    } else if (m_cursor.column < line.size()) {
        int column = m_cursor.column + 1;
        if (column < line.size() && line.at(column).isLowSurrogate())
            ++column;
        end = {m_cursor.line, column};
    } else if (m_cursor.line + 1 < m_buffer.lineCount()) {
        end = {m_cursor.line + 1, 0};
    } else {
        return;
    }
    const TextPosition at = m_cursor;
    replace({at, end}, QString(), UndoGroup::Delete, {at, at});
}

void Document::deleteSelection()
{
    if (!hasSelection())
        return;
    const TextRange range = selection();
    replace(range, QString(), UndoGroup::Other, {range.start, range.start});
}

// The lines a line-wise command works on: the selected ones, without a
// last line the selection only touches at its start.
static QPair<int, int> selectedLines(const TextRange &selection)
{
    int last = selection.end.line;
    if (last > selection.start.line && selection.end.column == 0)
        --last;
    return {selection.start.line, last};
}

void Document::indentSelection(bool unindent)
{
    const TextRange range = selection();
    const auto [first, last] = selectedLines(range);
    const QString indent = m_insertSpaces ? QString(m_indentWidth, QLatin1Char(' ')) : QStringLiteral("\t");
    const TextPosition oldCursor = m_cursor;
    const TextPosition oldAnchor = m_anchor;
    int cursorShift = 0;
    int anchorShift = 0;

    beginEdit();
    for (int line = first; line <= last; ++line) {
        const QString &text = m_buffer.line(line);
        int shift = 0;
        if (unindent) {
            int remove = 0;
            if (text.startsWith(QLatin1Char('\t')))
                remove = 1;
            else
                while (remove < m_indentWidth && remove < text.size() && text.at(remove) == QLatin1Char(' '))
                    ++remove;
            if (remove == 0)
                continue;
            replace({{line, 0}, {line, remove}}, QString(), UndoGroup::Other, {m_cursor, m_anchor});
            shift = -remove;
        } else {
            if (text.isEmpty() && first != last)
                continue;
            replace({{line, 0}, {line, 0}}, indent, UndoGroup::Other, {m_cursor, m_anchor});
            shift = int(indent.size());
        }
        if (line == oldCursor.line)
            cursorShift = shift;
        if (line == oldAnchor.line)
            anchorShift = shift;
    }
    m_cursor = m_buffer.clamp({oldCursor.line, qMax(0, oldCursor.column + cursorShift)});
    m_anchor = m_buffer.clamp({oldAnchor.line, qMax(0, oldAnchor.column + anchorShift)});
    if (oldAnchor.column == 0 && oldAnchor != oldCursor)
        m_anchor.column = 0;
    if (oldCursor.column == 0 && oldAnchor != oldCursor)
        m_cursor.column = 0;
    endEdit();
    emit cursorChanged();
}

void Document::moveLines(bool up)
{
    const auto [first, last] = selectedLines(selection());
    if ((up && first == 0) || (!up && last + 1 >= m_buffer.lineCount()))
        return;
    QStringList block;
    for (int line = first; line <= last; ++line)
        block.append(m_buffer.line(line));
    const int delta = up ? -1 : 1;
    const Selection after{{m_cursor.line + delta, m_cursor.column}, {m_anchor.line + delta, m_anchor.column}};
    if (up) {
        const QString above = m_buffer.line(first - 1);
        replace({{first - 1, 0}, {last, int(m_buffer.line(last).size())}},
                block.join(QLatin1Char('\n')) + QLatin1Char('\n') + above, UndoGroup::Other, after);
    } else {
        const QString below = m_buffer.line(last + 1);
        replace({{first, 0}, {last + 1, int(below.size())}},
                below + QLatin1Char('\n') + block.join(QLatin1Char('\n')), UndoGroup::Other, after);
    }
}

void Document::duplicateLines()
{
    const auto [first, last] = selectedLines(selection());
    QStringList block;
    for (int line = first; line <= last; ++line)
        block.append(m_buffer.line(line));
    const int count = last - first + 1;
    const Selection after{{m_cursor.line + count, m_cursor.column}, {m_anchor.line + count, m_anchor.column}};
    const TextPosition end{last, int(m_buffer.line(last).size())};
    replace({end, end}, QLatin1Char('\n') + block.join(QLatin1Char('\n')), UndoGroup::Other, after);
}

void Document::deleteLines()
{
    const auto [first, last] = selectedLines(selection());
    TextRange range;
    if (last + 1 < m_buffer.lineCount())
        range = {{first, 0}, {last + 1, 0}};
    else if (first > 0)
        range = {{first - 1, int(m_buffer.line(first - 1).size())}, {last, int(m_buffer.line(last).size())}};
    else
        range = {{0, 0}, {last, int(m_buffer.line(last).size())}};
    const int line = qMin(first, m_buffer.lineCount() - 1 - (last - first + 1));
    const TextPosition at{qMax(0, line), 0};
    replace(range, QString(), UndoGroup::Other, {at, at});
}

void Document::changeCase(int change)
{
    TextRange range = selection();
    if (range.isEmpty())
        range = wordAt(m_cursor);
    if (range.isEmpty())
        return;
    const QString text = m_buffer.text(range);
    QString result;
    if (change == Upper) {
        result = text.toUpper();
    } else if (change == Lower) {
        result = text.toLower();
    } else {
        result = text.toLower();
        bool start = true;
        for (QChar &c : result) {
            if (c.isLetter()) {
                if (start)
                    c = c.toTitleCase();
                start = false;
            } else {
                start = !c.isLetterOrNumber() && c != QLatin1Char('\'');
            }
        }
    }
    if (result == text)
        return;
    // Case changes can change lengths (e.g. "ß" to "SS").
    const TextPosition end = TextBuffer::endOf(range.start, result);
    const bool forward = m_anchor <= m_cursor;
    replace(range, result, UndoGroup::Other, forward ? Selection{end, range.start} : Selection{range.start, end});
}

TextRange Document::matchingBracket(const TextPosition &position) const
{
    static const QString open = QStringLiteral("([{");
    static const QString close = QStringLiteral(")]}");
    const TextRange none{{-1, -1}, {-1, -1}};
    if (m_large)
        return none;
    const TextPosition p = m_buffer.clamp(position);
    const QString &line = m_buffer.line(p.line);

    // The bracket after the cursor first, then the one before it.
    for (int column : {p.column, p.column - 1}) {
        if (column < 0 || column >= line.size())
            continue;
        const QChar c = line.at(column);
        qsizetype kind = open.indexOf(c);
        const bool forward = kind >= 0;
        if (!forward)
            kind = close.indexOf(c);
        if (kind < 0)
            continue;
        const QChar self = c;
        const QChar other = forward ? close.at(kind) : open.at(kind);

        int depth = 0;
        int scanned = 0;
        int l = p.line;
        int i = column;
        while (scanned < 50000) {
            const QString &text = m_buffer.line(l);
            while (forward ? i < text.size() : i >= 0) {
                const QChar ch = text.at(i);
                if (ch == self) {
                    ++depth;
                } else if (ch == other && --depth == 0) {
                    return {{p.line, column}, {l, i}};
                }
                i += forward ? 1 : -1;
                ++scanned;
            }
            l += forward ? 1 : -1;
            if (l < 0 || l >= m_buffer.lineCount() || qAbs(l - p.line) > 2000)
                return none;
            i = forward ? 0 : int(m_buffer.line(l).size()) - 1;
        }
        return none;
    }
    return none;
}

static void countText(QStringView text, qint64 *characters, qint64 *nonSpace, qint64 *words)
{
    bool inWord = false;
    for (const QChar c : text) {
        if (c.isLowSurrogate())
            continue;
        ++*characters;
        if (c.isSpace()) {
            inWord = false;
            continue;
        }
        ++*nonSpace;
        // Ideographs count as words of their own.
        const bool ideograph = c.script() == QChar::Script_Han || c.script() == QChar::Script_Hiragana
                || c.script() == QChar::Script_Katakana;
        if (ideograph) {
            ++*words;
            inWord = false;
        } else if (!inWord) {
            ++*words;
            inWord = true;
        }
    }
}

QVariantMap Document::statistics() const
{
    qint64 characters = 0;
    qint64 nonSpace = 0;
    qint64 words = 0;
    for (int i = 0; i < m_buffer.lineCount(); ++i) {
        countText(m_buffer.line(i), &characters, &nonSpace, &words);
        if (i > 0)
            ++characters;
    }
    QVariantMap result{
        {QStringLiteral("lines"), m_buffer.lineCount()},
        {QStringLiteral("characters"), characters},
        {QStringLiteral("charactersWithoutSpaces"), nonSpace},
        {QStringLiteral("words"), words},
    };
    if (hasSelection()) {
        qint64 selCharacters = 0;
        qint64 selNonSpace = 0;
        qint64 selWords = 0;
        const QString text = selectedText();
        countText(text, &selCharacters, &selNonSpace, &selWords);
        const TextRange r = selection();
        result.insert(QStringLiteral("selectionLines"), r.end.line - r.start.line + 1);
        result.insert(QStringLiteral("selectionCharacters"), selCharacters);
        result.insert(QStringLiteral("selectionWords"), selWords);
    }
    return result;
}

void Document::setSearch(const QString &text, bool caseSensitive, bool wholeWords, bool regex, bool inSelection)
{
    m_searcher.setPattern(text, {caseSensitive, wholeWords, regex});
    if (!inSelection)
        m_searchScope = TextRange();
    else if (hasSelection() && m_searchScope.isEmpty())
        m_searchScope = selection();
    emit searchChanged();
    computeMatches();
}

void Document::clearSearch()
{
    m_searcher.setPattern(QString(), {});
    m_searchScope = TextRange();
    emit searchChanged();
    computeMatches();
}

void Document::updateMatches()
{
    // Counting walks the whole text; edits and cursor moves are batched.
    if (m_searcher.isValid() || m_matchCount != 0)
        m_matchTimer.start();
}

void Document::computeMatches()
{
    m_matchTimer.stop();
    int count = 0;
    int index = 0;
    if (m_searcher.isValid()) {
        count = m_searcher.count(m_buffer, m_searchScope);
        // The current match is the selected one.
        const TextRange current = selection();
        if (!current.isEmpty()) {
            const TextRange scope = m_searchScope.isEmpty() ? TextRange{{0, 0}, m_buffer.end()} : m_searchScope;
            const TextRange before{scope.start, current.start};
            const int ahead = before.end <= before.start ? 0 : m_searcher.count(m_buffer, before);
            const QList<TextRange> here = m_searcher.matchesInLine(m_buffer.line(current.start.line),
                                                                   current.start.line);
            if (here.contains(current))
                index = ahead + 1;
        }
    }
    if (count != m_matchCount || index != m_matchIndex) {
        m_matchCount = count;
        m_matchIndex = index;
        emit matchesChanged();
    }
}

bool Document::findNext(bool backwards)
{
    if (!m_searcher.isValid())
        return false;
    const TextRange current = selection();
    const TextPosition from = backwards ? current.start : current.end;
    const TextRange match = m_searcher.find(m_buffer, from, backwards, m_searchScope, &m_wrapped);
    if (Searcher::isNotFound(match))
        return false;
    setCursor(match.end, match.start);
    computeMatches();
    emit revealCursor();
    return true;
}

bool Document::findIncremental()
{
    if (!m_searcher.isValid())
        return false;
    const TextRange match = m_searcher.find(m_buffer, selection().start, false, m_searchScope, &m_wrapped);
    if (Searcher::isNotFound(match))
        return false;
    setCursor(match.end, match.start);
    computeMatches();
    emit revealCursor();
    return true;
}

bool Document::replaceCurrent(const QString &replacement)
{
    if (!m_searcher.isValid())
        return false;
    const TextRange current = selection();
    const QList<TextRange> here = m_searcher.matchesInLine(m_buffer.line(current.start.line), current.start.line);
    if (!current.isEmpty() && here.contains(current)) {
        const QString text = m_searcher.replacement(m_buffer, current, replacement);
        const TextPosition end = TextBuffer::endOf(current.start, text);
        const TextRange scope = m_searchScope;
        replace(current, text, UndoGroup::Other, {end, end});
        if (!scope.isEmpty() && current.end.line == scope.end.line) {
            m_searchScope.end.column += int(text.size()) - (current.end.column - current.start.column);
            m_searchScope.end.line += text.count(QLatin1Char('\n'));
        }
    }
    return findNext(false);
}

int Document::replaceAll(const QString &replacement)
{
    if (!m_searcher.isValid() || m_locked)
        return 0;
    const TextRange scope = m_searchScope.isEmpty() ? TextRange{{0, 0}, m_buffer.end()} : m_searchScope;
    int count = 0;
    beginEdit();
    // Bottom up, so earlier positions stay valid.
    TextPosition scopeEnd = scope.end;
    for (int line = scope.end.line; line >= scope.start.line; --line) {
        const QString &text = m_buffer.line(line);
        const int lower = line == scope.start.line ? scope.start.column : 0;
        const int upper = line == scope.end.line ? scope.end.column : int(text.size());
        const QList<TextRange> matches = m_searcher.matchesInLine(text, line, lower, upper);
        for (auto it = matches.crbegin(); it != matches.crend(); ++it) {
            const QString with = m_searcher.replacement(m_buffer, *it, replacement);
            if (line == scope.end.line)
                scopeEnd.column += int(with.size()) - (it->end.column - it->start.column);
            replace(*it, with, UndoGroup::Other, {m_cursor, m_anchor});
            ++count;
        }
    }
    if (!m_searchScope.isEmpty())
        m_searchScope.end = m_buffer.clamp(scopeEnd);
    endEdit();
    computeMatches();
    return count;
}

void Document::scheduleDraft()
{
    m_draftTimer.start();
}

static void writeDraftFile(const QString &path, const QList<QString> &lines)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!QFileInfo::exists(directory)) {
        QDir().mkpath(directory);
        QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(TextCodec::encode(lines, QByteArrayLiteral("UTF-8"), false, LineEnding::Unix));
    file.commit();
}

void Document::writeDraftAsync()
{
    if (!m_modified && !(m_filePath.isEmpty() && !isEmpty())) {
        removeDraft();
        return;
    }
    // One write at a time, so an older text never lands after a newer one.
    m_draftWrite.waitForFinished();
    // The list is shared, not copied; later edits detach this document's copy.
    m_draftWrite = QtConcurrent::run(writeDraftFile, draftPath(), m_buffer.lines());
}

void Document::writeDraftNow()
{
    m_draftTimer.stop();
    m_draftWrite.waitForFinished();
    if (!m_modified && !(m_filePath.isEmpty() && !isEmpty())) {
        removeDraft();
        return;
    }
    writeDraftFile(draftPath(), m_buffer.lines());
}

void Document::removeDraft()
{
    m_draftTimer.stop();
    m_draftWrite.waitForFinished();
    QFile::remove(draftPath());
}

bool Document::restoreDraft()
{
    if (m_loading) {
        m_restoreDraftAfterLoad = true;
        return true;
    }
    QFile file(draftPath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QString text = QString::fromUtf8(file.readAll());
    if (text == m_buffer.text()) {
        if (m_filePath.isEmpty())
            m_diskDiffers = !text.isEmpty();
        updateModified();
        return true;
    }
    const int oldCount = m_buffer.lineCount();
    m_buffer.setText(text);
    m_undo.clear();
    m_undo.setCleanUnreachable();
    m_states.reset(m_states.language(), m_buffer.lineCount());
    m_cursor = m_anchor = m_buffer.clamp(m_cursor);
    if (m_filePath.isEmpty())
        detectLanguage();
    emit linesChanged(0, oldCount, m_buffer.lineCount());
    emit contentsChanged();
    emit titleChanged();
    emit undoChanged();
    emit cursorChanged();
    updateModified();
    if (!m_filePath.isEmpty())
        setNotice(Restored);
    return true;
}

void Document::checkDisk()
{
    if (m_filePath.isEmpty() || m_loading)
        return;
    const QFileInfo info(m_filePath);
    if (m_watcher && info.exists() && !m_watcher->files().contains(m_filePath))
        m_watcher->addPath(m_filePath);
    updateWritable();

    if (!info.exists()) {
        if (m_notice != Deleted) {
            // Quitting must keep the text: the file is gone.
            m_diskDiffers = true;
            updateModified();
            setNotice(Deleted);
        }
        return;
    }

    const bool returned = m_notice == Deleted;
    if (!returned && info.lastModified() == m_diskModified && info.size() == m_diskSize)
        return;
    if (m_ignoreDisk && !returned) {
        m_diskModified = info.lastModified();
        m_diskSize = info.size();
        return;
    }
    if (!m_modified || (returned && m_undo.isClean() && !m_formatModified)) {
        reload();
        return;
    }
    m_diskModified = info.lastModified();
    m_diskSize = info.size();
    setNotice(ChangedOnDisk);
}

bool Document::save()
{
    if (m_filePath.isEmpty() || m_loading)
        return false;
    return write(m_filePath, m_encoding, m_bom);
}

bool Document::saveAs(const QString &path)
{
    if (path.isEmpty() || m_loading)
        return false;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const bool changed = absolute != m_filePath;
    if (!write(absolute, m_encoding, m_bom))
        return false;
    if (changed) {
        setFilePath(absolute);
        updateWritable();
        if (!m_languageChosen)
            detectLanguage();
        if (s_recentFilesHook)
            s_recentFilesHook(m_filePath);
    }
    return true;
}

bool Document::saveReplacingMalformed()
{
    m_malformed = false;
    return save();
}

bool Document::saveAsUtf8()
{
    const QByteArray old = m_encoding;
    m_encoding = QByteArrayLiteral("UTF-8");
    if (!save()) {
        m_encoding = old;
        return false;
    }
    emit encodingChanged();
    return true;
}

// Replaces the file atomically. A file owned by someone else is written in
// place instead, as a rename would hand it to this user; its group is kept.
static bool writeFile(const QString &target, const QByteArray &bytes, QString *error)
{
    struct stat existing;
    const bool exists = ::stat(QFile::encodeName(target).constData(), &existing) == 0;
    if (exists && existing.st_uid != ::geteuid()) {
        QFile file(target);
        if (!file.open(QIODevice::ReadWrite)) {
            if (file.error() != QFileDevice::PermissionsError)
                *error = file.errorString();
            return false;
        }
        if (file.write(bytes) != bytes.size() || !file.resize(bytes.size()) || !file.flush()
                || ::fsync(file.handle()) != 0) {
            *error = file.errorString();
            return false;
        }
        return true;
    }

    QSaveFile file(target);
    // In a folder the user cannot write to, an existing writable file is
    // written in place.
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly)) {
        if (file.error() != QFileDevice::PermissionsError)
            *error = file.errorString();
        return false;
    }
    if (exists && existing.st_gid != ::getegid())
        [[maybe_unused]] const int result = ::fchown(file.handle(), uid_t(-1), existing.st_gid);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString().isEmpty() ? QCoreApplication::translate("Document", "Writing failed.")
                                              : file.errorString();
        return false;
    }
    return true;
}

bool Document::write(const QString &path, const QByteArray &encoding, bool bom)
{
    const bool newFile = m_filePath.isEmpty() || !QFileInfo::exists(path);
    const bool samePath = !m_filePath.isEmpty() && QFileInfo(path).absoluteFilePath() == m_filePath;

    // Someone else's change since it was loaded is not overwritten unasked.
    if (samePath && !newFile && !m_ignoreDisk && m_diskSize >= 0) {
        const QFileInfo info(path);
        if (info.lastModified() != m_diskModified || info.size() != m_diskSize) {
            setNotice(ChangedOnDisk);
            return false;
        }
    }
    // Bytes that did not decode are gone from the text; saving makes it final.
    if (m_malformed) {
        setNotice(MalformedSave, QString::fromLatin1(m_loadedEncoding));
        return false;
    }

    // EditorConfig's whitespace rules change the buffer itself, as one undo step.
    const bool finalNewline = m_finalNewline.value_or(newFile);
    const bool needsNewline = finalNewline && !m_buffer.line(m_buffer.lineCount() - 1).isEmpty()
            && !isEmpty();
    if ((m_trimTrailingWhitespace || needsNewline) && !m_locked) {
        beginEdit();
        if (m_trimTrailingWhitespace) {
            for (int line = 0; line < m_buffer.lineCount(); ++line) {
                const QString &text = m_buffer.line(line);
                int end = int(text.size());
                while (end > 0 && (text.at(end - 1) == QLatin1Char(' ') || text.at(end - 1) == QLatin1Char('\t')))
                    --end;
                if (end < text.size()) {
                    const TextPosition cursor = m_cursor.line == line ? TextPosition{line, qMin(m_cursor.column, end)}
                                                                      : m_cursor;
                    replace({{line, end}, {line, int(text.size())}}, QString(), UndoGroup::Other, {cursor, cursor});
                }
            }
        }
        if (needsNewline)
            replace({m_buffer.end(), m_buffer.end()}, QStringLiteral("\n"), UndoGroup::Other, {m_cursor, m_anchor});
        endEdit();
    }

    bool lossy = false;
    const QByteArray bytes = TextCodec::encode(m_buffer.lines(), encoding, bom, m_lineEnding, &lossy);
    if (lossy) {
        setNotice(LossyEncoding, QString::fromLatin1(encoding));
        return false;
    }

    // Through a symbolic link to the file it points at, keeping the link.
    QString target = path;
    const QFileInfo info(path);
    if (info.isSymLink() && info.exists())
        target = info.canonicalFilePath();

    QString error;
    if (!writeFile(target, bytes, &error)) {
        setNotice(error.isEmpty() ? ReadOnly : SaveFailed, error);
        return false;
    }

    const QFileInfo written(target);
    m_diskModified = written.lastModified();
    m_diskSize = written.size();
    m_undo.setClean();
    m_formatModified = false;
    m_diskDiffers = false;
    m_ignoreDisk = false;
    updateModified();
    removeDraft();
    if (m_notice != Malformed && m_notice != Binary)
        setNotice(NoNotice);
    emit undoChanged();
    emit saved();
    if (path == m_filePath && s_recentFilesHook)
        s_recentFilesHook(m_filePath);
    return true;
}
