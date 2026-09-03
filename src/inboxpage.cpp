// inboxpage.cpp
#include "inboxpage.h"

#include "apiclient.h"
#include "theme.h"
#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace awqtui {

namespace {
// 连接层失败（服务端不可达）才把应用判为离线；HTTP 4xx（如评论端点不存在）
// 不应误判离线——保留本地待同步即可
bool isConnectionError(QNetworkReply *r)
{
    switch (r->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::SslHandshakeFailedError:
        return true;
    default:
        return false;
    }
}
} // namespace

InboxPage::InboxPage(ApiClient *api, QWidget *parent) : QWidget(parent), m_api(api)
{
    // 先加载本地缓存：即使服务端没起，历史数据也在
    m_store.load();

    m_reconnect = new QTimer(this);
    m_reconnect->setInterval(10000);
    connect(m_reconnect, &QTimer::timeout, this, &InboxPage::tryReconnect);

    buildUi();
    QTimer::singleShot(0, this, [this] { refreshAll(); });
}

QString InboxPage::searchTerm() const
{
    return m_search->text();
}

void InboxPage::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 标签侧栏 ----
    m_tagPanel = new QWidget;
    m_tagPanel->setObjectName(QStringLiteral("TagPanel"));
    m_tagPanel->setStyleSheet(scaleQss(QStringLiteral(
        "QWidget#TagPanel { background: %1; border-right: 1px solid %2; }")
                                          .arg(glassBg(kColorBgElev), glassBorder())));
    m_tagPanel->setFixedWidth(si(m_sidebarWidth));
    auto *tagLay = new QVBoxLayout(m_tagPanel);
    tagLay->setContentsMargins(si(10), si(12), si(10), si(12));
    tagLay->setSpacing(si(8));
    m_tagTitle = new QLabel(QStringLiteral("标签"));
    m_tagTitle->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 11px; font-weight: 700;"
                                                       "padding: 0 2px 2px;")
                                           .arg(kColorFgMuted)));
    tagLay->addWidget(m_tagTitle);
    m_tagList = new QListWidget;
    m_tagList->setStyleSheet(scaleQss(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { padding: 6px 8px; border: none; border-radius: 6px; color: %1; }"
        "QListWidget::item:hover { background: %2; color: %3; }")
                                         .arg(kColorFg, kColorBgElev2, kColorFg)));
    connect(m_tagList, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { onTagToggled(); });
    tagLay->addWidget(m_tagList, 1);
    auto *btnClear = new QPushButton(QStringLiteral("清除过滤"));
    m_btnClear = btnClear;
    btnClear->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { background: transparent; border: none; color: %1; font-size: 11px;"
        " text-align: left; padding: 2px; }"
        "QPushButton:hover { color: %2; }")
                                        .arg(kColorFgMuted, kColorAccent)));
    connect(btnClear, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_tagList->count(); ++i)
            m_tagList->item(i)->setCheckState(Qt::Unchecked);
        m_selectedTags.clear();
        loadNotes(true);
    });
    tagLay->addWidget(btnClear);

    // ---- 设置入口（放在最左栏底部，不在收件箱主体工具栏） ----
    auto *btnSettings = new QPushButton(QStringLiteral("⚙  设置"));
    m_btnSettings = btnSettings;
    btnSettings->setToolTip(QStringLiteral("设置（全局快捷键）"));
    btnSettings->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { background: transparent; border: none; color: %1; font-size: 11px;"
        " text-align: left; padding: 2px; }"
        "QPushButton:hover { color: %2; }")
                                           .arg(kColorFgMuted, kColorAccent)));
    connect(btnSettings, &QPushButton::clicked, this, &InboxPage::settingsRequested);
    tagLay->addWidget(btnSettings);

    // ---- 主区 ----
    auto *main = new QWidget;
    auto *mainLay = new QVBoxLayout(main);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // 工具栏：大标题 + 搜索（MoeMemos 风格），右侧为次要控件
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(si(20), si(16), si(16), si(12));
    toolbar->setSpacing(si(10));

    m_title = new QLabel(QStringLiteral("收件箱"));
    m_title->setStyleSheet(scaleQss(QStringLiteral("font-size: 22px; font-weight: 700; color: %1;").arg(kColorFg)));
    toolbar->addWidget(m_title);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("搜索…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(si(240));
    connect(m_search, &QLineEdit::textChanged, this, &InboxPage::onSearchChanged);
    toolbar->addWidget(m_search);

    toolbar->addStretch(1);

    const QString subtleBtn = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 6px;"
        " color: %1; padding: 5px 10px; font-size: 12px; }"
        "QPushButton:hover { background: %2; color: %3; }");

    m_btnSidebar = new QPushButton(QStringLiteral("☰"));
    m_btnSidebar->setToolTip(QStringLiteral("显示 / 隐藏标签侧栏"));
    m_btnSidebar->setFixedSize(si(30), si(30));
    m_btnSidebar->setStyleSheet(scaleQss(subtleBtn.arg(kColorFgMuted, kColorBgElev2, kColorFg)));
    connect(m_btnSidebar, &QPushButton::clicked, this, [this] {
        m_sidebarVisible = !m_sidebarVisible;
        m_tagPanel->setVisible(m_sidebarVisible);
    });
    toolbar->addWidget(m_btnSidebar);

    m_sort = new QComboBox;
    m_sort->addItem(QStringLiteral("最新创建"), QStringLiteral("created"));
    m_sort->addItem(QStringLiteral("最新更新"), QStringLiteral("updated"));
    m_sort->addItem(QStringLiteral("按内容"), QStringLiteral("content"));
    m_sort->setFixedWidth(si(108));
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this](int) { loadNotes(true); });
    toolbar->addWidget(m_sort);

    m_btnCopy = new QPushButton(QStringLiteral("复制全部"));
    m_btnCopy->setStyleSheet(scaleQss(subtleBtn.arg(kColorFgMuted, kColorBgElev2, kColorFg)));
    connect(m_btnCopy, &QPushButton::clicked, this, &InboxPage::onCopyAll);
    toolbar->addWidget(m_btnCopy);

    m_btnRefresh = new QPushButton(QStringLiteral("⟳"));
    m_btnRefresh->setToolTip(QStringLiteral("刷新 (F5)"));
    m_btnRefresh->setFixedSize(si(30), si(30));
    m_btnRefresh->setStyleSheet(scaleQss(subtleBtn.arg(kColorFgMuted, kColorBgElev2, kColorFg)));
    connect(m_btnRefresh, &QPushButton::clicked, this, &InboxPage::onRefresh);
    toolbar->addWidget(m_btnRefresh);

    m_badge = new StatusBadge;
    toolbar->addWidget(m_badge);
    mainLay->addLayout(toolbar);

    // 列表 + 悬浮新建
    auto *stackHost = new QWidget;
    m_stack = new QStackedLayout(stackHost);
    m_list = new QListWidget;
    m_list->setStyleSheet(scaleQss(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { background: transparent; border: none; padding: 0; margin: 0; }")));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, &InboxPage::onScroll);
    m_stack->addWidget(m_list);

    auto *empty = new QWidget;
    auto *emptyLay = new QVBoxLayout(empty);
    emptyLay->setAlignment(Qt::AlignCenter);
    emptyLay->setSpacing(si(8));
    m_emptyIcon = new QLabel(QStringLiteral("📝"));
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    m_emptyIcon->setStyleSheet(scaleQss(QStringLiteral("font-size: 42px;")));
    emptyLay->addWidget(m_emptyIcon);
    m_emptyText = new QLabel(QStringLiteral("还没有笔记"));
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 15px;").arg(kColorFgMuted)));
    emptyLay->addWidget(m_emptyText);
    m_emptyHint = new QLabel(QStringLiteral("点击右下角 ＋ 新建一条"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted)));
    emptyLay->addWidget(m_emptyHint);
    m_stack->addWidget(empty);
    m_stack->setCurrentWidget(m_list);

    mainLay->addWidget(stackHost, 1);

    // 悬浮 +
    auto *host = new QWidget;
    auto *hostLay = new QVBoxLayout(host);
    hostLay->setContentsMargins(0, 0, 0, 0);
    hostLay->setSpacing(0);
    hostLay->addWidget(main, 1);

    m_fab = new QPushButton(QStringLiteral("＋"));
    m_fab->setObjectName(QStringLiteral("Fab"));
    m_fab->setFixedSize(si(56), si(56));
    m_fab->setCursor(Qt::PointingHandCursor);
    m_fab->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton#Fab { background: %1; color: white; border: none; border-radius: 28px;"
        " font-size: 28px; font-weight: 400; }"
        "QPushButton#Fab:hover { background: %2; }")
                                     .arg(kColorAccent, kColorAccentHover)));
    // 悬浮 + 投影（受全局阴影强度控制）
    m_fabShadow = makeDropShadow(m_fab);
    connect(m_fab, &QPushButton::clicked, this, &InboxPage::onNewNote);
    auto *fabRow = new QWidget;
    auto *fabLay = new QHBoxLayout(fabRow);
    fabLay->setContentsMargins(0, 0, si(20), si(20));
    fabLay->addStretch(1);
    fabLay->addWidget(m_fab, 0, Qt::AlignBottom | Qt::AlignRight);
    hostLay->addWidget(fabRow);

    // 组装
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(m_tagPanel);
    body->addWidget(host, 1);
    root->addLayout(body);
}

