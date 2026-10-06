// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "document.h"
#include "fileoptions.h"
#include "recentfiles.h"
#include "searcher.h"
#include "session.h"
#include "syntax.h"
#include "textbuffer.h"
#include "textcodec.h"
#include "undostack.h"

#include <QSignalSpy>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QtTest>

class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void bufferEdits();
    void bufferRanges();

    void encodingDetection_data();
    void encodingDetection();
    void binaryDetection();
    void lineEndings_data();
    void lineEndings();
    void encodeRoundTrip();
    void lossyEncoding();

    void undoGroupsWords();
    void undoCleanState();
    void undoRestoresSelection();

    void searchPlain();
    void searchWholeWordsAndCase();
    void searchRegexReplace();
    void searchScopeAndWrap();
    void replaceAllIsOneStep();

    void loadAndSave();
    void saveKeepsLineEndingsAndBom();
    void saveThroughSymlinkKeepsPermissions();
    void externalChangeReloadsUnmodified();
    void externalChangeWhileModified();
    void externalDelete();
    void reopenWithEncoding();
    void tooLarge();
    void saveRefusesOverwritingExternalChange();
    void saveRefusesLosingMalformedBytes();
    void saveInReadOnlyFolder();
    void saveUnwritableFileKeepsText();
    void deletedFileKeepsDraft();
    void draftWritesAreOrdered();
    void corruptSessionKeepsDrafts();
    void replaceAllThenTypingUndoesSeparately();
    void encodingRoundTripAll();

    void editingCommands();
    void indentCommands();
    void wordMovement();
    void bracketMatching();
    void statistics();

    void editorConfig();
    void indentationDetection();

    void languageDetection();
    void highlightCarriesState();
    void syntaxStatesResume();

    void sessionRoundTrip();
    void draftRestore();
    void recentFiles();

private:
    QString writeFile(const QString &name, const QByteArray &data);
    QTemporaryDir m_dir;
};

void CoreTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_dir.isValid());
}

QString CoreTest::writeFile(const QString &name, const QByteArray &data)
{
    const QString path = m_dir.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(data);
    return path;
}

void CoreTest::bufferEdits()
{
    TextBuffer buffer;
    QCOMPARE(buffer.lineCount(), 1);
    QCOMPARE(buffer.insert({0, 0}, QStringLiteral("hello\nworld")), (TextPosition{1, 5}));
    QCOMPARE(buffer.lineCount(), 2);
    QCOMPARE(buffer.length(), 11);
    buffer.insert({0, 5}, QStringLiteral(",\nbig"));
    QCOMPARE(buffer.text(), QStringLiteral("hello,\nbig\nworld"));
    buffer.remove({{0, 5}, {2, 0}});
    QCOMPARE(buffer.text(), QStringLiteral("helloworld"));
    QCOMPARE(buffer.length(), 10);
    buffer.setText(QStringLiteral("a\nb\n"));
    QCOMPARE(buffer.lineCount(), 3);
    QCOMPARE(buffer.length(), 4);
}

void CoreTest::bufferRanges()
{
    TextBuffer buffer;
    buffer.setText(QStringLiteral("one\ntwo\nthree"));
    QCOMPARE(buffer.text({{0, 1}, {2, 2}}), QStringLiteral("ne\ntwo\nth"));
    QCOMPARE(buffer.offset({2, 1}), 9);
    QCOMPARE(buffer.position(9), (TextPosition{2, 1}));
    QCOMPARE(buffer.clamp({5, 9}), (TextPosition{2, 5}));
    QCOMPARE(TextBuffer::endOf({1, 2}, QStringLiteral("x\nyz")), (TextPosition{2, 2}));
}

void CoreTest::encodingDetection_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QByteArray>("encoding");
    QTest::addColumn<bool>("bom");

    const QString chinese = QStringLiteral("这是一个简单的中文文本文件，用来测试编码检测。\n第二行也是中文。\n");
    const QString japanese = QStringLiteral("これは日本語のテキストです。エンコーディングを確認します。\n");
    const QString western = QStringLiteral("Café crème, naïve façade, résumé.\nÜber straße.\n");

    QTest::newRow("ascii") << QByteArray("plain text\n") << QByteArray("UTF-8") << false;
    QTest::newRow("utf-8") << chinese.toUtf8() << QByteArray("UTF-8") << false;
    QTest::newRow("utf-8 bom") << QByteArray("\xEF\xBB\xBFhi") << QByteArray("UTF-8") << true;
    QTest::newRow("utf-16le bom") << QByteArray("\xFF\xFEh\0i\0", 6) << QByteArray("UTF-16LE") << true;
    QTest::newRow("utf-16be bom") << QByteArray("\xFE\xFF\0h\0i", 6) << QByteArray("UTF-16BE") << true;
    QTest::newRow("utf-16le") << QByteArray(QStringEncoder(QStringEncoder::Utf16LE).encode(QStringLiteral("hello world\nmore text\n")))
                              << QByteArray("UTF-16LE") << false;
    QTest::newRow("gb18030") << QByteArray(QStringEncoder("GB18030").encode(chinese)) << QByteArray("GB18030") << false;
    QTest::newRow("big5") << QByteArray(QStringEncoder("Big5").encode(QStringLiteral("這是一個繁體中文的文字檔案，用來測試。\n")))
                          << QByteArray("Big5") << false;
    QTest::newRow("shift_jis") << QByteArray(QStringEncoder("Shift_JIS").encode(japanese)) << QByteArray("Shift_JIS") << false;
    QTest::newRow("latin-1") << QByteArray(QStringEncoder(QStringEncoder::Latin1).encode(western)) << QByteArray("windows-1252")
                             << false;
}

