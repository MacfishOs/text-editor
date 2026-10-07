// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "cutefishapplication.h"
#include "document.h"
#include "editorview.h"
#include "syntaxtheme.h"

#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <climits>

static QStringList qmlWarnings;
static QtMessageHandler previousHandler = nullptr;

static void captureWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.contains(QLatin1String("Binding loop")) || message.contains(QLatin1String("TypeError"))
            || message.contains(QLatin1String("ReferenceError")) || message.contains(QLatin1String("Cannot assign"))
            || message.contains(QLatin1String("is not a function")) || message.contains(QLatin1String("recursive rearrange"))
            || message.contains(QLatin1String("qrc:/qml/")))
        qmlWarnings.append(message);
    if (previousHandler)
        previousHandler(type, context, message);
}

static void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text)
        QTest::sendKeyEvent(QTest::Click, window, Qt::Key_unknown, QString(c), Qt::NoModifier);
}

class UiTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void opensFileAndTypes();
    void clickSelection();
    void tabsAndSession();
    void findBarAndStatus();
    void findCardKeepsMatchVisible();
    void gutter_data();
    void gutter();
    void wrappingAndDialogs();
    void gallery_data();
    void gallery();
    void largeFilePerformance();
    void longLinePerformance();
    void closeLastWindow_data();
    void closeLastWindow();

private:
    QQuickWindow *start(const QStringList &files);
    EditorView *editor(QQuickWindow *window) const;
    QString writeFile(const QString &name, const QByteArray &data);
    void grab(QQuickWindow *window, const QString &name);

    QTemporaryDir m_dir;
    QQmlEngine *m_engine = nullptr;
    Application *m_app = nullptr;
    SyntaxThemes *m_themes = nullptr;
    QObject *m_prefs = nullptr;
};

void UiTest::initTestCase()
{
    previousHandler = qInstallMessageHandler(captureWarnings);
    QStandardPaths::setTestModeEnabled(true);
    qmlRegisterType<EditorView>("Cutefish.TextEditor", 1, 0, "EditorView");
    qmlRegisterUncreatableType<Document>("Cutefish.TextEditor", 1, 0, "Document", QString());
    qmlRegisterUncreatableType<RecentFiles>("Cutefish.TextEditor", 1, 0, "RecentFiles", QString());
}

void UiTest::cleanupTestCase()
{
    qInstallMessageHandler(previousHandler);
}

void UiTest::init()
{
    qmlWarnings.clear();
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/cutefish-text-editor"))
            .removeRecursively();
    m_engine = new QQmlEngine;
    m_themes = new SyntaxThemes(m_engine);
    m_app = new Application(m_engine, m_engine);
    QQmlComponent settings(m_engine, QUrl(QStringLiteral("qrc:/qml/EditorSettings.qml")));
    m_prefs = settings.create();
    QVERIFY2(m_prefs, qPrintable(settings.errorString()));
    m_prefs->setParent(m_engine);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Prefs"), m_prefs);
    m_engine->rootContext()->setContextProperty(QStringLiteral("App"), m_app);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Themes"), m_themes);
}

void UiTest::cleanup()
{
    for (const QVariant &window : m_app->windowList())
        delete window.value<QQuickWindow *>();
    delete m_engine;
    m_engine = nullptr;
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
}

QString UiTest::writeFile(const QString &name, const QByteArray &data)
{
    const QString path = m_dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
    return path;
}

QQuickWindow *UiTest::start(const QStringList &files)
{
    m_app->start(files, m_dir.path(), QString());
    const QVariantList windows = m_app->windowList();
    if (windows.isEmpty())
        return nullptr;
    auto *window = windows.last().value<QQuickWindow *>();
    window->show();
    if (!QTest::qWaitForWindowExposed(window))
        return nullptr;
    return window;
}

EditorView *UiTest::editor(QQuickWindow *window) const
{
    QQuickItem *page = window->property("currentPage").value<QQuickItem *>();
    return page ? page->findChild<EditorView *>(QStringLiteral("editorView")) : nullptr;
}

