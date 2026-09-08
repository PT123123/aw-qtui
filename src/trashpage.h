// trashpage.h —— 回收站：已软删除的笔记列表，可恢复 / 永久删除
#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;
class QLabel;

// Qt Designer 布局（trashpage.ui），全局命名空间
namespace Ui { class TrashPage; }

namespace awqtui {

class LocalStore;

class TrashPage : public QWidget
{
    Q_OBJECT
public:
    explicit TrashPage(LocalStore *store, QWidget *parent = nullptr);
    ~TrashPage() override;
    void applyUiScale();
    void refresh();

private slots:
    void onRestore();
    void onDeleteForever();
    void onClearAll();

private:
    // Qt Designer 生成的布局对象（trashpage.ui -> ui_trashpage.h）
    Ui::TrashPage *ui = nullptr;
    void buildUi();
    void applyStyle();
    void rebuildList();

    LocalStore *m_store = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_restoreBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
};

} // namespace awqtui
