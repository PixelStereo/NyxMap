#pragma once
// The lists of the show's library (snapshots, presets, timelines): rows in groups, rearranged by a drag, each change one undo
// step. Engine::SnapshotNode is the shape of all three: `snapshot` holds the member's id, whichever the list holds.
#include "Engine.h"

#include <QAbstractItemView>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QMimeData>
#include <QObject>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <functional>
#include <utility>
#include <vector>

namespace librarytree {
using Node = Engine::SnapshotNode;

inline QString keyOf(const Node &n) { return n.snapshot ? QStringLiteral("s%1").arg(n.snapshot) : QStringLiteral("g%1").arg(n.group); }

// Takes the node out of the tree; false: not in it
inline bool takeNode(std::vector<Node> &nodes, const QString &key, Node *out)
{
    for (size_t k = 0; k < nodes.size(); ++k) {
        if (keyOf(nodes[k]) == key) {
            *out = nodes[k];
            nodes.erase(nodes.begin() + long(k));
            return true;
        }
        if (!nodes[k].snapshot && takeNode(nodes[k].children, key, out)) return true;
    }
    return false;
}

inline Node *findNode(std::vector<Node> &nodes, const QString &key)
{
    for (Node &n : nodes) {
        if (keyOf(n) == key) return &n;
        if (!n.snapshot)
            if (Node *f = findNode(n.children, key)) return f;
    }
    return nullptr;
}

// Next to `target` (before or after it, in its group); false: not in the tree
inline bool insertBeside(std::vector<Node> &nodes, const QString &target, const Node &n, bool after)
{
    for (size_t k = 0; k < nodes.size(); ++k) {
        if (keyOf(nodes[k]) == target) {
            nodes.insert(nodes.begin() + long(k) + (after ? 1 : 0), n);
            return true;
        }
        if (!nodes[k].snapshot && insertBeside(nodes[k].children, target, n, after)) return true;
    }
    return false;
}

// The groups, depth first, with their path ("Act 1 › Scene 2")
inline void listGroups(const std::vector<Node> &nodes, const QString &prefix, std::vector<std::pair<quint64, QString>> &out)
{
    for (const Node &n : nodes) {
        if (n.snapshot) continue;
        const QString name = prefix.isEmpty() ? n.name : prefix + QStringLiteral(" › ") + n.name;
        out.push_back({n.group, name});
        listGroups(n.children, name, out);
    }
}
} // namespace librarytree

// The keys of a list, the same in the library's lists and in the steps. Only while the list has the focus, ahead of the menus'
// shortcuts (⌘D, ⌘G are the layers' too). Return edits and never plays: it ends a typed name, and a look must not go out by
// a key that close to it. Playing takes the modifier. Held keys do not repeat: one press, one thing played or deleted.
class ListKeys : public QObject
{
public:
    struct Actions {
        std::function<void()> edit;      // Return: rename, or edit the cell
        std::function<void()> play;      // ⌘Return
        std::function<void()> remove;    // Delete, Backspace
        std::function<void()> duplicate; // ⌘D
        std::function<void()> group;     // ⌘G
    };
    ListKeys(QAbstractItemView *view, Actions actions) : QObject(view), m_actions(std::move(actions)) { view->installEventFilter(this); }

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() != QEvent::ShortcutOverride && e->type() != QEvent::KeyPress) return false;
        auto *k = static_cast<QKeyEvent *>(e);
        const std::function<void()> *action = actionOf(k);
        if (!action || !*action) return false;
        if (e->type() == QEvent::ShortcutOverride) {
            k->accept();
            return true;
        }
        if (!k->isAutoRepeat()) (*action)();
        return true;
    }

private:
    const std::function<void()> *actionOf(const QKeyEvent *k) const
    {
        const Qt::KeyboardModifiers mods = k->modifiers() & ~Qt::KeypadModifier;
        const int key = k->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter)
            return mods == Qt::NoModifier ? &m_actions.edit : mods == Qt::ControlModifier ? &m_actions.play : nullptr;
        if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && mods == Qt::NoModifier) return &m_actions.remove;
        if (key == Qt::Key_D && mods == Qt::ControlModifier) return &m_actions.duplicate;
        if (key == Qt::Key_G && mods == Qt::ControlModifier) return &m_actions.group;
        return nullptr;
    }
    Actions m_actions;
};

