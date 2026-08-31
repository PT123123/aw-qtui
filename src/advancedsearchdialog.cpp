// advancedsearchdialog.cpp
#include "advancedsearchdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include "charts.h"      // formatDuration
#include "filterparser.h"
#include "mockdata.h"
#include "theme.h"

namespace awqtui {

// 时间线索引：0=All local 1=afk 2=window 3=web 4=Tags
static const char *kTimelineNames[] = {"All local timelines", "afk-status", "aw-watcher-window",
                                       "aw-watcher-web", "Tags"};

AdvancedSearchDialog::AdvancedSearchDialog(TagStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("Advanced search"));
    resize(900, 620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    // 条件行
    auto *cond = new QHBoxLayout;
    m_timelineCombo = new QComboBox;
    for (const char *n : kTimelineNames)
        m_timelineCombo->addItem(QString::fromUtf8(n));
    m_filterEdit = new QLineEdit;
    m_filterEdit->setPlaceholderText(QStringLiteral(
        "Filter…  group: / duration>1m / start>22:00 / end<6:00PM / -xxx / or / ? * / #\"regex\""));
    m_fromEdit = new QDateEdit;
    m_fromEdit->setCalendarPopup(true);
    m_fromEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_toEdit = new QDateEdit;
    m_toEdit->setCalendarPopup(true);
    m_toEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_untaggedOnly = new QCheckBox(QStringLiteral("Show only untagged"));
    cond->addWidget(new QLabel(QStringLiteral("Timeline")));
    cond->addWidget(m_timelineCombo);
    cond->addWidget(new QLabel(QStringLiteral("From")));
    cond->addWidget(m_fromEdit);
    cond->addWidget(new QLabel(QStringLiteral("To")));
    cond->addWidget(m_toEdit);
    cond->addWidget(m_untaggedOnly);
    root->addLayout(cond);
    root->addWidget(m_filterEdit);

    // 结果表
    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Title"),
                                        QStringLiteral("Start"), QStringLiteral("End"),
                                        QStringLiteral("Duration"), QStringLiteral("Notes")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &AdvancedSearchDialog::onDoubleClick);
    connect(m_table, &QTableWidget::customContextMenuRequested, this,
            &AdvancedSearchDialog::onSelectionMenu);
    root->addWidget(m_table, 1);

    // 底部：操作 + 汇总
    auto *bottom = new QHBoxLayout;
    auto *find = new QPushButton(QStringLiteral("Find"));
    find->setObjectName(QStringLiteral("PrimaryBtn"));
    auto *tagAll = new QPushButton(QStringLiteral("Tag all as"));
    auto *delAll = new QPushButton(QStringLiteral("Delete all"));
    delAll->setObjectName(QStringLiteral("DangerBtn"));
    auto *exp = new QPushButton(QStringLiteral("Export"));
    bottom->addWidget(find);
    bottom->addWidget(tagAll);
    bottom->addWidget(delAll);
    bottom->addWidget(exp);
    bottom->addStretch(1);
    m_summary = new QLabel;
    m_summary->setStyleSheet(QStringLiteral("color:%1;").arg(kColorFgMuted));
    bottom->addWidget(m_summary);
    auto *close = new QPushButton(QStringLiteral("Close"));
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(find, &QPushButton::clicked, this, &AdvancedSearchDialog::onFind);
    connect(tagAll, &QPushButton::clicked, this, &AdvancedSearchDialog::onTagAll);
    connect(delAll, &QPushButton::clicked, this, &AdvancedSearchDialog::onDeleteAll);
    connect(exp, &QPushButton::clicked, this, &AdvancedSearchDialog::onExport);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &AdvancedSearchDialog::onFind);

    // 默认最近 7 天
    const QDate today = QDate::currentDate();
    m_fromEdit->setDate(today.addDays(-6));
    m_toEdit->setDate(today);
}

void AdvancedSearchDialog::focusDay(qint64 dayStartMs)
{
    const QDate d = QDateTime::fromMSecsSinceEpoch(dayStartMs, Qt::LocalTime).date();
    m_fromEdit->setDate(d);
    m_toEdit->setDate(d);
    onFind();
}

void AdvancedSearchDialog::onFind()
{
    runSearch();
}