void CoreTest::encodingDetection()
{
    QFETCH(QByteArray, data);
    QFETCH(QByteArray, encoding);
    QFETCH(bool, bom);
    bool detectedBom = !bom;
    QCOMPARE(TextCodec::detect(data, &detectedBom), encoding);
    QCOMPARE(detectedBom, bom);
    const DecodedText decoded = TextCodec::decode(data);
    QVERIFY(!decoded.malformed);
    QVERIFY(!decoded.binary);
}

void CoreTest::binaryDetection()
{
    QVERIFY(TextCodec::isBinary(QByteArray("ELF\0\1\2", 6)));
    QVERIFY(!TextCodec::isBinary(QByteArray("text\twith tabs\r\n")));
    QVERIFY(TextCodec::decode(QByteArray("abc\0def", 7)).binary);
    // UTF-16 is full of NUL bytes and is still text.
    QVERIFY(!TextCodec::decode(QByteArray("\xFF\xFEh\0i\0", 6)).binary);

    const DecodedText broken = TextCodec::decode(QByteArray("ok \xC3 broken"), QByteArray("UTF-8"));
    QVERIFY(broken.malformed);
}

void CoreTest::lineEndings_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("ending");
    QTest::addColumn<bool>("mixed");
    QTest::addColumn<int>("lines");

    QTest::newRow("lf") << QStringLiteral("a\nb\n") << int(LineEnding::Unix) << false << 3;
    QTest::newRow("crlf") << QStringLiteral("a\r\nb\r\n") << int(LineEnding::Windows) << false << 3;
    QTest::newRow("cr") << QStringLiteral("a\rb") << int(LineEnding::ClassicMac) << false << 2;
    QTest::newRow("mixed") << QStringLiteral("a\r\nb\r\nc\nd") << int(LineEnding::Windows) << true << 4;
    QTest::newRow("none") << QStringLiteral("single") << int(LineEnding::Unix) << false << 1;
}

void CoreTest::lineEndings()
{
    QFETCH(QString, text);
    QFETCH(int, ending);
    QFETCH(bool, mixed);
    QFETCH(int, lines);
    LineEnding dominant;
    bool isMixed;
    const QList<QString> result = TextCodec::splitLines(text, &dominant, &isMixed);
    QCOMPARE(result.size(), lines);
    QCOMPARE(int(dominant), ending);
    QCOMPARE(isMixed, mixed);
    for (const QString &line : result)
        QVERIFY(!line.contains(QLatin1Char('\r')));
}

void CoreTest::encodeRoundTrip()
{
    const QList<QString> lines{QStringLiteral("第一行"), QStringLiteral("second"), QString()};
    for (const char *name : {"UTF-8", "UTF-16LE", "GB18030", "Big5"}) {
        bool lossy = true;
        const QByteArray bytes = TextCodec::encode(lines, name, false, LineEnding::Windows, &lossy);
        QVERIFY2(!lossy, name);
        const DecodedText decoded = TextCodec::decode(bytes, name);
        QCOMPARE(decoded.lines, lines);
        QCOMPARE(decoded.lineEnding, LineEnding::Windows);
    }
    const QByteArray withBom = TextCodec::encode({QStringLiteral("x")}, "UTF-8", true, LineEnding::Unix);
    QCOMPARE(withBom, QByteArray("\xEF\xBB\xBFx"));
}

void CoreTest::lossyEncoding()
{
    bool lossy = false;
    TextCodec::encode({QStringLiteral("中文")}, "ISO-8859-1", false, LineEnding::Unix, &lossy);
    QVERIFY(lossy);
    TextCodec::encode({QStringLiteral("abc")}, "ISO-8859-1", false, LineEnding::Unix, &lossy);
    QVERIFY(!lossy);
}