void InboxPage::applyUiScale()
{
    // 标签侧栏
    if (m_tagPanel) {
        m_tagPanel->setStyleSheet(scaleQss(QStringLiteral(
            "QWidget#TagPanel { background: %1; border-right: 1px solid %2; }")
                                               .arg(glassBg(kColorBgElev), glassBorder())));
        m_tagPanel->setFixedWidth(si(m_sidebarWidth));
    }
    if (m_tagTitle)
        m_tagTitle->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 11px; font-weight: 700; padding: 0 2px 2px;")
                                               .arg(kColorFgMuted)));
    if (m_tagList)
        m_tagList->setStyleSheet(scaleQss(QStringLiteral(
            "QListWidget { background: transparent; border: none; outline: none; }"
            "QListWidget::item { padding: 6px 8px; border: none; border-radius: 6px; color: %1; }"
            "QListWidget::item:hover { background: %2; color: %3; }")
                                             .arg(kColorFg, kColorBgElev2, kColorFg)));
    if (m_btnClear)
        m_btnClear->setStyleSheet(scaleQss(QStringLiteral(
            "QPushButton { background: transparent; border: none; color: %1; font-size: 11px;"
            " text-align: left; padding: 2px; }"
            "QPushButton:hover { color: %2; }")
                                               .arg(kColorFgMuted, kColorAccent)));
    if (m_btnSettings)
        m_btnSettings->setStyleSheet(scaleQss(QStringLiteral(
            "QPushButton { background: transparent; border: none; color: %1; font-size: 11px;"
            " text-align: left; padding: 2px; }"
            "QPushButton:hover { color: %2; }")
                                               .arg(kColorFgMuted, kColorAccent)));

    // 工具栏
    if (m_title)
        m_title->setStyleSheet(scaleQss(QStringLiteral("font-size: 22px; font-weight: 700; color: %1;").arg(kColorFg)));
    if (m_search)
        m_search->setFixedWidth(si(240));
    if (m_sort)
        m_sort->setFixedWidth(si(108));
    const QString subtleBtn = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 6px;"
        " color: %1; padding: 5px 10px; font-size: 12px; }"
        "QPushButton:hover { background: %2; color: %3; }");
    const QString subtleStyle = scaleQss(subtleBtn.arg(kColorFgMuted, kColorBgElev2, kColorFg));
    for (QPushButton *b : {m_btnSidebar, m_btnRefresh, m_btnCopy}) {
        if (b)
            b->setStyleSheet(subtleStyle);
    }
    for (QPushButton *b : {m_btnSidebar, m_btnRefresh})
        if (b)
            b->setFixedSize(si(30), si(30));

    // 空状态
    if (m_emptyIcon)
        m_emptyIcon->setStyleSheet(scaleQss(QStringLiteral("font-size: 42px;")));
    if (m_emptyText)
        m_emptyText->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 15px;").arg(kColorFgMuted)));
    if (m_emptyHint)
        m_emptyHint->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted)));

    // 悬浮 +
    if (m_fab) {
        m_fab->setFixedSize(si(56), si(56));
        m_fab->setStyleSheet(scaleQss(QStringLiteral(
            "QPushButton#Fab { background: %1; color: white; border: none; border-radius: 28px;"
            " font-size: 28px; font-weight: 400; }"
            "QPushButton#Fab:hover { background: %2; }")
                                         .arg(kColorAccent, kColorAccentHover)));
        // 阴影随强度增删（受全局阴影强度控制）
        clearDropShadow(m_fab, m_fabShadow);
        m_fabShadow = makeDropShadow(m_fab);
    }

    if (m_badge)
        m_badge->applyUiScale();

    // 重渲染列表：卡片（NoteCard）在创建时按当前缩放比取样式
    applyClientFilter();
}