// A library's rows. Inside it, a row dragged is moved: between two rows, or into a group. `carry` adds what a member takes
// elsewhere (onto a step of a sequence).
class LibraryList : public QTreeWidget
{
public:
    LibraryList(int memberRole, int groupRole, QByteArray nodeMime, QStringList outgoing = {}, QWidget *parent = nullptr)
        : QTreeWidget(parent), m_memberRole(memberRole), m_groupRole(groupRole), m_nodeMime(std::move(nodeMime)), m_outgoing(std::move(outgoing))
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDropIndicatorShown(true);
        setDefaultDropAction(Qt::MoveAction);
    }
    // A row (its key) dropped by `target` (its key; empty: at the end): where -1 before it, 1 after it, 0 into it
    std::function<void(const QString &key, const QString &target, int where)> onMove;
    std::function<void(QMimeData *, QTreeWidgetItem *)> carry;

    QString nodeKey(const QTreeWidgetItem *it) const
    {
        if (!it) return {};
        if (const quint64 g = it->data(0, m_groupRole).toULongLong()) return QStringLiteral("g%1").arg(g);
        return QStringLiteral("s%1").arg(it->data(0, m_memberRole).toULongLong());
    }
    int groupRole() const { return m_groupRole; }

    // Only the rows whose name holds the text (whatever its case and accents) or whose number it is, with their groups
    // opened, a group's own name showing all it holds. The row selected stays. Nothing of this is saved in the groups.
    bool filtering() const { return !m_filter.isEmpty(); }
    void setFilter(const QString &text)
    {
        m_filter = folded(text.trimmed());
        applyFilter();
    }
    void applyFilter()
    {
        for (int k = 0; k < topLevelItemCount(); ++k) showIfMatching(topLevelItem(k), false);
    }

protected:
    QStringList mimeTypes() const override { return QStringList(m_outgoing) << QString::fromLatin1(m_nodeMime); }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.isEmpty()) return nullptr;
        QTreeWidgetItem *it = items.first();
        auto *m = new QMimeData;
        m->setData(QString::fromLatin1(m_nodeMime), nodeKey(it).toUtf8());
        if (carry && it->data(0, m_memberRole).toULongLong()) carry(m, it);
        return m;
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->source() == this && e->mimeData()->hasFormat(QString::fromLatin1(m_nodeMime))) QTreeWidget::dragEnterEvent(e);
        else e->ignore();
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->source() != this || !e->mimeData()->hasFormat(QString::fromLatin1(m_nodeMime))) {
            e->ignore();
            return;
        }
        QTreeWidget::dragMoveEvent(e);
        e->setDropAction(Qt::MoveAction);
        e->accept();
    }
    void dropEvent(QDropEvent *e) override
    {
        if (e->source() != this || !e->mimeData()->hasFormat(QString::fromLatin1(m_nodeMime))) {
            e->ignore();
            return;
        }
        const QString key = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(m_nodeMime)));
        QTreeWidgetItem *target = itemAt(e->position().toPoint());
        const DropIndicatorPosition where = dropIndicatorPosition();
        e->setDropAction(Qt::IgnoreAction); // the list is rebuilt from the engine: nothing moves here
        e->accept();
        if (onMove) {
            const QString t = where == OnViewport || !target ? QString() : nodeKey(target);
            const int w = where == OnItem ? 0 : where == AboveItem ? -1 : 1;
            QMetaObject::invokeMethod(this, [this, key, t, w] { onMove(key, t, w); }, Qt::QueuedConnection);
        }
    }

