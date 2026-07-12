// SPDX-License-Identifier: MIT

#pragma once

#include <QProxyStyle>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QApplication;
class QPainter;
class QStyleHintReturn;
class QStyleOption;
class QStyleOptionComplex;
class QWidget;

namespace vkui {

class VkStylePrivate;

/**
 * A theme-aware proxy style for standard Qt Widgets.
 *
 * Unsupported primitives and controls are deliberately delegated to an
 * explicitly created Fusion style, keeping Qt behavior intact on every
 * supported platform.
 */
class VKUI_WIDGETS_EXPORT VkStyle final : public QProxyStyle {
    Q_OBJECT

  public:
    explicit VkStyle();
    ~VkStyle() override;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override;

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override;
    int layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2,
                      Qt::Orientation orientation, const QStyleOption* option = nullptr,
                      const QWidget* widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize,
                           const QWidget* widget = nullptr) const override;
    QRect subElementRect(SubElement element, const QStyleOption* option,
                         const QWidget* widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                         SubControl subControl, const QWidget* widget = nullptr) const override;
    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                     const QPoint& position,
                                     const QWidget* widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override;
    QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption* option = nullptr,
                       const QWidget* widget = nullptr) const override;
    QPalette standardPalette() const override;

    void polish(QPalette& palette) override;
    void polish(QApplication* application) override;
    void polish(QWidget* widget) override;
    void unpolish(QApplication* application) override;
    void unpolish(QWidget* widget) override;

  private:
    std::unique_ptr<VkStylePrivate> d;
};

/**
 * Installs vkui's style and resolved palette on an application.
 *
 * QApplication assumes ownership of the newly installed VkStyle.
 */
VKUI_WIDGETS_EXPORT void installVkUi(QApplication& application);

} // namespace vkui
