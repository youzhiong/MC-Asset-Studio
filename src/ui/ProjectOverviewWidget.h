#pragma once

#include "core/Project.h"

#include <QJsonObject>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace mcstudio {

class ProjectOverviewWidget : public QWidget
{
public:
    explicit ProjectOverviewWidget(const Project &project, QWidget *parent = nullptr);

    std::function<void()> onExportNew;
    std::function<void()> onOverwrite;
    std::function<void()> onOpenFolder;
    std::function<void()> onOpenPackEditor;
    std::function<void(int packFormat, const QVector<int> &supportedFormats,
                       const QString &description)>
        onPackMetaSaved;

    bool applyPackMetaValues(int packFormat, const QString &description,
                             const QVector<int> &supportedFormats);

private:
    void savePackMeta();
    void updatePreview();
    QVector<int> supportedFormatsFromEdit() const;
    QString descriptionFromMeta(const QJsonObject &root) const;

    Project m_project;
    QJsonObject m_packMetaRoot;
    QSpinBox *m_formatSpin = nullptr;
    QLabel *m_formatHint = nullptr;
    QLineEdit *m_supportedEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QPlainTextEdit *m_jsonPreview = nullptr;
    QLabel *m_saveStatus = nullptr;
};

} // namespace mcstudio