// ------------------------------------------------------------------ //

void InboxPage::refreshAll()
{
    // 刷新/初次加载：给本次重建的卡片加入场淡入（过滤、翻页不触发）
    m_animateCards = true;
    if (isOffline()) {
        // 服务端不可用：直接渲染本地缓存，并尝试重连
        rebuildTagsFromLocal();
        renderLocal();
        startReconnect();
        return;
    }
    loadTags();
    loadNotes(true);
}

// ------------------------------------------------------------------ //
// 离线优先：本地存储
// ------------------------------------------------------------------ //

void InboxPage::renderLocal()
{
    ++m_reqGen; // 使在途的服务端请求回包过期，避免它们覆盖本地渲染
    m_notes.clear();
    const QList<Note> all = m_store.notes();
    const QString search = m_search->text().trimmed().toLower();
    const QString sortBy = m_sort ? m_sort->currentData().toString() : QStringLiteral("created");

    // 客户端过滤：搜索 + 标签（单/多标签统一 OR）
    QList<Note> visible;
    for (const Note &n : all) {
        if (!search.isEmpty() && !n.content.toLower().contains(search))
            continue;
        if (!m_selectedTags.isEmpty()) {
            bool any = false;
            for (const QString &t : m_selectedTags) {
                if (n.tags.contains(t)) {
                    any = true;
                    break;
                }
            }
            if (!any)
                continue;
        }
        visible << n;
    }

    // 注入本地置顶标记（置顶优先排序在 applyClientFilter 里做）
    for (Note &n : visible)
        n.pinned = m_store.isPinned(n.id);

    // 客户端排序（与服务端语义一致：新在前；content 按字母升序）
    std::sort(visible.begin(), visible.end(), [sortBy](const Note &a, const Note &b) {
        if (sortBy == QLatin1String("content"))
            return a.content.toLower() < b.content.toLower();
        const QString ka = (sortBy == QLatin1String("updated")) ? a.updatedAt : a.createdAt;
        const QString kb = (sortBy == QLatin1String("updated")) ? b.updatedAt : b.createdAt;
        if (ka != kb)
            return ka > kb;
        return a.id > b.id;
    });

    m_notes = visible;
    m_hasMore = false;
    m_loading = false;
    applyClientFilter();
    emit noteCountChanged(m_notes.size());
    updateOfflineBadge();
}

void InboxPage::rebuildTagsFromLocal()
{
    QMap<QString, int> counts;
    QMap<QString, QString> last;
    for (const Note &n : m_store.notes()) {
        for (const QString &t : n.tags) {
            counts[t] += 1;
            if (n.updatedAt > last.value(t))
                last[t] = n.updatedAt;
        }
    }
    m_tags.clear();
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        m_tags << DetailedTag{it.key(), it.value(), last.value(it.key())};
    rebuildTagSidebar();
}

