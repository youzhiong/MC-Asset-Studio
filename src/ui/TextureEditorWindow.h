#pragma once

#include <QWidget>
#include <functional>

class QCloseEvent;

namespace mcstudio {

class TextureEditorPage;

// Top-level container used by the "弹出独立窗口" action. The editor page keeps
// all of its state; this window only owns the frame, geometry and close prompt.
class TextureEditorWindow : public QWidget
{
public:
    explicit TextureEditorWindow(TextureEditorPage *page, QWidget *parent = nullptr);

    TextureEditorPage *page() const { return m_page; }

    // Return true to let the window close; false keeps it open.
    std::function<bool()> onCloseRequested;
    void setAllowClose(bool allow) { m_allowClose = allow; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    TextureEditorPage *m_page = nullptr;
    bool m_allowClose = false;
};

} // namespace mcstudio
