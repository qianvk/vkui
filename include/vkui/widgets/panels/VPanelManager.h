// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringView>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QWidget;

namespace vkui {

class VPanelManagerPrivate;

/** Immutable panel data suitable for layout previews and persistence adapters. */
struct VKUI_WIDGETS_EXPORT VPanelState final {
    QString id;
    QString title;
    int number = 0;
    QRectF normalizedRect;
    bool expanded = true;
};

/**
 * Window-scoped panel state, splitter binding, and layout-chooser controller.
 *
 * One manager belongs to one top-level window. Registered panel widgets may be
 * recreated while their semantic IDs, proportions, and collapse state remain
 * stable. Managed splitter and the inner-left-edge handle expose consistent collapse,
 * restore, and layout-chooser gestures without changing native window behavior.
 */
class VKUI_WIDGETS_EXPORT VPanelManager final : public QObject {
    Q_OBJECT

  public:
    explicit VPanelManager(QWidget& window);
    ~VPanelManager() override;

    VPanelManager(const VPanelManager&) = delete;
    VPanelManager& operator=(const VPanelManager&) = delete;

    [[nodiscard]] QWidget* window() const noexcept;
    [[nodiscard]] QWidget* layoutRoot() const noexcept;
    /** Returns the single client-area left-edge affordance for title-bar composition. */
    [[nodiscard]] QList<QWidget*> windowEdgeHandles() const;

    /** Defines the coordinate space represented by the layout chooser. */
    bool setLayoutRoot(QWidget* root);

    /**
     * Registers or rebinds a semantic panel.
     *
     * A non-positive number requests the next stable automatic number. Rebinding
     * an existing ID preserves its previous number, proportion, and expanded state.
     */
    bool registerPanel(QString id, QString title, QWidget* panel, int number = 0);
    bool unregisterPanel(QStringView id);
    void clearPanels();

    [[nodiscard]] QList<VPanelState> panelStates() const;
    [[nodiscard]] bool containsPanel(QStringView id) const;
    [[nodiscard]] bool isPanelExpanded(QStringView id) const;

    /** Animates visibility while guaranteeing that at least one live panel remains expanded. */
    bool setPanelExpanded(QStringView id, bool expanded);
    bool togglePanel(QStringView id);

  public slots:
    /** Opens the transient panel layout chooser with an exclusive popup input grab. */
    void showPanelChooser();
    void closePanelChooser();

  signals:
    void panelExpandedChanged(const QString& id, bool expanded);
    void panelLayoutChanged();

  private:
    std::unique_ptr<VPanelManagerPrivate> d_;
};

} // namespace vkui
