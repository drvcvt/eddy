#pragma once
#include <QDialog>
#include "recoverystore.h"
class QListWidget;
class QToolButton;
namespace eddy {

// "Resume editing" (21.09. plan 5): the kept edits, newest first, to open or
// discard. Not a history of captures, only editing states.
class ResumeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ResumeDialog(const RecoveryStore &store, QWidget *parent = nullptr);
    RecoveryStore::Entry chosen() const { return m_chosen; }
private:
    void reload();
    RecoveryStore m_store;
    QVector<RecoveryStore::Entry> m_entries;
    RecoveryStore::Entry m_chosen;
    QListWidget *m_list = nullptr;
    QToolButton *m_open = nullptr;
    QToolButton *m_discard = nullptr;
};

}