void InboxPage::createLocal(const QString &content, const QStringList &tags)
{
    m_store.insertLocal(content, tags, m_api->deviceId());
    m_store.save();
    rebuildTagsFromLocal();
    renderLocal();
}

void InboxPage::updateLocal(qint64 id, const QString &content, const QStringList &tags)
{
    m_store.updateLocal(id, content, tags);
    m_store.save();
    rebuildTagsFromLocal();
    renderLocal();
}

void InboxPage::deleteLocal(qint64 id)
{
    m_store.markDeleted(id);
    m_store.save();
    rebuildTagsFromLocal();
    renderLocal();
}

void InboxPage::pushDirty()
{
    const QList<Note> dirty = m_store.dirtyNotes();
    if (dirty.isEmpty()) {
        // 没有笔记改动时，若还有待同步评论则直接补推评论
        if (m_store.pendingCommentCount() > 0)
            pushComments();
        return;
    }
    m_pendingPush = dirty.size();
    setStatus(StatusBadge::State::Syncing,
              QStringLiteral("补推 %1 条离线改动…").arg(m_pendingPush));

    for (const Note &n : dirty) {
        if (n.pendingOp == QLatin1String("create")) {
            QNetworkReply *r = m_api->createNote(n.content, n.tags);
            connect(r, &QNetworkReply::finished, this, [this, r, localId = n.id] {
                QJsonDocument doc;
                QString err;
                if (!ApiClient::parseReply(r, &doc, &err)) {
                    pushFailed();
                    return;
                }
                const qint64 serverId = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
                m_store.remapId(localId, serverId);
                m_store.clearPending(serverId);
                m_store.save();
                pushDone();
            });
        } else if (n.pendingOp == QLatin1String("update")) {
            QNetworkReply *r = m_api->updateNote(n.id, n.content, n.tags);
            connect(r, &QNetworkReply::finished, this, [this, r, id = n.id] {
                QJsonDocument doc;
                QString err;
                if (!ApiClient::parseReply(r, &doc, &err)) {
                    pushFailed();
                    return;
                }
                m_store.clearPending(id);
                m_store.save();
                pushDone();
            });
        } else if (n.pendingOp == QLatin1String("delete")) {
            QNetworkReply *r = m_api->deleteNote(n.id);
            connect(r, &QNetworkReply::finished, this, [this, r, id = n.id] {
                QJsonDocument doc;
                QString err;
                if (!ApiClient::parseReply(r, &doc, &err)) {
                    pushFailed();
                    return;
                }
                m_store.clearPending(id);
                m_store.save();
                pushDone();
            });
        }
    }
}

void InboxPage::pushDone()
{
    if (--m_pendingPush <= 0) {
        m_pendingPush = 0;
        // 笔记已推完，若还有待同步评论则继续补推，全部完成后再回在线态重拉
        if (m_store.pendingCommentCount() > 0) {
            pushComments();
        } else {
            refreshAll();
        }
    }
}

void InboxPage::pushFailed()
{
    fprintf(stderr, "[awqtui-push] pushFailed -> offline, keep local\n");
    // 推送失败：保留脏数据，回到离线态，等下次重连再补推
    m_pendingPush = 0;
    m_online = false;
    setStatus(StatusBadge::State::Disconnected, QStringLiteral("离线改动推送失败，已保留本地"));
    renderLocal();
    startReconnect();
}

void InboxPage::tryReconnect()
{
    if (m_online || m_loading)
        return;
    setStatus(StatusBadge::State::Syncing, QStringLiteral("正在重新连接…"));
    QNetworkReply *r = m_api->getNotes(1, 0, QString(), QString(), QString());
    // 本机“连接被拒”回报慢，2.5s 没回就放弃本次探测（abort 会触发 finished）
    // 注意：探测请求可能在 2.5s 内已完成（成功或连接被拒）并被 parseReply 里 deleteLater
    // 销毁，定时器回调若再访问裸指针 r 就是 use-after-free。必须用 QPointer 防悬垂。
    QPointer<QNetworkReply> guard(r);
    QTimer::singleShot(2500, this, [guard] {
        if (guard && !guard->isFinished())
            guard->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            // 仍未恢复，保持离线；定时器继续跑
            updateOfflineBadge();
            return;
        }
        m_online = true;
        m_reconnect->stop();
        if (m_store.pendingCount() > 0)
            pushDirty();
        else
            refreshAll();
    });
}

void InboxPage::startReconnect()
{
    if (!m_online)
        m_reconnect->start();
}

void InboxPage::updateOfflineBadge()
{
    const int p = m_store.pendingCount();
    if (m_online) {
        // 在线但仍有推不动的改动（如服务端无评论端点）：保持已连接，仅提示待同步
        setStatus(p > 0 ? StatusBadge::State::Connected : StatusBadge::State::Connected,
                  p > 0 ? QStringLiteral("待同步 %1 条").arg(p) : QString());
        return;
    }
    if (p > 0)
        setStatus(StatusBadge::State::Disconnected,
                  QStringLiteral("已离线 · 本地待同步 %1 条").arg(p));
    else
        setStatus(StatusBadge::State::Disconnected, QStringLiteral("已离线 · 本地已存"));
}

