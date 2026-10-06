// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNDOSTACK_H
#define UNDOSTACK_H

#include "textbuffer.h"

#include <QList>

// One replacement: `removed` was at `at` and `inserted` took its place.
struct EditStep
{
    TextPosition at;
    QString removed;
    QString inserted;
};

struct Selection
{
    TextPosition cursor;
    TextPosition anchor;
};

struct UndoGroup
{
    enum Kind {
        Other,
        Typing,
        Backspace,
        Delete,
    };

    QList<EditStep> steps;
    Selection before;
    Selection after;
    Kind kind = Other;
    qint64 time = 0;
};

// Edits grouped the way they are undone: typing and deleting by the word,
// everything else one action at a time. Remembers which entry matches the
// file on disk, so undoing back to it makes the document unmodified again.
class UndoStack
{
public:
    void clear();

    // Starts a group that later records join until end(), e.g. Replace All.
    void begin(const Selection &before);
    void end(const Selection &after);
    bool inGroup() const { return m_depth > 0; }

    void record(const EditStep &step, UndoGroup::Kind kind, const Selection &before,
                const Selection &after, qint64 time);
    // Stops the next record from merging into the last group.
    void seal() { m_sealed = true; }

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_groups.size(); }
    // The group to revert or reapply; the stack moves past it.
    const UndoGroup &undo();
    const UndoGroup &redo();

    void setClean() { m_cleanIndex = m_index; }
    // The disk copy can no longer be reached by undo or redo.
    void setCleanUnreachable() { m_cleanIndex = -1; }
    bool isClean() const { return m_cleanIndex == m_index; }

    int index() const { return m_index; }
    int count() const { return m_groups.size(); }

private:
    bool canMerge(const UndoGroup &group, const EditStep &step, UndoGroup::Kind kind, qint64 time) const;

    QList<UndoGroup> m_groups;
    int m_index = 0;
    int m_cleanIndex = 0;
    int m_depth = 0;
    bool m_sealed = false;
};

#endif