void UiTest::grab(QQuickWindow *window, const QString &name)
{
    // Lets polish run first, as a real frame would.
    QTest::qWait(50);
    const QImage image = window->grabWindow();
    image.save(QDir::temp().filePath(QStringLiteral("text-editor-%1.png").arg(name)));
}

void UiTest::opensFileAndTypes()
{
    const QString path = writeFile(QStringLiteral("hello.c"), "int main(void)\n{\n    return 0; /* done */\n}\n");
    QQuickWindow *window = start({path});
    QVERIFY(window);
    EditorView *view = editor(window);
    QVERIFY(view);
    Document *document = view->document();
    QVERIFY(document->waitForLoaded());
    QTRY_COMPARE(document->language(), QStringLiteral("c"));
    QCOMPARE(window->title(), QStringLiteral("hello.c"));
    QTRY_VERIFY(view->contentHeight() > 4 * view->lineHeight());
    QVERIFY(view->hasActiveFocus());

    document->setCursor({2, 4});
    typeText(window, QStringLiteral("x = 1;"));
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(document->buffer().line(2), QStringLiteral("    x = 1;"));
    // Enter keeps the indentation.
    QCOMPARE(document->buffer().line(3), QStringLiteral("    return 0; /* done */"));
    QCOMPARE(document->cursor(), (TextPosition{3, 4}));
    QVERIFY(document->isModified());

    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(!document->isModified());

    // A click lands on the text under the pointer.
    const QPoint text = view->mapToScene(QPointF(view->gutterWidth() + 2, 6 + view->lineHeight() * 1.5)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, text);
    QCOMPARE(document->cursor().line, 1);
    grab(window, QStringLiteral("file"));
}

void UiTest::clickSelection()
{
    const QString path = writeFile(QStringLiteral("words.txt"), "alpha beta gamma\nsecond line\n");
    QQuickWindow *window = start({path});
    QVERIFY(window);
    EditorView *view = editor(window);
    QVERIFY(view);
    Document *document = view->document();
    QVERIFY(document->waitForLoaded());
    QTRY_VERIFY(view->contentHeight() > 2 * view->lineHeight());

    const qreal advance = QFontMetricsF(view->font()).horizontalAdvance(QLatin1Char('a'));
    const QPoint beta = view->mapToScene(QPointF(view->gutterWidth() + advance * 8, 6 + view->lineHeight() * 0.5)).toPoint();

    // A double click selects the word, a third click the line.
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, beta);
    QCOMPARE(document->selectedText(), QStringLiteral("beta"));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, beta);
    QCOMPARE(document->selectedText(), QStringLiteral("alpha beta gamma\n"));
}

void UiTest::tabsAndSession()
{
    const QString a = writeFile(QStringLiteral("a.txt"), "first file\n");
    const QString b = writeFile(QStringLiteral("b.md"), "# Title\n\nSome *text*.\n");
    QQuickWindow *window = start({a, b});
    QVERIFY(window);
    QTRY_COMPARE(window->property("tabCount").toInt(), 2);
    // Opening an open file again focuses its tab.
    m_app->openPaths({a});
    QCOMPARE(window->property("tabCount").toInt(), 2);
    QCOMPARE(editor(window)->document()->filePath(), a);

    QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier);
    QCOMPARE(window->property("tabCount").toInt(), 3);
    typeText(window, QStringLiteral("draft text"));
    QCOMPARE(editor(window)->document()->title(), QStringLiteral("draft text"));
    grab(window, QStringLiteral("tabs"));

    m_app->saveSession();
    editor(window)->document()->writeDraftNow();
    const QList<WindowState> session = Session::load();
    QCOMPARE(session.size(), 1);
    QCOMPARE(session.first().tabs.size(), 3);
    QVERIFY(session.first().tabs.at(2).unsaved);
    QCOMPARE(session.first().current, 2);

    // A new process restores it, the untitled text included.
    for (const QVariant &w : m_app->windowList())
        delete w.value<QQuickWindow *>();
    QQuickWindow *restored = start({});
    QVERIFY(restored);
    QTRY_COMPARE(restored->property("tabCount").toInt(), 3);
    QCOMPARE(editor(restored)->document()->text(), QStringLiteral("draft text"));
    QVERIFY(editor(restored)->document()->isModified());

    // Closing a tab can be undone.
    QMetaObject::invokeMethod(restored, "closeTab", Q_ARG(QVariant, 0), Q_ARG(QVariant, false));
    QCOMPARE(restored->property("tabCount").toInt(), 2);
    QVERIFY(m_app->canReopenClosedTab());
    QTest::keyClick(restored, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(restored->property("tabCount").toInt(), 3);
}