void AdvancedSearchDialog::runSearch()
{
    m_results.clear();
    if (!m_store)
        return;
    FilterQuery q;
    q.parse(m_filterEdit->text());
    const int timelineIdx = m_timelineCombo->currentIndex();

    const QDate from = m_fromEdit->date();
    const QDate to = m_toEdit->date();
    const int maxDays = 370;
    int days = from.daysTo(to) + 1;
    if (days > maxDays)
        days = maxDays;

    for (int i = 0; i < days; ++i) {
        const QDate date = from.addDays(i);
        const qint64 dayStart = date.startOfDay().toMSecsSinceEpoch();
        const qint64 dayEnd = date.addDays(1).startOfDay().toMSecsSinceEpoch();

        if (timelineIdx == 4) {
            // Tags timeline
            for (const auto &seg : m_store->segmentsInRange(dayStart, dayEnd)) {
                ActivityRow row;
                row.title = seg.tags.join(QStringLiteral(", "));
                row.group = seg.tags.isEmpty() ? QString() : seg.tags.first();
                row.startMs = seg.startMs;
                row.endMs = seg.endMs;
                row.notes = seg.notes;
                row.billable = seg.billable;
                if (!q.matches(row))
                    continue;
                if (m_untaggedOnly->isChecked())
                    continue; // 标签段不可能是未标记
                ResultRow r;
                r.startMs = seg.startMs;
                r.endMs = seg.endMs;
                r.title = row.title;
                r.notes = seg.notes;
                r.isTagSegment = true;
                r.tagId = seg.id;
                m_results.append(r);
            }
            continue;
        }

        // 活动时间线
        const QList<TimelineLane> lanes = generateTimelineLanes(date);
        for (const auto &lane : lanes) {
            bool matchLane = false;
            if (timelineIdx == 0)
                matchLane = true;
            else if (timelineIdx == 1 && lane.name.startsWith(QStringLiteral("afk")))
                matchLane = true;
            else if (timelineIdx == 2 && lane.name.contains(QStringLiteral("window")))
                matchLane = true;
            else if (timelineIdx == 3 && lane.name.contains(QStringLiteral("web")))
                matchLane = true;
            if (!matchLane)
                continue;
            for (const auto &ev : lane.events) {
                if (ev.endMs <= dayStart || ev.startMs >= dayEnd)
                    continue;
                ActivityRow row;
                row.title = ev.label;
                row.group = ev.label;
                row.startMs = ev.startMs;
                row.endMs = ev.endMs;
                if (!q.matches(row))
                    continue;
                if (m_untaggedOnly->isChecked()) {
                    const qint64 tagged = m_store->taggedTimeInRange(ev.startMs, ev.endMs);
                    const qint64 total = ev.endMs - ev.startMs;
                    if (tagged >= total)
                        continue; // 完全被标签覆盖
                }
                ResultRow r;
                r.startMs = ev.startMs;
                r.endMs = ev.endMs;
                r.title = ev.label;
                m_results.append(r);
            }
        }
    }

    fillTable();
}

void AdvancedSearchDialog::fillTable()
{
    m_table->setRowCount(m_results.size());
    qint64 totalMs = 0;
    for (int r = 0; r < m_results.size(); ++r) {
        const ResultRow &res = m_results[r];
        const QDateTime start(QDateTime::fromMSecsSinceEpoch(res.startMs, Qt::LocalTime));
        m_table->setItem(r, 0, new QTableWidgetItem(start.date().toString(QStringLiteral("yyyy-MM-dd"))));
        auto *titleItem = new QTableWidgetItem(res.title);
        if (res.isTagSegment)
            titleItem->setForeground(QColor(kColorAccent));
        m_table->setItem(r, 1, titleItem);
        m_table->setItem(r, 2,
                         new QTableWidgetItem(start.time().toString(QStringLiteral("HH:mm:ss"))));
        m_table->setItem(r, 3,
                         new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(res.endMs, Qt::LocalTime)
                                                  .time()
                                                  .toString(QStringLiteral("HH:mm:ss"))));
        m_table->setItem(r, 4,
                         new QTableWidgetItem(formatDuration((res.endMs - res.startMs) / 1000)));
        m_table->setItem(r, 5, new QTableWidgetItem(res.notes));
        totalMs += (res.endMs - res.startMs);
    }
    m_table->resizeColumnsToContents();
    m_summary->setText(QStringLiteral("Found: %1    Total: %2")
                           .arg(m_results.size())
                           .arg(formatDuration(totalMs / 1000)));
}

void AdvancedSearchDialog::onDoubleClick(int row, int)
{
    if (row < 0 || row >= m_results.size())
        return;
    const qint64 dayStart =
        QDateTime::fromMSecsSinceEpoch(m_results[row].startMs, Qt::LocalTime)
            .date()
            .startOfDay()
            .toMSecsSinceEpoch();
    emit jumpToDay(dayStart);
}

QString AdvancedSearchDialog::selectedComboText()
{
    bool ok = false;
    const QString text =
        QInputDialog::getText(this, QStringLiteral("Tag all as"),
                              QStringLiteral("标签组合（逗号分隔，可选 Notes 用 # 分隔）："),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty())
        return QString();
    return text.trimmed();
}

