// activitypage.h —— ActivityWatch 风格 Activity 统计面板
#pragma once

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QWidget>
#include "awdatastore.h"
#include "mockdata.h"

class QLabel;
class QMenu;
class QPushButton;
class QTabWidget;
class QNetworkReply;
class QFrame;

// Qt Designer 布局（activitypage.ui），全局命名空间
namespace Ui { class ActivityPage; }

namespace awqtui {

class ApiClient;
class HourlyActivityBars;
class HorizontalBarChart;
class CategoryBars;
class DonutChart;

class ActivityPage : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityPage(ApiClient *api, QWidget *parent = nullptr);
    ~ActivityPage() override;

    void setDate(const QDate &date);
    QDate date() const { return m_dateStart; }
    void refresh();
    // B 方案：切走隐藏页时释放驻留数据（事件缓存/时间线/数据集），切回时由 refresh() 重拉
    void releaseWeight();
    // 按当前主题重建页面内联样式（主题切换时调用）
    void applyTheme();
    // 按全局 gUiScale 重应用主题样式（Ctrl± 缩放时调用）
    void applyUiScale();

private slots:
    void onPrevDay();
    void onNextDay();
    void onToday();
    void onRangeAction();
    void onBucketsLoaded();
    void onEventLoaded();

private:
    // Qt Designer 生成的布局对象（activitypage.ui -> ui_activitypage.h）
    Ui::ActivityPage *ui = nullptr;
    void buildUi();
    void reloadData();
    void fetchAllEvents();
    // 图表渲染内容签名：一致说明图表不会变，跳过重建（同步轮询会周期性触发 refresh）
    QString renderSignature() const;
    void updateUiFromLanes();
    void showEmptyState(const QString &msg);
    QStringList computeHourlyCategories() const;
    QList<BarItem> mockEditorFiles(int limit) const;

    void updateTrendsFromLanes();

    ApiClient *m_api = nullptr;
    QDate m_dateStart;
    QDate m_dateEnd;
    QString m_rangeLabel;
    QList<TimelineLane> m_lanes;
    QList<BucketInfo> m_buckets;
    QHash<QString, QJsonArray> m_eventsMap;
    int m_pendingEvents = 0;
    // 每次 fetchAllEvents/releaseWeight 递增：在途 reply 落地时比对代次，切页期间作废的请求不污染新加载
    int m_fetchGen = 0;
    bool m_loading = false;
    // 上次实际渲染的内容签名：相同则跳过图表重建，避免无变化的刷新让界面闪动
    QString m_renderSig;

    QLabel *m_dateLabel = nullptr;
    QLabel *m_hostLabel = nullptr;
    QLabel *m_activeLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_todayBtn = nullptr;
    QPushButton *m_rangeBtn = nullptr;
    QMenu *m_rangeMenu = nullptr;
    QList<QAction *> m_rangeActions;
    void setRangeChecked(int index);
    void uncheckAllChips();
    HourlyActivityBars *m_hourlyBars = nullptr;
    QTabWidget *m_tabs = nullptr;

    HorizontalBarChart *m_topApps = nullptr;
    HorizontalBarChart *m_topTitles = nullptr;
    HorizontalBarChart *m_topCats = nullptr;
    CategoryBars *m_catBars = nullptr;
    HorizontalBarChart *m_catTree = nullptr;
    DonutChart *m_donut = nullptr;

    HorizontalBarChart *m_winApps = nullptr;
    HorizontalBarChart *m_winTitles = nullptr;

    HorizontalBarChart *m_topDomains = nullptr;
    HorizontalBarChart *m_topUrls = nullptr;

    HorizontalBarChart *m_editorFiles;

    // 趋势 Tab（多日聚合）
    HorizontalBarChart *m_trendApps = nullptr;
    HorizontalBarChart *m_trendCats = nullptr;
    HorizontalBarChart *m_trendDaily = nullptr;
    QFrame *m_trendPlaceholder = nullptr;
};

} // namespace awqtui
