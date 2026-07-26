// SPDX-License-Identifier: MIT

#pragma once

#include <QDialog>
#include <vkui/VkUiGlobal.h>

class QFrame;
class QLabel;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace vkui {

class VkWindowAgent;

/**
 * Reusable close-only, full-content dialog chrome.
 *
 * Applications own the content while VkUI owns native frameless behavior,
 * title-bar hit testing, fallback close controls, and host-relative placement.
 */
class VKUI_WINDOW_EXPORT VkFramelessDialog : public QDialog {
    Q_OBJECT

  public:
    explicit VkFramelessDialog(const QString& title, QWidget* parent = nullptr);
    ~VkFramelessDialog() override;

    [[nodiscard]] bool isResizable() const;
    void setResizable(bool resizable);

    [[nodiscard]] QWidget* titleBar() const;
    [[nodiscard]] QWidget* contentWidget() const;
    [[nodiscard]] QVBoxLayout* contentLayout() const;
    [[nodiscard]] VkWindowAgent* windowAgent() const;

    void positionForHost(const QWidget* host, QSizeF fraction = QSizeF(0.8, 0.8));

  protected:
    void changeEvent(QEvent* event) override;

  private:
    void buildUi();
    void installWindowChrome();
    void refreshTitle();

    QFrame* surface_ = nullptr;
    QWidget* titleBar_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QToolButton* fallbackCloseButton_ = nullptr;
    QWidget* nativeButtonReserve_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    VkWindowAgent* windowAgent_ = nullptr;
    bool resizable_ = false;
};

} // namespace vkui
