#pragma once

#include <vkui/panel/VkPanelLayoutModel.h>

#include <QPointer>
#include <QString>
#include <QStringView>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QObject;
class QWidget;

namespace vkui::vk::widgets {

enum class VkBlockKind : std::uint8_t
{
    Editor,
    Navigation,
    List,
    Control,
    ReadOnly,
};

/**
 * One smallest keyboard-addressable section inside a panel.
 *
 * Ctrl+h/j/k/l changes blocks through live geometry. Once focused, ordinary
 * h/j/k/l and Enter are delegated to navigate/activate so a tree, list,
 * editor or preferences control can retain its native item semantics.
 */
struct VkBlockDescriptor final
{
    using NavigateCallback =
        std::function<bool(int horizontal, int vertical)>;

    QString id;
    QString panelId;
    QString title;
    VkBlockKind kind = VkBlockKind::Control;
    QPointer<QWidget> widget;
    std::function<bool()> isEnabled;
    std::function<bool()> focus;
    NavigateCallback navigate;
    std::function<bool()> activate;
    /** Optional continuation when spatial Ctrl-navigation has no neighbour. */
    NavigateCallback navigateBoundary;
};

class VkBlockRegistry;

class VkBlockLease final
{
public:
    VkBlockLease() = default;
    ~VkBlockLease();

    VkBlockLease(VkBlockLease &&other) noexcept;
    VkBlockLease &operator=(VkBlockLease &&other) noexcept;
    VkBlockLease(const VkBlockLease &) = delete;
    VkBlockLease &operator=(const VkBlockLease &) = delete;

    void reset();
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    friend class VkBlockRegistry;
    struct State;

    VkBlockLease(
        std::weak_ptr<State> state,
        std::uint64_t registration,
        std::shared_ptr<std::atomic_bool> active);

    std::weak_ptr<State> m_state;
    std::uint64_t m_registration = 0;
    std::shared_ptr<std::atomic_bool> m_active;
};

/** GUI-thread registry for block focus and local item actions. */
class VkBlockRegistry final
{
public:
    VkBlockRegistry();
    ~VkBlockRegistry();

    VkBlockRegistry(const VkBlockRegistry &) = delete;
    VkBlockRegistry &operator=(const VkBlockRegistry &) = delete;

    [[nodiscard]] VkBlockLease registerBlock(
        VkBlockDescriptor descriptor,
        QString *error = nullptr);
    [[nodiscard]] std::optional<VkBlockDescriptor> block(
        QStringView id) const;
    [[nodiscard]] std::optional<VkBlockDescriptor> blockFor(
        const QObject *object) const;
    [[nodiscard]] QVector<VkBlockDescriptor> blocks(
        QStringView panelId = {}) const;

    [[nodiscard]] bool focus(QStringView id);
    [[nodiscard]] bool focusAdjacent(
        QStringView originId,
        vkui::panel::VkSpatialDirection direction);
    [[nodiscard]] bool navigate(
        QStringView id,
        int horizontal,
        int vertical);
    [[nodiscard]] bool activate(QStringView id);

    /** Resolves QApplication::focusWidget() to a stable block identity. */
    [[nodiscard]] std::optional<QString> focusedBlock() const;

private:
    std::shared_ptr<VkBlockLease::State> m_state;
};

enum class VkPreferenceControlKind : std::uint8_t
{
    Navigation,
    Action,
    Toggle,
    Choice,
    Text,
    Numeric,
};

struct VkPreferenceBlock final
{
    QString pageId;
    VkPreferenceControlKind control =
        VkPreferenceControlKind::Action;
    VkBlockDescriptor block;
};

/**
 * Declarative preferences-to-block adapter.
 *
 * The left navigation is registered once; each page contributes its ordered
 * controls independently. Switching pages changes the returned snapshot,
 * never the identity or ownership of an already registered QWidget.
 */
class VkPreferencesBlockCatalog final
{
public:
    [[nodiscard]] bool setNavigation(
        VkBlockDescriptor descriptor,
        QString *error = nullptr);
    [[nodiscard]] bool setPage(
        QString pageId,
        QVector<VkPreferenceBlock> blocks,
        QString *error = nullptr);
    void removePage(QStringView pageId);

    [[nodiscard]] QVector<VkPreferenceBlock> page(
        QStringView pageId) const;
    [[nodiscard]] QVector<VkBlockDescriptor> descriptors(
        QStringView pageId) const;

private:
    std::optional<VkBlockDescriptor> m_navigation;
    QVector<VkPreferenceBlock> m_blocks;
};

} // namespace vkui::vk::widgets
