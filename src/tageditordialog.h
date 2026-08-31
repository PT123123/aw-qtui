// tageditordialog.h —— Tag editor：Tag combinations / Tags / Tag shortcuts / Tag sources
#pragma once

#include <QDialog>

#include "tagstore.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;

namespace awqtui {

class TagEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TagEditorDialog(TagStore *store, QWidget *parent = nullptr);

signals:
    void tagsChanged();

private slots:
    void onTabChanged(int);
    void onRename();
    void onReplace();
    void onDeleteSelected();
    void onChangeColor();
    void onImport();
    void onExport();
    void onFilterText(const QString &);
    // shortcuts tab
    void onAddShortcut();
    void onEditShortcut();
    void onDeleteShortcut();
    // sources tab
    void onImportSourceText();
    void onLoadSourceFile();

private:
    void buildUi();
    void reloadCombos();
    void reloadTags();
    void reloadShortcuts();
    void showContextMenu(const QPoint &pos);
    void setStatus(const QString &msg);

    TagStore *m_store = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_comboTable = nullptr;
    QTableWidget *m_tagTable = nullptr;
    QTableWidget *m_shortcutTable = nullptr;
    QLineEdit *m_filter = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr;
    QPushButton *m_renameBtn = nullptr;
    QPushButton *m_replaceBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace awqtui