void UiTest::findBarAndStatus()
{
    const QString path = writeFile(QStringLiteral("find.txt"), "apple banana apple\ncherry apple\n");
    QQuickWindow *window = start({path});
    QVERIFY(window);
    Document *document = editor(window)->document();
    QVERIFY(document->waitForLoaded());

    // Window shortcuts only fire in the active window; a compositor that hands
    // out no keyboard focus (headless, unattended) cannot run this part.
    window->requestActivate();
    if (!QTest::qWaitFor([window] { return window->isActive(); }, 3000))
        QSKIP("The window was not activated, so window shortcuts cannot fire");
    QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
    QQuickItem *findBar = window->findChild<QQuickItem *>(QStringLiteral("findBar"));
    QVERIFY(findBar);
    QTRY_VERIFY2(findBar->isVisible(), qPrintable(qmlWarnings.join(QLatin1Char('\n'))));
    typeText(window, QStringLiteral("apple"));
    QTRY_COMPARE(document->matchCount(), 3);
    QCOMPARE(document->selection(), (TextRange{{0, 0}, {0, 5}}));
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(document->selection(), (TextRange{{0, 13}, {0, 18}}));
    QCOMPARE(document->matchIndex(), 2);
    grab(window, QStringLiteral("find"));
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!findBar->isVisible());
    QVERIFY(editor(window)->hasActiveFocus());

    QTest::keyClick(window, Qt::Key_H, Qt::ControlModifier);
    QQuickItem *replaceField = window->findChild<QQuickItem *>(QStringLiteral("replaceField"));
    QVERIFY(replaceField && replaceField->isVisible());
    // Inside the card: both fields start on one edge, the plates at either
    // end sit as far from the card's edges, and the rows end on one edge.
    auto edges = [window](const char *name, bool plate = false) {
        QQuickItem *item = window->findChild<QQuickItem *>(QLatin1String(name));
        if (item && plate)
            item = item->property("background").value<QQuickItem *>();
        return item ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF();
    };
    // Plates are snapped to device pixels, so within one of them.
    const qreal pixel = 1.0 / window->effectiveDevicePixelRatio() + 0.01;
    const QRectF card = edges("findBar");
    QTRY_VERIFY(qAbs(edges("replaceField").left() - edges("findField").left()) <= pixel);
    QTRY_VERIFY(qAbs(edges("replaceField").right() + 6 + edges("replaceButton").width() + 6
                     + edges("replaceAllButton").width() - edges("replaceAllButton").right()) <= pixel);
    QTRY_VERIFY2(qAbs(edges("replaceAllButton").right() - edges("closeFindButton", true).right()) <= pixel,
             qPrintable(QStringLiteral("%1 %2 card %3").arg(edges("replaceAllButton").right())
                                .arg(edges("closeFindButton", true).right()).arg(card.right())));
    const qreal leftPadding = edges("replaceToggle", true).left() - card.left();
    const qreal rightPadding = card.right() - edges("closeFindButton", true).right();
    QVERIFY2(qAbs(leftPadding - rightPadding) <= pixel, qPrintable(QStringLiteral("%1 %2").arg(leftPadding).arg(rightPadding)));
    // The card is inset from the editor's top and right edges alike.
    const QRectF area = editor(window)->mapRectToScene(QRectF(0, 0, editor(window)->width(), editor(window)->height()));
    QVERIFY(qAbs((card.top() - area.top()) - (area.right() - card.right())) <= pixel);
    QVERIFY(qAbs(card.top() - area.top() - 12) <= pixel);
    // Opening it moved nothing: the matches here are clear of it.
    QCOMPARE(editor(window)->contentY(), 0.0);
    grab(window, QStringLiteral("replace"));
    QTest::keyClick(window, Qt::Key_Escape);

    QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
    typeText(window, QStringLiteral("2:3"));
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(document->cursor(), (TextPosition{1, 2}));
}

