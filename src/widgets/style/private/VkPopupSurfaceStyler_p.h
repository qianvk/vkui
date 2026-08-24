// SPDX-License-Identifier: MIT

#pragma once

#include "../../effects/private/VkShadowCache_p.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtGui/QPalette>
#include <QtGui/QRegion>

class QMenu;
class QPainter;
class QWidget;

namespace vkui {

class VCombobox;
class VLiquidGlassBackdrop;
class VLiquidGlassSurface;
struct VkMetricTokens;

class VkPopupSurfaceStyler final : public QObject {
  public:
    explicit VkPopupSurfaceStyler(QObject* parent);
    ~VkPopupSurfaceStyler() override;

    [[nodiscard]] static bool isComboBoxPopup(const QWidget* widget);
    [[nodiscard]] static bool isVComboboxPopup(const QWidget* widget);
    [[nodiscard]] static const VCombobox* owningVCombobox(const QWidget* widget);
    [[nodiscard]] static bool isMenuPopup(const QWidget* widget);
    [[nodiscard]] static bool isPopupContainer(const QWidget* widget);
    [[nodiscard]] static int shadowMargin(const VkMetricTokens& metrics) noexcept;
    [[nodiscard]] static int contentMargin(const VkMetricTokens& metrics) noexcept;
    [[nodiscard]] static int layoutMargin(const VkMetricTokens& metrics) noexcept;
    [[nodiscard]] static QRect surfaceRect(const QWidget& popup,
                                           const VkMetricTokens& metrics) noexcept;
    [[nodiscard]] bool isPopupPart(const QWidget* widget) const;
    void drawPopupSurface(const QWidget& popup, QPainter& painter) const;

    void polish(QWidget* widget);
    void unpolish(QWidget* widget);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct ContentWidgetState final {
        QPointer<QWidget> widget;
        bool noSystemBackground = false;
        bool opaquePaintEvent = false;
        bool styledBackground = false;
        bool autoFillBackground = false;
        QPalette::ColorRole backgroundRole = QPalette::NoRole;
        QPalette palette;
    };

    struct PopupState final {
        bool translucentBackground = false;
        bool noSystemBackground = false;
        bool opaquePaintEvent = false;
        bool styledBackground = false;
        bool autoFillBackground = false;
        QPalette palette;
        QRegion mask;
        Qt::WindowFlags windowFlags;
        QList<ContentWidgetState> contentWidgets;
        VLiquidGlassBackdrop* glassBackdrop = nullptr;
        VLiquidGlassSurface* glassSurface = nullptr;
        mutable VkShadowCache shadowCache;
    };

    static void raiseVisibleSubmenuChain(QMenu* menu);
    [[nodiscard]] static bool hasMenuTransientParent(const QMenu* menu);
    static void scheduleMenuStackRestore(QMenu* menu, bool raiseMenu);
    static void applyTransparentPalette(QWidget& widget);
    static void makeContentWidgetTransparent(QWidget& widget, PopupState& state);
    static void syncTransparentContent(QWidget& popup, PopupState& state);
    static void restoreContentWidgets(const PopupState& state);
    [[nodiscard]] static QWidget* backdropSourceFor(QWidget& popup);
    void syncLiquidGlassSurface(QWidget& popup);

    QHash<QWidget*, PopupState> popups_;
};

} // namespace vkui