void CoreTest::undoGroupsWords()
{
    Document document;
    for (const QChar c : QStringLiteral("hello world")) {
        document.insertText(QString(c));
    }
    QCOMPARE(document.text(), QStringLiteral("hello world"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("hello "));
    document.undo();
    QCOMPARE(document.text(), QString());
    document.redo();
    document.redo();
    QCOMPARE(document.text(), QStringLiteral("hello world"));

    // Backspaces group by the word too.
    for (int i = 0; i < 6; ++i)
        document.deleteBackward(false);
    QCOMPARE(document.text(), QStringLiteral("hello"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("hello "));
}

void CoreTest::undoCleanState()
{
    const QString path = writeFile(QStringLiteral("clean.txt"), "abc\n");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QVERIFY(!document.isModified());
    document.setCursor({0, 3});
    document.insertText(QStringLiteral("d"));
    QVERIFY(document.isModified());
    document.undo();
    QVERIFY(!document.isModified());
    document.redo();
    QVERIFY(document.save());
    QVERIFY(!document.isModified());
    // Typing right after a save starts a new group, so undo reaches the saved text.
    document.insertText(QStringLiteral("e"));
    document.undo();
    QVERIFY(!document.isModified());
    QCOMPARE(document.text(), QStringLiteral("abcd\n"));
    document.undo();
    QVERIFY(document.isModified());
    // A new edit after undoing past the save point makes it unreachable.
    document.insertText(QStringLiteral("x"));
    document.undo();
    QVERIFY(document.isModified());
}

void CoreTest::undoRestoresSelection()
{
    Document document;
    document.setText(QStringLiteral("alpha beta"));
    document.setCursor({0, 10}, {0, 6});
    document.insertText(QStringLiteral("gamma"));
    QCOMPARE(document.text(), QStringLiteral("alpha gamma"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("alpha beta"));
    QCOMPARE(document.cursor(), (TextPosition{0, 10}));
    QCOMPARE(document.anchor(), (TextPosition{0, 6}));
}

void CoreTest::searchPlain()
{
    TextBuffer buffer;
    buffer.setText(QStringLiteral("Foo bar foo\nbaz FOO"));
    Searcher searcher;
    searcher.setPattern(QStringLiteral("foo"), {});
    QCOMPARE(searcher.count(buffer), 3);
    QCOMPARE(searcher.find(buffer, {0, 1}, false), (TextRange{{0, 8}, {0, 11}}));
    QCOMPARE(searcher.find(buffer, {0, 8}, true), (TextRange{{0, 0}, {0, 3}}));
    searcher.setPattern(QStringLiteral("foo"), {true, false, false});
    QCOMPARE(searcher.count(buffer), 1);
}

void CoreTest::searchWholeWordsAndCase()
{
    TextBuffer buffer;
    buffer.setText(QStringLiteral("cat concat cat_ cat."));
    Searcher searcher;
    searcher.setPattern(QStringLiteral("cat"), {false, true, false});
    const QList<TextRange> matches = searcher.matchesInLine(buffer.line(0), 0);
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(1).start.column, 16);
    // Regular expression characters are literal without the option.
    searcher.setPattern(QStringLiteral("cat."), {});
    QCOMPARE(searcher.count(buffer), 1);
}

void CoreTest::searchRegexReplace()
{
    Document document;
    document.setText(QStringLiteral("x=1, y=22"));
    document.setSearch(QStringLiteral("(\\w)=(\\d+)"), false, false, true, false);
    QVERIFY(document.searchValid());
    QCOMPARE(document.matchCount(), 2);
    QCOMPARE(document.replaceAll(QStringLiteral("$2:\\1")), 2);
    QCOMPARE(document.text(), QStringLiteral("1:x, 22:y"));

    document.setSearch(QStringLiteral("(unclosed"), false, false, true, false);
    QVERIFY(!document.searchValid());
    QVERIFY(!document.searchError().isEmpty());
}

void CoreTest::searchScopeAndWrap()
{
    TextBuffer buffer;
    buffer.setText(QStringLiteral("a a\na a\na a"));
    Searcher searcher;
    searcher.setPattern(QStringLiteral("a"), {});
    bool wrapped = false;
    QCOMPARE(searcher.find(buffer, {2, 3}, false, {}, &wrapped), (TextRange{{0, 0}, {0, 1}}));
    QVERIFY(wrapped);
    const TextRange scope{{1, 0}, {1, 3}};
    QCOMPARE(searcher.count(buffer, scope), 2);
    QCOMPARE(searcher.find(buffer, {1, 3}, false, scope, &wrapped), (TextRange{{1, 0}, {1, 1}}));
    QVERIFY(wrapped);

    Document document;
    document.setText(QStringLiteral("a a\na a\na a"));
    document.setCursor({1, 3}, {1, 0});
    document.setSearch(QStringLiteral("a"), false, false, false, true);
    QCOMPARE(document.matchCount(), 2);
    QCOMPARE(document.replaceAll(QStringLiteral("bb")), 2);
    QCOMPARE(document.text(), QStringLiteral("a a\nbb bb\na a"));
}

void CoreTest::replaceAllIsOneStep()
{
    Document document;
    document.setText(QStringLiteral("one two one\none"));
    document.setSearch(QStringLiteral("one"), false, false, false, false);
    QCOMPARE(document.replaceAll(QStringLiteral("1")), 3);
    QCOMPARE(document.text(), QStringLiteral("1 two 1\n1"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("one two one\none"));

    document.setCursor({0, 0});
    QVERIFY(document.findNext());
    QCOMPARE(document.selection(), (TextRange{{0, 0}, {0, 3}}));
    QCOMPARE(document.matchIndex(), 1);
    QVERIFY(document.replaceCurrent(QStringLiteral("uno")));
    QCOMPARE(document.text(), QStringLiteral("uno two one\none"));
    QCOMPARE(document.selection(), (TextRange{{0, 8}, {0, 11}}));
}

void CoreTest::loadAndSave()
{
    const QString path = writeFile(QStringLiteral("load.txt"), "line one\nline two\n");
    Document document;
    QSignalSpy loaded(&document, &Document::loaded);
    document.load(path);
    QVERIFY(document.isLoading());
    QVERIFY(document.waitForLoaded());
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(document.lineCount(), 3);
    QCOMPARE(document.title(), QStringLiteral("load.txt"));
    QCOMPARE(document.encoding(), QStringLiteral("UTF-8"));

    document.setCursor({1, 8});
    document.insertText(QStringLiteral("!"));
    QVERIFY(document.save());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("line one\nline two!\n"));

    // An untitled document is named after its first line.
    Document untitled;
    QCOMPARE(untitled.title(), QStringLiteral("Untitled"));
    untitled.insertText(QStringLiteral("Shopping list\nmilk"));
    QCOMPARE(untitled.title(), QStringLiteral("Shopping list"));
    const QString newPath = m_dir.filePath(QStringLiteral("new.txt"));
    QVERIFY(untitled.saveAs(newPath));
    QCOMPARE(untitled.filePath(), newPath);
    QFile saved(newPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    // New files end with a newline.
    QCOMPARE(saved.readAll(), QByteArray("Shopping list\nmilk\n"));
}

void CoreTest::saveKeepsLineEndingsAndBom()
{
    const QString path = writeFile(QStringLiteral("crlf.txt"), "\xEF\xBB\xBF" "a\r\nb");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.lineEnding(), int(LineEnding::Windows));
    QVERIFY(document.hasBom());
    document.setCursor({1, 1});
    document.insertText(QStringLiteral("c"));
    QVERIFY(document.save());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    // No final newline was added: the file had none.
    QCOMPARE(file.readAll(), QByteArray("\xEF\xBB\xBF" "a\r\nbc"));

    document.setLineEnding(int(LineEnding::Unix));
    document.setBom(false);
    QVERIFY(document.isModified());
    QVERIFY(document.save());
    file.close();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("a\nbc"));
}

void CoreTest::saveThroughSymlinkKeepsPermissions()
{
    const QString target = writeFile(QStringLiteral("target.sh"), "echo hi\n");
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString link = m_dir.filePath(QStringLiteral("link.sh"));
    QVERIFY(QFile::link(target, link));
    Document document;
    document.load(link);
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.language(), QStringLiteral("sh"));
    document.setCursor({0, 7});
    document.insertText(QStringLiteral("!"));
    QVERIFY(document.save());
    QVERIFY(QFileInfo(link).isSymLink());
    QVERIFY(QFileInfo(target).permissions() & QFileDevice::ExeOwner);
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("echo hi!\n"));
}

