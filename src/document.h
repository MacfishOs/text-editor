// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "searcher.h"
#include "syntax.h"
#include "textbuffer.h"
#include "textcodec.h"
#include "undostack.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

class QFileSystemWatcher;

// The end states of each line's highlighting, computed lazily from the top.
// After an edit only the changed lines are recomputed when the state they
// end in is the same as before.
class SyntaxStates
{
public:
    void reset(const Language *language, int lineCount);
    void linesChanged(int line, int removed, int added);

    const Language *language() const { return m_language; }
    int validLines() const { return m_valid; }
    // The state entering `line`; a guess while it is beyond validLines().
    Syntax::State before(int line) const;
    // Computes states up to `line` or until `deadline` (ms since epoch timer)
    // runs out. Returns the first line whose entering state changed, or -1,
    // and sets `last` to the last such line.
    int advance(const TextBuffer &buffer, int line, qint64 budgetMs, int *last);

private:
    const Language *m_language = nullptr;
    QList<Syntax::State> m_end;
    int m_valid = 0;
    // Lines from m_changedEnd on were valid before the edit; once a line there
    // ends in the state it had, everything up to m_resume is valid again.
    int m_changedEnd = -1;
    int m_resume = -1;
};

struct LoadResult;

class Document : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY filePathChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY filePathChanged)
    Q_PROPERTY(bool untitled READ isUntitled NOTIFY filePathChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY contentsChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(qreal loadProgress READ loadProgress NOTIFY loadProgressChanged)
    // Editing is locked, e.g. for a binary file until the user unlocks it.
    Q_PROPERTY(bool locked READ isLocked WRITE setLocked NOTIFY lockedChanged)
    // The file on disk cannot be written; Save turns into Save As.
    Q_PROPERTY(bool writable READ isWritable NOTIFY writableChanged)

    Q_PROPERTY(QString encoding READ encoding NOTIFY encodingChanged)
    Q_PROPERTY(bool bom READ hasBom WRITE setBom NOTIFY encodingChanged)
    Q_PROPERTY(int lineEnding READ lineEnding WRITE setLineEnding NOTIFY lineEndingChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString languageName READ languageName NOTIFY languageChanged)
    Q_PROPERTY(bool highlighting READ isHighlighting NOTIFY languageChanged)
    Q_PROPERTY(bool large READ isLarge NOTIFY largeChanged)
    Q_PROPERTY(bool insertSpaces READ insertSpaces WRITE setInsertSpaces NOTIFY indentationChanged)
    Q_PROPERTY(int tabWidth READ tabWidth WRITE setTabWidth NOTIFY indentationChanged)
    Q_PROPERTY(int indentWidth READ indentWidth WRITE setIndentWidth NOTIFY indentationChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY contentsChanged)

    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorChanged)
    // The column counted with tabs expanded, as shown to the user (from 1).
    Q_PROPERTY(int visualColumn READ visualColumn NOTIFY cursorChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY cursorChanged)
    Q_PROPERTY(int selectionLength READ selectionLength NOTIFY cursorChanged)

    Q_PROPERTY(int notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString noticeDetail READ noticeDetail NOTIFY noticeChanged)

    Q_PROPERTY(QString searchText READ searchText NOTIFY searchChanged)
    Q_PROPERTY(bool searchValid READ searchValid NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
    Q_PROPERTY(int matchIndex READ matchIndex NOTIFY matchesChanged)

    Q_PROPERTY(QString draftId READ draftId WRITE setDraftId NOTIFY draftIdChanged)

public:
    enum Notice {
        NoNotice,
        Restored,
        ChangedOnDisk,
        Deleted,
        Malformed,
        Binary,
        ReadOnly,
        PermissionDenied,
        LoadFailed,
        TooLarge,
        SaveFailed,
        LossyEncoding,
        LargeFile,
        MixedLineEndings,
        MalformedSave,
    };
    Q_ENUM(Notice)

    enum CaseChange {
        Upper,
        Lower,
        Title,
    };
    Q_ENUM(CaseChange)

    // The biggest file that is opened; anything larger is refused.
    static constexpr qint64 maximumFileSize = 256ll * 1024 * 1024;
    // Above this, highlighting and bracket matching are off.
    static constexpr qint64 largeFileSize = 4ll * 1024 * 1024;
    // Lines longer than this are not highlighted.
    static constexpr int maximumHighlightedLine = 20000;

    explicit Document(QObject *parent = nullptr, const QString &draftId = QString());
    ~Document() override;

    // Opens `path`; empty `encoding` detects it.
    Q_INVOKABLE void load(const QString &path, const QString &encoding = QString());
    Q_INVOKABLE void reload();
    Q_INVOKABLE void reloadWithEncoding(const QString &encoding);
    Q_INVOKABLE void cancelLoading();
    // Waits for a load in progress, for tests and the command line.
    bool waitForLoaded(int timeoutMs = 30000);

    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);
    // Saves to the same file in UTF-8, after a LossyEncoding notice.
    Q_INVOKABLE bool saveAsUtf8();
    // Saves after a MalformedSave notice, giving up the bytes that did not decode.
    Q_INVOKABLE bool saveReplacingMalformed();
    Q_INVOKABLE void setEncoding(const QString &encoding);
    Q_INVOKABLE void dismissNotice();
    Q_INVOKABLE void keepLocalVersion();

    QString title() const;
    QString filePath() const { return m_filePath; }
    QString fileName() const;
    QString folder() const;
    bool isUntitled() const { return m_filePath.isEmpty(); }
    bool isModified() const { return m_modified; }
    bool isEmpty() const { return m_buffer.length() == 0; }
    bool isLoading() const { return m_loading; }
    qreal loadProgress() const { return m_loadProgress; }
    bool isLocked() const { return m_locked; }
    void setLocked(bool locked);
    bool isWritable() const { return m_writable; }

    QString encoding() const { return QString::fromLatin1(m_encoding); }
    bool hasBom() const { return m_bom; }
    void setBom(bool bom);
    int lineEnding() const { return int(m_lineEnding); }
    void setLineEnding(int lineEnding);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString languageName() const;
    bool isHighlighting() const { return m_highlightingEnabled && m_states.language(); }
    bool isLarge() const { return m_large; }
    bool insertSpaces() const { return m_insertSpaces; }
    void setInsertSpaces(bool insertSpaces);
    int tabWidth() const { return m_tabWidth; }
    void setTabWidth(int width);
    int indentWidth() const { return m_indentWidth; }
    void setIndentWidth(int width);
    // Uses the defaults when the file says nothing about its indentation.
    Q_INVOKABLE void setIndentationDefaults(bool insertSpaces, int width);

    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    const TextBuffer &buffer() const { return m_buffer; }
    int lineCount() const { return m_buffer.lineCount(); }
    Q_INVOKABLE QString text() const { return m_buffer.text(); }
    // Replaces everything as one undoable step.
    Q_INVOKABLE void setText(const QString &text);

    // The editing primitive: replaces `range` with `text` and records it.
    void replace(const TextRange &range, const QString &text, UndoGroup::Kind kind,
                 const Selection &after);
    void beginEdit();
    void endEdit();

    TextPosition cursor() const { return m_cursor; }
    TextPosition anchor() const { return m_anchor; }
    void setCursor(const TextPosition &cursor, const TextPosition &anchor);
    void setCursor(const TextPosition &cursor) { setCursor(cursor, cursor); }
    TextRange selection() const { return TextRange{m_anchor, m_cursor}.normalized(); }
    bool hasSelection() const { return m_cursor != m_anchor; }
    int cursorLine() const { return m_cursor.line; }
    int cursorColumn() const { return m_cursor.column; }
    int visualColumn() const;
    int selectionLength() const;
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void goToLine(int line, int column = 1);

    // Typing: replaces the selection; in overwrite mode replaces the next character.
    void insertText(const QString &text, bool overwrite = false);
    Q_INVOKABLE void paste(const QString &text) { insertText(text); }
    void insertNewline(bool autoIndent);
    void deleteBackward(bool word);
    void deleteForward(bool word);
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void indentSelection(bool unindent);
    Q_INVOKABLE void moveLines(bool up);
    Q_INVOKABLE void duplicateLines();
    Q_INVOKABLE void deleteLines();
    Q_INVOKABLE void changeCase(int change);

    // Word movement: the start of the previous word or the end of the next.
    TextPosition wordBoundary(const TextPosition &from, bool forward) const;
    TextRange wordAt(const TextPosition &position) const;
    // The bracket matching the one before or after the cursor, or {-1,-1}.
    TextRange matchingBracket(const TextPosition &position) const;

    Q_INVOKABLE QVariantMap statistics() const;

    int notice() const { return m_notice; }
    QString noticeDetail() const { return m_noticeDetail; }

    // Search; the matches stay highlighted while the pattern is set.
    Q_INVOKABLE void setSearch(const QString &text, bool caseSensitive, bool wholeWords, bool regex,
                               bool inSelection);
    Q_INVOKABLE void clearSearch();
    // Selects the next match; returns false when there is none. Sets wrapped.
    Q_INVOKABLE bool findNext(bool backwards = false);
    // While typing the pattern: the first match from where the selection starts.
    Q_INVOKABLE bool findIncremental();
    Q_INVOKABLE bool replaceCurrent(const QString &replacement);
    Q_INVOKABLE int replaceAll(const QString &replacement);
    QString searchText() const { return m_searcher.pattern(); }
    bool searchValid() const { return m_searcher.isValid(); }
    QString searchError() const { return m_searcher.errorString(); }
    const Searcher &searcher() const { return m_searcher; }
    TextRange searchScope() const { return m_searchScope; }
    int matchCount() const { return m_matchCount; }
    int matchIndex() const { return m_matchIndex; }
    Q_INVOKABLE bool lastSearchWrapped() const { return m_wrapped; }

    SyntaxStates &syntaxStates() { return m_states; }
    // Computes highlighting states up to `line`, a slice at a time.
    void requestHighlighting(int line);

    QString draftId() const { return m_draftId; }
    void setDraftId(const QString &id);
    static QString draftDirectory();
    QString draftPath() const;
    // Writes the unsaved text to the draft now, on this thread.
    Q_INVOKABLE void writeDraftNow();
    Q_INVOKABLE void removeDraft();
    // Takes the text of a draft as the unsaved state of this document.
    Q_INVOKABLE bool restoreDraft();

    QDateTime diskModified() const { return m_diskModified; }
    // Compares the file with what was loaded or saved; reloads or reports.
    Q_INVOKABLE void checkDisk();

    static void setRecentFilesHook(std::function<void(const QString &)> hook);

