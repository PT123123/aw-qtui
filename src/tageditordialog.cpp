// tageditordialog.cpp
#include "tageditordialog.h"

#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "theme.h"

namespace awqtui {

static QString comboOfRow(const QTableWidget *table, int row)
{
    if (!table || row < 0)
        return QString();
    const QTableWidgetItem *it = table->item(row, 0);
    return it ? it->text() : QString();
}

static QStringList parseCombo(const QString &text)
{
    QStringList out;
    for (const auto &p : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

TagEditorDialog::TagEditorDialog(TagStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("Tag editor"));
    resize(860, 560);
    buildUi();
    reloadCombos();
    reloadTags();
    reloadShortcuts();
}

void TagEditorDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // 顶部工具栏
    auto *toolbar = new QHBoxLayout;
    m_renameBtn = new QPushButton(QStringLiteral("Rename"));
    m_replaceBtn = new QPushButton(QStringLiteral("Replace"));
    m_deleteBtn = new QPushButton(QStringLiteral("Delete"));
    m_deleteBtn->setObjectName(QStringLiteral("DangerBtn"));
    m_colorBtn = new QPushButton(QStringLiteral("Change color"));
    m_importBtn = new QPushButton(QStringLiteral("Import"));
    m_exportBtn = new QPushButton(QStringLiteral("Export"));
    toolbar->addWidget(m_renameBtn);
    toolbar->addWidget(m_replaceBtn);
    toolbar->addWidget(m_deleteBtn);
    toolbar->addWidget(m_colorBtn);
    toolbar->addWidget(m_importBtn);
    toolbar->addWidget(m_exportBtn);
    toolbar->addStretch(1);
    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    toolbar->addWidget(m_status);
    root->addLayout(toolbar);

    connect(m_renameBtn, &QPushButton::clicked, this, &TagEditorDialog::onRename);
    connect(m_replaceBtn, &QPushButton::clicked, this, &TagEditorDialog::onReplace);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TagEditorDialog::onDeleteSelected);
    connect(m_colorBtn, &QPushButton::clicked, this, &TagEditorDialog::onChangeColor);
    connect(m_importBtn, &QPushButton::clicked, this, &TagEditorDialog::onImport);
    connect(m_exportBtn, &QPushButton::clicked, this, &TagEditorDialog::onExport);

    // Tab 区
    m_tabs = new QTabWidget;

    // ── Tab1 组合 ──
    auto *comboPane = new QWidget;
    auto *cl = new QVBoxLayout(comboPane);
    cl->setContentsMargins(4, 4, 4, 4);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter tag combinations..."));
    connect(m_filter, &QLineEdit::textChanged, this, &TagEditorDialog::onFilterText);
    cl->addWidget(m_filter);
    m_comboTable = new QTableWidget(0, 4);
    m_comboTable->setHorizontalHeaderLabels(
        {QStringLiteral("Tag group"), QStringLiteral("Last used"), QStringLiteral("No of uses"),
         QStringLiteral("Tagged time")});
    m_comboTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_comboTable->horizontalHeader()->setMinimumSectionSize(48);
    m_comboTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_comboTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_comboTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cl->addWidget(m_comboTable, 1);
    m_tabs->addTab(comboPane, QStringLiteral("Tag combinations"));

