// focuscharts.cpp —— 专注统计图表 / 日历实现（QPainter 自绘）
#include "focuscharts.h"

#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "focusstore.h"
#include "theme.h"
#include "todomodels.h"
#include "todostore.h"

namespace awqtui {

namespace {

// 按比例生成强调色块（透明 -> 实色）
inline QColor cellColor(qreal ratio)
{
    QColor c(QString::fromLatin1(kColorAccent));
    c.setAlphaF(qBound(0.0, 0.10 + 0.82 * ratio, 0.95));
    return c;
}

// 年度热力图五档颜色（0 空、1 最浅 .. 4 最深）
inline QColor levelColor(int level)
{
    const qreal alphas[] = {0.0, 0.16, 0.38, 0.62, 0.92};
    QColor c(QString::fromLatin1(kColorAccent));
    c.setAlphaF(qBound(0.0, alphas[level], 0.95));
    return c;
}

inline void drawRoundedCell(QPainter &p, const QRectF &r, const QColor &fill)
{
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(r, 2.0, 2.0);
}

const QString kDowLabels = QStringLiteral("一二三四五六日");

} // namespace

// ==================================================================== //
// 周热力格
// ==================================================================== //
WeekHeatWidget::WeekHeatWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(si(520), si(260));
}

void WeekHeatWidget::setSessions(const QList<FocusSession> &sessions, const QDate &weekStart)
{
    m_sessions = sessions;
    m_weekStart = weekStart;
    m_minutes = QVector<QVector<qint64>>(7, QVector<qint64>(24, 0));
    m_maxMinutes = 0;

    const qint64 week0 = dayStartMs(weekStart);
    const qint64 week1 = dayStartMs(weekStart.addDays(7));
    for (const auto &s : m_sessions) {
        qint64 t0 = qMax(s.startMs, week0);
        const qint64 t1 = qMin(s.endMs, week1);
        if (t1 <= t0)
            continue;
        while (t0 < t1) {
            const QDateTime cur = QDateTime::fromMSecsSinceEpoch(t0);
            const int dow = cur.date().dayOfWeek(); // 1=Mon..7=Sun
            const int hour = cur.time().hour();
            QDateTime nextHour = cur.addSecs(3600 - cur.time().minute() * 60 - cur.time().second());
            const qint64 cellEnd = qMin(t1, nextHour.toMSecsSinceEpoch());
            const qint64 mins = (cellEnd - t0) / 60000;
            if (mins > 0) {
                m_minutes[dow - 1][hour] += mins;
                m_maxMinutes = qMax(m_maxMinutes, m_minutes[dow - 1][hour]);
            }
            t0 = cellEnd;
        }
    }
    update();
}

QSize WeekHeatWidget::sizeHint() const
{
    return QSize(si(560), si(300));
}

void WeekHeatWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int margin = si(6);
    const int leftPad = si(42);
    const int topPad = si(22);
    const int spacing = si(2);

    const qreal cellW = (width() - leftPad - margin * 2 - 6.0 * spacing) / 7.0;
    const qreal cellH = (height() - topPad - margin * 2 - 23.0 * spacing) / 24.0;

    // 列头（周一~周日）
    p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
    QFont f = p.font();
    f.setPixelSize(qMax(9, si(11)));
    p.setFont(f);
    for (int d = 0; d < 7; ++d) {
        const QRectF r(leftPad + margin + d * (cellW + spacing), si(2), cellW, topPad - si(4));
        p.drawText(r, Qt::AlignCenter, QString(kDowLabels.at(d)));
    }

    // 时间刻度
    for (int h = 0; h < 24; h += 4) {
        const QRectF r(0, topPad + margin + h * (cellH + spacing), leftPad - si(6), cellH);
        p.drawText(r, Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1:00").arg(h, 2, 10, QLatin1Char('0')));
    }

    // 今日列高亮
    const QDate today = QDate::currentDate();
    const int todayCol = today >= m_weekStart && today < m_weekStart.addDays(7)
                             ? today.dayOfWeek() - 1 : -1;

    for (int d = 0; d < 7; ++d) {
        for (int h = 0; h < 24; ++h) {
            const QRectF r(leftPad + margin + d * (cellW + spacing),
                           topPad + margin + h * (cellH + spacing), cellW, cellH);
            const qint64 mins = m_minutes[d][h];
            const qreal ratio = m_maxMinutes > 0 ? qreal(mins) / m_maxMinutes : 0.0;
            drawRoundedCell(p, r, cellColor(ratio));
            if (d == todayCol) {
                p.setPen(QPen(QColor(QString::fromLatin1(kColorAccent)), 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, 2.0, 2.0);
            }
        }
    }
}

// ==================================================================== //
// 专注时间线页
// ==================================================================== //
FocusWeekPage::FocusWeekPage(FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_focus(focus)
{
    m_weekStart = QDate::currentDate().addDays(1 - QDate::currentDate().dayOfWeek());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("专注时间线"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    head->addWidget(title);
    m_title = new QLabel;
    m_title->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                               .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    head->addWidget(m_title);
    head->addStretch(1);
    auto *prev = new QToolButton;
    prev->setText(QStringLiteral("‹"));
    auto *next = new QToolButton;
    next->setText(QStringLiteral("›"));
    auto *todayBtn = new QPushButton(QStringLiteral("本周"));
    head->addWidget(prev);
    head->addWidget(next);
    head->addWidget(todayBtn);
    root->addLayout(head);

    m_heat = new WeekHeatWidget;
    root->addWidget(m_heat, 1);

    connect(prev, &QToolButton::clicked, this, [this] { shiftWeek(-1); });
    connect(next, &QToolButton::clicked, this, [this] { shiftWeek(1); });
    connect(todayBtn, &QPushButton::clicked, this, [this] {
        m_weekStart = QDate::currentDate().addDays(1 - QDate::currentDate().dayOfWeek());
        refresh();
    });
    connect(m_focus, &FocusSource::dataChanged, this, &FocusWeekPage::refresh);
    refresh();
}

void FocusWeekPage::applyUiScale()
{
    m_heat->update();
    setStyleSheet(QString());
}

void FocusWeekPage::shiftWeek(int n)
{
    m_weekStart = m_weekStart.addDays(7 * n);
    refresh();
}

void FocusWeekPage::updateTitle()
{
    m_title->setText(QStringLiteral("%1 ~ %2")
                         .arg(m_weekStart.toString(QStringLiteral("MM-dd")),
                              m_weekStart.addDays(6).toString(QStringLiteral("MM-dd"))));
}

void FocusWeekPage::refresh()
{
    updateTitle();
    if (m_focus)
        m_heat->setSessions(m_focus->sessions(), m_weekStart);
}

// ==================================================================== //
// 月度热力图
// ==================================================================== //
MonthlyHeatWidget::MonthlyHeatWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(si(520), si(200));
}

void MonthlyHeatWidget::setSessions(const QList<FocusSession> &sessions, const QDate &month)
{
    m_sessions = sessions;
    m_month = QDate(month.year(), month.month(), 1);
    m_maxMinutes = 0;
    const int days = m_month.daysInMonth();
    for (int d = 1; d <= days; ++d)
        m_maxMinutes = qMax(m_maxMinutes, dayFocusSec(m_sessions, m_month.addDays(d - 1)) / 60);
    update();
}

QSize MonthlyHeatWidget::sizeHint() const
{
    return QSize(si(560), si(220));
}

void MonthlyHeatWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int margin = si(6);
    const int topPad = si(22);
    const int spacing = si(2);

    const QDate first = m_month;
    const int startOffset = first.dayOfWeek() - 1; // 周一 = 0
    const int cellsTotal = startOffset + first.daysInMonth();
    const int rows = (cellsTotal + 6) / 7;

    const qreal cellW = (width() - margin * 2 - 6.0 * spacing) / 7.0;
    const qreal cellH = (height() - topPad - margin * 2 - (rows - 1.0) * spacing) / rows;

    // 列头
    p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
    QFont f = p.font();
    f.setPixelSize(qMax(9, si(11)));
    p.setFont(f);
    for (int d = 0; d < 7; ++d) {
        const QRectF r(margin + d * (cellW + spacing), si(2), cellW, topPad - si(4));
        p.drawText(r, Qt::AlignCenter, QString(kDowLabels.at(d)));
    }

    const QDate today = QDate::currentDate();
    const QDate gridStart = first.addDays(-startOffset);
    for (int i = 0; i < rows * 7; ++i) {
        const QDate date = gridStart.addDays(i);
        const int col = i % 7, row = i / 7;
        const QRectF r(margin + col * (cellW + spacing),
                       topPad + margin + row * (cellH + spacing), cellW, cellH);
        const bool inMonth = (date.year() == m_month.year() && date.month() == m_month.month());
        if (!inMonth) {
            drawRoundedCell(p, r, QColor(0, 0, 0, 0));
            continue;
        }
        const qreal ratio = m_maxMinutes > 0
                                ? qreal(dayFocusSec(m_sessions, date) / 60) / m_maxMinutes : 0.0;
        drawRoundedCell(p, r, cellColor(ratio));
        // 天数
        p.setPen(QColor(QString::fromLatin1(kColorFg)));
        p.drawText(r.adjusted(si(4), si(2), 0, 0), Qt::AlignLeft | Qt::AlignTop,
                   QString::number(date.day()));
        if (date == today) {
            p.setPen(QPen(QColor(QString::fromLatin1(kColorAccent)), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 2.0, 2.0);
        }
    }
}

// ==================================================================== //
// 年度热力图
// ==================================================================== //
AnnualHeatWidget::AnnualHeatWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(si(520), si(180));
}

void AnnualHeatWidget::setSessions(const QList<FocusSession> &sessions, int year)
{
    m_sessions = sessions;
    m_year = year;
    update();
}

QSize AnnualHeatWidget::sizeHint() const
{
    return QSize(si(560), si(200));
}

void AnnualHeatWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int margin = si(6);
    const int topPad = si(18);
    const int legendH = si(22);

    const QDate jan1(m_year, 1, 1);
    const int startOffset = jan1.dayOfWeek() - 1; // 周一 = 0
    const int totalDays = jan1.daysInYear();
    const int cols = (startOffset + totalDays + 6) / 7;

    const qreal cellW = (width() - margin * 2) / qreal(qMax(1, cols));
    const qreal cellH = (height() - topPad - legendH - margin * 2) / 7.0;
    const qreal size = qMin(cellW, cellH);

    // 月份标注（每月 1 号所在列）
    p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
    QFont f = p.font();
    f.setPixelSize(qMax(9, si(10)));
    p.setFont(f);
    for (int m = 1; m <= 12; ++m) {
        const QDate first(m_year, m, 1);
        const int col = (first.toJulianDay() - jan1.toJulianDay() + startOffset) / 7;
        const qreal x = margin + col * cellW;
        p.drawText(QRectF(x, si(2), cellW * 2, topPad - si(4)), Qt::AlignLeft,
                   QStringLiteral("%1月").arg(m));
    }

    // 色块
    const QDate today = QDate::currentDate();
    for (int i = 0; i < totalDays; ++i) {
        const QDate date = jan1.addDays(i);
        const int col = (date.toJulianDay() - jan1.toJulianDay() + startOffset) / 7;
        const int row = date.dayOfWeek() - 1;
        const qreal x = margin + col * cellW + (cellW - size) / 2;
        const qreal y = topPad + row * cellH + (cellH - size) / 2;
        const QRectF r(x, y, size, size);
        const qint64 sec = dayFocusSec(m_sessions, date);
        int level = 0;
        if (sec > 0) {
            const qint64 h = sec / 3600;
            level = h >= 5 ? 4 : h >= 3 ? 3 : h >= 1 ? 2 : 1;
        }
        if (level > 0)
            drawRoundedCell(p, r, levelColor(level));
        else
            drawRoundedCell(p, r, QColor(0, 0, 0, 0));
        if (date == today) {
            p.setPen(QPen(QColor(QString::fromLatin1(kColorAccent)), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 2.0, 2.0);
        }
    }

    // 图例
    const QStringList labels = {QStringLiteral("0m"), QStringLiteral("≤1h"), QStringLiteral("≤3h"),
                                QStringLiteral("≤5h"), QStringLiteral(">5h")};
    const qreal lw = si(16), gap = si(6);
    qreal lx = margin;
    const qreal ly = height() - legendH + si(4);
    for (int i = 0; i < 5; ++i) {
        if (i > 0)
            drawRoundedCell(p, QRectF(lx, ly, lw, lw), levelColor(i));
        else {
            p.setPen(QPen(QColor(QString::fromLatin1(kColorBorder)), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(lx, ly, lw, lw), 2.0, 2.0);
        }
        p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
        p.drawText(QRectF(lx + lw + si(3), ly - si(2), si(34), lw + si(4)),
                   Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
        lx += lw + si(40);
    }
}

// ==================================================================== //
// 热力图页
// ==================================================================== //
FocusHeatmapPage::FocusHeatmapPage(FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_focus(focus)
{
    const QDate today = QDate::currentDate();
    m_month = QDate(today.year(), today.month(), 1);
    m_year = today.year();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(14));

    // 月度
    auto *mh = new QHBoxLayout;
    auto *mt = new QLabel(QStringLiteral("月度热力图"));
    mt->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                          .arg(sp(15), QString::fromLatin1(kColorFg)));
    mh->addWidget(mt);
    m_monthTitle = new QLabel;
    m_monthTitle->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                    .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    mh->addWidget(m_monthTitle);
    mh->addStretch(1);
    auto *mp = new QToolButton;
    mp->setText(QStringLiteral("‹"));
    auto *mn = new QToolButton;
    mn->setText(QStringLiteral("›"));
    mh->addWidget(mp);
    mh->addWidget(mn);
    root->addLayout(mh);
    m_monthHeat = new MonthlyHeatWidget;
    root->addWidget(m_monthHeat);

    // 年度
    auto *yh = new QHBoxLayout;
    auto *yt = new QLabel(QStringLiteral("年度热力图"));
    yt->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                          .arg(sp(15), QString::fromLatin1(kColorFg)));
    yh->addWidget(yt);
    m_yearTitle = new QLabel;
    m_yearTitle->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                   .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    yh->addWidget(m_yearTitle);
    yh->addStretch(1);
    auto *yp = new QToolButton;
    yp->setText(QStringLiteral("‹"));
    auto *yn = new QToolButton;
    yn->setText(QStringLiteral("›"));
    yh->addWidget(yp);
    yh->addWidget(yn);
    root->addLayout(yh);
    m_yearHeat = new AnnualHeatWidget;
    root->addWidget(m_yearHeat);

    connect(mp, &QToolButton::clicked, this, [this] { shiftMonth(-1); });
    connect(mn, &QToolButton::clicked, this, [this] { shiftMonth(1); });
    connect(yp, &QToolButton::clicked, this, [this] { shiftYear(-1); });
    connect(yn, &QToolButton::clicked, this, [this] { shiftYear(1); });
    connect(m_focus, &FocusSource::dataChanged, this, &FocusHeatmapPage::refresh);
    refresh();
}

void FocusHeatmapPage::applyUiScale()
{
    m_monthHeat->update();
    m_yearHeat->update();
    setStyleSheet(QString());
}

void FocusHeatmapPage::shiftMonth(int n)
{
    m_month = m_month.addMonths(n);
    refresh();
}

void FocusHeatmapPage::shiftYear(int n)
{
    m_year += n;
    refresh();
}

void FocusHeatmapPage::updateTitles()
{
    m_monthTitle->setText(QStringLiteral("%1 年 %2 月").arg(m_month.year()).arg(m_month.month()));
    m_yearTitle->setText(QStringLiteral("%1 年").arg(m_year));
}

void FocusHeatmapPage::refresh()
{
    updateTitles();
    if (!m_focus)
        return;
    m_monthHeat->setSessions(m_focus->sessions(), m_month);
    m_yearHeat->setSessions(m_focus->sessions(), m_year);
}

// ==================================================================== //
// 最佳专注时间（24h 柱状）
// ==================================================================== //
BestChartWidget::BestChartWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(si(520), si(220));
}