void InboxPage::loadDetailedTags()
{
    QNetworkReply *r = m_api->getDetailedTags();
    if (!r)
        return;
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            setStatus(StatusBadge::State::Error, err);
            return;
        }
        m_tags.clear();
        for (const auto &v : doc.array()) {
            if (v.isObject())
                m_tags << DetailedTag::fromJson(v.toObject());
        }
        rebuildTagSidebar();
    });
}

void InboxPage::loadTags()
{
    QNetworkReply *r = m_api->getTags();
    if (!r)
        return;
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err))
            return;
        // tags 端点可能只返回名字数组；detailed 返回 count。这里以 detailed 为主
        Q_UNUSED(doc);
        loadDetailedTags();
    });
}

void InboxPage::rebuildTagSidebar()
{
    QSignalBlocker blocker(m_tagList);
    m_tagList->clear();
    for (const DetailedTag &t : m_tags) {
        auto *item = new QListWidgetItem(QStringLiteral("#%1 (%2)").arg(t.name).arg(t.count));
        item->setData(Qt::UserRole, t.name);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(m_selectedTags.contains(t.name) ? Qt::Checked : Qt::Unchecked);
        m_tagList->addItem(item);
    }
}

void InboxPage::onTagToggled()
{
    m_selectedTags.clear();
    for (int i = 0; i < m_tagList->count(); ++i) {
        auto *item = m_tagList->item(i);
        if (item->checkState() == Qt::Checked)
            m_selectedTags << item->data(Qt::UserRole).toString();
    }
    loadNotes(true);
}

void InboxPage::loadNotes(bool reset)
{
    if (m_loading)
        return;
    if (isOffline()) {
        renderLocal();
        return;
    }
    if (reset) {
        ++m_reqGen;
        m_offset = 0;
        m_hasMore = true;
        m_list->clear();
        m_notes.clear();
    }
    if (!m_hasMore)
        return;
    m_loading = true;
    setStatus(StatusBadge::State::Syncing, QStringLiteral("加载中…"));

    // 单标签：让服务端过滤；多标签：不传 tag，客户端 OR 过滤
    QString serverTag;
    if (m_selectedTags.size() == 1)
        serverTag = m_selectedTags.first();
    const QString sortBy = m_sort->currentData().toString();
    QNetworkReply *r = m_api->getNotes(m_limit, m_offset, serverTag, m_search->text(), sortBy);
    const int gen = m_reqGen;

    // 兜底：本机“连接被拒”可能要数秒才回报，超过阈值直接判离线，避免界面长时间卡在“加载中”
    QTimer::singleShot(3000, this, [this, gen] {
        if (gen != m_reqGen)
            return; // 已被新请求或本地渲染取代
        if (!m_loading)
            return; // 请求已正常完成
        m_loading = false;
        m_online = false;
        ++m_reqGen; // 使在途回包过期
        setStatus(StatusBadge::State::Disconnected, QStringLiteral("服务端无响应，改用本地缓存"));
        rebuildTagsFromLocal();
        renderLocal();
        startReconnect();
    });

    connect(r, &QNetworkReply::finished, this, [this, r, gen] {
        if (gen != m_reqGen) {
            r->deleteLater();
            return; // 过期回包：期间已切离线/已重新加载
        }
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_loading = false;
            m_online = false;
            setStatus(StatusBadge::State::Disconnected,
                      QStringLiteral("服务端不可用，改用本地缓存"));
            rebuildTagsFromLocal();
            renderLocal();
            startReconnect();
            return;
        }
        m_loading = false;
        m_online = true;
        QList<Note> batch;
        for (const auto &v : doc.array()) {
            if (v.isObject())
                batch << Note::fromJson(v.toObject());
        }
        // 拉到的服务端数据并入本地镜像（本地脏改动不会被覆盖）
        m_store.applyServerNotes(batch);
        m_store.save();
        m_hasMore = (batch.size() >= m_limit);
        appendNotes(batch, false);
        setStatus(StatusBadge::State::Connected);
        emit noteCountChanged(m_notes.size());

        // 若本地还有积压的离线改动且没有正在补推，则推送到服务端
        if (m_pendingPush == 0 && m_store.pendingCount() > 0)
            pushDirty();
    });
}

void InboxPage::appendNotes(const QList<Note> &notes, bool clear)
{
    if (clear)
        m_list->clear();
    for (Note n : notes) {
        n.pinned = m_store.isPinned(n.id);
        // 服务端笔记 JSON 不携带 comment_parent_id（applyServerNotes 仅在本地镜像保留）：
        // 从本地镜像补回，否则评论笔记在收件箱里不会显示「被评论笔记」的引用预览
        if (const Note *local = m_store.find(n.id))
            n.commentParentId = local->commentParentId;
        m_notes << n;
    }
    // 多标签客户端 OR 过滤后重新渲染
    applyClientFilter();
}

