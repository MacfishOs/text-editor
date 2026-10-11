// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application.h"
#include "cutefishapplication.h"
#include "document.h"
#include "editorview.h"
#include "preferences.h"
#include "syntaxtheme.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QSocketNotifier>
#include <QTranslator>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

static int signalPipe[2];

static void handleTerminate(int)
{
    const char byte = 1;
    // Only async-signal-safe calls here; the event loop does the rest.
    [[maybe_unused]] const ssize_t written = ::write(signalPipe[0], &byte, 1);
}

int cutefish_application_main(int argc, char *argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    QGuiApplication &app = *qGuiApp;
    app.setApplicationName(QStringLiteral("cutefish-text-editor"));
    app.setApplicationVersion(QStringLiteral(TEXT_EDITOR_VERSION));
    app.setDesktopFileName(QStringLiteral("cutefish-text-editor"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("accessories-text-editor")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cutefish Text Editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("new-window"), QStringLiteral("Open a new window.")));
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Files to open; +LINE goes to that line of the next file, - reads standard input."),
                                 QStringLiteral("[+LINE] [FILE...] [-]"));
    parser.process(app);

    QStringList files = parser.positionalArguments();
    QString standardInput;
    if (files.removeAll(QStringLiteral("-")) > 0) {
        QFile input;
        if (input.open(stdin, QIODevice::ReadOnly))
            standardInput = QString::fromLocal8Bit(input.readAll());
        if (standardInput.isNull())
            standardInput = QLatin1String("");
    }

    // A running editor takes the files and this process ends.
    auto bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("com.cutefish.TextEditor");
    const QString path = QStringLiteral("/TextEditor");
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface existing(service, path, service, bus);
        existing.setTimeout(5000);
        if (parser.isSet(QStringLiteral("new-window")))
            existing.call(QStringLiteral("NewWindow"));
        if (!files.isEmpty() || standardInput.isNull())
            existing.call(QStringLiteral("Open"), files, QDir::currentPath());
        if (!standardInput.isNull())
            existing.call(QStringLiteral("OpenText"), standardInput);
        return 0;
    }

    const QString translation = QStringLiteral("/usr/share/cutefish-text-editor/translations/%1.qm")
                                        .arg(QLocale().name());
    auto *translator = new QTranslator(&app);
    if (QFile::exists(translation) && translator->load(translation))
        QCoreApplication::installTranslator(translator);

    qmlRegisterType<EditorView>("Cutefish.TextEditor", 1, 0, "EditorView");
    qmlRegisterUncreatableType<Document>("Cutefish.TextEditor", 1, 0, "Document",
                                         QStringLiteral("Created by App.createDocument()"));
    qmlRegisterUncreatableType<RecentFiles>("Cutefish.TextEditor", 1, 0, "RecentFiles",
                                            QStringLiteral("Provided by App"));

    QQmlEngine engine;
    SyntaxThemes themes;
    Application application(&engine);
    {
        const QSettings settings(Preferences::ownFile(), QSettings::IniFormat);
        application.setProperty("restoreSession", settings.value(QStringLiteral("Editor/restoreSession"), true));
    }
    QQmlComponent settingsComponent(&engine, QUrl(QStringLiteral("qrc:/qml/EditorSettings.qml")));
    QObject *preferences = settingsComponent.create();
    if (!preferences) {
        qWarning().noquote() << settingsComponent.errorString();
        return 1;
    }
    engine.rootContext()->setContextProperty(QStringLiteral("Prefs"), preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &application);
    engine.rootContext()->setContextProperty(QStringLiteral("Themes"), &themes);
    bus.registerObject(path, &application, QDBusConnection::ExportScriptableSlots);

    // Logging out ends the process with SIGTERM: the session is kept, as on quit.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe) == 0) {
        auto *notifier = new QSocketNotifier(signalPipe[1], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &application, [&application, notifier] {
            notifier->setEnabled(false);
            char byte;
            [[maybe_unused]] const ssize_t read = ::read(signalPipe[1], &byte, 1);
            application.terminate();
        });
        struct sigaction action = {};
        action.sa_handler = handleTerminate;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGHUP, &action, nullptr);
    }

    if (parser.isSet(QStringLiteral("new-window")) && files.isEmpty())
        application.start({}, QDir::currentPath(), standardInput);
    else
        application.start(files, QDir::currentPath(), standardInput);

    return app.exec();
}