signals:
    void titleChanged();
    void filePathChanged();
    void modifiedChanged();
    void loadingChanged();
    void loadProgressChanged();
    void lockedChanged();
    void writableChanged();
    void encodingChanged();
    void lineEndingChanged();
    void languageChanged();
    void largeChanged();
    void indentationChanged();
    void undoChanged();
    void cursorChanged();
    void noticeChanged();
    void searchChanged();
    void matchesChanged();
    // Lines [line, line + removed) became [line, line + added).
    void linesChanged(int line, int removed, int added);
    void contentsChanged();
    // The entering highlight state of lines [first, last] changed.
    void syntaxChanged(int first, int last);
    // A view should scroll the cursor into view.
    void revealCursor();
    void loaded();
    void draftIdChanged();
    void saved();

private:
    void applyStep(const EditStep &step, bool reverse);
    void finishLoad(const LoadResult &result);
    void setNotice(Notice notice, const QString &detail = QString());
    void updateModified();
    void setFilePath(const QString &path);
    void watchFile();
    void updateWritable();
    void detectLanguage();
    void updateMatches();
    void computeMatches();
    void scheduleDraft();
    Q_INVOKABLE void writeDraftAsync();
    void applyEditorConfig();
    bool write(const QString &path, const QByteArray &encoding, bool bom);
    void highlightSlice();

    TextBuffer m_buffer;
    UndoStack m_undo;
    TextPosition m_cursor;
    TextPosition m_anchor;

    QString m_filePath;
    bool m_modified = false;
    // The encoding, BOM or line ending changed since the last save.
    bool m_formatModified = false;
    // The file is gone or its text was replaced by a draft: the disk copy is not the buffer.
    bool m_diskDiffers = false;
    bool m_restoreDraftAfterLoad = false;
    // Some bytes did not decode; saving as is would replace them for good.
    bool m_malformed = false;
    QByteArray m_loadedEncoding;
    bool m_loading = false;
    qreal m_loadProgress = 0;
    bool m_locked = false;
    bool m_writable = true;
    bool m_large = false;
    QByteArray m_encoding = QByteArrayLiteral("UTF-8");
    bool m_bom = false;
    LineEnding m_lineEnding = LineEnding::Unix;
    QString m_language;
    bool m_languageChosen = false;
    bool m_highlightingEnabled = true;
    bool m_insertSpaces = true;
    int m_tabWidth = 4;
    int m_indentWidth = 4;
    bool m_indentationFromFile = false;
    bool m_trimTrailingWhitespace = false;
    std::optional<bool> m_finalNewline;

    int m_notice = NoNotice;
    QString m_noticeDetail;

    Searcher m_searcher;
    TextRange m_searchScope;
    int m_matchCount = 0;
    int m_matchIndex = 0;
    bool m_wrapped = false;
    QTimer m_matchTimer;

    SyntaxStates m_states;
    int m_highlightTarget = 0;
    QTimer m_highlightTimer;

    QString m_draftId;
    QTimer m_draftTimer;
    QFuture<void> m_draftWrite;

    QFileSystemWatcher *m_watcher = nullptr;
    QDateTime m_diskModified;
    qint64 m_diskSize = -1;
    QTimer m_diskCheckTimer;
    // The disk changed while the buffer was modified and the user kept theirs.
    bool m_ignoreDisk = false;

    QFutureWatcher<LoadResult> *m_loadWatcher = nullptr;
    std::shared_ptr<std::atomic<int>> m_loadProgressShared;
    std::shared_ptr<std::atomic<bool>> m_loadCancel;
    QTimer m_progressTimer;
    // A reload keeps the cursor and scroll position.
    bool m_reloading = false;
    TextPosition m_reloadCursor;
};

#endif