void InboxPage::applyClientFilter()
{
    // 防重入：循环内 addItem 会触发 verticalScrollBar::valueChanged → onScroll →
    // loadNotes → 离线时 renderLocal → 本函数重入，内层 m_list->clear() 会删除外层
    // 刚 addItem 的 item，外层继续 setItemWidget 即 use-after-free 崩溃（三个 dump 证实）。
    if (m_rebuilding)
        return;
    m_rebuilding = true;
    QList<Note> visible = m_notes;
    if (m_selectedTags.size() > 1) {
        visible.clear();
        for (const Note &n : m_notes) {
            bool any = false;
            for (const QString &t : m_selectedTags) {
                if (n.tags.contains(t)) {
                    any = true;
                    break;
                }
            }
            if (any)
                visible << n;
        }
    }
    // 置顶优先（稳定分区：置顶笔记排在最前，其余保持原顺序）
    QList<Note> ordered;
    for (const Note &n : visible)
        if (n.pinned)
            ordered << n;
    for (const Note &n : visible)
        if (!n.pinned)
            ordered << n;
    visible = ordered;

    m_visibleIds.clear();
    m_list->clear();
    for (const Note &n : visible) {
        m_visibleIds << n.id;
        QWidget *card = makeCard(n);
        // Inset 分组：卡片左右缩进、上下留缝，模拟 iOS InsetGroupedListStyle
        auto *item = new QListWidgetItem;
        auto *wrap = new QWidget;
        auto *wrapLay = new QVBoxLayout(wrap);
        wrapLay->setContentsMargins(si(20), si(5), si(20), si(6));
        wrapLay->setSpacing(0);
        wrapLay->addWidget(card);
        item->setSizeHint(QSize(0, wrap->sizeHint().height()));
        m_list->addItem(item);
        m_list->setItemWidget(item, wrap);
        // 入场淡入：仅刷新/初次加载时（阴影在卡片上、透明度在包裹层上，互不冲突）
        if (m_animateCards)
            fadeInWidget(wrap, 180);
    }
    m_animateCards = false;
    if (visible.isEmpty())
        m_stack->setCurrentIndex(1);
    else
        m_stack->setCurrentIndex(0);

    // 渲染完成后，若有待跳转目标（此前被搜索/标签过滤），滚动定位并高亮
    if (m_pendingJumpId != 0) {
        const qint64 target = m_pendingJumpId;
        m_pendingJumpId = 0;
        jumpToNote(target);
    }
    m_rebuilding = false;
}

QWidget *InboxPage::makeCard(const Note &n)
{
    auto *card = new NoteCard(n, n.pinned);
    connect(card, &NoteCard::editRequested, this, &InboxPage::onEditNote);
    connect(card, &NoteCard::deleteRequested, this, &InboxPage::onDeleteNote);
    connect(card, &NoteCard::commentRequested, this, &InboxPage::onComment);
    connect(card, &NoteCard::togglePinnedRequested, this, &InboxPage::onTogglePinned);
    connect(card, &NoteCard::historyRequested, this, &InboxPage::onNoteHistory);
    connect(card, &NoteCard::taskToggled, this, &InboxPage::onTaskToggled);
    connect(card, &NoteCard::parentReferenceClicked, this, &InboxPage::onParentReferenceClicked);
    // 评论笔记：在内容下方展示被评论笔记的引用预览
    if (n.commentParentId != 0) {
        const QString preview = parentPreview(n.commentParentId);
        if (!preview.isEmpty())
            card->setParentReference(n.commentParentId, preview);
    }
    return card;
}

QString InboxPage::parentPreview(qint64 parentId) const
{
    const Note *p = m_store.find(parentId);
    if (!p || p->deleted)
        return QString();
    QString s = p->content;
    // 轻量去除常见 markdown 标记，保留可读文本（# 标签保留）
    s.remove(QRegularExpression(QStringLiteral("[`*_~>]")));
    s.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
    // 折叠换行/空白为单个空格
    s = s.simplified();
    // 100 字截断
    if (s.size() > 100)
        s = s.left(100).trimmed() + QStringLiteral("…");
    return s;
}

void InboxPage::jumpToNote(qint64 id)
{
    const int idx = m_visibleIds.indexOf(id);
    if (idx < 0 || idx >= m_list->count())
        return;
    QListWidgetItem *item = m_list->item(idx);
    m_list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    QWidget *wrap = m_list->itemWidget(item);
    if (!wrap)
        return;
    if (auto *card = wrap->findChild<NoteCard *>())
        card->flashHighlight();
}

void InboxPage::onParentReferenceClicked(qint64 parentId)
{
    if (m_visibleIds.contains(parentId)) {
        jumpToNote(parentId);
        return;
    }
    // 目标不在当前可见列表（被搜索/标签过滤，或尚未加载）：本地也不存在则无法跳转
    if (!m_store.find(parentId))
        return;
    // 清除搜索与标签过滤并重载，重载完成后跳转
    m_pendingJumpId = parentId;
    {
        const QSignalBlocker bSearch(m_search);
        m_search->clear();
    }
    {
        const QSignalBlocker bTags(m_tagList);
        for (int i = 0; i < m_tagList->count(); ++i)
            m_tagList->item(i)->setCheckState(Qt::Unchecked);
    }
    m_selectedTags.clear();
    loadNotes(true);
}

void InboxPage::onScroll()
{
    auto *bar = m_list->verticalScrollBar();
    if (bar->value() >= bar->maximum() - 40)
        // 延迟到事件循环再加载：避免在 applyClientFilter 循环内（addItem 引发的
        // valueChanged 同步回调）同步重入 loadNotes → renderLocal → 重建列表，
        // 从而清除外层循环刚 addItem 的 item 造成 use-after-free。
        QTimer::singleShot(0, this, [this] { loadNotes(false); });
}