    // ── Tab2 单标签 ──
    auto *tagPane = new QWidget;
    auto *tl = new QVBoxLayout(tagPane);
    tl->setContentsMargins(4, 4, 4, 4);
    auto *tagHint = new QLabel(
        QStringLiteral("右键标签可切换 Skip 颜色 / 默认可计费；每个标签有独立颜色，组合显示首个标签的颜色。"));
    tagHint->setWordWrap(true);
    tagHint->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    tl->addWidget(tagHint);
    m_tagTable = new QTableWidget(0, 4);
    m_tagTable->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Color"), QStringLiteral("Uses"),
         QStringLiteral("Tagged time")});
    m_tagTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tagTable->horizontalHeader()->setMinimumSectionSize(48);
    m_tagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tagTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tagTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tagTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tagTable, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showContextMenu(pos); });
    tl->addWidget(m_tagTable, 1);
    m_tabs->addTab(tagPane, QStringLiteral("Tags"));

    // ── Tab3 快捷键 ──
    auto *shortcutPane = new QWidget;
    auto *sl = new QVBoxLayout(shortcutPane);
    sl->setContentsMargins(4, 4, 4, 4);
    auto *shortcutHint = new QLabel(
        QStringLiteral("为最常用标签绑定按键（A-Z 或数字）：在 Day 视图选中时间段后按对应键即打标签。"));
    shortcutHint->setWordWrap(true);
    shortcutHint->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    sl->addWidget(shortcutHint);
    m_shortcutTable = new QTableWidget(0, 2);
    m_shortcutTable->setHorizontalHeaderLabels({QStringLiteral("Key"), QStringLiteral("Tag")});
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_shortcutTable->horizontalHeader()->setMinimumSectionSize(48);
    m_shortcutTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sl->addWidget(m_shortcutTable, 1);
    auto *shortcutBtns = new QHBoxLayout;
    auto *addSc = new QPushButton(QStringLiteral("Add"));
    auto *editSc = new QPushButton(QStringLiteral("Edit"));
    auto *delSc = new QPushButton(QStringLiteral("Delete"));
    delSc->setObjectName(QStringLiteral("DangerBtn"));
    connect(addSc, &QPushButton::clicked, this, &TagEditorDialog::onAddShortcut);
    connect(editSc, &QPushButton::clicked, this, &TagEditorDialog::onEditShortcut);
    connect(delSc, &QPushButton::clicked, this, &TagEditorDialog::onDeleteShortcut);
    shortcutBtns->addWidget(addSc);
    shortcutBtns->addWidget(editSc);
    shortcutBtns->addWidget(delSc);
    shortcutBtns->addStretch(1);
    sl->addLayout(shortcutBtns);
    m_tabs->addTab(shortcutPane, QStringLiteral("Tag shortcuts"));

    // ── Tab4 标签源 ──
    auto *sourcePane = new QWidget;
    auto *sol = new QVBoxLayout(sourcePane);
    sol->setContentsMargins(4, 4, 4, 4);
    auto *sourceHint = new QLabel(
        QStringLiteral("本地文件 / 手动输入标签：每行一个组合，逗号分隔；支持部分标签展开。\n"
                       "例：\n  Project X,\n  Project Y,\n  ,Design\n  ,Testing\n→ Project X, Design ..."));
    sourceHint->setWordWrap(true);
    sourceHint->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    sol->addWidget(sourceHint);
    m_sourceEdit = new QPlainTextEdit;
    m_sourceEdit->setPlaceholderText(QStringLiteral("在此粘贴标签（每行一个组合，逗号分隔）..."));
    sol->addWidget(m_sourceEdit, 1);
    auto *sourceBtns = new QHBoxLayout;
    auto *importText = new QPushButton(QStringLiteral("导入文本"));
    auto *loadFile = new QPushButton(QStringLiteral("从文件加载"));
    connect(importText, &QPushButton::clicked, this, &TagEditorDialog::onImportSourceText);
    connect(loadFile, &QPushButton::clicked, this, &TagEditorDialog::onLoadSourceFile);
    sourceBtns->addWidget(importText);
    sourceBtns->addWidget(loadFile);
    sourceBtns->addStretch(1);
    sol->addLayout(sourceBtns);
    m_tabs->addTab(sourcePane, QStringLiteral("Tag sources"));

    root->addWidget(m_tabs, 1);

    // 底部
    auto *btnRow = new QHBoxLayout;
    auto *close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addStretch(1);
    btnRow->addWidget(close);
    root->addLayout(btnRow);

    connect(m_tabs, &QTabWidget::currentChanged, this, &TagEditorDialog::onTabChanged);
    onTabChanged(0);
}

void TagEditorDialog::onTabChanged(int idx)
{
    // 工具栏按钮按 tab 启用/禁用
    const bool combo = (idx == 0);
    const bool tag = (idx == 1);
    m_replaceBtn->setEnabled(combo);
    m_colorBtn->setEnabled(combo || tag);
    m_renameBtn->setEnabled(combo || tag);
    m_deleteBtn->setEnabled(combo || tag);
    m_importBtn->setEnabled(true);
    m_exportBtn->setEnabled(true);
}