private:
    static QString folded(const QString &text)
    {
        QString out;
        for (const QChar c : text.normalized(QString::NormalizationForm_D))
            if (c.category() != QChar::Mark_NonSpacing) out += c;
        return out.toCaseFolded();
    }
    bool showIfMatching(QTreeWidgetItem *it, bool parentMatched)
    {
        const bool group = it->data(0, m_groupRole).toULongLong() != 0;
        bool match = m_filter.isEmpty() || parentMatched || folded(it->text(group ? 0 : 1)).contains(m_filter);
        if (!group) {
            QString number = it->text(0);
            match = match || it == currentItem() || number.remove(QStringLiteral("▶")).trimmed() == m_filter;
        }
        bool inside = false;
        for (int k = 0; k < it->childCount(); ++k) inside = showIfMatching(it->child(k), match) || inside;
        it->setHidden(!match && !inside);
        if (filtering() && inside) it->setExpanded(true);
        return match || inside;
    }
    int m_memberRole, m_groupRole;
    QByteArray m_nodeMime;
    QStringList m_outgoing;
    QString m_filter;
};

// The field above a list: what is typed narrows the list; Esc empties it, Return or ↓ goes on to the list.
// `refill` builds the list again when it is emptied: the groups as they were left
class LibraryFilter : public QLineEdit
{
public:
    LibraryFilter(LibraryList *list, const QString &name, std::function<void()> refill) : m_list(list)
    {
        setObjectName(name);
        setPlaceholderText(QStringLiteral("Filter"));
        setClearButtonEnabled(true);
        setToolTip(QStringLiteral("Show only the rows whose name holds this, or whose number it is"));
        connect(this, &QLineEdit::textChanged, list, [list, refill = std::move(refill)](const QString &text) {
            list->setFilter(text);
            if (!list->filtering()) refill();
        });
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) {
            clear();
            m_list->setFocus();
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Down) {
            m_list->setFocus();
        } else {
            QLineEdit::keyPressEvent(e);
        }
    }

private:
    LibraryList *m_list;
};

// What a list's groups do: made, renamed, folded, taken away, rows moved into them
class LibraryGroups
{
public:
    using Tree = std::vector<Engine::SnapshotNode>;
    struct Access {
        std::function<Tree()> read;
        std::function<void(const Tree &before, const Tree &after, const QString &text)> commit; // one undo step
        std::function<void(const Tree &)> quiet;                                                // kept, not an undo step
        std::function<bool()> filling;                                                          // the list being rebuilt
        std::function<quint64()> newId;
    };
    LibraryGroups(LibraryList *list, QString members, Access access) : m_list(list), m_members(std::move(members)), m_access(std::move(access))
    {
        using namespace librarytree;
        list->onMove = [this](const QString &key, const QString &target, int where) { move(key, target, where); };
        QObject::connect(list, &QTreeWidget::itemChanged, list, [this](QTreeWidgetItem *it, int col) {
            const quint64 g = it->data(0, m_list->groupRole()).toULongLong();
            if (m_access.filling() || !g || col != 0) return;
            const QString name = it->text(0).trimmed();
            edit(QStringLiteral("Rename Group"), [&](Tree &t) {
                if (Node *n = findNode(t, QStringLiteral("g%1").arg(g))) n->name = name.isEmpty() ? n->name : name;
            });
        });
        auto fold = [this](QTreeWidgetItem *it, bool open) { // kept (saved), not an undo step
            const quint64 g = it->data(0, m_list->groupRole()).toULongLong();
            if (m_access.filling() || !g || m_list->filtering()) return;
            Tree t = m_access.read();
            Node *n = findNode(t, QStringLiteral("g%1").arg(g));
            if (!n || n->open == open) return;
            n->open = open;
            m_access.quiet(t);
        };
        QObject::connect(list, &QTreeWidget::itemExpanded, list, [fold](QTreeWidgetItem *it) { fold(it, true); });
        QObject::connect(list, &QTreeWidget::itemCollapsed, list, [fold](QTreeWidgetItem *it) { fold(it, false); });
    }

    void edit(const QString &text, const std::function<void(Tree &)> &fn)
    {
        const Tree before = m_access.read();
        Tree after = before;
        fn(after);
        if (after == before) return;
        m_access.commit(before, after, text);
    }

