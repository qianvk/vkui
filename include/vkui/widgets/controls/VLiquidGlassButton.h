// SPDX-License-Identifier: MIT

#pragma once

#include <QtWidgets/QPushButton>
#include <vkui/widgets/effects/VLiquidGlass.h>

namespace vkui {

/** A QPushButton whose background uses the shared Liquid Glass renderer. */
class VKUI_WIDGETS_EXPORT VLiquidGlassButton final : public QPushButton {
    Q_OBJECT

  public:
    explicit VLiquidGlassButton(QWidget* parent = nullptr);
    explicit VLiquidGlassButton(const QString& text, QWidget* parent = nullptr);

    void setBackdrop(VLiquidGlassBackdrop* backdrop);
    [[nodiscard]] VLiquidGlassBackdrop* backdrop() const noexcept;

    void setGlassStyle(const VLiquidGlassStyle& style);
    [[nodiscard]] VLiquidGlassStyle glassStyle() const noexcept;

    void setGlassEnabled(bool enabled);
    [[nodiscard]] bool isGlassEnabled() const noexcept;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void initialize();

    VLiquidGlassSurface* material_ = nullptr;
    QMetaObject::Connection backdropInvalidatedConnection_;
};

} // namespace vkui
