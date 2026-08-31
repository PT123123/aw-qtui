// autotagdialog.cpp
#include "autotagdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "mockdata.h"
#include "theme.h"

namespace awqtui {

static const char *kTypeNames[] = {"Regular", "Append", "Prepend", "Absorb"};
static const char *kFieldNames[] = {"title", "group", "detail", "url", "domain"};

static QString typeName(int t)
{
    if (t >= 0 && t < 4)
        return QString::fromUtf8(kTypeNames[t]);
    return QStringLiteral("Regular");
}

static QString describeRule(const AutoTagRule &r)
{
    QStringList conds;
    for (const auto &c : r.conditions)
        conds << QStringLiteral("%1:%2%3").arg(c.field, c.value,
                                               c.regex ? QStringLiteral(" (regex)") : QString());
    return conds.join(QStringLiteral("; "));
}

// ── 规则编辑对话框 ──────────────────────────────────────────
static bool editRuleDlg(TagStore *store, AutoTagRule &rule, QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(rule.id ? QStringLiteral("Edit autotag") : QStringLiteral("Add autotag"));
    dlg.resize(520, 420);

    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(rule.name);
    name->setPlaceholderText(QStringLiteral("如 Work, {{group}}"));
    form->addRow(QStringLiteral("Name"), name);

    auto *type = new QComboBox;
    for (int i = 0; i < 4; ++i)
        type->addItem(QString::fromUtf8(kTypeNames[i]));
    type->setCurrentIndex(rule.type);
    form->addRow(QStringLiteral("Type"), type);

    auto *notes = new QLineEdit(rule.notes);
    notes->setPlaceholderText(QStringLiteral("可选，支持 {{...}} 变量"));
    form->addRow(QStringLiteral("Notes"), notes);

    auto *billable = new QCheckBox(QStringLiteral("Billable"));
    billable->setChecked(rule.billable);
    form->addRow(QStringLiteral(""), billable);

    // 条件编辑
    auto *condHint = new QLabel(QStringLiteral("条件（AND 关系）"));
    condHint->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    form->addRow(condHint);
    auto *condTable = new QTableWidget(0, 3);
    condTable->setHorizontalHeaderLabels(
        {QStringLiteral("Field"), QStringLiteral("Value"), QStringLiteral("Regex")});
    condTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    condTable->horizontalHeader()->setMinimumSectionSize(48);
    condTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (const auto &c : rule.conditions) {
        const int row = condTable->rowCount();
        condTable->insertRow(row);
        auto *field = new QComboBox;
        for (const char *f : kFieldNames)
            field->addItem(QString::fromUtf8(f));
        field->setCurrentText(c.field);
        condTable->setCellWidget(row, 0, field);
        condTable->setItem(row, 1, new QTableWidgetItem(c.value));
        auto *regex = new QCheckBox;
        regex->setChecked(c.regex);
        condTable->setCellWidget(row, 2, regex);
    }
    auto *addCond = new QPushButton(QStringLiteral("+ 条件"));
    auto *delCond = new QPushButton(QStringLiteral("- 条件"));
    auto *condBtns = new QHBoxLayout;
    condBtns->addWidget(addCond);
    condBtns->addWidget(delCond);
    condBtns->addStretch(1);
    form->addRow(condBtns);
    form->addRow(condTable);
    Q_UNUSED(store);

    QObject::connect(addCond, &QPushButton::clicked, [condTable] {
        const int row = condTable->rowCount();
        condTable->insertRow(row);
        auto *field = new QComboBox;
        for (const char *f : kFieldNames)
            field->addItem(QString::fromUtf8(f));
        condTable->setCellWidget(row, 0, field);
        condTable->setItem(row, 1, new QTableWidgetItem);
        condTable->setCellWidget(row, 2, new QCheckBox);
    });
    QObject::connect(delCond, &QPushButton::clicked, [condTable] {
        if (condTable->currentRow() >= 0)
            condTable->removeRow(condTable->currentRow());
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    rule.name = name->text().trimmed();
    rule.type = type->currentIndex();
    rule.notes = notes->text().trimmed();
    rule.billable = billable->isChecked();
    rule.conditions.clear();
    for (int r = 0; r < condTable->rowCount(); ++r) {
        auto *field = qobject_cast<QComboBox *>(condTable->cellWidget(r, 0));
        QTableWidgetItem *val = condTable->item(r, 1);
        auto *regex = qobject_cast<QCheckBox *>(condTable->cellWidget(r, 2));
        if (!field || !val)
            continue;
        const QString v = val->text().trimmed();
        if (v.isEmpty())
            continue;
        AutoTagCondition c;
        c.field = field->currentText();
        c.value = v;
        c.regex = regex && regex->isChecked();
        rule.conditions.append(c);
    }
    return true;
}

AutoTagDialog::AutoTagDialog(TagStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("Autotagging — 规则编辑器"));
    resize(760, 500);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *hint = new QLabel(
        QStringLiteral("自动标签是按规则计算的结果，规则变更后历史任意一天随之重算。"
                       "点击时间线活动后可用右上角「自动标签」按钮快速建规则。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    root->addWidget(hint);

    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Order"), QStringLiteral("On"), QStringLiteral("Name"),
         QStringLiteral("Type"), QStringLiteral("Conditions")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto *btns = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add"));
    auto *edit = new QPushButton(QStringLiteral("Edit"));
    auto *del = new QPushButton(QStringLiteral("Delete"));
    del->setObjectName(QStringLiteral("DangerBtn"));
    m_upBtn = new QPushButton(QStringLiteral("↑"));
    m_downBtn = new QPushButton(QStringLiteral("↓"));
    m_upBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_downBtn->setObjectName(QStringLiteral("ToolBtn"));
    auto *settings = new QPushButton(QStringLiteral("设置…"));
    btns->addWidget(add);
    btns->addWidget(edit);
    btns->addWidget(del);
    btns->addWidget(m_upBtn);
    btns->addWidget(m_downBtn);
    btns->addStretch(1);
    btns->addWidget(settings);
    root->addLayout(btns);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    root->addWidget(m_status);

    connect(add, &QPushButton::clicked, this, &AutoTagDialog::onAdd);
    connect(edit, &QPushButton::clicked, this, &AutoTagDialog::onEdit);
    connect(del, &QPushButton::clicked, this, &AutoTagDialog::onDelete);
    connect(m_upBtn, &QPushButton::clicked, this, [this] { onMove(true); });
    connect(m_downBtn, &QPushButton::clicked, this, [this] { onMove(false); });
    connect(settings, &QPushButton::clicked, this, &AutoTagDialog::onSettings);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &AutoTagDialog::onDoubleClick);

    reload();
}

void AutoTagDialog::reload()
{
    if (!m_table || !m_store)
        return;
    const QList<AutoTagRule> rules = m_store->autotagRules();
    m_table->setRowCount(rules.size());
    for (int r = 0; r < rules.size(); ++r) {
        const AutoTagRule &rule = rules[r];
        m_table->setItem(r, 0, new QTableWidgetItem(QString::number(rule.order)));
        auto *on = new QTableWidgetItem(rule.enabled ? QStringLiteral("✓") : QString());
        on->setForeground(rule.enabled ? QColor(kColorOk)
                                       : QColor(kColorMuted2));
        m_table->setItem(r, 1, on);
        auto *name = new QTableWidgetItem(rule.name);
        name->setForeground(colorForString(rule.name));
        m_table->setItem(r, 2, name);
        m_table->setItem(r, 3, new QTableWidgetItem(typeName(rule.type)));
        m_table->setItem(r, 4, new QTableWidgetItem(describeRule(rule)));
    }
    m_table->resizeColumnsToContents();
    m_status->setText(QStringLiteral("%1 条规则 · 首个命中规则生效（可改）").arg(rules.size()));
}

void AutoTagDialog::editRule(int row)
{
    if (!m_store)
        return;
    QList<AutoTagRule> rules = m_store->autotagRules();
    if (row < 0)
        row = m_table->currentRow();
    if (row < 0 || row >= rules.size())
        return;
    AutoTagRule rule = rules[row];
    if (editRuleDlg(m_store, rule, this)) {
        rules[row] = rule;
        m_store->setAutotagRules(rules);
        reload();
        emit rulesChanged();
    }
}

void AutoTagDialog::onAdd()
{
    if (!m_store)
        return;
    QList<AutoTagRule> rules = m_store->autotagRules();
    AutoTagRule rule;
    rule.id = m_store->nextRuleId();
    rule.order = rules.isEmpty() ? 0 : rules.last().order + 1;
    if (editRuleDlg(m_store, rule, this)) {
        rules.append(rule);
        m_store->setAutotagRules(rules);
        reload();
        emit rulesChanged();
    }
}

void AutoTagDialog::onEdit()
{
    editRule(-1);
}

void AutoTagDialog::onDelete()
{
    if (!m_store)
        return;
    const int row = m_table->currentRow();
    QList<AutoTagRule> rules = m_store->autotagRules();
    if (row < 0 || row >= rules.size())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete"),
                              QStringLiteral("删除规则「%1」？").arg(rules[row].name)) != QMessageBox::Yes)
        return;
    rules.removeAt(row);
    m_store->setAutotagRules(rules);
    reload();
    emit rulesChanged();
}

void AutoTagDialog::onMove(bool up)
{
    if (!m_store)
        return;
    const int row = m_table->currentRow();
    QList<AutoTagRule> rules = m_store->autotagRules();
    if (row < 0 || row >= rules.size())
        return;
    const int target = up ? row - 1 : row + 1;
    if (target < 0 || target >= rules.size())
        return;
    rules.swapItemsAt(row, target);
    for (int i = 0; i < rules.size(); ++i)
        rules[i].order = i;
    m_store->setAutotagRules(rules);
    reload();
    emit rulesChanged();
    m_table->setCurrentCell(target, 0);
}

void AutoTagDialog::onSettings()
{
    if (!m_store)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Autotag 设置"));
    auto *form = new QFormLayout(&dlg);

    auto *gap = new QSpinBox;
    gap->setRange(0, 600);
    gap->setValue(m_store->autotagGapFillSec());
    gap->setSuffix(QStringLiteral(" s"));
    form->addRow(QStringLiteral("自动填充小于"), gap);

    auto *skipRest = new QCheckBox(QStringLiteral("首个命中规则生效，跳过其余"));
    skipRest->setChecked(m_store->autotagSkipRest());
    form->addRow(QString(), skipRest);

    auto *hl = new QCheckBox(QStringLiteral("高亮自动填充与吸收（猜测）部分"));
    hl->setChecked(m_store->autotagHighlightGuesses());
    form->addRow(QString(), hl);

    auto *appendData = new QCheckBox(QStringLiteral("把命中规则数据追加到标签（Append rules data）"));
    appendData->setChecked(m_store->autotagAppendRuleData());
    form->addRow(QString(), appendData);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;
    m_store->setAutotagGapFillSec(gap->value());
    m_store->setAutotagSkipRest(skipRest->isChecked());
    m_store->setAutotagHighlightGuesses(hl->isChecked());
    m_store->setAutotagAppendRuleData(appendData->isChecked());
    emit rulesChanged();
    setStatus(QStringLiteral("设置已保存"));
}

void AutoTagDialog::onDoubleClick(int row, int)
{
    editRule(row);
}

void AutoTagDialog::setStatus(const QString &msg)
{
    if (m_status)
        m_status->setText(msg);
}

} // namespace awqtui
