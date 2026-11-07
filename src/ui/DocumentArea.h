#pragma once

#include <QHash>
#include <QIcon>
#include <QSet>
#include <QStringList>
#include <QTabWidget>
#include <functional>

class QWidget;

namespace mcstudio {

class DocumentArea : public QTabWidget
{
public:
    explicit DocumentArea(QWidget *parent = nullptr);

    void setBrowserPage(QWidget *page);
    void setBrowserVisible(bool visible);
    int openDocument(const QString &key, const QString &title, const QIcon &icon,
                     QWidget *content, const QString &path = {});
    int addFixedDocument(const QString &key, const QString &title, const QIcon &icon,
                         QWidget *content, const QString &path = {});
    void removeDocument(const QString &key);
    void setDocumentTitle(const QString &key, const QString &title);
    bool isFixedDocument(const QString &key) const;
    bool hasDocument(const QString &key) const;
    void activateDocument(const QString &key);
    void closeCurrentDocument();
    bool closeAllDocuments();
    QStringList documentPaths() const;
    QString currentDocumentPath() const;
    QString currentDocumentKey() const;

    // Return false to veto closing a tab.
    std::function<bool(const QString &key)> beforeClose;

private:
    bool closeDocumentAt(int index);
    void showTabContextMenu(const QPoint &pos);
    void rebuildIndex();
    QString keyAt(int index) const;

    QHash<QString, int> m_indexByKey;
    QSet<QString> m_fixedKeys;
};

} // namespace mcstudio
