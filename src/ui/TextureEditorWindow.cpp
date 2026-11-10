#include "ui/TextureEditorWindow.h"

#include "ui/TextureEditorPage.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QVBoxLayout>

namespace mcstudio {

TextureEditorWindow::TextureEditorWindow(TextureEditorPage *page, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_page(page)
{
    setObjectName(QStringLiteral("TextureEditorWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(900, 680);
    resize(1280, 900);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (m_page) {
        m_page->setParent(this);
        layout->addWidget(m_page);
        setWindowTitle(tr("材质编辑 — %1").arg(QFileInfo(m_page->relativePath()).fileName()));
    }
}

void TextureEditorWindow::closeEvent(QCloseEvent *event)
{
    if (!m_allowClose && onCloseRequested && !onCloseRequested()) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

} // namespace mcstudio
