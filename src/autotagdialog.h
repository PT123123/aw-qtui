// autotagdialog.h —— 自动标签规则编辑器
#pragma once

#include <QDialog>

#include "tagstore.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

namespace awqtui {

class AutoTagDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AutoTagDialog(TagStore *store, QWidget *parent = nullptr);

signals:
    void rulesChanged();

private slots:
    void onAdd();
    void onEdit();
    void onDelete();
    void onMove(bool up);
    void onSettings();
    void onDoubleClick(int row, int);

private:
    void reload();
    void editRule(int row);
    void setStatus(const QString &msg);

    TagStore *m_store = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
};

} // namespace awqtui
