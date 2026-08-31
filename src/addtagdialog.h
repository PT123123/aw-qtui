// addtagdialog.h —— Add tag 窗口：标签（逗号分隔）+ Tag picker 层级 + 最近标签 + 备注/Billable/时间
#pragma once

#include <QDialog>

#include "tagstore.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

namespace awqtui {

class AddTagDialog : public QDialog
{
    Q_OBJECT
public:
    AddTagDialog(TagStore *store, qint64 startMs, qint64 endMs, QWidget *parent = nullptr);

    // 结果
    QStringList tags() const;
    QString notes() const;
    bool billable() const;
    qint64 startMs() const;
    qint64 endMs() const;

private slots:
    void onTagsEdited();
    void onTimeChanged();
    void applyRecent(QListWidgetItem *item);
    void onPickerItem(QListWidgetItem *item);
    void onSortChanged(int);
    void goUp();

private:
    void refreshPicker();
    void fillRecent();

    TagStore *m_store = nullptr;
    QLineEdit *m_tagsEdit = nullptr;
    QPlainTextEdit *m_notesEdit = nullptr;
    QCheckBox *m_billable = nullptr;
    QDateTimeEdit *m_startEdit = nullptr;
    QDateTimeEdit *m_endEdit = nullptr;
    QLabel *m_durationLabel = nullptr;
    QListWidget *m_recent = nullptr;
    QListWidget *m_picker = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QPushButton *m_upBtn = nullptr;
    QStringList m_path;   // Tag picker 当前层级路径
    bool m_building = false;
};

} // namespace awqtui