// ── 数据加载 ────────────────────────────────────────────────
void TagEditorDialog::reloadCombos()
{
    if (!m_comboTable || !m_store)
        return;
    QList<TagMeta> metas = m_store->combinationMetas();
    const QString filter = m_filter ? m_filter->text().trimmed() : QString();
    if (!filter.isEmpty()) {
        QList<TagMeta> kept;
        for (const auto &m : metas)
            if (m.name.contains(filter, Qt::CaseInsensitive))
                kept << m;
        metas = kept;
    }
    m_comboTable->setRowCount(metas.size());
    for (int r = 0; r < metas.size(); ++r) {
        const TagMeta &m = metas[r];
        const QStringList parts = m.name.split(QLatin1Char(','));
        auto *nameItem = new QTableWidgetItem(m.name);
        nameItem->setForeground(parts.isEmpty() ? QColor(kColorFg)
                                                : m_store->tagColor(parts.first().trimmed()));
        m_comboTable->setItem(r, 0, nameItem);
        m_comboTable->setItem(r, 1, new QTableWidgetItem(m.lastUsed.left(10)));
        m_comboTable->setItem(r, 2, new QTableWidgetItem(QString::number(m.useCount)));
        m_comboTable->setItem(r, 3,
                              new QTableWidgetItem(QStringLiteral("%1 h")
                                                       .arg(m.taggedMs / 3600000.0, 0, 'f', 2)));
    }
    m_comboTable->resizeColumnsToContents();
    m_comboTable->setColumnWidth(0, qMax(m_comboTable->columnWidth(0), 200));
    m_comboTable->setColumnWidth(1, qMax(m_comboTable->columnWidth(1), 90));
}

void TagEditorDialog::reloadTags()
{
    if (!m_tagTable || !m_store)
        return;
    const QList<TagMeta> metas = m_store->tagMetas();
    m_tagTable->setRowCount(metas.size());
    for (int r = 0; r < metas.size(); ++r) {
        const TagMeta &m = metas[r];
        auto *nameItem = new QTableWidgetItem(m.name);
        nameItem->setForeground(m_store->tagColor(m.name));
        m_tagTable->setItem(r, 0, nameItem);
        auto *colorItem = new QTableWidgetItem(m.skipColor ? QStringLiteral("(skip)")
                                                           : m_store->tagColor(m.name).name());
        m_tagTable->setItem(r, 1, colorItem);
        m_tagTable->setItem(r, 2, new QTableWidgetItem(QString::number(m.useCount)));
        m_tagTable->setItem(r, 3,
                            new QTableWidgetItem(QStringLiteral("%1 h")
                                                     .arg(m.taggedMs / 3600000.0, 0, 'f', 2)));
    }
    m_tagTable->resizeColumnsToContents();
    m_tagTable->setColumnWidth(0, qMax(m_tagTable->columnWidth(0), 140));
}

void TagEditorDialog::reloadShortcuts()
{
    if (!m_shortcutTable || !m_store)
        return;
    const QMap<QString, QString> sc = m_store->shortcuts();
    m_shortcutTable->setRowCount(sc.size());
    int r = 0;
    for (auto it = sc.constBegin(); it != sc.constEnd(); ++it, ++r) {
        m_shortcutTable->setItem(r, 0, new QTableWidgetItem(it.key()));
        m_shortcutTable->setItem(r, 1, new QTableWidgetItem(it.value()));
    }
    m_shortcutTable->resizeColumnsToContents();
    m_shortcutTable->setColumnWidth(1, qMax(m_shortcutTable->columnWidth(1), 120));
}

void TagEditorDialog::setStatus(const QString &msg)
{
    if (m_status)
        m_status->setText(msg);
}

