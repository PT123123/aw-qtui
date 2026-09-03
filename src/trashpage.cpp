// trashpage.cpp —— 回收站实现
#include "trashpage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "localstore.h"
#include "models.h"
#include "theme.h"

namespace awqtui {

TrashPage::TrashPage(LocalStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    buildUi();
    refresh();
}

void TrashPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("回收站"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    head->addWidget(title);
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                    .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    head->addWidget(m_countLabel);
    head->addStretch(1);
    m_restoreBtn = new QPushButton(QStringLiteral("恢复"));
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setEnabled(false);
    head->addWidget(m_restoreBtn);
    m_deleteBtn = new QPushButton(QStringLiteral("永久删除"));
    m_deleteBtn->setObjectName(QStringLiteral("DangerBtn"));
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setEnabled(false);
    head->addWidget(m_deleteBtn);
    m_clearBtn = new QPushButton(QStringLiteral("清空回收站"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    head->addWidget(m_clearBtn);
    root->addLayout(head);

    m_list = new QListWidget;
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    root->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const bool has = !m_list->selectedItems().isEmpty();
        m_restoreBtn->setEnabled(has);
        m_deleteBtn->setEnabled(has);
    });
    connect(m_restoreBtn, &QPushButton::clicked, this, &TrashPage::onRestore);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TrashPage::onDeleteForever);
    connect(m_clearBtn, &QPushButton::clicked, this, &TrashPage::onClearAll);

    applyStyle();
}

void TrashPage::applyStyle()
{
    setStyleSheet(QString());
}

void TrashPage::applyUiScale()
{
    applyStyle();
}

void TrashPage::refresh()
{
    rebuildList();
}

void TrashPage::rebuildList()
{
    m_list->clear();
    if (!m_store)
        return;
    const auto notes = m_store->deletedNotes();
    m_countLabel->setText(QStringLiteral("共 %1 项").arg(notes.size()));

    for (const auto &n : notes) {
        auto *item = new QListWidgetItem;
        QString preview = n.content;
        preview.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (preview.length() > 80)
            preview = preview.left(80) + QStringLiteral("…");
        if (preview.isEmpty())
            preview = QStringLiteral("（空笔记）");

        const QString date = n.createdAt.isEmpty()
            ? QString()
            : QStringLiteral(" · %1").arg(n.createdAt.left(19));
        item->setText(QStringLiteral("%1%2").arg(preview, date));
        item->setData(Qt::UserRole, n.id);
        m_list->addItem(item);
    }
}

void TrashPage::onRestore()
{
    const auto sel = m_list->selectedItems();
    if (sel.isEmpty())
        return;
    const qint64 id = sel.first()->data(Qt::UserRole).toLongLong();
    m_store->undelete(id);
    rebuildList();
}

void TrashPage::onDeleteForever()
{
    const auto sel = m_list->selectedItems();
    if (sel.isEmpty())
        return;

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("永久删除"));
    box.setText(QStringLiteral("确定永久删除选中的笔记？此操作不可恢复。"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;

    for (auto *item : sel) {
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        m_store->drop(id);
    }
    rebuildList();
}

void TrashPage::onClearAll()
{
    if (!m_store || m_store->deletedNotes().isEmpty())
        return;

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("清空回收站"));
    box.setText(QStringLiteral("确定永久删除回收站中的全部笔记？此操作不可恢复。"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;

    const auto notes = m_store->deletedNotes();
    for (const auto &n : notes)
        m_store->drop(n.id);
    rebuildList();
}

} // namespace awqtui
