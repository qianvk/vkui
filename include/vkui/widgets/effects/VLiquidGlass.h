// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QObject>
#include <QtCore/QtTypes>
#include <QtWidgets/QWidget>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QPainter;

namespace vkui {

/** Controls the CPU sampling resolution used by the liquid-glass renderer. */
enum class VLiquidGlassQuality {
    Automatic,
    Reduced,
    Balanced,
    High,
};

/** Material parameters shared by liquid-glass surfaces. Values use logical pixels. */
struct VKUI_WIDGETS_EXPORT VLiquidGlassStyle final {
    qreal cornerRadius = -1.0;
    qreal blurRadius = 1.0;
    qreal refractionHeight = 12.0;
    qreal refractionAmount = 24.0;
    qreal chromaticAberration = 1.25;
    qreal saturation = 1.12;
    qreal tintOpacity = 0.10;
    bool adaptiveLuminance = true;
    /** Opts into a neutral semantic outline in addition to the optical edge highlights. */
    bool drawsBorder = false;
    VLiquidGlassQuality quality = VLiquidGlassQuality::Automatic;

    /** Returns the balanced material intended for controls and compact panels. */
    [[nodiscard]] static VLiquidGlassStyle regular() noexcept;

    /** Returns a clearer material for content where backdrop detail should remain visible. */
    [[nodiscard]] static VLiquidGlassStyle clear() noexcept;

    friend bool operator==(const VLiquidGlassStyle&, const VLiquidGlassStyle&) = default;
};

class VLiquidGlassBackdropPrivate;
class VLiquidGlassSurfacePrivate;
class VkPopupSurfaceStyler;

/**
 * Observes a backdrop source and shares local captures between glass surfaces.
 *
 * The source and glass surfaces should normally be overlapping siblings. A top-level popup may
 * sample its transient owner because Qt renders popup windows independently. Same-window ancestor
 * captures are rejected to prevent recursive self-capture.
 */
class VKUI_WIDGETS_EXPORT VLiquidGlassBackdrop final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        QWidget* sourceWidget READ sourceWidget WRITE setSourceWidget NOTIFY sourceWidgetChanged)

  public:
    explicit VLiquidGlassBackdrop(QWidget* sourceWidget = nullptr, QObject* parent = nullptr);
    ~VLiquidGlassBackdrop() override;

    void setSourceWidget(QWidget* sourceWidget);
    [[nodiscard]] QWidget* sourceWidget() const noexcept;

  public Q_SLOTS:
    /** Discards captured regions after an application-owned drawing source changes. */
    void invalidate();

  Q_SIGNALS:
    void sourceWidgetChanged(QWidget* sourceWidget);
    void invalidated();

  private:
    Q_DISABLE_COPY_MOVE(VLiquidGlassBackdrop)
    friend class VLiquidGlassSurfacePrivate;

    std::unique_ptr<VLiquidGlassBackdropPrivate> d;
};

/**
 * A QWidget container that renders a live, rounded liquid-glass material behind its children.
 *
 * The process-wide VkThemeManager policy selects Liquid Glass or the opaque semantic fallback.
 * setGlassEnabled(false) keeps the surface fully unpainted for application-controlled composition.
 */
class VKUI_WIDGETS_EXPORT VLiquidGlassSurface final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(
        vkui::VLiquidGlassBackdrop* backdrop READ backdrop WRITE setBackdrop NOTIFY backdropChanged)
    Q_PROPERTY(
        bool glassEnabled READ isGlassEnabled WRITE setGlassEnabled NOTIFY glassEnabledChanged)

  public:
    explicit VLiquidGlassSurface(QWidget* parent = nullptr);
    ~VLiquidGlassSurface() override;

    void setBackdrop(VLiquidGlassBackdrop* backdrop);
    [[nodiscard]] VLiquidGlassBackdrop* backdrop() const noexcept;

    void setGlassStyle(const VLiquidGlassStyle& style);
    [[nodiscard]] VLiquidGlassStyle glassStyle() const noexcept;

    void setGlassEnabled(bool enabled);
    [[nodiscard]] bool isGlassEnabled() const noexcept;

  Q_SIGNALS:
    void backdropChanged(vkui::VLiquidGlassBackdrop* backdrop);
    void glassStyleChanged();
    void glassEnabledChanged(bool enabled);

  protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

  private:
    Q_DISABLE_COPY_MOVE(VLiquidGlassSurface)
    friend class VkPopupSurfaceStyler;

    void paintMaterial(QPainter& painter);

    std::unique_ptr<VLiquidGlassSurfacePrivate> d;
};

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VLiquidGlassQuality)
Q_DECLARE_METATYPE(vkui::VLiquidGlassStyle)