void InboxPage::onSearchChanged()
{
    QTimer::singleShot(200, this, [this] { loadNotes(true); });
}

void InboxPage::onSortChanged()
{
    loadNotes(true);
}

void InboxPage::onRefresh()
{
    refreshAll();
}

void InboxPage::onNewNote()
{
    QStringList existing;
    for (const DetailedTag &t : m_tags)
        existing << t.name;
    NoteEditorDialog dlg(QString(), existing, QStringLiteral("新建笔记"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString text = dlg.text();
    if (text.isEmpty())
        return;
    const QStringList tags = extractTags(text);

    if (isOffline()) {
        // 服务端不可用：直接写入本地，标记待同步
        createLocal(text, tags);
        return;
    }
    QNetworkReply *r = m_api->createNote(text, tags);
    connect(r, &QNetworkReply::finished, this, [this, r, text, tags] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            // 请求失败（服务端可能刚挂）：落本地并切离线
            m_online = false;
            startReconnect();
            createLocal(text, tags);
            return;
        }
        m_store.applyServerNotes({Note::fromJson(doc.object())});
        m_store.save();
        refreshAll();
    });
}

void InboxPage::openNewNote()
{
    onNewNote();
}

void InboxPage::onEditNote(qint64 id)
{
    Note note;
    for (const Note &n : m_notes) {
        if (n.id == id) {
            note = n;
            break;
        }
    }
    QStringList existing;
    for (const DetailedTag &t : m_tags)
        existing << t.name;
    NoteEditorDialog dlg(note.content, existing, QStringLiteral("编辑笔记"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    applyContent(id, dlg.text());
}

void InboxPage::onTogglePinned(qint64 id)
{
    m_store.setPinned(id, !m_store.isPinned(id));
    m_store.save();
    // 即时更新内存列表并重排（置顶是本地行为，无需网络往返）
    for (Note &n : m_notes) {
        if (n.id == id) {
            n.pinned = !n.pinned;
            break;
        }
    }
    applyClientFilter();
}

void InboxPage::onNoteHistory(qint64 id)
{
    // 历史版本由服务端维护：离线或本地尚未同步的新建（负 id）都没有可查的历史
    if (id < 0 || isOffline()) {
        QMessageBox::information(this, QStringLiteral("历史版本"),
                                 id < 0 ? QStringLiteral("本地新建的笔记尚未同步到服务端，暂无历史版本。")
                                        : QStringLiteral("当前离线，无法获取服务端的历史版本。"));
        return;
    }
    QNetworkReply *r = m_api->getNoteHistory(id);
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            QMessageBox::warning(this, QStringLiteral("历史版本"),
                                 QStringLiteral("获取历史版本失败：%1").arg(err));
            return;
        }
        QList<NoteHistory> items;
        const auto arr = doc.isArray() ? doc.array() : QJsonArray();
        for (const auto &v : arr)
            items << NoteHistory::fromJson(v.toObject());

        auto *dlg = new NoteHistoryDialog(id, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setHistory(items);
        // 恢复 = 把历史版本内容当作一次普通编辑提交（走 applyContent，离线也能兜底）
        connect(dlg, &NoteHistoryDialog::restoreRequested, this,
                [this](qint64 noteId, const QString &content) { applyContent(noteId, content); });
        dlg->show();
    });
}

void InboxPage::onTaskToggled(qint64 id, const QString &content)
{
    applyContent(id, content);
}

void InboxPage::applyContent(qint64 id, const QString &text)
{
    const QStringList tags = extractTags(text);

    if (isOffline() || id < 0) {
        // 离线，或编辑的是本地未同步的新建（负 id）：只改本地
        updateLocal(id, text, tags);
        return;
    }
    QNetworkReply *r = m_api->updateNote(id, text, tags);
    connect(r, &QNetworkReply::finished, this, [this, r, id, text, tags] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_online = false;
            startReconnect();
            updateLocal(id, text, tags);
            return;
        }
        refreshAll();
    });
}

void InboxPage::onDeleteNote(qint64 id)
{
    const auto ret = QMessageBox::question(this, QStringLiteral("删除笔记"),
                                           QStringLiteral("确定删除这条笔记？"));
    if (ret != QMessageBox::Yes)
        return;

    if (isOffline() || id < 0) {
        // 离线，或本地未同步的新建：本地删除（新建会直接消失，服务端笔记留 tombstone）
        deleteLocal(id);
        return;
    }
    QNetworkReply *r = m_api->deleteNote(id);
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_online = false;
            startReconnect();
            deleteLocal(id);
            return;
        }
        m_store.drop(id);
        m_store.save();
        refreshAll();
    });
}