void AdvancedSearchDialog::onTagAll()
{
    if (m_results.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Tag all as"), QStringLiteral("没有结果"));
        return;
    }
    const QString text = selectedComboText();
    if (text.isEmpty())
        return;
    const QStringList parts = text.split(QLatin1Char('#'), Qt::KeepEmptyParts);
    const QStringList tags = parts.isEmpty()
                                 ? QStringList()
                                 : parts[0].split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList cleanTags;
    for (const auto &t : tags) {
        const QString s = t.trimmed();
        if (!s.isEmpty())
            cleanTags << s;
    }
    if (cleanTags.isEmpty())
        return;
    const QString notes = parts.size() > 1 ? parts[1].trimmed() : QString();
    int count = 0;
    for (const auto &r : m_results) {
        m_store->addSegment(r.startMs, r.endMs, cleanTags, notes, false);
        ++count;
    }
    emit tagsChanged();
    QMessageBox::information(this, QStringLiteral("Tag all as"),
                             QStringLiteral("已为 %1 段打上标签").arg(count));
    runSearch();
}

void AdvancedSearchDialog::onDeleteAll()
{
    if (m_results.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Delete all"), QStringLiteral("没有结果"));
        return;
    }
    int tagCount = 0;
    for (const auto &r : m_results)
        if (r.isTagSegment)
            ++tagCount;
    if (tagCount == 0) {
        QMessageBox::information(
            this, QStringLiteral("Delete all"),
            QStringLiteral("当前结果都是活动时间线，无法直接删除。\n请切换到 Tags 时间线搜索标签段后再删除。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Delete all"),
                              QStringLiteral("删除 %1 个标签段，确定？").arg(tagCount)) !=
        QMessageBox::Yes)
        return;
    for (const auto &r : m_results)
        if (r.isTagSegment)
            m_store->removeSegment(r.tagId);
    emit tagsChanged();
    runSearch();
}

void AdvancedSearchDialog::onExport()
{
    if (m_results.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export"), QStringLiteral("没有结果"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export results"),
                                                      QStringLiteral("search_results.txt"),
                                                      QStringLiteral("Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QStringList lines;
    lines << QStringLiteral("Date\tTitle\tStart\tEnd\tDuration\tNotes");
    for (const auto &r : m_results) {
        const QDateTime st(QDateTime::fromMSecsSinceEpoch(r.startMs, Qt::LocalTime));
        lines << QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6")
                     .arg(st.date().toString(QStringLiteral("yyyy-MM-dd")))
                     .arg(r.title)
                     .arg(st.time().toString(QStringLiteral("HH:mm:ss")))
                     .arg(QDateTime::fromMSecsSinceEpoch(r.endMs, Qt::LocalTime)
                              .time()
                              .toString(QStringLiteral("HH:mm:ss")))
                     .arg(formatDuration((r.endMs - r.startMs) / 1000))
                     .arg(r.notes);
    }
    f.write(lines.join(QLatin1Char('\n')).toUtf8());
    QMessageBox::information(this, QStringLiteral("Export"),
                             QStringLiteral("已导出 %1 行到 %2").arg(m_results.size()).arg(path));
}

void AdvancedSearchDialog::onSelectionMenu(const QPoint &pos)
{
    const QList<QTableWidgetItem *> sel = m_table->selectedItems();
    if (sel.isEmpty())
        return;
    QSet<int> rows;
    for (const auto *it : sel)
        rows.insert(it->row());

    QMenu menu(this);
    QAction *tagSel = menu.addAction(QStringLiteral("Tag selected as…"));
    QAction *delSel = menu.addAction(QStringLiteral("Delete selected"));
    QAction *act = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (act == tagSel) {
        const QString text = selectedComboText();
        if (text.isEmpty())
            return;
        const QStringList tags = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        QStringList clean;
        for (const auto &t : tags) {
            const QString s = t.trimmed();
            if (!s.isEmpty())
                clean << s;
        }
        if (clean.isEmpty())
            return;
        for (const int row : rows) {
            const ResultRow &r = m_results[row];
            m_store->addSegment(r.startMs, r.endMs, clean, QString(), false);
        }
        emit tagsChanged();
        runSearch();
    } else if (act == delSel) {
        int tagCount = 0;
        for (const int row : rows)
            if (m_results[row].isTagSegment)
                ++tagCount;
        if (tagCount == 0) {
            QMessageBox::information(this, QStringLiteral("Delete selected"),
                                     QStringLiteral("所选结果不是标签段，无法删除。"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete selected"),
                                  QStringLiteral("删除 %1 个标签段？").arg(tagCount)) != QMessageBox::Yes)
            return;
        for (const int row : rows)
            if (m_results[row].isTagSegment)
                m_store->removeSegment(m_results[row].tagId);
        emit tagsChanged();
        runSearch();
    }
}

} // namespace awqtui
