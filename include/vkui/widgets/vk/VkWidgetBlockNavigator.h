#pragma once

class QWidget;

namespace vkui::vk::widgets {

/**
 * Semantic adapter from an arbitrary Qt control subtree to one VK block.
 *
 * VkCore owns the key grammar and emits navigation/activation intents.  This
 * adapter is deliberately key-agnostic: it projects those intents onto item
 * views and ordinary controls without synthesizing QKeyEvent instances.
 * VkFiles, Reader popovers, menus and Preferences can therefore share one
 * input contract while retaining their native model/view implementations.
 */
class VkWidgetBlockNavigator final
{
public:
    [[nodiscard]] static bool focus(QWidget *root);
    /** Seeds a block's semantic cursor at one explicit descendant control. */
    [[nodiscard]] static bool focus(
        QWidget *root,
        QWidget *preferredControl);
    [[nodiscard]] static bool navigate(
        QWidget *root,
        int horizontal,
        int vertical);
    /** Applies one batched half-page movement without intermediate repaint. */
    [[nodiscard]] static bool navigatePage(
        QWidget *root,
        int direction,
        int rows);
    /** Implements gg/G for generic model/view and control surfaces. */
    [[nodiscard]] static bool navigateBoundary(
        QWidget *root,
        bool last,
        int count = 1,
        bool countWasExplicit = false);
    [[nodiscard]] static bool activate(QWidget *root);
    /** Visible semantic rows used by Core's half-page navigation grammar. */
    [[nodiscard]] static int viewportRows(QWidget *root);
};

} // namespace vkui::vk::widgets
