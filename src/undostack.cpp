// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undostack.h"

// A pause this long ends a typing run even within a word.
static constexpr qint64 mergeInterval = 5000;

static bool isSpace(QChar c)
{
    return c.isSpace();
}

void UndoStack::clear()
{
    m_groups.clear();
    m_index = 0;
    m_cleanIndex = 0;
    m_depth = 0;
    m_sealed = false;
}

void UndoStack::begin(const Selection &before)
{
    if (m_depth++ > 0)
        return;
    m_groups.resize(m_index);
    if (m_cleanIndex > m_index)
        m_cleanIndex = -1;
    UndoGroup group;
    group.before = before;
    group.after = before;
    m_groups.append(group);
    ++m_index;
}

void UndoStack::end(const Selection &after)
{
    if (m_depth == 0 || --m_depth > 0)
        return;
    UndoGroup &group = m_groups.last();
    group.after = after;
    m_sealed = true;
    // Nothing was changed: drop the empty group.
    if (group.steps.isEmpty()) {
        m_groups.removeLast();
        --m_index;
    }
}

bool UndoStack::canMerge(const UndoGroup &group, const EditStep &step, UndoGroup::Kind kind, qint64 time) const
{
    if (m_sealed || group.kind != kind || kind == UndoGroup::Other || group.steps.size() != 1)
        return false;
    if (m_cleanIndex == m_index || time - group.time > mergeInterval)
        return false;

    const EditStep &last = group.steps.last();
    switch (kind) {
    case UndoGroup::Typing:
        if (!step.removed.isEmpty() || step.inserted.contains(QLatin1Char('\n')) || last.inserted.isEmpty())
            return false;
        if (TextBuffer::endOf(last.at, last.inserted) != step.at)
            return false;
        // A new word starts a new group; the spaces before it stay with the last one.
        return !(isSpace(last.inserted.back()) && !isSpace(step.inserted.front()));
    case UndoGroup::Backspace:
        if (!step.inserted.isEmpty() || !last.inserted.isEmpty() || step.removed.contains(QLatin1Char('\n')))
            return false;
        if (TextBuffer::endOf(step.at, step.removed) != last.at)
            return false;
        return !(isSpace(step.removed.back()) && !isSpace(last.removed.front()));
    case UndoGroup::Delete:
        if (!step.inserted.isEmpty() || !last.inserted.isEmpty() || step.removed.contains(QLatin1Char('\n')))
            return false;
        if (step.at != last.at)
            return false;
        return !(!isSpace(last.removed.back()) && isSpace(step.removed.front()));
    case UndoGroup::Other:
        break;
    }
    return false;
}

void UndoStack::record(const EditStep &step, UndoGroup::Kind kind, const Selection &before,
                       const Selection &after, qint64 time)
{
    if (m_depth > 0) {
        m_groups.last().steps.append(step);
        return;
    }

    m_groups.resize(m_index);
    if (m_cleanIndex > m_index)
        m_cleanIndex = -1;

    if (!m_groups.isEmpty() && canMerge(m_groups.last(), step, kind, time)) {
        UndoGroup &group = m_groups.last();
        EditStep &last = group.steps.last();
        if (kind == UndoGroup::Typing) {
            last.inserted += step.inserted;
        } else if (kind == UndoGroup::Backspace) {
            last.at = step.at;
            last.removed.prepend(step.removed);
        } else {
            last.removed += step.removed;
        }
        group.after = after;
        group.time = time;
        return;
    }

    UndoGroup group;
    group.steps.append(step);
    group.before = before;
    group.after = after;
    group.kind = kind;
    group.time = time;
    m_groups.append(group);
    ++m_index;
    m_sealed = false;
}

const UndoGroup &UndoStack::undo()
{
    m_sealed = true;
    return m_groups.at(--m_index);
}

const UndoGroup &UndoStack::redo()
{
    m_sealed = true;
    return m_groups.at(m_index++);
}
