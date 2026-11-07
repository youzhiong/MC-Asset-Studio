#include "ui/DocumentArea.h"

#include "ui/IconFactory.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QShortcut>
#include <QTabBar>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

const QString kBrowserKey = QStringLiteral("__browser__");

} // namespace

DocumentArea::DocumentArea(QWidget *parent)
    : QTabWidget(parent)
{
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(false);
    setElideMode(Qt::ElideRight);
    setUsesScrollButtons(true);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &QTabWidget::tabCloseRequested, this, &DocumentArea::closeDocumentAt);
    connect(tabBar(), &QTabBar::customContextMenuRequested, this,
            &DocumentArea::showTabContextMenu);

    auto *closeShortcut = new QShortcut(QKeySequence::Close, this);
    closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut, &QShortcut::activated, this, &DocumentArea::closeCurrentDocument);

    auto *nextShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    nextShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextShortcut, &QShortcut::activated, this, [this] {
        if (count() > 1)
            setCurrentIndex((currentIndex() + 1) % count());
    });

    auto *prevShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    prevShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(prevShortcut, &QShortcut::activated, this, [this] {
        if (count() > 1)
            setCurrentIndex((currentIndex() - 1 + count()) % count());
    });
}

void DocumentArea::setBrowserPage(QWidget *page)
{
    if (!page || m_indexByKey.contains(kBrowserKey))
        return;

    const int index = addTab(page,
                             IconFactory::icon(IconFactory::Glyph::Grid,
                                               QColor(QStringLiteral("#94a3b8")), 16),
                             tr("资源概览"));
    tabBar()->setTabData(index, kBrowserKey);
    tabBar()->setTabToolTip(index, tr("资源概览（仅无项目时显示）"));
    tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
    tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
    m_indexByKey.insert(kBrowserKey, index);
}

void DocumentArea::setBrowserVisible(bool visible)
{
    const int index = m_indexByKey.value(kBrowserKey, -1);
    if (index >= 0 && index < count())
        setTabVisible(index, visible);
}

int DocumentArea::addFixedDocument(const QString &key, const QString &title,
                                   const QIcon &icon, QWidget *content,
                                   const QString &path)
{
    if (key.isEmpty() || key == kBrowserKey)
        return -1;

    if (m_indexByKey.contains(key)) {
        const int existing = m_indexByKey.value(key);
        if (existing >= 0 && existing < count()) {
            setCurrentIndex(existing);
            return existing;
        }
    }

    if (!content)
        return -1;

    int insertIndex = 1; // after the browser page
    while (insertIndex < count() && m_fixedKeys.contains(keyAt(insertIndex)))
        ++insertIndex;

    const int index = insertTab(insertIndex, content, icon, title);
    tabBar()->setTabData(index, key);
    tabBar()->setTabToolTip(index, path.isEmpty() ? title : path);
    tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
    tabBar()->setTabButton(index, QTabBar::LeftSide, nullptr);
    m_fixedKeys.insert(key);
    m_indexByKey.insert(key, index);
    rebuildIndex();
    setCurrentIndex(m_indexByKey.value(key, index));
    return index;
}

void DocumentArea::removeDocument(const QString &key)
{
    if (key == kBrowserKey)
        return;
    const int index = m_indexByKey.value(key, -1);
    if (index < 0 || index >= count())
        return;

    QWidget *page = widget(index);
    removeTab(index);
    if (page)
        page->deleteLater();
    m_fixedKeys.remove(key);
    m_indexByKey.remove(key);
    rebuildIndex();
}

void DocumentArea::setDocumentTitle(const QString &key, const QString &title)
{
    const int index = m_indexByKey.value(key, -1);
    if (index >= 0 && index < count())
        setTabText(index, title);
}

bool DocumentArea::isFixedDocument(const QString &key) const
{
    return key == kBrowserKey || m_fixedKeys.contains(key);
}