void UiTest::findCardKeepsMatchVisible()
{
    // A match at the end of the first line lies where the card floats.
    const QString path = writeFile(QStringLiteral("under.txt"),
                                   QByteArray(70, 'x') + " needle\nsecond line\nthird\n");
    QQuickWindow *window = start({path});
    QVERIFY(window);
    window->resize(700, 400);
    EditorView *view = editor(window);
    QVERIFY(view->document()->waitForLoaded());
    QTRY_VERIFY(view->contentHeight() > 0);
    const qreal before = view->contentY();
    QMetaObject::invokeMethod(window->property("currentPage").value<QObject *>(), "showFind", Q_ARG(QVariant, false));
    typeText(window, QStringLiteral("needle"));
    QTRY_COMPARE(view->document()->matchCount(), 1);
    QTest::qWait(50);
    QQuickItem *card = window->findChild<QQuickItem *>(QStringLiteral("findBar"));
    const QRectF cardRect = card->mapRectToScene(QRectF(0, 0, card->width(), card->height()));
    const QRectF match = view->mapRectToScene(view->cursorRectangle());
    QVERIFY2(match.top() >= cardRect.bottom(), qPrintable(QStringLiteral("match %1, card bottom %2").arg(match.top()).arg(cardRect.bottom())));
    QVERIFY(view->contentY() < before);
    grab(window, QStringLiteral("under"));

    // Closing puts the first line back at the top.
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(view->contentY(), 0.0);
    QVERIFY(view->hasActiveFocus());
}

void UiTest::gutter_data()
{
    QTest::addColumn<int>("lines");
    QTest::newRow("1 digit") << 5;
    QTest::newRow("2 digits") << 15;
    QTest::newRow("5 digits") << 10050;
}

// Ink: a pixel clearly darker than the background, on a light scheme.
static bool isInk(const QColor &pixel, const QColor &background)
{
    return background.lightness() - pixel.lightness() > 60;
}

