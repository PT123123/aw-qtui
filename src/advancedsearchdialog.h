// advancedsearchdialog.h —— 高级搜索：全库/日期范围/未标记过滤/批量打标签/删除/导出/跳转
#pragma once

#include <QDialog>

#include "tagstore.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTableWidgetItem;

namespace awqtui {

class AdvancedSearchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AdvancedSearchDialog(TagStore *store, QWidget *parent = nullptr);

    // 外部触发搜索（并把日期范围定位到 dayStartMs 所在天）
    void focusDay(qint64 dayStartMs);

signals:
    void jumpToDay(qint64 dayStartMs);
    void tagsChanged();

private slots:
    void onFind();
    void onDoubleClick(int row, int col);
    void onTagAll();
    void onDeleteAll();
    void onExport();
    void onSelectionMenu(const QPoint &pos);

private:
    void runSearch();
    void fillTable();
    QString selectedComboText();

    struct ResultRow {
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString title;
        QString notes;
        bool isTagSegment = false;
        qint64 tagId = 0;
    };

    TagStore *m_store = nullptr;
    QComboBox *m_timelineCombo = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QDateEdit *m_fromEdit = nullptr;
    QDateEdit *m_toEdit = nullptr;
    QCheckBox *m_untaggedOnly = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QList<ResultRow> m_results;
};

} // namespace awqtui
