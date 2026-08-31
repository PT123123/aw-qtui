// addtagdialog.cpp
#include "addtagdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QVBoxLayout>

#include "charts.h" // formatDuration
#include "theme.h"

namespace awqtui {

AddTagDialog::AddTagDialog(TagStore *store, qint64 startMs, qint64 endMs, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("Add tag"));
    resize(760, 520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    // 上半：编辑区 + 最近标签
    auto *split = new QSplitter(Qt::Horizontal);

    auto *editPane = new QWidget;
    auto *el = new QVBoxLayout(editPane);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(8);

    auto *tagLbl = new QLabel(QStringLiteral("Tags"));
    tagLbl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(kColorFgMuted));
    el->addWidget(tagLbl);
    m_tagsEdit = new QLineEdit;
    m_tagsEdit->setPlaceholderText(QStringLiteral("用逗号分隔（如 \"Project 1, Testing\"）"));
    connect(m_tagsEdit, &QLineEdit::textEdited, this, &AddTagDialog::onTagsEdited);
    el->addWidget(m_tagsEdit);

    // Billable + 时间行
    auto *timeRow = new QHBoxLayout;
    m_billable = new QCheckBox(QStringLiteral("Billable ($)"));
    timeRow->addWidget(m_billable);
    timeRow->addStretch(1);
    el->addLayout(timeRow);

    auto *startLbl = new QLabel(QStringLiteral("Start"));
    startLbl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(kColorFgMuted));
    el->addWidget(startLbl);
    m_startEdit = new QDateTimeEdit;
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_startEdit->setCalendarPopup(true);
    el->addWidget(m_startEdit);

    auto *endLbl = new QLabel(QStringLiteral("End"));
    endLbl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(kColorFgMuted));
    el->addWidget(endLbl);
    m_endEdit = new QDateTimeEdit;
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_endEdit->setCalendarPopup(true);
    el->addWidget(m_endEdit);

    m_durationLabel = new QLabel;
    m_durationLabel->setStyleSheet(QStringLiteral("color:%1;font-weight:600;").arg(kColorAccent));
    el->addWidget(m_durationLabel);

    auto *notesLbl = new QLabel(QStringLiteral("Notes"));
    notesLbl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(kColorFgMuted));
    el->addWidget(notesLbl);
    m_notesEdit = new QPlainTextEdit;
    m_notesEdit->setPlaceholderText(QStringLiteral("备注（可选）"));
    m_notesEdit->setMaximumHeight(80);
    el->addWidget(m_notesEdit);
    el->addStretch(1);
    split->addWidget(editPane);

    // 右侧：Tag picker（层级）
    auto *pickerPane = new QWidget;
    auto *pl = new QVBoxLayout(pickerPane);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(6);
    auto *pickerTitle = new QLabel(QStringLiteral("Tag picker"));
    pickerTitle->setStyleSheet(QStringLiteral("color:%1;font-size:12px;font-weight:600;").arg(kColorFgMuted));
    pl->addWidget(pickerTitle);

    auto *pickerBar = new QHBoxLayout;
    m_upBtn = new QPushButton(QStringLiteral("↑ 上一级"));
    m_upBtn->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_upBtn, &QPushButton::clicked, this, &AddTagDialog::goUp);
    pickerBar->addWidget(m_upBtn);
    m_sortCombo = new QComboBox;
    m_sortCombo->addItems({QStringLiteral("A-Z"), QStringLiteral("Last used")});
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AddTagDialog::onSortChanged);
    pickerBar->addWidget(m_sortCombo);
    pl->addLayout(pickerBar);

    m_picker = new QListWidget;
    connect(m_picker, &QListWidget::itemClicked, this, &AddTagDialog::onPickerItem);
    pl->addWidget(m_picker, 1);
    split->addWidget(pickerPane);

    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    // 最近标签
    auto *recentLbl = new QLabel(QStringLiteral("最近使用"));
    recentLbl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(kColorFgMuted));
    root->addWidget(recentLbl);
    m_recent = new QListWidget;
    m_recent->setMaximumHeight(120);
    connect(m_recent, &QListWidget::itemClicked, this, &AddTagDialog::applyRecent);
    root->addWidget(m_recent);

    // 按钮行
    auto *btnRow = new QHBoxLayout;
    auto *help = new QLabel(QStringLiteral("提示：标签用逗号分隔，首个标签决定颜色"));
    help->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    btnRow->addWidget(help);
    btnRow->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    auto *ok = new QPushButton(QStringLiteral("OK"));
    ok->setObjectName(QStringLiteral("PrimaryBtn"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancel);
    btnRow->addWidget(ok);
    root->addLayout(btnRow);

    // 初始值
    m_building = true;
    m_billable->setChecked(store ? store->newTagsBillableByDefault() : false);
    m_startEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(startMs, Qt::LocalTime));
    m_endEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(endMs, Qt::LocalTime));
    m_building = false;
    onTimeChanged();
    fillRecent();
    refreshPicker();
}

