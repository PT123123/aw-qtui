// trashpage.cpp —— 回收站实现
#include "trashpage.h"
#include "ui_trashpage.h"

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

TrashPage::~TrashPage()
{
    delete ui;
}

void TrashPage::buildUi()
{
    // 静态布局来自 Qt Designer（trashpage.ui -> ui_trashpage.h），
    // .ui 中边距/间距为基准值，si() 缩放几何在此重设
    ui = new Ui::TrashPage;
    ui->setupUi(this);

    ui->rootLayout->setContentsMargins(si(20), si(16), si(20), si(16));
    ui->rootLayout->setSpacing(si(10));

    // 成员别名：业务逻辑沿用 m_* 指针，静态布局归属 .ui 文件
    m_list = ui->TrashList;
    m_countLabel = ui->countLabel;
    m_restoreBtn = ui->btnRestore;
    m_deleteBtn = ui->btnDelete;
    m_clearBtn = ui->btnClear;

    for (auto *b : {m_restoreBtn, m_deleteBtn, m_clearBtn}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setEnabled(false);
    }
    m_clearBtn->setEnabled(true);
    // 主题样式角色（全局 QSS 按 objectName 选择器匹配危险按钮）
    m_deleteBtn->setObjectName(QStringLiteral("DangerBtn"));

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
    // 标题/计数用 sp() 动态字号，需随缩放/主题重设（控件本体在 .ui 中）
    ui->TrashTitle->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                                      .arg(sp(16), QString::fromLatin1(kColorFg)));
    ui->countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                      .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
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
