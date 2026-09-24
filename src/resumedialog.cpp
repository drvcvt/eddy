#include "resumedialog.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QToolButton>
#include <QVBoxLayout>

namespace eddy {

static QString sizeText(qint64 bytes) {
    return bytes >= 1024 * 1024 ? QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1)
                                : QStringLiteral("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
}

ResumeDialog::ResumeDialog(const RecoveryStore &store, QWidget *parent) : QDialog(parent), m_store(store) {
    setObjectName(QStringLiteral("ResumeDialog"));
    setWindowTitle(tr("Resume editing"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    m_list = new QListWidget(this);
    m_list->setAccessibleName(tr("Kept edits"));
    m_list->setMinimumSize(420, 240);
    layout->addWidget(m_list);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    auto button = [&](const QString &text, const QString &name) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setText(text);
        b->setAccessibleName(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        buttons->addWidget(b);
        return b;
    };
    m_discard = button(tr("Discard"), QStringLiteral("ResumeDiscard"));
    buttons->addStretch(1);
    auto *close = button(tr("Close"), QStringLiteral("ResumeClose"));
    m_open = button(tr("Open"), QStringLiteral("ResumeOpen"));
    layout->addLayout(buttons);
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    connect(m_open, &QToolButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_entries.size() || m_entries[row].inUse) return;
        m_chosen = m_entries[row];
        accept();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, m_open, &QToolButton::click);
    connect(m_discard, &QToolButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_entries.size()) return;
        m_store.discard(m_entries[row].id);
        reload();
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool usable = row >= 0 && row < m_entries.size() && !m_entries[row].inUse;
        m_open->setEnabled(usable);
        m_discard->setEnabled(usable);
    });
    reload();
}

void ResumeDialog::reload() {
    m_entries = m_store.entries();
    m_list->clear();
    for (const RecoveryStore::Entry &e : std::as_const(m_entries)) {
        // Name, then when and how much, as quieter columns separated by space.
        const QString when = QLocale().toString(e.updated.toLocalTime(), QLocale::ShortFormat);
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2   %3%4").arg(e.name.isEmpty() ? tr("Image") : e.name,
            when, sizeText(e.bytes), e.inUse ? tr("   open in another window") : QString()), m_list);
        item->setToolTip(e.source);
    }
    if (m_entries.isEmpty()) new QListWidgetItem(tr("No kept edits"), m_list);
    m_list->setCurrentRow(m_entries.isEmpty() ? -1 : 0);
    m_open->setEnabled(!m_entries.isEmpty() && !m_entries.first().inUse);
    m_discard->setEnabled(!m_entries.isEmpty() && !m_entries.first().inUse);
}

}