void BestChartWidget::setSessions(const QList<FocusSession> &sessions, const QDate &month)
{
    m_sessions = sessions;
    m_month = QDate(month.year(), month.month(), 1);
    m_bucketSec = QVector<qint64>(8, 0);
    m_maxSec = 0;

    const qint64 m0 = dayStartMs(m_month);
    const qint64 m1 = dayStartMs(m_month.addMonths(1));
    for (const auto &s : m_sessions) {
        qint64 t0 = qMax(s.startMs, m0);
        const qint64 t1 = qMin(s.endMs, m1);
        if (t1 <= t0)
            continue;
        while (t0 < t1) {
            const QDateTime cur = QDateTime::fromMSecsSinceEpoch(t0);
            const int bucket = qBound(0, cur.time().hour() / 3, 7);
            QDateTime nextHour = cur.addSecs(3600 - cur.time().minute() * 60 - cur.time().second());
            const qint64 cellEnd = qMin(t1, nextHour.toMSecsSinceEpoch());
            m_bucketSec[bucket] += (cellEnd - t0) / 1000;
            t0 = cellEnd;
        }
    }
    for (qint64 v : m_bucketSec)
        m_maxSec = qMax(m_maxSec, v);
    update();
}

QSize BestChartWidget::sizeHint() const
{
    return QSize(si(560), si(260));
}

void BestChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int leftPad = si(52);
    const int bottomPad = si(24);
    const int topPad = si(16);
    const int rightPad = si(12);

    const QRectF plot(leftPad, topPad, width() - leftPad - rightPad,
                      height() - topPad - bottomPad);

    // 网格 + y 轴刻度
    p.setPen(QColor(kColorAxis));
    const qint64 maxV = qMax<qint64>(m_maxSec, 1);
    const int gridN = 4;
    for (int i = 0; i <= gridN; ++i) {
        const qreal y = plot.bottom() - plot.height() * i / gridN;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
        p.drawText(QRectF(0, y - si(8), leftPad - si(6), si(16)), Qt::AlignRight | Qt::AlignVCenter,
                   fmtDuration(maxV * i / gridN));
        p.setPen(QColor(kColorAxis));
    }

    // 柱子
    const qreal bw = plot.width() / 8.0 * 0.58;
    const QStringList labels = {QStringLiteral("00:00"), QStringLiteral("03:00"), QStringLiteral("06:00"),
                                QStringLiteral("09:00"), QStringLiteral("12:00"), QStringLiteral("15:00"),
                                QStringLiteral("18:00"), QStringLiteral("21:00")};
    for (int i = 0; i < 8; ++i) {
        const qreal cx = plot.left() + plot.width() / 8.0 * (i + 0.5);
        const qreal h = m_bucketSec[i] > 0 ? plot.height() * qreal(m_bucketSec[i]) / maxV : 0.0;
        const QRectF r(cx - bw / 2, plot.bottom() - h, bw, h);
        if (h > 0) {
            QColor c(QString::fromLatin1(kColorAccent));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(r, 3.0, 3.0);
            p.setPen(QColor(QString::fromLatin1(kColorFg)));
            p.drawText(QRectF(cx - bw, plot.bottom() - h - si(16), bw * 2, si(14)),
                       Qt::AlignHCenter, fmtDuration(m_bucketSec[i]));
        }
        p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
        p.drawText(QRectF(cx - si(28), plot.bottom() + si(4), si(56), si(16)),
                   Qt::AlignHCenter, labels[i]);
    }
}

