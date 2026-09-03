// trashpage.h —— 回收站：已软删除的笔记列表，可恢复 / 永久删除
#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;
class QLabel;

namespace awqtui {

class LocalStore;

class TrashPage : public QWidget
{
    Q_OBJECT
public:
    explicit TrashPage(LocalStore *store, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private slots:
    void onRestore();
    void onDeleteForever();
    void onClearAll();

private:
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
