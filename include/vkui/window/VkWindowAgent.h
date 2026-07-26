// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QVariant>
#include <functional>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QWidget;

namespace vkui {

class VkWindowAgentPrivate;

/**
 * Host-owned frameless-window controller.
 *
 * The implementation is derived from QWindowKit and lives inside VkUI. Public
 * consumers never depend on QWindowKit headers, targets, or source layout.
 */
class VKUI_WINDOW_EXPORT VkWindowAgent final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool resizable READ isResizable WRITE setResizable NOTIFY resizableChanged)
    Q_PROPERTY(SystemButtonVisibility systemButtonVisibility READ systemButtonVisibility WRITE
                   setSystemButtonVisibility NOTIFY systemButtonVisibilityChanged)

  public:
    enum class SystemButton {
        Unknown,
        WindowIcon,
        Help,
        Minimize,
        Maximize,
        Close,
    };
    Q_ENUM(SystemButton)

    enum class SystemButtonVisibility {
        AlwaysVisible,
        VisibleOnHover,
        AlwaysHidden,
    };
    Q_ENUM(SystemButtonVisibility)

    using ScreenRectCallback = std::function<QRect(const QSize&)>;

    explicit VkWindowAgent(QObject* parent = nullptr);
    ~VkWindowAgent() override;

    VkWindowAgent(const VkWindowAgent&) = delete;
    VkWindowAgent& operator=(const VkWindowAgent&) = delete;

    [[nodiscard]] bool setup(QWidget* window);

    [[nodiscard]] QList<QWidget*> titleBars() const;
    [[nodiscard]] QWidget* titleBar() const;
    void setTitleBar(QWidget* titleBar);
    [[nodiscard]] bool addTitleBar(QWidget* titleBar);
    [[nodiscard]] bool removeTitleBar(QWidget* titleBar);
    void clearTitleBars();

    [[nodiscard]] QWidget* systemButton(SystemButton button) const;
    void setSystemButton(SystemButton button, QWidget* widget);
    [[nodiscard]] bool installSystemButtons();
    [[nodiscard]] QRect systemButtonAreaGeometry() const;

    [[nodiscard]] QWidget* systemButtonArea() const;
    void setSystemButtonArea(QWidget* widget);
    void setSystemButtonAreaGeometry(const QRect& rect);
    [[nodiscard]] ScreenRectCallback systemButtonAreaCallback() const;
    void setSystemButtonAreaCallback(ScreenRectCallback callback);

    [[nodiscard]] bool hasSystemButtonPosition(SystemButton button) const;
    [[nodiscard]] QPoint systemButtonPosition(SystemButton button) const;
    void setSystemButtonPosition(SystemButton button, const QPoint& position);
    void clearSystemButtonPosition(SystemButton button);

    [[nodiscard]] bool isHitTestVisible(QWidget* titleBar, const QWidget* widget) const;
    [[nodiscard]] bool setHitTestVisible(QWidget* titleBar, QWidget* widget, bool visible = true);
    [[nodiscard]] bool isHitTestVisible(const QWidget* widget) const;
    void setHitTestVisible(QWidget* widget, bool visible = true);

    [[nodiscard]] bool isResizable() const;
    void setResizable(bool resizable);
    [[nodiscard]] SystemButtonVisibility systemButtonVisibility() const;
    void setSystemButtonVisibility(SystemButtonVisibility visibility);

    [[nodiscard]] QVariant windowAttribute(const QString& key) const;
    [[nodiscard]] bool setWindowAttribute(const QString& key, const QVariant& value);

  public slots:
    void showSystemMenu(const QPoint& position);
    void centralize();
    void raiseWindow();

  signals:
    void resizableChanged(bool resizable);
    void systemButtonVisibilityChanged(SystemButtonVisibility visibility);
    void titleBarChanged(QWidget* titleBar);
    void titleBarAdded(QWidget* titleBar);
    void titleBarRemoved(QWidget* titleBar);
    void titleBarsCleared();
    void systemButtonChanged(SystemButton button, QWidget* widget);

  private:
    std::unique_ptr<VkWindowAgentPrivate> d_;
};

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkWindowAgent::SystemButton)
Q_DECLARE_METATYPE(vkui::VkWindowAgent::SystemButtonVisibility)