void CoreTest::externalChangeReloadsUnmodified()
{
    const QString path = writeFile(QStringLiteral("watched.txt"), "first\nsecond\n");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    document.setCursor({1, 3});
    QTest::qWait(1100);
    writeFile(QStringLiteral("watched.txt"), "first\nchanged here\n");
    QTRY_COMPARE_WITH_TIMEOUT(document.buffer().line(1), QStringLiteral("changed here"), 5000);
    QVERIFY(!document.isModified());
    QCOMPARE(document.cursor(), (TextPosition{1, 3}));
    QCOMPARE(document.notice(), int(Document::NoNotice));
}

void CoreTest::externalChangeWhileModified()
{
    const QString path = writeFile(QStringLiteral("conflict.txt"), "mine\n");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    document.setCursor({0, 4});
    document.insertText(QStringLiteral("!"));
    QTest::qWait(1100);
    writeFile(QStringLiteral("conflict.txt"), "theirs\n");
    QTRY_COMPARE_WITH_TIMEOUT(document.notice(), int(Document::ChangedOnDisk), 5000);
    QCOMPARE(document.buffer().line(0), QStringLiteral("mine!"));

    document.keepLocalVersion();
    QCOMPARE(document.notice(), int(Document::NoNotice));
    document.undo();
    // The disk copy is someone else's now: back at the loaded text is still modified.
    QVERIFY(document.isModified());

    document.reload();
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.buffer().line(0), QStringLiteral("theirs"));
    QVERIFY(!document.isModified());
}

void CoreTest::externalDelete()
{
    const QString path = writeFile(QStringLiteral("doomed.txt"), "bye\n");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QFile::remove(path);
    QTRY_COMPARE_WITH_TIMEOUT(document.notice(), int(Document::Deleted), 5000);
    QVERIFY(document.isModified());
    QVERIFY(document.save());
    QVERIFY(QFile::exists(path));
    QVERIFY(!document.isModified());
}

void CoreTest::reopenWithEncoding()
{
    const QString text = QStringLiteral("编码测试");
    const QString path = writeFile(QStringLiteral("gbk.txt"), QByteArray(QStringEncoder("GB18030").encode(text)));
    Document document;
    document.load(path, QStringLiteral("UTF-8"));
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.notice(), int(Document::Malformed));
    document.reloadWithEncoding(QStringLiteral("GB18030"));
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.buffer().line(0), text);
    QCOMPARE(document.encoding(), QStringLiteral("GB18030"));
    QCOMPARE(document.notice(), int(Document::NoNotice));

    // Saving with another encoding converts the file.
    document.setEncoding(QStringLiteral("UTF-8"));
    QVERIFY(document.isModified());
    QVERIFY(document.save());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), text);

    // A character the encoding lacks stops the save instead of being lost.
    document.setEncoding(QStringLiteral("ISO-8859-1"));
    QVERIFY(!document.save());
    QCOMPARE(document.notice(), int(Document::LossyEncoding));
    QVERIFY(document.saveAsUtf8());
}

void CoreTest::tooLarge()
{
    const QString path = m_dir.filePath(QStringLiteral("huge.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(Document::maximumFileSize + 1));
    file.close();
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.notice(), int(Document::TooLarge));
    QFile::remove(path);
}

void CoreTest::saveRefusesOverwritingExternalChange()
{
    const QString path = writeFile(QStringLiteral("race.txt"), "base\n");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    document.setCursor({0, 4});
    document.insertText(QStringLiteral(" mine"));
    // Changed on disk just before the save, before any watcher can tell.
    QTest::qWait(20);
    writeFile(QStringLiteral("race.txt"), "theirs, longer\n");
    QVERIFY(!document.save());
    QCOMPARE(document.notice(), int(Document::ChangedOnDisk));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("theirs, longer\n"));
    file.close();
    document.keepLocalVersion();
    QVERIFY(document.save());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("base mine\n"));
}

void CoreTest::saveRefusesLosingMalformedBytes()
{
    const QString path = writeFile(QStringLiteral("bad.txt"), "ok \xff here\n");
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.encoding(), QStringLiteral("windows-1252"));
    document.reloadWithEncoding(QStringLiteral("UTF-8"));
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.notice(), int(Document::Malformed));
    document.setCursor({0, 0});
    document.insertText(QStringLiteral("x"));
    QVERIFY(!document.save());
    QCOMPARE(document.notice(), int(Document::MalformedSave));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("ok \xff here\n"));
    file.close();
    // Another encoding cannot bring them back either.
    document.setEncoding(QStringLiteral("UTF-16LE"));
    QVERIFY(!document.save());
    QVERIFY(document.saveReplacingMalformed());
    QVERIFY(!document.isModified());
}

