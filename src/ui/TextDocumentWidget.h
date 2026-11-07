#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

namespace mcstudio {

class TextDocumentWidget : public QWidget
{
public:
    TextDocumentWidget(const QString &title, const QString &relativePath,
                       const QByteArray &data, QWidget *parent = nullptr);
};

} // namespace mcstudio
