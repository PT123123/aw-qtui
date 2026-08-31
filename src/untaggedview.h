// untaggedview.h —— 未标记时间热力图（月历：行=月，列=日）
#pragma once

#include <QDate>
#include <QList>
#include <QWidget>

#include "tagstore.h"

namespace awqtui {

class UntaggedView : public QWidget
{
    Q_OBJECT
public:
    explicit UntaggedView(TagStore *store, QWidget *parent = nullptr);

    void setRange(const QDate &from, const QDate &to);
    QDate from() const { return m_from; }
    QDate to() const { return m_to; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void dayClicked(const QDate &date);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct DayStat {
        QDate date;
        qint64 activeSec = 0; // 当天活跃总秒
        qint64 taggedSec = 0; // 当天已标签总秒
    };
    void rebuild();
    void paintMonth(QPainter &p, const QDate &monthStart, const QList<DayStat> &stats,
                    int baseY, const QRect &monthRect);

    TagStore *m_store = nullptr;
    QDate m_from;
    QDate m_to;
    QList<DayStat> m_stats;
    int m_cell = 26;
    int m_gap = 6;
    int m_labelW = 90;
    int m_monthRows = 0;
    QList<QRect> m_cellRects; // 与 m_stats 对应
};

} // namespace awqtui
