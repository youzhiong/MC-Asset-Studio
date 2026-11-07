#include "ui/TextDocumentWidget.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace mcstudio {

TextDocumentWidget::TextDocumentWidget(const QString &title, const QString &relativePath,
                                       const QByteArray &data, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("DocumentHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 9, 16, 9);
    headerLayout->setSpacing(10);

    auto *name = new QLabel(title);
    name->setObjectName(QStringLiteral("DocumentTitle"));
    const QString displayPath = relativePath == title ? tr("项目根目录")
                                                       : relativePath;
    auto *path = new QLabel(displayPath);
    path->setObjectName(QStringLiteral("DocumentPath"));
    path->setToolTip(relativePath);
    auto *badge = new QLabel(tr("只读预览"));
    badge->setObjectName(QStringLiteral("DocumentBadge"));

    headerLayout->addWidget(name);
    headerLayout->addWidget(path, 1);
    headerLayout->addWidget(badge);
    layout->addWidget(header);

    auto *text = new QPlainTextEdit;
    text->setObjectName(QStringLiteral("DocumentText"));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(QString::fromUtf8(data));

    const bool looksJson = relativePath.endsWith(QLatin1String(".json"),
                                                 Qt::CaseInsensitive)
                           || relativePath.endsWith(QLatin1String(".mcmeta"),
                                                    Qt::CaseInsensitive);
    if (looksJson) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error == QJsonParseError::NoError)
            text->setPlainText(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
        else
            badge->setText(tr("JSON 解析失败 · 原始文本"));
    }

    layout->addWidget(text, 1);
}

} // namespace mcstudio
