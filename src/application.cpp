// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "document.h"
#include "preferences.h"
#include "syntax.h"
#include "textcodec.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>

static constexpr int closedTabLimit = 20;

// QWindow::show() is showNormal(), which would take a maximized window out of its state.
static void present(QQuickWindow *window)
{
    if (!window->isVisible())
        window->setVisible(true);
    else if (window->windowStates() & Qt::WindowMinimized)
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    window->raise();
    window->requestActivate();
}

Application::Application(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_recentFiles(Preferences::ownFile())
{
    m_sessionTimer.setSingleShot(true);
    m_sessionTimer.setInterval(1000);
    connect(&m_sessionTimer, &QTimer::timeout, this, &Application::saveSession);

    Document::setRecentFilesHook([this](const QString &path) {
        m_recentFiles.add(path);
    });

    // The windows stay: aboutToQuit can run inside one of their QML handlers
    // (quitting from onClosing), which must not see its window deleted.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (!m_quitting)
            saveSession();
        m_quitting = true;
    });
}

Application::~Application()
{
    // After the event loop, before the engine their QML runs on.
    for (const QPointer<QQuickWindow> &window : std::as_const(m_windows))
        delete window.data();
    Document::setRecentFilesHook(nullptr);
}

QFont Application::systemFont() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

QStringList Application::monospaceFamilies() const
{
    QStringList result;
    for (const QString &family : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(family) && !QFontDatabase::isPrivateFamily(family))
            result.append(family);
    }
    return result;
}

QVariantList Application::encodings() const
{
    QVariantList result;
    for (const EncodingInfo &info : TextCodec::encodings()) {
        if (!TextCodec::isSupported(info.name))
            continue;
        result.append(QVariantMap{{QStringLiteral("name"), QString::fromLatin1(info.name)},
                                  {QStringLiteral("label"), QCoreApplication::translate("Encoding", info.label)}});
    }
    return result;
}

QString Application::encodingLabel(const QString &encoding) const
{
    for (const EncodingInfo &info : TextCodec::encodings()) {
        if (QString::fromLatin1(info.name) == encoding)
            return QCoreApplication::translate("Encoding", info.label);
    }
    return encoding;
}

QVariantList Application::languages() const
{
    QVariantList result;
    result.append(QVariantMap{{QStringLiteral("id"), QString()},
                              {QStringLiteral("name"), Syntax::name(QString())}});
    for (const Language *language : Syntax::languages()) {
        const QString id = Syntax::id(language);
        result.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), Syntax::name(id)}});
    }
    return result;
}

Document *Application::createDocument(QObject *owner, const QString &draftId)
{
    auto *document = new Document(owner, draftId);
    QQmlEngine::setObjectOwnership(document, QQmlEngine::CppOwnership);
    connect(document, &Document::filePathChanged, this, &Application::scheduleSessionSave);
    connect(document, &Document::modifiedChanged, this, &Application::scheduleSessionSave);
    return document;
}

QQuickWindow *Application::newWindow(const QVariantMap &properties)
{
    if (!m_component)
        m_component = new QQmlComponent(m_engine, QUrl(QStringLiteral("qrc:/qml/Main.qml")), this);
    QObject *object = m_component->createWithInitialProperties(properties, m_engine->rootContext());
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning().noquote() << "Cannot create a text editor window:" << m_component->errorString();
        delete object;
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    m_windows.removeAll(nullptr);
    m_windows.append(window);
    m_lastActive = window;
    connect(window, &QWindow::activeChanged, this, [this, window] {
        if (window->isActive())
            windowActivated(window);
    });
    emit windowCountChanged();
    scheduleSessionSave();
    return window;
}

void Application::releaseWindow(QQuickWindow *window)
{
    if (!window)
        return;
    m_windows.removeAll(window);
    m_windows.removeAll(nullptr);
    window->deleteLater();
    emit windowCountChanged();
    scheduleSessionSave();
}

void Application::windowActivated(QQuickWindow *window)
{
    m_lastActive = window;
    // The window brought forward is the one that comes back in front.
    m_windows.removeAll(window);
    m_windows.append(window);
}

QVariantList Application::windowList() const
{
    QVariantList result;
    for (QQuickWindow *window : windows())
        result.append(QVariant::fromValue(window));
    return result;
}