void CoreTest::saveInReadOnlyFolder()
{
    const QString path = writeFile(QStringLiteral("locked/inside.txt"), "a\n");
    const QString folder = QFileInfo(path).absolutePath();
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    document.setCursor({0, 1});
    document.insertText(QStringLiteral("b"));
    const bool saved = document.save();
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QVERIFY(saved);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("ab\n"));
}

void CoreTest::saveUnwritableFileKeepsText()
{
    const QString path = writeFile(QStringLiteral("readonly.txt"), "keep\n");
    QFile::setPermissions(path, QFileDevice::ReadOwner);
    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QVERIFY(!document.isWritable());
    QCOMPARE(document.notice(), int(Document::ReadOnly));
    document.setCursor({0, 4});
    document.insertText(QStringLiteral("!"));
    QVERIFY(!document.save());
    QVERIFY(document.isModified());
    QCOMPARE(document.buffer().line(0), QStringLiteral("keep!"));
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep\n"));
}

void CoreTest::deletedFileKeepsDraft()
{
    const QString path = writeFile(QStringLiteral("gone.txt"), "text\n");
    QString id;
    {
        Document document;
        id = document.draftId();
        document.load(path);
        QVERIFY(document.waitForLoaded());
        document.setCursor({0, 4});
        document.insertText(QStringLiteral(" unsaved"));
        document.writeDraftNow();
    }
    QFile::remove(path);

    // As a restored session does: the file is gone, the draft is not.
    Document restored(nullptr, id);
    restored.load(path);
    restored.restoreDraft();
    QVERIFY(restored.waitForLoaded());
    QCOMPARE(restored.buffer().line(0), QStringLiteral("text unsaved"));
    QVERIFY(restored.isModified());
    QCOMPARE(restored.notice(), int(Document::Deleted));
    QVERIFY(QFile::exists(restored.draftPath()));
    QVERIFY(restored.save());
    QVERIFY(QFile::exists(path));
}

