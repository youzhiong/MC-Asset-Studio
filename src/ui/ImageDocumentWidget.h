#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

namespace mcstudio {

class ImageDocumentWidget : public QWidget
{
public:
    ImageDocumentWidget(const QString &title, const QString &relativePath,
                        const QByteArray &data, QWidget *parent = nullptr);
};

} // namespace mcstudio