int Application::windowCount() const
{
    return int(windows().size());
}

QList<QQuickWindow *> Application::windows() const
{
    QList<QQuickWindow *> result;
    for (const QPointer<QQuickWindow> &window : m_windows) {
        if (window)
            result.append(window);
    }
    return result;
}

QQuickWindow *Application::targetWindow(QQuickWindow *preferred)
{
    if (preferred)
        return preferred;
    if (m_lastActive && m_lastActive->isVisible())
        return m_lastActive;
    const QList<QQuickWindow *> all = windows();
    for (auto it = all.crbegin(); it != all.crend(); ++it) {
        if ((*it)->isVisible())
            return *it;
    }
    return newWindow();
}

QVariantMap Application::findOpenDocument(const QString &path) const
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (QQuickWindow *window : windows()) {
        QVariant index;
        QMetaObject::invokeMethod(window, "indexOfPath", Q_RETURN_ARG(QVariant, index), Q_ARG(QVariant, absolute),
                                  Q_ARG(QVariant, canonical));
        if (index.toInt() >= 0)
            return {{QStringLiteral("window"), QVariant::fromValue(window)}, {QStringLiteral("index"), index}};
    }
    return {};
}

void Application::openPaths(const QStringList &paths, int line, QQuickWindow *preferred)
{
    QQuickWindow *window = nullptr;
    for (const QString &path : paths) {
        const QVariantMap open = findOpenDocument(path);
        QQuickWindow *target = open.isEmpty() ? targetWindow(preferred) : open.value(QStringLiteral("window")).value<QQuickWindow *>();
        if (!target)
            continue;
        QMetaObject::invokeMethod(target, "openFile", Q_ARG(QVariant, QFileInfo(path).absoluteFilePath()),
                                  Q_ARG(QVariant, line));
        window = target;
    }
    if (window)
        present(window);
}

void Application::openText(const QString &text)
{
    QQuickWindow *window = targetWindow();
    if (!window)
        return;
    QMetaObject::invokeMethod(window, "openText", Q_ARG(QVariant, text));
    present(window);
}

