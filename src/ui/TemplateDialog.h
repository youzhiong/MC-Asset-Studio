#pragma once

#include "core/TemplateLibrary.h"

#include <QDialog>
#include <QImage>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

namespace mcstudio {

class TemplateDialog : public QDialog
{
public:
    TemplateDialog(std::function<QStringList()> pathProvider,
                   std::function<QByteArray(const QString &)> bytesProvider,
                   QWidget *parent = nullptr);

    QImage selectedImage() const { return m_selectedImage; }
    QString selectedName() const { return m_selectedName; }
    QString selectedPackZip() const { return m_selectedPackZip; }
    bool equipmentRequested() const { return m_equipmentRequested; }

private:
    void refresh();
    void loadSelection();
    void importZip();
    void importFromLibrary();
    void importVanillaSet();
    int importVanillaTemplates(bool quiet);
    bool hasVanillaSet() const;
    void openTemplatesFolder();

    TemplateLibrary m_library;
    QVector<TemplateEntry> m_entries;
    QImage m_selectedImage;
    QString m_selectedName;
    QString m_selectedPackZip;
    bool m_equipmentRequested = false;

    std::function<QStringList()> m_pathProvider;
    std::function<QByteArray(const QString &)> m_bytesProvider;

    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_info = nullptr;
    QPushButton *m_useButton = nullptr;
    QPushButton *m_equipmentButton = nullptr;
};

} // namespace mcstudio