void CoreTest::draftWritesAreOrdered()
{
    Document document;
    document.insertText(QStringLiteral("one"));
    QMetaObject::invokeMethod(&document, "writeDraftAsync");
    document.insertText(QStringLiteral(" two"));
    document.writeDraftNow();
    QFile file(document.draftPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("one two"));
    file.close();
    document.removeDraft();
    QVERIFY(!QFile::exists(document.draftPath()));
    QCOMPARE(QFileInfo(QFileInfo(document.draftPath()).absolutePath()).permissions()
                     & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
}

void CoreTest::corruptSessionKeepsDrafts()
{
    const QString path = writeFile(QStringLiteral("broken/session.json"), "{\"windows\": [");
    bool ok = true;
    QVERIFY(Session::load(path, &ok).isEmpty());
    QVERIFY(!ok);
    Session::load(m_dir.filePath(QStringLiteral("missing.json")), &ok);
    QVERIFY(ok);

    // Fresh orphans stay while they may still be wanted.
    const QString drafts = m_dir.filePath(QStringLiteral("orphans"));
    QDir().mkpath(drafts);
    writeFile(QStringLiteral("orphans/fresh.txt"), "x");
    Session::removeOrphanDrafts(drafts, {}, 7);
    QVERIFY(QFile::exists(drafts + QStringLiteral("/fresh.txt")));
}

void CoreTest::replaceAllThenTypingUndoesSeparately()
{
    Document document;
    document.setText(QStringLiteral("a a a"));
    document.setSearch(QStringLiteral("a"), false, false, false, false);
    QCOMPARE(document.replaceAll(QStringLiteral("b")), 3);
    document.setCursor({0, 5});
    document.insertText(QStringLiteral("c"));
    QCOMPARE(document.text(), QStringLiteral("b b bc"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("b b b"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("a a a"));
    document.redo();
    document.redo();
    QCOMPARE(document.text(), QStringLiteral("b b bc"));
}

void CoreTest::encodingRoundTripAll()
{
    // Text each encoding can hold: ASCII plus a character of its own script.
    const QHash<QByteArray, QString> samples{
        {"GB18030", QStringLiteral("中文")}, {"Big5", QStringLiteral("中文")}, {"Big5-HKSCS", QStringLiteral("中文")},
        {"Shift_JIS", QStringLiteral("日本")}, {"EUC-JP", QStringLiteral("日本")}, {"EUC-KR", QStringLiteral("한국")},
        {"windows-1251", QStringLiteral("Привет")}, {"KOI8-R", QStringLiteral("Привет")}, {"KOI8-U", QStringLiteral("Привіт")},
        {"windows-1253", QStringLiteral("Γεια")}, {"windows-1255", QStringLiteral("שלום")}, {"windows-1256", QStringLiteral("سلام")},
        {"windows-874", QStringLiteral("สวัสดี")}, {"windows-1258", QStringLiteral("Đà")},
    };
    for (const EncodingInfo &info : TextCodec::encodings()) {
        if (!TextCodec::isSupported(info.name))
            continue;
        const QString special = samples.value(info.name, QStringLiteral("é"));
        const QList<QString> lines{QStringLiteral("plain ascii"), special, QString()};
        bool lossy = true;
        const QByteArray bytes = TextCodec::encode(lines, info.name, false, LineEnding::Unix, &lossy);
        QVERIFY2(!lossy, info.name.constData());
        const DecodedText decoded = TextCodec::decode(bytes, info.name);
        QVERIFY2(decoded.lines == lines, info.name.constData());
        QVERIFY2(!decoded.malformed, info.name.constData());
    }
}

void CoreTest::editingCommands()
{
    Document document;
    document.setText(QStringLiteral("a\nb\nc"));
    document.setCursor({1, 0});
    document.moveLines(true);
    QCOMPARE(document.text(), QStringLiteral("b\na\nc"));
    QCOMPARE(document.cursor().line, 0);
    document.moveLines(false);
    document.moveLines(false);
    QCOMPARE(document.text(), QStringLiteral("a\nc\nb"));
    document.duplicateLines();
    QCOMPARE(document.text(), QStringLiteral("a\nc\nb\nb"));
    QCOMPARE(document.cursor().line, 3);
    document.deleteLines();
    QCOMPARE(document.text(), QStringLiteral("a\nc\nb"));
    document.setCursor({0, 0});
    document.deleteLines();
    QCOMPARE(document.text(), QStringLiteral("c\nb"));

    document.setText(QStringLiteral("hello wide world"));
    document.setCursor({0, 10}, {0, 6});
    document.changeCase(Document::Upper);
    QCOMPARE(document.text(), QStringLiteral("hello WIDE world"));
    document.selectAll();
    document.changeCase(Document::Title);
    QCOMPARE(document.text(), QStringLiteral("Hello Wide World"));

    document.setText(QStringLiteral("    indented"));
    document.setCursor({0, 12});
    document.insertNewline(true);
    QCOMPARE(document.text(), QStringLiteral("    indented\n    "));

    document.setText(QStringLiteral("abc"));
    document.setCursor({0, 0});
    document.insertText(QStringLiteral("XY"), true);
    QCOMPARE(document.text(), QStringLiteral("XYc"));
}

void CoreTest::indentCommands()
{
    Document document;
    document.setIndentationDefaults(true, 4);
    document.setText(QStringLiteral("a\nb\n\nc"));
    document.setCursor({3, 1}, {0, 0});
    document.indentSelection(false);
    QCOMPARE(document.text(), QStringLiteral("    a\n    b\n\n    c"));
    document.indentSelection(true);
    QCOMPARE(document.text(), QStringLiteral("a\nb\n\nc"));
    document.undo();
    QCOMPARE(document.text(), QStringLiteral("    a\n    b\n\n    c"));

    // Backspace in leading spaces returns to the previous indentation stop.
    document.setText(QStringLiteral("      x"));
    document.setCursor({0, 6});
    document.deleteBackward(false);
    QCOMPARE(document.text(), QStringLiteral("    x"));
}

void CoreTest::wordMovement()
{
    Document document;
    document.setText(QStringLiteral("foo.bar  baz\nnext"));
    QCOMPARE(document.wordBoundary({0, 0}, true), (TextPosition{0, 3}));
    QCOMPARE(document.wordBoundary({0, 3}, true), (TextPosition{0, 4}));
    QCOMPARE(document.wordBoundary({0, 7}, true), (TextPosition{0, 12}));
    QCOMPARE(document.wordBoundary({0, 12}, true), (TextPosition{1, 0}));
    QCOMPARE(document.wordBoundary({0, 9}, false), (TextPosition{0, 4}));
    QCOMPARE(document.wordAt({0, 5}), (TextRange{{0, 4}, {0, 7}}));
    document.setCursor({0, 12});
    document.deleteBackward(true);
    QCOMPARE(document.text(), QStringLiteral("foo.bar  \nnext"));
}

void CoreTest::bracketMatching()
{
    Document document;
    document.setText(QStringLiteral("f(a, [b]) {\n  x\n}"));
    QCOMPARE(document.matchingBracket({0, 1}), (TextRange{{0, 1}, {0, 8}}));
    QCOMPARE(document.matchingBracket({0, 9}), (TextRange{{0, 8}, {0, 1}}));
    QCOMPARE(document.matchingBracket({0, 10}), (TextRange{{0, 10}, {2, 0}}));
    QCOMPARE(document.matchingBracket({1, 1}).start.line, -1);
}

void CoreTest::statistics()
{
    Document document;
    document.setText(QStringLiteral("two words\n中文"));
    const QVariantMap stats = document.statistics();
    QCOMPARE(stats.value(QStringLiteral("lines")).toInt(), 2);
    QCOMPARE(stats.value(QStringLiteral("words")).toInt(), 4);
    QCOMPARE(stats.value(QStringLiteral("characters")).toInt(), 12);
    QCOMPARE(stats.value(QStringLiteral("charactersWithoutSpaces")).toInt(), 10);
}

void CoreTest::editorConfig()
{
    QVERIFY(EditorConfig::globMatches(QStringLiteral("*.py"), QStringLiteral("src/a.py")));
    QVERIFY(EditorConfig::globMatches(QStringLiteral("*.{c,h}"), QStringLiteral("x.h")));
    QVERIFY(!EditorConfig::globMatches(QStringLiteral("*.{c,h}"), QStringLiteral("x.cpp")));
    QVERIFY(EditorConfig::globMatches(QStringLiteral("lib/**.js"), QStringLiteral("lib/a/b.js")));
    QVERIFY(!EditorConfig::globMatches(QStringLiteral("/top.txt"), QStringLiteral("sub/top.txt")));

    writeFile(QStringLiteral("ec/.editorconfig"),
              "root = true\n[*]\nindent_style = space\nindent_size = 2\n"
              "[*.mk]\nindent_style = tab\ntab_width = 8\n"
              "[sub/*.txt]\ntrim_trailing_whitespace = true\nend_of_line = crlf\n");
    writeFile(QStringLiteral("ec/sub/.editorconfig"), "[*.txt]\ninsert_final_newline = false\n");
    const QString path = writeFile(QStringLiteral("ec/sub/a.txt"), "x  \ny\n");

    const EditorConfig config = EditorConfig::forFile(path);
    QCOMPARE(config.insertSpaces.value(), true);
    QCOMPARE(config.indentSize.value(), 2);
    QVERIFY(config.trimTrailingWhitespace.value());
    QVERIFY(!config.insertFinalNewline.value());
    QCOMPARE(config.lineEnding.value(), LineEnding::Windows);

    const EditorConfig make = EditorConfig::forFile(m_dir.filePath(QStringLiteral("ec/rules.mk")));
    QCOMPARE(make.insertSpaces.value(), false);
    QCOMPARE(make.tabWidth.value(), 8);

    Document document;
    document.load(path);
    QVERIFY(document.waitForLoaded());
    QCOMPARE(document.indentWidth(), 2);
    document.setCursor({1, 1});
    document.insertText(QStringLiteral("z "));
    QVERIFY(document.save());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("x\nyz\n"));
}

void CoreTest::indentationDetection()
{
    const QList<QString> spaces{QStringLiteral("a {"), QStringLiteral("  b {"), QStringLiteral("    c"),
                                QStringLiteral("  }"), QStringLiteral("  d"), QStringLiteral("}")};
    const auto detected = FileOptions::detectIndentation(spaces);
    QVERIFY(detected);
    QVERIFY(detected->insertSpaces);
    QCOMPARE(detected->width, 2);

    const QList<QString> tabs{QStringLiteral("a"), QStringLiteral("\tb"), QStringLiteral("\t\tc"), QStringLiteral("\td")};
    QVERIFY(!FileOptions::detectIndentation(tabs)->insertSpaces);
    QVERIFY(!FileOptions::detectIndentation({QStringLiteral("flat")}));
}

void CoreTest::languageDetection()
{
    QCOMPARE(Syntax::detect(QStringLiteral("/x/main.cpp"), QString()), QStringLiteral("cpp"));
    QCOMPARE(Syntax::detect(QStringLiteral("/x/CMakeLists.txt"), QString()), QStringLiteral("cmake"));
    QCOMPARE(Syntax::detect(QStringLiteral("/x/notes.txt"), QString()), QString());
    QCOMPARE(Syntax::detect(QStringLiteral("/x/script"), QStringLiteral("#!/usr/bin/env python3")), QStringLiteral("python"));
    QCOMPARE(Syntax::detect(QString(), QStringLiteral("#!/bin/bash")), QStringLiteral("sh"));
    QCOMPARE(Syntax::detect(QStringLiteral("Makefile"), QString()), QStringLiteral("makefile"));
    QCOMPARE(Syntax::detect(QStringLiteral("a.desktop"), QString()), QStringLiteral("ini"));
    QCOMPARE(Syntax::detect(QStringLiteral("README.md"), QString()), QStringLiteral("markdown"));

    // An untitled document follows its first line.
    Document document;
    document.insertText(QStringLiteral("#!/bin/sh\necho"));
    QCOMPARE(document.language(), QStringLiteral("sh"));
}

void CoreTest::highlightCarriesState()
{
    const Language *c = Syntax::language(QStringLiteral("c"));
    QList<SyntaxSpan> spans;
    Syntax::State state = Syntax::highlight(c, QStringLiteral("int x; /* open"), 0, &spans);
    QVERIFY(state != 0);
    QCOMPARE(spans.first().style, SyntaxStyle::Type);
    QCOMPARE(spans.last().style, SyntaxStyle::Comment);
    spans.clear();
    state = Syntax::highlight(c, QStringLiteral("still */ return \"s\\n\";"), state, &spans);
    QCOMPARE(state, 0);
    QCOMPARE(spans.at(0).style, SyntaxStyle::Comment);
    QCOMPARE(spans.at(0).length, 8);
    QCOMPARE(spans.at(1).style, SyntaxStyle::Keyword);
    QCOMPARE(spans.at(2).style, SyntaxStyle::String);
    QCOMPARE(spans.at(3).style, SyntaxStyle::Escape);

    const Language *python = Syntax::language(QStringLiteral("python"));
    spans.clear();
    state = Syntax::highlight(python, QStringLiteral("x = \"\"\"doc"), 0, &spans);
    QVERIFY(state != 0);
    spans.clear();
    QCOMPARE(Syntax::highlight(python, QStringLiteral("end\"\"\" # c"), state, &spans), 0);
    QCOMPARE(spans.last().style, SyntaxStyle::Comment);

    const Language *markdown = Syntax::language(QStringLiteral("markdown"));
    spans.clear();
    Syntax::highlight(markdown, QStringLiteral("a **bold** and `code`"), 0, &spans);
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans.at(0).style, SyntaxStyle::Strong);
    QCOMPARE(spans.at(1).style, SyntaxStyle::Code);
}

void CoreTest::syntaxStatesResume()
{
    TextBuffer buffer;
    QStringList lines;
    for (int i = 0; i < 1000; ++i)
        lines.append(QStringLiteral("int a%1;").arg(i));
    buffer.setLines(lines);
    SyntaxStates states;
    states.reset(Syntax::language(QStringLiteral("c")), buffer.lineCount());
    int last = -1;
    states.advance(buffer, 999, 1000, &last);
    QCOMPARE(states.validLines(), 1000);

    // An edit that keeps the end state leaves the lines below valid.
    buffer.remove({{10, 0}, {10, 3}});
    states.linesChanged(10, 1, 1);
    QCOMPARE(states.validLines(), 10);
    QCOMPARE(states.advance(buffer, 999, 1000, &last), -1);
    QCOMPARE(states.validLines(), 1000);

    // Opening a comment changes every line below.
    buffer.insert({10, 0}, QStringLiteral("/*"));
    states.linesChanged(10, 1, 1);
    const int first = states.advance(buffer, 999, 1000, &last);
    QCOMPARE(first, 11);
    QCOMPARE(last, 999);
    QVERIFY(states.before(500) != 0);
}

void CoreTest::sessionRoundTrip()
{
    const QString path = m_dir.filePath(QStringLiteral("session/session.json"));
    QList<WindowState> windows(2);
    windows[0].tabs = {TabState{QStringLiteral("/a.txt"), QStringLiteral("d1"), false, 3, 4, 120.5, QStringLiteral("GB18030"), QString()},
                       TabState{QString(), QStringLiteral("d2"), true, 0, 0, 0, QString(), QStringLiteral("python")}};
    windows[0].current = 1;
    windows[1].tabs = {TabState{QString(), QStringLiteral("d3"), false, 0, 0, 0, QString(), QString()}};
    QVERIFY(Session::save(windows, path));

    const QList<WindowState> loaded = Session::load(path);
    // The empty untitled tab is not brought back.
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).tabs.size(), 2);
    QCOMPARE(loaded.at(0).current, 1);
    QCOMPARE(loaded.at(0).tabs.at(0).path, QStringLiteral("/a.txt"));
    QCOMPARE(loaded.at(0).tabs.at(0).line, 3);
    QCOMPARE(loaded.at(0).tabs.at(0).scroll, 120.5);
    QCOMPARE(loaded.at(0).tabs.at(0).encoding, QStringLiteral("GB18030"));
    QVERIFY(loaded.at(0).tabs.at(1).unsaved);
    QCOMPARE(loaded.at(0).tabs.at(1).language, QStringLiteral("python"));

    const QString drafts = m_dir.filePath(QStringLiteral("drafts"));
    QDir().mkpath(drafts);
    writeFile(QStringLiteral("drafts/keep.txt"), "x");
    writeFile(QStringLiteral("drafts/old.txt"), "y");
    Session::removeOrphanDrafts(drafts, {QStringLiteral("keep")});
    QVERIFY(QFile::exists(drafts + QStringLiteral("/keep.txt")));
    QVERIFY(!QFile::exists(drafts + QStringLiteral("/old.txt")));
}

