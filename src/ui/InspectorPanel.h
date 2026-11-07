#pragma once

#include "core/AnimationMeta.h"

#include <QByteArray>
#include <QImage>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QTimer;

namespace mcstudio {

class Project;

class InspectorPanel : public QWidget
{
public:
    explicit InspectorPanel(QWidget *parent = nullptr);

    void setTexture(const Project &project, const QString &relativePath);
    void setTextureBytes(const QString &relativePath, const QByteArray &bytes,
                         const QByteArray &mcmeta, const QString &sourceLabel,
                         bool reference);
    void setReferenceInfo(const QString &text);
    void setAssociationHint(const QString &text);
    void clear();

    std::function<void(const QString &)> onImportReference;

private:
    void changeEvent(QEvent *event) override;
    void tickPreview();
    void updatePreview();
    void setField(QLabel *label, const QString &value);

    QLabel *m_badge = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_type = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_resolution = nullptr;
    QLabel *m_source = nullptr;
    QLabel *m_referenceSource = nullptr;
    QLabel *m_animation = nullptr;
    QLabel *m_path = nullptr;
    QLabel *m_reference = nullptr;
    QPushButton *m_importButton = nullptr;
    QString m_currentPath;

    QVector<QImage> m_frames;
    AnimationMeta m_animationMeta;
    QTimer *m_timer = nullptr;
    int m_frame = 0;
    qint64 m_elapsedMs = 0;
};

} // namespace mcstudio