    // A row moved: where = -1 before `target`, 1 after it, 0 into it (a group), 2 at the end of the top level
    void move(const QString &key, const QString &target, int where)
    {
        using namespace librarytree;
        if (key == target) return;
        edit(QStringLiteral("Move in %1").arg(m_members), [&](Tree &t) {
            const Node *moved = findNode(t, key);
            if (!moved) return;
            if (!moved->snapshot && !target.isEmpty()) { // a group never goes inside itself
                Tree inside = moved->children;
                if (findNode(inside, target)) return;
            }
            Node n;
            takeNode(t, key, &n);
            Node *into = target.isEmpty() ? nullptr : findNode(t, target);
            if (target.isEmpty() || where == 2 || !into) {
                t.push_back(n);
            } else if (where == 0 && !into->snapshot) {
                into->children.push_back(n);
                into->open = true;
            } else {
                insertBeside(t, target, n, where >= 0);
            }
        });
    }

    void newGroup()
    {
        using namespace librarytree;
        const quint64 g = m_access.newId();
        std::vector<std::pair<quint64, QString>> groups;
        listGroups(m_access.read(), QString(), groups);
        edit(QStringLiteral("New Group"), [&](Tree &t) {
            Node n;
            n.group = g;
            n.name = QStringLiteral("Group %1").arg(groups.size() + 1);
            t.push_back(n);
        });
        for (QTreeWidgetItemIterator it(m_list); *it; ++it)
            if ((*it)->data(0, m_list->groupRole()).toULongLong() == g) {
                m_list->setCurrentItem(*it);
                m_list->editItem(*it, 0);
            }
    }

    // A group taken away: what it holds goes where it was
    void removeGroup(quint64 group)
    {
        using namespace librarytree;
        edit(QStringLiteral("Delete Group"), [&](Tree &t) {
            std::function<bool(Tree &)> drop = [&](Tree &nodes) {
                for (size_t k = 0; k < nodes.size(); ++k) {
                    if (!nodes[k].snapshot && nodes[k].group == group) {
                        Tree kids = nodes[k].children;
                        nodes.erase(nodes.begin() + long(k));
                        nodes.insert(nodes.begin() + long(k), kids.begin(), kids.end());
                        return true;
                    }
                    if (!nodes[k].snapshot && drop(nodes[k].children)) return true;
                }
                return false;
            };
            drop(t);
        });
    }

    // Add to Group ▸ every group (not itself, nor one inside it), or back to the top level; a group's own actions
    void fillMenu(QMenu &menu, QTreeWidgetItem *it)
    {
        using namespace librarytree;
        if (!it) return;
        const QString key = m_list->nodeKey(it);
        Tree tree = m_access.read();
        const Node *self = findNode(tree, key);
        std::vector<std::pair<quint64, QString>> groups;
        listGroups(tree, QString(), groups);
        QMenu *add = menu.addMenu(QStringLiteral("Add to Group"));
        const bool nested = it->parent() != nullptr;
        QAction *top = add->addAction(QStringLiteral("Top Level"), m_list, [this, key] { move(key, QString(), 2); });
        top->setEnabled(nested);
        add->addSeparator();
        for (const auto &[g, name] : groups) {
            const QString gk = QStringLiteral("g%1").arg(g);
            Tree inside = self && !self->snapshot ? self->children : Tree();
            if (gk == key || (self && !self->snapshot && findNode(inside, gk))) continue;
            add->addAction(name, m_list, [this, key, gk] { move(key, gk, 0); });
        }
        add->setEnabled(nested || !groups.empty());
        if (const quint64 group = it->data(0, m_list->groupRole()).toULongLong()) {
            menu.addSeparator();
            menu.addAction(QStringLiteral("Rename Group"), m_list, [this, it] { m_list->editItem(it, 0); });
            menu.addAction(QStringLiteral("Delete Group (keep its %1)").arg(m_members.toLower()), m_list, [this, group] { removeGroup(group); });
        }
    }

    // A group's row: its name across the row, bold
    static void styleGroup(QTreeWidgetItem *it, const Engine::SnapshotNode &node, int groupRole, const QString &members)
    {
        it->setText(0, node.name);
        it->setData(0, groupRole, node.group);
        it->setFirstColumnSpanned(true);
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
        it->setForeground(0, QColor(0xc8, 0xc8, 0xd0));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable);
        it->setToolTip(0, QStringLiteral("Group — drag %1 into it; double-click to rename").arg(members.toLower()));
    }

private:
    LibraryList *m_list;
    QString m_members;
    Access m_access;
};