// ── 工具栏动作 ──────────────────────────────────────────────
void TagEditorDialog::onRename()
{
    if (!m_store)
        return;
    const int tab = m_tabs->currentIndex();
    if (tab == 0) {
        const int row = m_comboTable->currentRow();
        if (row < 0) {
            setStatus(QStringLiteral("请先选择一个标签组合"));
            return;
        }
        const QString oldCombo = comboOfRow(m_comboTable, row);
        bool ok = false;
        const QString newCombo =
            QInputDialog::getText(this, QStringLiteral("Rename tag combination"),
                                  QStringLiteral("新组合（逗号分隔）："), QLineEdit::Normal, oldCombo, &ok);
        if (!ok || newCombo.isEmpty() || newCombo == oldCombo)
            return;
        m_store->renameCombination(parseCombo(oldCombo), parseCombo(newCombo));
        setStatus(QStringLiteral("已重命名组合"));
    } else if (tab == 1) {
        const int row = m_tagTable->currentRow();
        if (row < 0) {
            setStatus(QStringLiteral("请先选择一个标签"));
            return;
        }
        const QString oldName = m_tagTable->item(row, 0)->text();
        bool ok = false;
        const QString newName =
            QInputDialog::getText(this, QStringLiteral("Rename tag"), QStringLiteral("新标签名："),
                                  QLineEdit::Normal, oldName, &ok);
        if (!ok || newName.trimmed().isEmpty() || newName == oldName)
            return;
        m_store->renameTag(oldName, newName.trimmed());
        setStatus(QStringLiteral("已重命名标签（含所有组合）"));
    }
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

void TagEditorDialog::onReplace()
{
    if (!m_store || m_tabs->currentIndex() != 0)
        return;
    const QList<QTableWidgetItem *> sel = m_comboTable->selectedItems();
    QSet<int> rows;
    for (const auto *it : sel)
        rows.insert(it->row());
    if (rows.isEmpty()) {
        setStatus(QStringLiteral("请先选择要替换的标签组合（可多选）"));
        return;
    }
    bool ok = false;
    const QString find =
        QInputDialog::getText(this, QStringLiteral("Replace"), QStringLiteral("Find:"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok || find.isEmpty())
        return;
    const QString repl =
        QInputDialog::getText(this, QStringLiteral("Replace"), QStringLiteral("Replace with:"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;

    int count = 0;
    for (const int row : rows) {
        const QString oldCombo = comboOfRow(m_comboTable, row);
        QString newCombo = oldCombo;
        newCombo.replace(find, repl, Qt::CaseSensitive);
        if (newCombo == oldCombo)
            continue;
        m_store->renameCombination(parseCombo(oldCombo), parseCombo(newCombo));
        ++count;
    }
    setStatus(QStringLiteral("已替换 %1 个组合").arg(count));
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

void TagEditorDialog::onDeleteSelected()
{
    if (!m_store)
        return;
    const int tab = m_tabs->currentIndex();
    if (tab == 0) {
        const QList<QTableWidgetItem *> sel = m_comboTable->selectedItems();
        QSet<int> rows;
        for (const auto *it : sel)
            rows.insert(it->row());
        if (rows.isEmpty()) {
            setStatus(QStringLiteral("请先选择组合"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete"),
                                  QStringLiteral("删除所选组合将同时删除其下所有时间段标签，确定？")) !=
            QMessageBox::Yes)
            return;
        for (const int row : rows)
            m_store->deleteCombination(parseCombo(comboOfRow(m_comboTable, row)));
        setStatus(QStringLiteral("已删除 %1 个组合").arg(rows.size()));
    } else if (tab == 1) {
        const QList<QTableWidgetItem *> sel = m_tagTable->selectedItems();
        QSet<int> rows;
        for (const auto *it : sel)
            rows.insert(it->row());
        if (rows.isEmpty()) {
            setStatus(QStringLiteral("请先选择标签"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete"),
                                  QStringLiteral("删除标签会从所有组合中移除它（含它的段将降级/删除），确定？")) !=
            QMessageBox::Yes)
            return;
        for (const int row : rows)
            m_store->deleteTag(m_tagTable->item(row, 0)->text());
        setStatus(QStringLiteral("已删除标签"));
    }
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

void TagEditorDialog::onChangeColor()
{
    if (!m_store)
        return;
    const int tab = m_tabs->currentIndex();
    if (tab == 0) {
        const int row = m_comboTable->currentRow();
        if (row < 0) {
            setStatus(QStringLiteral("请先选择组合"));
            return;
        }
        const QStringList tags = parseCombo(comboOfRow(m_comboTable, row));
        const QColor cur = m_store->segmentColor(m_store->segmentsOfCombination(tags).isEmpty()
                                                     ? TagSegment{}
                                                     : m_store->segmentsOfCombination(tags).first());
        const QColor c = QColorDialog::getColor(cur, this, QStringLiteral("Change color"));
        if (!c.isValid())
            return;
        m_store->setCombinationColor(tags, c.name());
        setStatus(QStringLiteral("已为该组合覆盖颜色（仅此组合）"));
    } else if (tab == 1) {
        const int row = m_tagTable->currentRow();
        if (row < 0) {
            setStatus(QStringLiteral("请先选择标签"));
            return;
        }
        const QString name = m_tagTable->item(row, 0)->text();
        const QColor c = QColorDialog::getColor(m_store->tagColor(name), this,
                                                QStringLiteral("Change color"));
        if (!c.isValid())
            return;
        m_store->setTagColor(name, c.name());
        setStatus(QStringLiteral("已修改标签颜色（影响所有以此标签开头的组合）"));
    }
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

void TagEditorDialog::onImport()
{
    if (!m_store)
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import tags"),
                                                      QString(), QStringLiteral("Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Import"), QStringLiteral("无法读取文件"));
        return;
    }
    const int n = m_store->importText(QString::fromUtf8(f.readAll()));
    setStatus(QStringLiteral("已导入 %1 个标签组合").arg(n));
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

void TagEditorDialog::onExport()
{
    if (!m_store)
        return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export tags"),
                                                      QStringLiteral("tags.txt"),
                                                      QStringLiteral("Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Export"), QStringLiteral("无法写入文件"));
        return;
    }
    f.write(m_store->exportText().toUtf8());
    setStatus(QStringLiteral("已导出到 %1").arg(path));
}

void TagEditorDialog::onFilterText(const QString &)
{
    reloadCombos();
}

// ── 快捷键 tab ──────────────────────────────────────────────
void TagEditorDialog::onAddShortcut()
{
    if (!m_store)
        return;
    bool ok = false;
    const QString key = QInputDialog::getText(this, QStringLiteral("Add tag shortcut"),
                                              QStringLiteral("按键（单个字母或数字，如 L 或 1）："),
                                              QLineEdit::Normal, QString(), &ok);
    if (!ok || key.trimmed().isEmpty())
        return;
    const QString combo =
        QInputDialog::getText(this, QStringLiteral("Add tag shortcut"), QStringLiteral("标签组合："),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    m_store->setShortcut(key.trimmed(), combo.trimmed());
    reloadShortcuts();
    setStatus(QStringLiteral("已添加快捷键 %1").arg(key.trimmed()));
    emit tagsChanged();
}

void TagEditorDialog::onEditShortcut()
{
    if (!m_store)
        return;
    const int row = m_shortcutTable->currentRow();
    if (row < 0) {
        setStatus(QStringLiteral("请先选择快捷键"));
        return;
    }
    const QString key = m_shortcutTable->item(row, 0)->text();
    bool ok = false;
    const QString combo =
        QInputDialog::getText(this, QStringLiteral("Edit tag shortcut"), QStringLiteral("标签组合："),
                              QLineEdit::Normal, m_shortcutTable->item(row, 1)->text(), &ok);
    if (!ok)
        return;
    m_store->setShortcut(key, combo.trimmed());
    reloadShortcuts();
    setStatus(QStringLiteral("已更新快捷键 %1").arg(key));
    emit tagsChanged();
}

void TagEditorDialog::onDeleteShortcut()
{
    if (!m_store)
        return;
    const int row = m_shortcutTable->currentRow();
    if (row < 0) {
        setStatus(QStringLiteral("请先选择快捷键"));
        return;
    }
    m_store->removeShortcut(m_shortcutTable->item(row, 0)->text());
    reloadShortcuts();
    emit tagsChanged();
}

// ── 标签源 tab ──────────────────────────────────────────────
void TagEditorDialog::onImportSourceText()
{
    if (!m_store)
        return;
    const int n = m_store->importText(m_sourceEdit->toPlainText());
    setStatus(QStringLiteral("已从文本导入 %1 个标签组合").arg(n));
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

void TagEditorDialog::onLoadSourceFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load tags from file"),
                                                      QString(), QStringLiteral("Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Load"), QStringLiteral("无法读取文件"));
        return;
    }
    m_sourceEdit->setPlainText(QString::fromUtf8(f.readAll()));
    setStatus(QStringLiteral("已加载文件，点击「导入文本」应用"));
}

// ── 右键菜单（单标签 tab） ──────────────────────────────────
void TagEditorDialog::showContextMenu(const QPoint &pos)
{
    const int row = m_tagTable->rowAt(pos.y());
    if (row < 0 || !m_store)
        return;
    m_tagTable->selectRow(row);
    const QString name = m_tagTable->item(row, 0)->text();
    const TagMeta meta = [&] {
        for (const auto &m : m_store->tagMetas())
            if (m.name == name)
                return m;
        return TagMeta{};
    }();

    QMenu menu(this);
    QAction *skip = menu.addAction(meta.skipColor ? QStringLiteral("恢复使用该标签颜色")
                                                  : QStringLiteral("Skip（跳过该标签颜色）"));
    QAction *bill = menu.addAction(meta.billableDefault ? QStringLiteral("取消默认可计费")
                                                        : QStringLiteral("设为默认可计费"));
    QAction *act = menu.exec(m_tagTable->viewport()->mapToGlobal(pos));
    if (act == skip) {
        m_store->setTagSkip(name, !meta.skipColor);
        setStatus(QStringLiteral("已更新颜色跳过标记"));
    } else if (act == bill) {
        m_store->setNewTagsBillableByDefault(!meta.billableDefault);
        setStatus(QStringLiteral("已更新默认可计费"));
    } else {
        return;
    }
    reloadCombos();
    reloadTags();
    emit tagsChanged();
}

} // namespace awqtui