int DocumentArea::openDocument(const QString &key, const QString &title,
                               const QIcon &icon, QWidget *content,
                               const QString &path)
{
    if (key.isEmpty() || key == kBrowserKey)
        return -1;

    if (m_indexByKey.contains(key)) {
        const int existing = m_indexByKey.value(key);
        if (existing >= 0 && existing < count()) {
            setCurrentIndex(existing);
            return existing;
        }
    }

    if (!content) {
        auto *placeholder = new QWidget;
        auto *layout = new QVBoxLayout(placeholder);
        layout->addWidget(new QLabel(tr("无法预览此文件")));
        content = placeholder;
    }

    const int index = addTab(content, icon, title);
    tabBar()->setTabData(index, key);
    tabBar()->setTabToolTip(index, path.isEmpty() ? title : path);
    m_indexByKey.insert(key, index);
    setCurrentIndex(index);
    return index;
}

bool DocumentArea::hasDocument(const QString &key) const
{
    return m_indexByKey.contains(key);
}

void DocumentArea::activateDocument(const QString &key)
{
    const int index = m_indexByKey.value(key, -1);
    if (index >= 0 && index < count())
        setCurrentIndex(index);
}

void DocumentArea::closeCurrentDocument()
{
    closeDocumentAt(currentIndex());
}

bool DocumentArea::closeAllDocuments()
{
    for (int index = count() - 1; index >= 0; --index) {
        const QString key = keyAt(index);
        if (isFixedDocument(key))
            continue;
        if (!closeDocumentAt(index))
            return false;
    }
    const int browser = m_indexByKey.value(kBrowserKey, -1);
    if (browser >= 0 && browser < count())
        setCurrentIndex(browser);
    return true;
}

QStringList DocumentArea::documentPaths() const
{
    QStringList paths;
    for (int index = 1; index < count(); ++index) {
        const QString key = keyAt(index);
        if (key.isEmpty() || key.startsWith(QLatin1String("__"))
            || key.startsWith(QLatin1String("edit:"))) {
            continue;
        }
        paths.append(key);
    }
    return paths;
}

QString DocumentArea::currentDocumentPath() const
{
    const QString key = keyAt(currentIndex());
    if (key.isEmpty() || key.startsWith(QLatin1String("__"))
        || key.startsWith(QLatin1String("edit:"))) {
        return {};
    }
    return key;
}

QString DocumentArea::currentDocumentKey() const
{
    return keyAt(currentIndex());
}

bool DocumentArea::closeDocumentAt(int index)
{
    if (index < 0 || index >= count())
        return false;

    const QString key = keyAt(index);
    if (isFixedDocument(key))
        return false;

    if (beforeClose && !beforeClose(key))
        return false;

    QWidget *page = widget(index);
    removeTab(index);
    if (page)
        page->deleteLater();
    if (!key.isEmpty())
        m_indexByKey.remove(key);
    rebuildIndex();

    const int browser = m_indexByKey.value(kBrowserKey, -1);
    if (count() == 1 && browser >= 0)
        setCurrentIndex(browser);
    return true;
}

void DocumentArea::rebuildIndex()
{
    m_indexByKey.clear();
    for (int i = 0; i < count(); ++i) {
        const QString key = tabBar()->tabData(i).toString();
        if (!key.isEmpty())
            m_indexByKey.insert(key, i);
    }
}

void DocumentArea::showTabContextMenu(const QPoint &pos)
{
    const int index = tabBar()->tabAt(pos);
    if (index < 0)
        return;

    const QString key = keyAt(index);
    QMenu menu(this);

    QAction *closeAction = menu.addAction(tr("关闭"));
    closeAction->setEnabled(!isFixedDocument(key));
    QAction *closeOthers = menu.addAction(tr("关闭其他标签"));
    QAction *closeAll = menu.addAction(tr("关闭全部标签"));
    menu.addSeparator();
    QAction *copyPath = menu.addAction(tr("复制路径"));
    copyPath->setEnabled(!key.isEmpty() && !key.startsWith(QLatin1String("__")));

    QAction *chosen = menu.exec(tabBar()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == closeAction) {
        closeDocumentAt(index);
    } else if (chosen == closeOthers) {
        for (int i = count() - 1; i >= 1; --i) {
            if (i != index)
                closeDocumentAt(i);
        }
        activateDocument(key);
    } else if (chosen == closeAll) {
        closeAllDocuments();
    } else if (chosen == copyPath) {
        QApplication::clipboard()->setText(key);
    }
}

QString DocumentArea::keyAt(int index) const
{
    if (index < 0 || index >= count())
        return {};
    return tabBar()->tabData(index).toString();
}

} // namespace mcstudio