void CoreTest::draftRestore()
{
    const QString path = writeFile(QStringLiteral("drafted.txt"), "saved text\n");
    QString id;
    {
        Document document;
        id = document.draftId();
        document.load(path);
        QVERIFY(document.waitForLoaded());
        document.setCursor({0, 10});
        document.insertText(QStringLiteral(" plus edits"));
        document.writeDraftNow();
        QVERIFY(QFile::exists(document.draftPath()));
    }

    Document restored(nullptr, id);
    restored.load(path);
    restored.restoreDraft();
    QVERIFY(restored.waitForLoaded());
    QCOMPARE(restored.buffer().line(0), QStringLiteral("saved text plus edits"));
    QVERIFY(restored.isModified());
    QCOMPARE(restored.notice(), int(Document::Restored));
    QVERIFY(restored.save());
    QVERIFY(!QFile::exists(restored.draftPath()));

    // An untitled draft comes back as an unsaved document.
    QString untitledId;
    {
        Document untitled;
        untitledId = untitled.draftId();
        untitled.insertText(QStringLiteral("scratch"));
        untitled.writeDraftNow();
    }
    Document back(nullptr, untitledId);
    QVERIFY(back.restoreDraft());
    QCOMPARE(back.text(), QStringLiteral("scratch"));
    QVERIFY(back.isModified());
    back.removeDraft();
}