void UiTest::gutter()
{
    QFETCH(int, lines);
    QByteArray data;
    for (int i = 0; i < lines; ++i)
        data += "#line\n";
    const QString path = writeFile(QStringLiteral("gutter.c"), data);
    QQuickWindow *window = start({path});
    QVERIFY(window);
    EditorView *view = editor(window);
    QVERIFY(view->document()->waitForLoaded());
    QTRY_VERIFY(view->contentHeight() > view->lineHeight() * lines);
    // The cursor at the end: every number measured is a dimmed one.
    view->document()->goToLine(lines + 1, 1);
    if (lines > 20)
        QTRY_VERIFY(view->topLine() > lines - 40);
    QTest::qWait(50);
    const QImage image = window->grabWindow();
    QVERIFY(!image.isNull());
    const qreal ratio = image.devicePixelRatio();
    image.save(QDir::temp().filePath(QStringLiteral("text-editor-gutter-%1.png").arg(lines)));

    const QColor background = view->backgroundColor();
    const qreal textLeft = view->gutterWidth();
    const QPointF origin = view->mapToScene(QPointF(0, 0));
    int gutterRight = -1;
    int gutterRightMin = INT_MAX;
    int textLeftInk = INT_MAX;
    int gutterDarkest = 255;
    int textDarkest = 255;
    int rows = 0;
    const int first = qCeil(view->topLine());
    const qreal firstTop = 6 + first * view->lineHeight() - view->contentY();
    for (int line = first; line < lines - 1; ++line) {
        const int top = qRound((origin.y() + firstTop + (line - first) * view->lineHeight() + 2) * ratio);
        const int bottom = qRound((origin.y() + firstTop + (line - first + 1) * view->lineHeight() - 2) * ratio);
        if (bottom >= image.height() || top < 0)
            break;
        int rowGutterRight = -1;
        for (int y = top; y < bottom; ++y) {
            for (int x = 0; x < qRound((origin.x() + textLeft + 40) * ratio) && x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (!isInk(pixel, background))
                    continue;
                if (x < qRound((origin.x() + textLeft) * ratio)) {
                    rowGutterRight = qMax(rowGutterRight, x);
                    gutterDarkest = qMin(gutterDarkest, pixel.lightness());
                } else {
                    textLeftInk = qMin(textLeftInk, x);
                    textDarkest = qMin(textDarkest, pixel.lightness());
                }
            }
        }
        if (rowGutterRight < 0)
            continue;
        ++rows;
        gutterRight = qMax(gutterRight, rowGutterRight);
        gutterRightMin = qMin(gutterRightMin, rowGutterRight);
    }
    QVERIFY(rows >= 3);
    // Numbers end on one edge, right-aligned, well clear of the text.
    // Ink edges differ by glyph shape ("1" against "8"), within part of a digit.
    const int digit = qCeil(QFontMetricsF(view->font()).horizontalAdvance(QLatin1Char('0')) * ratio);
    QVERIFY2(gutterRight - gutterRightMin <= digit / 2, qPrintable(QStringLiteral("%1 %2").arg(gutterRight).arg(gutterRightMin)));
    QVERIFY2(textLeftInk - gutterRight >= qRound(8 * ratio),
             qPrintable(QStringLiteral("gap %1 px").arg(textLeftInk - gutterRight)));
    // The numbers are dimmer than the text.
    QVERIFY2(gutterDarkest > textDarkest + 30, qPrintable(QStringLiteral("%1 %2").arg(gutterDarkest).arg(textDarkest)));
}

void UiTest::wrappingAndDialogs()
{
    const QString path = writeFile(QStringLiteral("wrap.md"),
                                   "# Notes\n\n- A list item that is long enough to wrap around the edge of the window at least once, "
                                   "and its continuation should line up with the start of its text rather than the margin.\n"
                                   "Plain **strong** and `code` text.\n");
    QQuickWindow *window = start({path});
    QVERIFY(window);
    window->resize(520, 360);
    EditorView *view = editor(window);
    QVERIFY(view->document()->waitForLoaded());
    QTRY_VERIFY(view->contentHeight() > 5 * view->lineHeight());
    // The long item wraps; the next line starts below its last row.
    const QPoint after = view->positionAt(view->gutterWidth() + 20, 6 + view->lineHeight() * 0.5);
    QCOMPARE(after.y(), 0);
    grab(window, QStringLiteral("wrap"));
    m_prefs->setProperty("colorScheme", QStringLiteral("tokyonight"));
    m_prefs->setProperty("highlightCurrentLine", true);
    m_prefs->setProperty("showWhitespace", true);
    view->document()->setCursor({4, 7}, {4, 2});
    grab(window, QStringLiteral("scheme"));
    m_prefs->setProperty("colorScheme", QStringLiteral("default"));
    m_prefs->setProperty("highlightCurrentLine", false);
    m_prefs->setProperty("showWhitespace", false);

    auto *menu = window->findChild<QQuickWindow *>(QStringLiteral("mainMenu"));
    QVERIFY(menu);
    QMetaObject::invokeMethod(menu, "popup");
    QTRY_VERIFY(menu->isVisible());
    menu->close();

    QMetaObject::invokeMethod(window, "showPreferences");
    QQuickWindow *preferences = window->findChild<QQuickWindow *>(QStringLiteral("preferencesDialog"));
    QVERIFY(preferences);
    QVERIFY(QTest::qWaitForWindowExposed(preferences));
    grab(preferences, QStringLiteral("preferences"));
    preferences->close();
}

