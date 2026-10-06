// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSION_H
#define SESSION_H

#include <QList>
#include <QSet>
#include <QString>

struct TabState
{
    QString path;
    QString draftId;
    // The text in the draft is the document's, not the file's.
    bool unsaved = false;
    int line = 0;
    int column = 0;
    double scroll = 0;
    QString encoding;
    QString language;
};

struct WindowState
{
    QList<TabState> tabs;
    int current = 0;
};

// The open windows and tabs, kept in session.json next to the drafts, so
// they come back after a restart or a crash.
namespace Session
{
QString defaultPath();

bool save(const QList<WindowState> &windows, const QString &path = defaultPath());
// `ok` is false when the file exists but cannot be read or parsed.
QList<WindowState> load(const QString &path = defaultPath(), bool *ok = nullptr);
void remove(const QString &path = defaultPath());

// Deletes drafts no tab refers to any more, once they are older than
// `minimumAgeDays`: a draft of a closed tab may still be wanted.
void removeOrphanDrafts(const QString &directory, const QSet<QString> &keep, int minimumAgeDays = 0);
}

#endif
