// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtGui/QFont>

class QEvent;
class QWidget;

namespace vkui {

/** Resolves responsive body typography without overriding application-owned fonts. */
class VkWidgetTypographyController final : public QObject {
  public:
    explicit VkWidgetTypographyController(QObject* parent);
    ~VkWidgetTypographyController() override;

    void polish(QWidget* widget);
    void restoreAll();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct WidgetState final {
        QFont appliedFont;
        bool originalWindowPropagation = false;
        bool isWindow = false;
        bool managesFont = false;
        bool hasAppliedFont = false;
        bool applyingFont = false;
    };

    [[nodiscard]] bool hasApplicationFontOverride(const QWidget* widget) const;
    void applyThemeFont(QWidget& widget, WidgetState& state);
    void releaseManagedDescendants(QWidget& root);

    QHash<QWidget*, WidgetState> widgets_;
};

} // namespace vkui