void Application::Open(const QStringList &arguments, const QString &workingDirectory)
{
    const QDir dir(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    int line = 0;
    QStringList pending;
    bool any = false;
    for (const QString &argument : arguments) {
        // "+42" applies to the files after it, as in other editors.
        if (argument.startsWith(QLatin1Char('+')) && argument.size() > 1) {
            bool ok = false;
            const int value = argument.mid(1).toInt(&ok);
            if (ok) {
                if (!pending.isEmpty())
                    openPaths(pending, line);
                pending.clear();
                line = value;
                continue;
            }
        }
        const QUrl url(argument);
        const QString path = url.isLocalFile() ? url.toLocalFile() : dir.absoluteFilePath(argument);
        pending.append(path);
        any = true;
    }
    if (!pending.isEmpty())
        openPaths(pending, line);
    if (!any) {
        QQuickWindow *window = targetWindow();
        if (window)
            present(window);
    }
}

void Application::OpenText(const QString &text)
{
    openText(text);
}

void Application::NewWindow()
{
    QQuickWindow *window = newWindow();
    if (window)
        window->requestActivate();
}

void Application::start(const QStringList &arguments, const QString &workingDirectory, const QString &standardInput)
{
    // With restoring off the session is removed on a clean quit, so one that
    // is still there was left by a crash and comes back anyway.
    restore();
    if (!arguments.isEmpty())
        Open(arguments, workingDirectory);
    if (!standardInput.isNull())
        openText(standardInput);
    if (windows().isEmpty())
        newWindow();
}

void Application::restore()
{
    bool ok = true;
    const QList<WindowState> session = Session::load(Session::defaultPath(), &ok);
    for (const WindowState &state : session) {
        QVariantList tabs;
        for (const TabState &tab : state.tabs) {
            tabs.append(QVariantMap{
                    {QStringLiteral("path"), tab.path},
                    {QStringLiteral("draftId"), tab.draftId},
                    {QStringLiteral("unsaved"), tab.unsaved},
                    {QStringLiteral("line"), tab.line},
                    {QStringLiteral("column"), tab.column},
                    {QStringLiteral("scroll"), tab.scroll},
                    {QStringLiteral("encoding"), tab.encoding},
                    {QStringLiteral("language"), tab.language},
            });
        }
        newWindow({{QStringLiteral("initialTabs"), tabs}, {QStringLiteral("initialIndex"), state.current}});
    }
    // Drafts nothing refers to are left over from a closed tab or a crash;
    // they are kept a week, and all of them when the session is unreadable.
    if (ok)
        Session::removeOrphanDrafts(Document::draftDirectory(), liveDraftIds(), 7);
}

QSet<QString> Application::liveDraftIds() const
{
    QSet<QString> ids;
    for (const WindowState &state : Session::load()) {
        for (const TabState &tab : state.tabs)
            ids.insert(tab.draftId);
    }
    for (const QVariantMap &closed : m_closedTabs)
        ids.insert(closed.value(QStringLiteral("draftId")).toString());
    return ids;
}

void Application::pushClosedTab(const QVariantMap &state)
{
    m_closedTabs.append(state);
    while (m_closedTabs.size() > closedTabLimit) {
        const QVariantMap dropped = m_closedTabs.takeFirst();
        if (dropped.value(QStringLiteral("unsaved")).toBool())
            QFile::remove(Document::draftDirectory() + QLatin1Char('/') + dropped.value(QStringLiteral("draftId")).toString()
                          + QLatin1String(".txt"));
    }
    emit closedTabsChanged();
}

QVariantMap Application::takeClosedTab()
{
    if (m_closedTabs.isEmpty())
        return {};
    const QVariantMap state = m_closedTabs.takeLast();
    emit closedTabsChanged();
    return state;
}

void Application::scheduleSessionSave()
{
    if (!m_quitting)
        m_sessionTimer.start();
}

void Application::saveSession()
{
    m_sessionTimer.stop();
    QList<WindowState> session;
    for (QQuickWindow *window : windows()) {
        if (!window->isVisible())
            continue;
        QVariant tabs;
        QVariant current;
        QMetaObject::invokeMethod(window, "sessionTabs", Q_RETURN_ARG(QVariant, tabs));
        QMetaObject::invokeMethod(window, "sessionCurrentIndex", Q_RETURN_ARG(QVariant, current));
        WindowState state;
        state.current = current.toInt();
        for (const QVariant &value : tabs.toList()) {
            const QVariantMap map = value.toMap();
            TabState tab;
            tab.path = map.value(QStringLiteral("path")).toString();
            tab.draftId = map.value(QStringLiteral("draftId")).toString();
            tab.unsaved = map.value(QStringLiteral("unsaved")).toBool();
            tab.line = map.value(QStringLiteral("line")).toInt();
            tab.column = map.value(QStringLiteral("column")).toInt();
            tab.scroll = map.value(QStringLiteral("scroll")).toDouble();
            tab.encoding = map.value(QStringLiteral("encoding")).toString();
            tab.language = map.value(QStringLiteral("language")).toString();
            state.tabs.append(tab);
        }
        session.append(state);
    }
    Session::save(session);
}

void Application::flushDrafts()
{
    // Every unsaved text goes to its draft before the session points at it.
    for (QQuickWindow *window : windows()) {
        for (Document *document : window->findChildren<Document *>())
            document->writeDraftNow();
    }
}

void Application::terminate()
{
    flushDrafts();
    saveSession();
    m_quitting = true;
    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
}

void Application::quit()
{
    flushDrafts();
    saveSession();
    if (!m_restoreSession) {
        // Asked and answered already: nothing comes back next time.
        Session::remove();
        Session::removeOrphanDrafts(Document::draftDirectory(), {});
    }
    m_quitting = true;
    // Later: this is called from QML handlers of the windows quitting closes.
    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
}

void Application::showInFileManager(const QString &path) const
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                          QStringLiteral("/org/freedesktop/FileManager1"),
                                                          QStringLiteral("org.freedesktop.FileManager1"),
                                                          QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
    QDBusConnection::sessionBus().asyncCall(message);
}

void Application::copyText(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

QString Application::urlToPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QUrl Application::pathToUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path);
}

bool Application::fileExists(const QString &path) const
{
    return QFileInfo::exists(path);
}
