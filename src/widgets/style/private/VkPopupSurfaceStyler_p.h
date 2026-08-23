// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtGui/QPalette>
#include <QtGui/QRegion>

class QMenu;
class QWidget;

namespace vkui {

class VkPopupSurfaceStyler final : public QObject {
  public:
    explicit VkPopupSurfaceStyler(QObject* parent);
    ~VkPopupSurfaceStyler() override;

    [[nodiscard]] static bool isComboBoxPopup(const QWidget* widget);
    [[nodiscard]] static bool isVComboboxPopup(const QWidget* widget);
    [[nodiscard]] static bool isMenuPopup(const QWidget* widget);
    [[nodiscard]] static bool isPopupContainer(const QWidget* widget);
    [[nodiscard]] bool isPopupPart(const QWidget* widget) const;

    void polish(QWidget* widget);
    void unpolish(QWidget* widget);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct PopupState final {
        bool translucentBackground = false;
        bool noSystemBackground = false;
        bool opaquePaintEvent = false;
        bool styledBackground = false;
        bool autoFillBackground = false;
        QPalette palette;
        QRegion mask;
        Qt::WindowFlags windowFlags;
    };

    static void raiseVisibleSubmenuChain(QMenu* menu);
    [[nodiscard]] static bool hasMenuTransientParent(const QMenu* menu);
    static void scheduleMenuStackRestore(QMenu* menu, bool raiseMenu);
    static void applyTransparentPalette(QWidget& widget);

    QHash<QWidget*, PopupState> popups_;
};

} // namespace vkui