void UiTest::gallery_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

// Every surface in one state each, saved for a look at crispness and alignment.
void UiTest::gallery()
{
    QFETCH(bool, dark);
    const QString tag = QStringLiteral("%1-%2").arg(qRound(qApp->devicePixelRatio() * 10)).arg(dark ? "dark" : "light");
    const QString folder = QDir::temp().filePath(QStringLiteral("text-editor-gallery"));
    QDir().mkpath(folder);
    auto save = [&](QWindow *target, const QString &name) {
        QTest::qWait(80);
        auto *quick = qobject_cast<QQuickWindow *>(target);
        const QImage image = quick ? quick->grabWindow() : QImage();
        if (!image.isNull())
            image.save(folder + QStringLiteral("/%1-%2.png").arg(tag, name));
    };

    const QString code = writeFile(QStringLiteral("gallery.cpp"),
                                   "#include <cstdio>\n\n// Prints a greeting.\nint main()\n{\n    const char *name = \"world\";\n"
                                   "    std::printf(\"hello %s\\n\", name); /* done */\n    return 0;\n}\n");
    const QString notes = writeFile(QStringLiteral("notes.md"), "# Notes\n\nSome *text* and `code`.\n");
    const QString broken = writeFile(QStringLiteral("broken.txt"), "caf\xe9 au lait\n");
    QQuickWindow *window = start({code, notes});
    QVERIFY(window);
    if (QObject *theme = m_engine->singletonInstance<QObject *>("FishUI", "Theme"))
        theme->setProperty("darkMode", dark);
    window->resize(820, 520);
    EditorView *view = editor(window);
    QVERIFY(view->document()->waitForLoaded());
    QMetaObject::invokeMethod(window, "selectTab", Q_ARG(QVariant, 0));
    view = editor(window);
    QVERIFY(view->document()->waitForLoaded());
    view->document()->setCursor({6, 8}, {6, 4});
    save(window, QStringLiteral("tabs"));

    QMetaObject::invokeMethod(window->property("currentPage").value<QObject *>(), "showFind", Q_ARG(QVariant, true));
    save(window, QStringLiteral("find"));
    window->findChild<QQuickItem *>(QStringLiteral("findBar"))->setVisible(false);

    for (const char *item : {"positionItem", "indentationItem", "languageItem", "lineEndingItem", "encodingItem"}) {
        QQuickItem *button = window->findChild<QQuickItem *>(QLatin1String(item));
        QVERIFY2(button, item);
        QMetaObject::invokeMethod(button, "clicked");
        QTest::qWait(80);
        for (QWindow *popup : QGuiApplication::topLevelWindows()) {
            if (popup != window && popup->isVisible() && qobject_cast<QQuickWindow *>(popup)) {
                save(popup, QLatin1String(item));
                popup->close();
            }
        }
    }

    if (auto *menu = window->findChild<QQuickWindow *>(QStringLiteral("mainMenu"))) {
        QMetaObject::invokeMethod(menu, "popup");
        QTest::qWait(80);
        save(menu, QStringLiteral("mainmenu"));
        menu->close();
    }

    // Info bars.
    Document *document = view->document();
    document->setCursor({0, 0});
    document->insertText(QStringLiteral("x"));
    QTest::qWait(1100);
    writeFile(QStringLiteral("gallery.cpp"), "changed elsewhere\n");
    QTRY_COMPARE(document->notice(), int(Document::ChangedOnDisk));
    save(window, QStringLiteral("changed"));
    document->keepLocalVersion();

    m_app->openPaths({broken});
    Document *malformed = editor(window)->document();
    QVERIFY(malformed->waitForLoaded());
    malformed->reloadWithEncoding(QStringLiteral("UTF-8"));
    QVERIFY(malformed->waitForLoaded());
    save(window, QStringLiteral("malformed"));

    QMetaObject::invokeMethod(window, "showPreferences");
    if (auto *preferences = window->findChild<QQuickWindow *>(QStringLiteral("preferencesDialog"))) {
        QVERIFY(QTest::qWaitForWindowExposed(preferences));
        save(preferences, QStringLiteral("preferences"));
        preferences->close();
    }
    QMetaObject::invokeMethod(window, "showShortcuts");
    if (auto *shortcuts = window->findChild<QQuickWindow *>(QStringLiteral("shortcutsDialog"))) {
        QVERIFY(QTest::qWaitForWindowExposed(shortcuts));
        save(shortcuts, QStringLiteral("shortcuts"));
        shortcuts->close();
    }

    // The unsaved-changes question, for the modified first tab.
    QMetaObject::invokeMethod(window, "closeTab", Q_ARG(QVariant, 0), Q_ARG(QVariant, true));
    QTest::qWait(100);
    for (QWindow *dialog : QGuiApplication::topLevelWindows()) {
        if (dialog != window && dialog->isVisible() && dialog->title().contains(QLatin1String("gallery.cpp"))) {
            save(dialog, QStringLiteral("unsaved"));
            dialog->close();
        }
    }
    if (QObject *theme = m_engine->singletonInstance<QObject *>("FishUI", "Theme"))
        theme->setProperty("darkMode", false);
}

