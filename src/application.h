// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef APPLICATION_H
#define APPLICATION_H

#include "recentfiles.h"
#include "session.h"

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class Document;
class QQmlComponent;
class QQmlEngine;
class QQuickWindow;

// The editor process: its windows, the session they are saved to, the
// recently used files and the D-Bus service later launches hand their files to.
class Application : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.TextEditor")
    Q_PROPERTY(RecentFiles *recentFiles READ recentFiles CONSTANT)
    Q_PROPERTY(QFont systemFont READ systemFont NOTIFY systemFontChanged)
    Q_PROPERTY(QStringList monospaceFamilies READ monospaceFamilies CONSTANT)
    Q_PROPERTY(QVariantList encodings READ encodings CONSTANT)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(bool canReopenClosedTab READ canReopenClosedTab NOTIFY closedTabsChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)
    // Set by QML from the settings: quitting keeps the session without asking.
    Q_PROPERTY(bool restoreSession MEMBER m_restoreSession NOTIFY restoreSessionChanged)
    // Windows closing now are the process ending, not the user closing them.
    Q_PROPERTY(bool quitting READ isQuitting CONSTANT)

public:
    explicit Application(QQmlEngine *engine, QObject *parent = nullptr);
    ~Application() override;

    // Restores the last session, then opens `arguments` (paths and +LINE),
    // or an empty document when there is nothing at all.
    void start(const QStringList &arguments, const QString &workingDirectory, const QString &standardInput);

    RecentFiles *recentFiles() { return &m_recentFiles; }
    QFont systemFont() const;
    QStringList monospaceFamilies() const;
    QVariantList encodings() const;
    QVariantList languages() const;
    bool canReopenClosedTab() const { return !m_closedTabs.isEmpty(); }
    int windowCount() const;
    bool isQuitting() const { return m_quitting; }

    Q_INVOKABLE Document *createDocument(QObject *owner, const QString &draftId = QString());
    Q_INVOKABLE QQuickWindow *newWindow(const QVariantMap &properties = {});
    // Destroys a closed window once the event loop it closed in is back.
    Q_INVOKABLE void releaseWindow(QQuickWindow *window);
    Q_INVOKABLE void windowActivated(QQuickWindow *window);
    // Opens files in the window used last, or focuses their tabs where they are open.
    Q_INVOKABLE void openPaths(const QStringList &paths, int line = 0, QQuickWindow *window = nullptr);
    Q_INVOKABLE QVariantMap findOpenDocument(const QString &path) const;
    Q_INVOKABLE QVariantList windowList() const;

    Q_INVOKABLE void pushClosedTab(const QVariantMap &state);
    Q_INVOKABLE QVariantMap takeClosedTab();

    Q_INVOKABLE void scheduleSessionSave();
    Q_INVOKABLE void saveSession();
    // Saves the session and drafts and ends the process, without asking.
    Q_INVOKABLE void quit();
    // For logout: keeps everything for the next start, whatever the setting.
    void terminate();

    Q_INVOKABLE QString encodingLabel(const QString &encoding) const;
    Q_INVOKABLE void showInFileManager(const QString &path) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE QString urlToPath(const QUrl &url) const;
    Q_INVOKABLE QUrl pathToUrl(const QString &path) const;
    Q_INVOKABLE bool fileExists(const QString &path) const;

    QQmlEngine *engine() const { return m_engine; }

public slots:
    // D-Bus: a later launch's files, resolved against its working directory.
    Q_SCRIPTABLE void Open(const QStringList &arguments, const QString &workingDirectory);
    Q_SCRIPTABLE void OpenText(const QString &text);
    Q_SCRIPTABLE void NewWindow();

signals:
    void systemFontChanged();
    void closedTabsChanged();
    void windowCountChanged();
    void restoreSessionChanged();

private:
    QQuickWindow *targetWindow(QQuickWindow *preferred = nullptr);
    QList<QQuickWindow *> windows() const;
    void restore();
    void openText(const QString &text);
    QSet<QString> liveDraftIds() const;
    void flushDrafts();

    QQmlEngine *m_engine;
    QQmlComponent *m_component = nullptr;
    QList<QPointer<QQuickWindow>> m_windows;
    QPointer<QQuickWindow> m_lastActive;
    RecentFiles m_recentFiles;
    QList<QVariantMap> m_closedTabs;
    QTimer m_sessionTimer;
    bool m_restoreSession = true;
    bool m_quitting = false;
};

#endif