QStringList AddTagDialog::tags() const
{
    const QString text = m_tagsEdit->text();
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList out;
    for (const auto &p : parts) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

QString AddTagDialog::notes() const
{
    return m_notesEdit->toPlainText().trimmed();
}

bool AddTagDialog::billable() const
{
    return m_billable->isChecked();
}

qint64 AddTagDialog::startMs() const
{
    return m_startEdit->dateTime().toMSecsSinceEpoch();
}

qint64 AddTagDialog::endMs() const
{
    return m_endEdit->dateTime().toMSecsSinceEpoch();
}

void AddTagDialog::onTagsEdited()
{
    // 输入变化时高亮提示（无实际动作；预留联想）
}

void AddTagDialog::onTimeChanged()
{
    const qint64 dur = qMax<qint64>(0, endMs() - startMs());
    m_durationLabel->setText(QStringLiteral("Duration: %1").arg(formatDuration(dur / 1000)));
}

void AddTagDialog::fillRecent()
{
    m_recent->clear();
    if (!m_store)
        return;
    const QStringList combos = m_store->allCombinations();
    for (const auto &c : combos) {
        auto *item = new QListWidgetItem(c);
        QListWidgetItem *unused = item;
        Q_UNUSED(unused);
        const QStringList parts = c.split(QLatin1Char(','));
        item->setForeground(m_store->tagColor(parts.first()));
        m_recent->addItem(item);
    }
}

void AddTagDialog::applyRecent(QListWidgetItem *item)
{
    if (!item)
        return;
    m_tagsEdit->setText(item->text());
}

void AddTagDialog::onPickerItem(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString data = item->data(Qt::UserRole).toString();
    if (data == QLatin1String("__commit__")) {
        // 选择当前路径
        m_tagsEdit->setText(m_path.join(QStringLiteral(", ")));
        return;
    }
    // 下钻
    m_path.append(data);
    refreshPicker();
}

void AddTagDialog::onSortChanged(int)
{
    refreshPicker();
}

void AddTagDialog::goUp()
{
    if (!m_path.isEmpty()) {
        m_path.removeLast();
        refreshPicker();
    }
}

void AddTagDialog::refreshPicker()
{
    if (!m_picker)
        return;
    m_picker->clear();
    if (!m_store)
        return;
    m_upBtn->setEnabled(!m_path.isEmpty());

    // 收集所有组合
    QStringList combos = m_store->allCombinations();
    if (m_sortCombo && m_sortCombo->currentIndex() == 0) {
        std::sort(combos.begin(), combos.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
    }

    QSet<QString> nextLevel; // 下一级候选
    for (const auto &c : combos) {
        const QStringList parts = c.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() <= m_path.size())
            continue;
        bool prefix = true;
        for (int i = 0; i < m_path.size(); ++i) {
            if (parts[i].trimmed() != m_path[i]) {
                prefix = false;
                break;
            }
        }
        if (prefix)
            nextLevel.insert(parts[m_path.size()].trimmed());
    }

    if (!m_path.isEmpty()) {
        auto *commit = new QListWidgetItem(QStringLiteral("✔ 使用：%1").arg(m_path.join(QStringLiteral(", "))));
        commit->setData(Qt::UserRole, QStringLiteral("__commit__"));
        commit->setForeground(QColor(kColorOk));
        m_picker->addItem(commit);
    }

    if (nextLevel.isEmpty() && !m_path.isEmpty()) {
        // 已经是叶子：直接提供提交
    }

    QStringList sorted(nextLevel.begin(), nextLevel.end());
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    for (const auto &n : sorted) {
        auto *item = new QListWidgetItem(n);
        item->setData(Qt::UserRole, n);
        item->setForeground(m_store->tagColor(n));
        m_picker->addItem(item);
    }
    if (m_path.isEmpty() && nextLevel.isEmpty())
        m_picker->addItem(QStringLiteral("（暂无标签，先在收件箱/时间线创建）"));
}

} // namespace awqtui