// ==================================================================== //
// 最佳专注时间页
// ==================================================================== //
FocusBestPage::FocusBestPage(FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_focus(focus)
{
    const QDate today = QDate::currentDate();
    m_month = QDate(today.year(), today.month(), 1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("最佳专注时间"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    head->addWidget(title);
    m_title = new QLabel;
    m_title->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                               .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    head->addWidget(m_title);
    head->addStretch(1);
    auto *prev = new QToolButton;
    prev->setText(QStringLiteral("‹"));
    auto *next = new QToolButton;
    next->setText(QStringLiteral("›"));
    head->addWidget(prev);
    head->addWidget(next);
    root->addLayout(head);

    m_chart = new BestChartWidget;
    root->addWidget(m_chart, 1);

    connect(prev, &QToolButton::clicked, this, [this] { shiftMonth(-1); });
    connect(next, &QToolButton::clicked, this, [this] { shiftMonth(1); });
    connect(m_focus, &FocusSource::dataChanged, this, &FocusBestPage::refresh);
    refresh();
}

void FocusBestPage::applyUiScale()
{
    m_chart->update();
    setStyleSheet(QString());
}

void FocusBestPage::shiftMonth(int n)
{
    m_month = m_month.addMonths(n);
    refresh();
}

void FocusBestPage::updateTitle()
{
    m_title->setText(QStringLiteral("%1 年 %2 月 · 各时段专注总时长（3h 一档）")
                         .arg(m_month.year()).arg(m_month.month()));
}

void FocusBestPage::refresh()
{
    updateTitle();
    if (m_focus)
        m_chart->setSessions(m_focus->sessions(), m_month);
}

// ==================================================================== //
// 月历控件
// ==================================================================== //
MonthCalendarWidget::MonthCalendarWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(si(520), si(230));
}

void MonthCalendarWidget::setSessions(const QList<FocusSession> &sessions, const QDate &month)
{
    m_sessions = sessions;
    m_month = QDate(month.year(), month.month(), 1);
    update();
}

void MonthCalendarWidget::selectDate(const QDate &date)
{
    if (!date.isValid())
        return;
    m_selected = date;
    m_hasSel = true;
    m_month = QDate(date.year(), date.month(), 1);
    update();
}

QSize MonthCalendarWidget::sizeHint() const
{
    return QSize(si(560), si(250));
}

QRect MonthCalendarWidget::dayRect(int index) const
{
    const int margin = si(4);
    const int topPad = si(22);
    const int spacing = si(2);
    const int col = index % 7, row = index / 7;
    const qreal cellW = (width() - margin * 2 - 6.0 * spacing) / 7.0;
    const qreal cellH = (height() - topPad - margin * 2 - 5.0 * spacing) / 6.0;
    return QRect(margin + qRound(col * (cellW + spacing)),
                 topPad + margin + qRound(row * (cellH + spacing)),
                 qRound(cellW), qRound(cellH));
}

void MonthCalendarWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QDate first = m_month;
    const int startOffset = first.dayOfWeek() - 1;
    const QDate gridStart = first.addDays(-startOffset);
    const QDate today = QDate::currentDate();

    // 列头
    p.setPen(QColor(QString::fromLatin1(kColorFgMuted)));
    QFont f = p.font();
    f.setPixelSize(qMax(9, si(11)));
    p.setFont(f);
    const QRect head0 = dayRect(0);
    for (int d = 0; d < 7; ++d) {
        const QRectF r(head0.left() + d * (qreal(head0.width()) + si(2)), si(2),
                       head0.width(), si(18));
        p.drawText(r, Qt::AlignCenter, QString(kDowLabels.at(d)));
    }

    for (int i = 0; i < 42; ++i) {
        const QDate date = gridStart.addDays(i);
        const QRect r = dayRect(i);
        const bool inMonth = (date.year() == m_month.year() && date.month() == m_month.month());
        QRectF body = QRectF(r).adjusted(1, 1, -1, -1);

        if (m_hasSel && date == m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QString::fromLatin1(kColorAccent)));
            p.drawRoundedRect(body, 6.0, 6.0);
            p.setPen(QColor(QStringLiteral("white")));
        } else {
            p.setPen(inMonth ? QColor(QString::fromLatin1(kColorFg))
                             : QColor(QString::fromLatin1(kColorFgMuted)));
            if (date == today) {
                p.setPen(QPen(QColor(QString::fromLatin1(kColorAccent)), 1.2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(body, 6.0, 6.0);
                p.setPen(QColor(QString::fromLatin1(kColorFg)));
            }
        }
        p.drawText(body.adjusted(si(6), si(3), 0, 0), Qt::AlignLeft | Qt::AlignTop,
                   QString::number(date.day()));

        // 当天有专注 -> 小圆点
        const qint64 sec = dayFocusSec(m_sessions, date);
        if (sec > 0) {
            const bool sel = m_hasSel && date == m_selected;
            p.setPen(Qt::NoPen);
            p.setBrush(sel ? QColor(QStringLiteral("white"))
                           : QColor(QString::fromLatin1(kColorAccent)));
            const QPointF c(body.center().x(), body.bottom() - si(5));
            p.drawEllipse(c, si(2.5), si(2.5));
        }
    }
}

void MonthCalendarWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 42; ++i) {
        if (dayRect(i).contains(event->pos())) {
            const QDate date = m_month.addDays(i - (m_month.dayOfWeek() - 1));
            m_selected = date;
            m_hasSel = true;
            update();
            emit dateSelected(date);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

// ==================================================================== //
// 日历页
// ==================================================================== //
FocusCalendarPage::FocusCalendarPage(FocusSource *focus, TodoSource *todo, QWidget *parent)
    : QWidget(parent), m_focus(focus), m_todo(todo)
{
    const QDate today = QDate::currentDate();
    m_month = QDate(today.year(), today.month(), 1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("日历"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    head->addWidget(title);
    m_title = new QLabel;
    m_title->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                               .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    head->addWidget(m_title);
    head->addStretch(1);
    auto *prev = new QToolButton;
    prev->setText(QStringLiteral("‹"));
    auto *next = new QToolButton;
    next->setText(QStringLiteral("›"));
    auto *todayBtn = new QPushButton(QStringLiteral("今天"));
    head->addWidget(prev);
    head->addWidget(next);
    head->addWidget(todayBtn);
    root->addLayout(head);

    m_cal = new MonthCalendarWidget;
    root->addWidget(m_cal);

    m_dayTitle = new QLabel;
    m_dayTitle->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                                  .arg(sp(14), QString::fromLatin1(kColorFg)));
    root->addWidget(m_dayTitle);

    auto *lists = new QHBoxLayout;
    lists->setSpacing(si(12));
    auto mkList = [lists](const QString &) {
        auto *w = new QListWidget;
        w->setSelectionMode(QAbstractItemView::NoSelection);
        lists->addWidget(w, 1);
        return w;
    };
    m_taskList = mkList(QStringLiteral("该日暂无任务"));
    m_focusList = mkList(QStringLiteral("该日暂无专注记录"));
    root->addLayout(lists, 1);

    connect(prev, &QToolButton::clicked, this, [this] { shiftMonth(-1); });
    connect(next, &QToolButton::clicked, this, [this] { shiftMonth(1); });
    connect(todayBtn, &QPushButton::clicked, this, [this] {
        m_month = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1);
        refresh();
    });
    connect(m_cal, &MonthCalendarWidget::dateSelected, this, &FocusCalendarPage::onDateSelected);
    connect(m_focus, &FocusSource::dataChanged, this, &FocusCalendarPage::refresh);
    connect(m_todo, &TodoSource::dataChanged, this, &FocusCalendarPage::refresh);
    refresh();
}

void FocusCalendarPage::applyUiScale()
{
    m_cal->update();
    setStyleSheet(QString());
}

void FocusCalendarPage::shiftMonth(int n)
{
    m_month = m_month.addMonths(n);
    refresh();
}

void FocusCalendarPage::updateTitle()
{
    m_title->setText(QStringLiteral("%1 年 %2 月").arg(m_month.year()).arg(m_month.month()));
}

void FocusCalendarPage::refresh()
{
    updateTitle();
    if (!m_focus)
        return;
    m_cal->setSessions(m_focus->sessions(), m_month);
    if (m_cal->selectedDate().isValid())
        updateDayDetail(m_cal->selectedDate());
    else
        onDateSelected(QDate::currentDate());
}

void FocusCalendarPage::onDateSelected(const QDate &date)
{
    updateDayDetail(date);
}

void FocusCalendarPage::updateDayDetail(const QDate &date)
{
    m_dayTitle->setText(QStringLiteral("%1 的任务与专注")
                            .arg(date.toString(QStringLiteral("yyyy 年 M 月 d 日（ddd）"))));

    m_taskList->clear();
    if (m_todo) {
        const QString iso = date.toString(Qt::ISODate);
        for (const auto &t : m_todo->tasks()) {
            if (t.dueDate == iso) {
                auto *item = new QListWidgetItem;
                item->setText(QStringLiteral("%1%2").arg(t.completed ? QStringLiteral("✓ ") : QString(),
                                                         t.title));
                item->setForeground(t.completed ? QColor(QString::fromLatin1(kColorFgMuted))
                                                : QColor(QString::fromLatin1(kColorFg)));
                m_taskList->addItem(item);
            }
        }
    }

    m_focusList->clear();
    if (m_focus) {
        for (const auto &s : m_focus->sessions()) {
            if (sessionDate(s) != date)
                continue;
            const QString kindName = s.kind == FocusStopwatch
                                         ? QStringLiteral("正计时") : QStringLiteral("番茄");
            auto *item = new QListWidgetItem;
            item->setText(QStringLiteral("%1  %2 - %3 · %4 · %5")
                              .arg(kindName, fmtClock(s.startMs), fmtClock(s.endMs),
                                   fmtDuration(s.durationSec),
                                   s.eventName.isEmpty() ? QStringLiteral("（未命名）") : s.eventName));
            m_focusList->addItem(item);
        }
    }
}

} // namespace awqtui