void CoreTest::recentFiles()
{
    const QString store = m_dir.filePath(QStringLiteral("recently-used.xbel"));
    writeFile(QStringLiteral("recently-used.xbel"),
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<xbel version=\"1.0\" xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\" "
              "xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
              "<bookmark href=\"file:///other/app.png\" added=\"2026-01-01T00:00:00Z\" modified=\"2026-01-01T00:00:00Z\">"
              "<info><metadata owner=\"http://freedesktop.org\"><mime:mime-type type=\"image/png\"/>"
              "<bookmark:applications><bookmark:application name=\"viewer\" exec=\"viewer %u\" "
              "modified=\"2026-01-01T00:00:00Z\" count=\"1\"/></bookmark:applications></metadata></info></bookmark>\n"
              "</xbel>\n");
    const QString a = writeFile(QStringLiteral("recent a.txt"), "a");
    const QString b = writeFile(QStringLiteral("recent-b.txt"), "b");

    RecentFiles recent(store);
    QVERIFY(recent.files().isEmpty());
    recent.add(a, QStringLiteral("text/plain"));
    QTest::qWait(5);
    recent.add(b, QStringLiteral("text/plain"));
    QCOMPARE(recent.files(), (QStringList{b, a}));

    RecentFiles again(store);
    QCOMPARE(again.files(), (QStringList{b, a}));
    again.clear();
    QVERIFY(again.files().isEmpty());

    // Other applications' entries survive.
    QFile file(store);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains("file:///other/app.png"));
    QVERIFY(!contents.contains("recent-b.txt"));
}

QTEST_GUILESS_MAIN(CoreTest)

#include "tst_core.moc"