void UiTest::largeFilePerformance()
{
    // About 24 MB of code-like lines.
    QByteArray data;
    data.reserve(25 * 1024 * 1024);
    for (int i = 0; i < 400000; ++i)
        data += "    if (value_" + QByteArray::number(i) + " > limit) { total += compute(value, \"text\"); } // note\n";
    const QString path = writeFile(QStringLiteral("large.c"), data);

    QQuickWindow *window = start({});
    QVERIFY(window);
    QElapsedTimer timer;
    timer.start();
    m_app->openPaths({path});
    EditorView *view = editor(window);
    Document *document = view->document();
    QVERIFY(document->waitForLoaded(60000));
    const qint64 loadMs = timer.elapsed();
    window->grabWindow();
    const qint64 firstFrameMs = timer.elapsed();
    QCOMPARE(document->lineCount(), 400001);

    // The wheel scrolls through the Flickable under the view; Ctrl zooms.
    const QPointF centre = view->mapToScene(QPointF(view->width() / 2, view->height() / 2));
    QWheelEvent wheel(centre, window->mapToGlobal(centre), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(window, &wheel);
    QTRY_VERIFY(view->contentY() > 0);
    QWheelEvent zoom(centre, window->mapToGlobal(centre), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                     Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(window, &zoom);
    QTRY_VERIFY(window->property("zoom").toReal() > 1);
    QMetaObject::invokeMethod(window, "zoomBy", Q_ARG(QVariant, 0));

    // Typing: one character per frame, each frame rendered.
    document->setCursor({200000, 10});
    view->ensureCursorVisible();
    window->grabWindow();
    timer.restart();
    for (int i = 0; i < 50; ++i) {
        QTest::keyClick(window, Qt::Key_A);
        window->grabWindow();
    }
    const qint64 typingMs = timer.elapsed();

    // The same without rendering: the edit, the layout and the node update.
    timer.restart();
    for (int i = 0; i < 200; ++i) {
        document->insertText(QStringLiteral("b"));
        view->ensurePolished();
    }
    const qint64 editMs = timer.elapsed();

    // Enter splits a line in the middle of the file.
    timer.restart();
    for (int i = 0; i < 20; ++i) {
        QTest::keyClick(window, Qt::Key_Return);
        window->grabWindow();
    }
    const qint64 enterMs = timer.elapsed();

    // Scrolling a screen at a time.
    timer.restart();
    for (int i = 0; i < 50; ++i) {
        view->setContentY(view->contentY() + view->height());
        window->grabWindow();
    }
    const qint64 scrollMs = timer.elapsed();

    timer.restart();
    view->scrollToLine(399000);
    window->grabWindow();
    const qint64 jumpMs = timer.elapsed();

    qInfo("large file: load %lld ms, first frame %lld ms, typing %.1f ms/char (%.2f ms without rendering), "
          "enter %.1f ms, scroll %.1f ms/screen, jump %lld ms",
          loadMs, firstFrameMs, typingMs / 50.0, editMs / 200.0, enterMs / 20.0, scrollMs / 50.0, jumpMs);
    grab(window, QStringLiteral("large"));
    // Generous bounds, so a slow test machine does not fail; the log has the numbers.
    QVERIFY(typingMs / 50 < 50);
    QVERIFY(scrollMs / 50 < 100);
}

void UiTest::longLinePerformance()
{
    // A minified file: one 4 MB line.
    QByteArray data;
    for (int i = 0; i < 200000; ++i)
        data += "a" + QByteArray::number(i) + "=b+c;";
    const QString path = writeFile(QStringLiteral("min.js"), data);
    QQuickWindow *window = start({path});
    QVERIFY(window);
    EditorView *view = editor(window);
    Document *document = view->document();
    QVERIFY(document->waitForLoaded(60000));
    QElapsedTimer timer;
    timer.start();
    window->grabWindow();
    const qint64 firstMs = timer.elapsed();
    document->setCursor({0, 1000});
    view->ensureCursorVisible();
    window->grabWindow();
    timer.restart();
    for (int i = 0; i < 20; ++i) {
        QTest::keyClick(window, Qt::Key_X);
        window->grabWindow();
    }
    const qint64 typingMs = timer.elapsed();
    timer.restart();
    for (int i = 0; i < 50; ++i) {
        document->insertText(QStringLiteral("y"));
        view->ensurePolished();
    }
    const qint64 editMs = timer.elapsed();
    qInfo("long line: first frame %lld ms, typing %.1f ms/char (%.2f ms without rendering)", firstMs,
          typingMs / 20.0, editMs / 50.0);
    QVERIFY(typingMs / 20 < 200);
}

void UiTest::closeLastWindow_data()
{
    QTest::addColumn<bool>("modified");
    QTest::addColumn<bool>("restore");
    QTest::newRow("unmodified, restore on") << false << true;
    QTest::newRow("modified, restore on") << true << true;
    QTest::newRow("unmodified, restore off") << false << false;
    QTest::newRow("modified, restore off") << true << false;
}

// Closing the last window quits from inside its own closing handler; nothing
// may be destroyed under that handler (a qFatal aborts the test).
void UiTest::closeLastWindow()
{
    QFETCH(bool, modified);
    QFETCH(bool, restore);
    m_prefs->setProperty("restoreSession", restore);
    const QString path = writeFile(QStringLiteral("last.txt"), "text\n");
    QQuickWindow *window = start({path});
    QVERIFY(window);
    Document *document = editor(window)->document();
    QVERIFY(document->waitForLoaded());
    if (modified)
        document->insertText(QStringLiteral("more "));

    QSignalSpy quitting(qApp, &QCoreApplication::aboutToQuit);
    window->close();
    if (modified && !restore) {
        // Unsaved changes are asked about first; answer "Don't Save".
        QQuickWindow *dialog = nullptr;
        QTRY_VERIFY((dialog = [window] {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (w != window && w->isVisible() && w->title().contains(QLatin1String("last.txt")))
                    return qobject_cast<QQuickWindow *>(w);
            }
            return static_cast<QQuickWindow *>(nullptr);
        }()));
        QMetaObject::invokeMethod(dialog, "finish", Q_ARG(QVariant, true));
    }
    QTRY_VERIFY(m_app->isQuitting());
    QTest::qWait(100);
    QVERIFY(m_app->isQuitting());
    // With restore on, the session keeps the tab and any unsaved text.
    if (restore) {
        const QList<WindowState> session = Session::load();
        QCOMPARE(session.size(), 1);
        QCOMPARE(session.first().tabs.first().unsaved, modified);
    }
}

int main(int argc, char *argv[])
{
    Cutefish::Application::prepare();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cutefish-text-editor-test"));
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ui.moc"