void InboxPage::onComment(qint64 id)
{
    auto *dlg = new CommentsDialog(id, this);
    // 离线优先：先显示本地缓存的评论（服务端不可用也能看已加载的评论）
    dlg->setComments(m_store.commentsFor(id));
    if (!isOffline()) {
        // 在线时再异步拉取服务端评论，成功后合并刷新（失败静默，保留本地缓存，不弹连接错误）
        QNetworkReply *r = m_api->getComments(id);
        QPointer<CommentsDialog> guard(dlg);
        connect(r, &QNetworkReply::finished, this, [this, r, guard, id] {
            QJsonDocument doc;
            QString err;
            if (ApiClient::parseReply(r, &doc, &err)) {
                QList<Comment> comments;
                for (const auto &v : doc.array()) {
                    if (v.isObject())
                        comments << Comment::fromJson(v.toObject());
                }
                m_store.setComments(id, comments); // 覆盖服务端数据，保留本地待同步评论
                m_store.save();
                if (guard)
                    guard->setComments(m_store.commentsFor(id));
            }
        });
    }
    if (dlg->exec() != QDialog::Accepted) {
        dlg->deleteLater();
        return;
    }
    const QString text = dlg->commentText();
    dlg->deleteLater();
    if (text.isEmpty())
        return;
    submitComment(id, text);
}

void InboxPage::submitComment(qint64 noteId, const QString &text)
{
    // 离线优先：先落本地 —— 创建一条本地评论笔记（收件箱立即可见）+ 评论缓存 + 入待同步队列
    const QString ts = m_store.addLocalComment(noteId, text, m_api->deviceId());
    m_store.save();

    if (isOffline()) {
        // 服务端不可用：本地保存并立即渲染（评论笔记出现在收件箱），重连后自动补推
        renderLocal();
        setStatus(StatusBadge::State::Disconnected, QStringLiteral("评论已保存，离线待同步"));
        QTimer::singleShot(2000, this, [this] { updateOfflineBadge(); });
        return;
    }

    // 在线：直接推送；期间标记在途，避免 loadNotes 触发的 pushComments 重复补推同一条
    m_inflightComments.insert(ts);
    QNetworkReply *rr = m_api->addComment(noteId, text);
    connect(rr, &QNetworkReply::finished, this, [this, rr, noteId, text, ts] {
        m_inflightComments.remove(ts);
        QJsonDocument doc;
        QString err;
        const bool connErr = isConnectionError(rr);
        if (!ApiClient::parseReply(rr, &doc, &err)) {
            // 连接类失败（服务端刚挂）：切离线等待重连补推
            if (connErr) {
                m_online = false;
                startReconnect();
            }
            // 服务端未接受：评论笔记保留在本地（带待同步标记），渲染到收件箱
            renderLocal();
            setStatus(StatusBadge::State::Connected, QStringLiteral("评论已本地保存，待同步"));
            QTimer::singleShot(2000, this, [this] { updateOfflineBadge(); });
            return;
        }
        // 成功：本地评论笔记转正（重映射到服务端 id 并清 pending），再拉回全量刷新
        // 服务端把评论也建成一条笔记并返回，因此刷新后评论笔记会出现在收件箱
        const qint64 serverNoteId = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
        m_store.confirmComment(noteId, text, ts, serverNoteId);
        m_store.save();
        setStatus(StatusBadge::State::Connected, QStringLiteral("评论已发表"));
        QTimer::singleShot(2000, this, [this] { updateOfflineBadge(); });
        refreshAll();
    });
}

void InboxPage::pushComments()
{
    const QList<PendingComment> pending = m_store.pendingComments();
    if (pending.isEmpty())
        return;

    // 过滤掉正在由 submitComment 直接推送的同一条评论，避免重复 POST
    QList<PendingComment> toPush;
    for (const PendingComment &c : pending) {
        if (!m_inflightComments.contains(c.createdAt))
            toPush << c;
    }
    if (toPush.isEmpty()) {
        m_pendingPush = 0;
        return;
    }

    m_pendingPush = toPush.size();
    setStatus(StatusBadge::State::Syncing,
              QStringLiteral("补推 %1 条离线评论…").arg(m_pendingPush));

    for (const PendingComment &c : toPush) {
        QNetworkReply *r = m_api->addComment(c.noteId, c.content);
        connect(r, &QNetworkReply::finished, this, [this, r, c] {
            QJsonDocument doc;
            QString err;
            const bool connErr = isConnectionError(r);
            if (!ApiClient::parseReply(r, &doc, &err)) {
                // 连接类失败才整机切离线；4xx（无评论端点）保留待同步并继续
                if (connErr) {
                    pushFailed();
                } else {
                    if (--m_pendingPush <= 0) {
                        m_pendingPush = 0;
                        updateOfflineBadge();
                    }
                }
                return;
            }
            // 成功：本地评论笔记转正（重映射到服务端 id），收件箱保留该评论笔记
            const qint64 serverNoteId = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
            m_store.confirmComment(c.noteId, c.content, c.createdAt, serverNoteId);
            m_store.save();
            pushDone();
        });
    }
}

void InboxPage::onCopyAll()
{
    QStringList lines;
    for (const Note &n : m_notes) {
        // 与 aw-webui「复制全部」同款格式：修改时间∣内容
        lines << QStringLiteral("%1∣%2").arg(formatLocal(n.updatedAt.isEmpty() ? n.createdAt : n.updatedAt),
                                              n.content);
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    setStatus(StatusBadge::State::Connected, QStringLiteral("已复制 %1 条").arg(lines.size()));
    QTimer::singleShot(1500, this, [this] { setStatus(StatusBadge::State::Connected); });
}

void InboxPage::setStatus(StatusBadge::State s, const QString &text)
{
    m_badge->setState(s, text);
}

} // namespace awqtui
