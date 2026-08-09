#include <vkui/widgets/vk/VkBlockRegistry.h>

#include <QApplication>
#include <QCoreApplication>
#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>

namespace vkui::vk::widgets {
namespace {

struct BlockEntry final
{
    std::uint64_t registration = 0;
    const QWidget *widgetIdentity = nullptr;
    VkBlockDescriptor descriptor;
    QMetaObject::Connection destroyed;
    std::shared_ptr<std::atomic_bool> active;
};

[[nodiscard]] bool validDescriptor(
    const VkBlockDescriptor &descriptor)
{
    return !descriptor.id.isEmpty()
        && !descriptor.panelId.isEmpty()
        && !descriptor.id.contains(QChar::Null)
        && !descriptor.panelId.contains(QChar::Null)
        && descriptor.widget != nullptr;
}

[[nodiscard]] bool enabled(
    const VkBlockDescriptor &descriptor)
{
    return descriptor.widget != nullptr
        && descriptor.widget->isVisible()
        && descriptor.widget->isEnabled()
        && (!descriptor.isEnabled
            || descriptor.isEnabled());
}

[[nodiscard]] QRect globalGeometry(
    const VkBlockDescriptor &descriptor)
{
    QWidget *const widget = descriptor.widget.data();
    return widget == nullptr
        ? QRect{}
        : QRect(widget->mapToGlobal(QPoint()), widget->size());
}

[[nodiscard]] bool horizontalDirection(
    const vkui::panel::VkSpatialDirection direction)
{
    return direction == vkui::panel::VkSpatialDirection::Left
        || direction == vkui::panel::VkSpatialDirection::Right;
}

} // namespace

struct VkBlockLease::State final
    : std::enable_shared_from_this<VkBlockLease::State>
{
    QHash<std::uint64_t, BlockEntry> entries;
    QHash<QString, std::uint64_t> identities;
    QHash<const QWidget *, std::uint64_t> widgets;
    std::uint64_t nextRegistration = 1;
    QThread *thread = QThread::currentThread();
    QObject *executor = new QObject;
    QMutex lifecycleMutex;
    bool shuttingDown = false;

    State()
    {
        Q_ASSERT_X(
            QCoreApplication::instance() == nullptr
                || thread == QCoreApplication::instance()->thread(),
            "VkBlockRegistry",
            "The block registry must be created on the GUI thread.");
        executor->setObjectName(QStringLiteral("vkBlockRegistryExecutor"));
    }

    ~State()
    {
        Q_ASSERT(executor == nullptr);
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return QThread::currentThread() == thread;
    }

    void removeOnOwnerThread(const std::uint64_t registration)
    {
        Q_ASSERT(isOwnerThread());
        const auto found = entries.find(registration);
        if (found == entries.end()) {
            return;
        }
        found->active->store(false, std::memory_order_release);
        QObject::disconnect(found->destroyed);
        identities.remove(found->descriptor.id);
        widgets.remove(found->widgetIdentity);
        entries.erase(found);
    }

    void requestRemove(const std::uint64_t registration)
    {
        if (isOwnerThread()) {
            removeOnOwnerThread(registration);
            return;
        }
        const std::weak_ptr<State> weak = weak_from_this();
        QMutexLocker lock(&lifecycleMutex);
        if (shuttingDown || executor == nullptr) {
            return;
        }
        (void)QMetaObject::invokeMethod(
            executor,
            [weak, registration] {
                if (const auto state = weak.lock()) {
                    state->removeOnOwnerThread(registration);
                }
            },
            Qt::QueuedConnection);
    }

    void shutdown()
    {
        Q_ASSERT(isOwnerThread());
        QObject *ownedExecutor = nullptr;
        {
            QMutexLocker lock(&lifecycleMutex);
            shuttingDown = true;
            ownedExecutor = std::exchange(executor, nullptr);
        }
        delete ownedExecutor;
        for (const BlockEntry &entry : std::as_const(entries)) {
            entry.active->store(false, std::memory_order_release);
            QObject::disconnect(entry.destroyed);
        }
        entries.clear();
        identities.clear();
        widgets.clear();
    }
};

VkBlockLease::VkBlockLease(
    std::weak_ptr<State> state,
    const std::uint64_t registration,
    std::shared_ptr<std::atomic_bool> active)
    : m_state(std::move(state))
    , m_registration(registration)
    , m_active(std::move(active))
{
}

VkBlockLease::~VkBlockLease()
{
    reset();
}

VkBlockLease::VkBlockLease(VkBlockLease &&other) noexcept
    : m_state(std::move(other.m_state))
    , m_registration(std::exchange(other.m_registration, 0))
    , m_active(std::move(other.m_active))
{
}

VkBlockLease &VkBlockLease::operator=(VkBlockLease &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    m_state = std::move(other.m_state);
    m_registration = std::exchange(other.m_registration, 0);
    m_active = std::move(other.m_active);
    return *this;
}

void VkBlockLease::reset()
{
    if (m_registration == 0) {
        return;
    }
    if (m_active != nullptr) {
        m_active->store(false, std::memory_order_release);
    }
    if (const auto state = m_state.lock()) {
        state->requestRemove(m_registration);
    }
    m_state.reset();
    m_registration = 0;
    m_active.reset();
}

VkBlockLease::operator bool() const noexcept
{
    return m_registration != 0
        && !m_state.expired()
        && m_active != nullptr
        && m_active->load(std::memory_order_acquire);
}

VkBlockRegistry::VkBlockRegistry()
    : m_state(std::make_shared<VkBlockLease::State>())
{
}

VkBlockRegistry::~VkBlockRegistry()
{
    if (m_state != nullptr) {
        m_state->shutdown();
    }
}

VkBlockLease VkBlockRegistry::registerBlock(
    VkBlockDescriptor descriptor,
    QString *const error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return VkBlockLease{};
    };
    if (!m_state->isOwnerThread()) {
        return fail(QStringLiteral("Blocks must be registered on the GUI thread."));
    }
    if (!validDescriptor(descriptor)) {
        return fail(QStringLiteral("A block requires stable IDs and a live widget."));
    }
    if (m_state->identities.contains(descriptor.id)) {
        return fail(QStringLiteral("The block identity is already registered."));
    }
    if (m_state->widgets.contains(descriptor.widget.data())) {
        return fail(QStringLiteral("The widget already belongs to another block."));
    }
    std::uint64_t registration = 0;
    do {
        registration = m_state->nextRegistration++;
        if (m_state->nextRegistration == 0) {
            m_state->nextRegistration = 1;
        }
    } while (registration == 0 || m_state->entries.contains(registration));

    auto active = std::make_shared<std::atomic_bool>(true);
    BlockEntry entry;
    entry.registration = registration;
    entry.widgetIdentity = descriptor.widget.data();
    entry.descriptor = std::move(descriptor);
    entry.active = active;
    const std::weak_ptr<VkBlockLease::State> weak = m_state;
    entry.destroyed = QObject::connect(
        entry.descriptor.widget.data(),
        &QObject::destroyed,
        m_state->executor,
        [weak, registration] {
            if (const auto state = weak.lock()) {
                state->removeOnOwnerThread(registration);
            }
        },
        Qt::DirectConnection);
    m_state->identities.insert(entry.descriptor.id, registration);
    m_state->widgets.insert(entry.widgetIdentity, registration);
    m_state->entries.insert(registration, std::move(entry));
    if (error != nullptr) {
        error->clear();
    }
    return VkBlockLease(m_state, registration, std::move(active));
}

std::optional<VkBlockDescriptor> VkBlockRegistry::block(
    const QStringView id) const
{
    if (!m_state->isOwnerThread()) {
        return std::nullopt;
    }
    const auto registration = m_state->identities.constFind(id.toString());
    if (registration == m_state->identities.cend()) {
        return std::nullopt;
    }
    const auto found = m_state->entries.constFind(*registration);
    return found == m_state->entries.cend()
        || found->descriptor.widget == nullptr
        || !found->active->load(std::memory_order_acquire)
        ? std::nullopt
        : std::optional<VkBlockDescriptor>(found->descriptor);
}

std::optional<VkBlockDescriptor> VkBlockRegistry::blockFor(
    const QObject *object) const
{
    if (!m_state->isOwnerThread()) {
        return std::nullopt;
    }
    while (object != nullptr) {
        const auto registration = m_state->widgets.constFind(
            qobject_cast<const QWidget *>(object));
        if (registration != m_state->widgets.cend()) {
            const auto found = m_state->entries.constFind(*registration);
            if (found != m_state->entries.cend()
                && found->active->load(std::memory_order_acquire)) {
                return found->descriptor;
            }
        }
        object = object->parent();
    }
    return std::nullopt;
}

QVector<VkBlockDescriptor> VkBlockRegistry::blocks(
    const QStringView panelId) const
{
    QVector<VkBlockDescriptor> result;
    if (!m_state->isOwnerThread()) {
        return result;
    }
    result.reserve(m_state->entries.size());
    for (const BlockEntry &entry : std::as_const(m_state->entries)) {
        if (entry.descriptor.widget != nullptr
            && entry.active->load(std::memory_order_acquire)
            && (panelId.isEmpty() || entry.descriptor.panelId == panelId)) {
            result.push_back(entry.descriptor);
        }
    }
    std::ranges::sort(
        result,
        [](const VkBlockDescriptor &left, const VkBlockDescriptor &right) {
            const QRect a = globalGeometry(left);
            const QRect b = globalGeometry(right);
            return std::tuple(a.top(), a.left(), left.id)
                < std::tuple(b.top(), b.left(), right.id);
        });
    return result;
}

bool VkBlockRegistry::focus(const QStringView id)
{
    const auto descriptor = block(id);
    if (!descriptor || !enabled(*descriptor)) {
        return false;
    }
    if (descriptor->focus) {
        return descriptor->focus();
    }
    descriptor->widget->setFocus(Qt::ShortcutFocusReason);
    return descriptor->widget->hasFocus();
}

bool VkBlockRegistry::focusAdjacent(
    const QStringView originId,
    const vkui::panel::VkSpatialDirection direction)
{
    const auto origin = block(originId);
    if (!origin || !enabled(*origin)) {
        return false;
    }
    const QRectF a(globalGeometry(*origin));
    qreal best = std::numeric_limits<qreal>::max();
    std::optional<QString> target;
    for (const VkBlockDescriptor &candidate : blocks(origin->panelId)) {
        if (candidate.id == originId || !enabled(candidate)) {
            continue;
        }
        const QRectF b(globalGeometry(candidate));
        if (!b.isValid()) {
            continue;
        }
        const qreal dx = b.center().x() - a.center().x();
        const qreal dy = b.center().y() - a.center().y();
        if ((direction == vkui::panel::VkSpatialDirection::Left && dx >= 0.0)
            || (direction == vkui::panel::VkSpatialDirection::Right && dx <= 0.0)
            || (direction == vkui::panel::VkSpatialDirection::Up && dy >= 0.0)
            || (direction == vkui::panel::VkSpatialDirection::Down && dy <= 0.0)) {
            continue;
        }
        const qreal primary = horizontalDirection(direction)
            ? std::abs(dx) : std::abs(dy);
        const qreal secondary = horizontalDirection(direction)
            ? std::abs(dy) : std::abs(dx);
        const qreal score = primary + secondary * 2.0;
        if (score < best) {
            best = score;
            target = candidate.id;
        }
    }
    if (target) {
        return focus(*target);
    }
    if (!origin->navigateBoundary) {
        return false;
    }
    const int horizontal =
        direction == vkui::panel::VkSpatialDirection::Left
        ? -1
        : direction == vkui::panel::VkSpatialDirection::Right
        ? 1
        : 0;
    const int vertical =
        direction == vkui::panel::VkSpatialDirection::Up
        ? -1
        : direction == vkui::panel::VkSpatialDirection::Down
        ? 1
        : 0;
    return origin->navigateBoundary(horizontal, vertical);
}

bool VkBlockRegistry::navigate(
    const QStringView id,
    const int horizontalValue,
    const int verticalValue)
{
    const auto descriptor = block(id);
    return descriptor && enabled(*descriptor)
        && descriptor->navigate
        && descriptor->navigate(
            (horizontalValue > 0) - (horizontalValue < 0),
            (verticalValue > 0) - (verticalValue < 0));
}

bool VkBlockRegistry::activate(const QStringView id)
{
    const auto descriptor = block(id);
    return descriptor && enabled(*descriptor)
        && descriptor->activate
        && descriptor->activate();
}

std::optional<QString> VkBlockRegistry::focusedBlock() const
{
    const auto descriptor = blockFor(QApplication::focusWidget());
    return descriptor
        ? std::optional<QString>(descriptor->id)
        : std::nullopt;
}

bool VkPreferencesBlockCatalog::setNavigation(
    VkBlockDescriptor descriptor,
    QString *const error)
{
    if (descriptor.id.isEmpty() || descriptor.panelId.isEmpty()
        || descriptor.widget == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("The preferences navigation block is invalid.");
        }
        return false;
    }
    descriptor.kind = VkBlockKind::Navigation;
    m_navigation = std::move(descriptor);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool VkPreferencesBlockCatalog::setPage(
    QString pageId,
    QVector<VkPreferenceBlock> blocksValue,
    QString *const error)
{
    if (pageId.isEmpty() || pageId.contains(QChar::Null)) {
        if (error != nullptr) {
            *error = QStringLiteral("The preferences page identity is invalid.");
        }
        return false;
    }
    QSet<QString> identities;
    if (m_navigation) {
        identities.insert(m_navigation->id);
    }
    for (const VkPreferenceBlock &existing : std::as_const(m_blocks)) {
        if (existing.pageId != pageId) {
            identities.insert(existing.block.id);
        }
    }
    for (VkPreferenceBlock &entry : blocksValue) {
        entry.pageId = pageId;
        entry.block.kind = VkBlockKind::Control;
        if (entry.block.id.isEmpty() || entry.block.panelId.isEmpty()
            || entry.block.widget == nullptr
            || identities.contains(entry.block.id)) {
            if (error != nullptr) {
                *error = QStringLiteral("A preferences control block is invalid or duplicated.");
            }
            return false;
        }
        identities.insert(entry.block.id);
    }
    removePage(pageId);
    m_blocks += std::move(blocksValue);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void VkPreferencesBlockCatalog::removePage(const QStringView pageId)
{
    m_blocks.erase(
        std::remove_if(
            m_blocks.begin(), m_blocks.end(),
            [pageId](const VkPreferenceBlock &entry) {
                return entry.pageId == pageId;
            }),
        m_blocks.end());
}

QVector<VkPreferenceBlock> VkPreferencesBlockCatalog::page(
    const QStringView pageId) const
{
    QVector<VkPreferenceBlock> result;
    for (const VkPreferenceBlock &entry : m_blocks) {
        if (entry.pageId == pageId) {
            result.push_back(entry);
        }
    }
    return result;
}

QVector<VkBlockDescriptor> VkPreferencesBlockCatalog::descriptors(
    const QStringView pageId) const
{
    QVector<VkBlockDescriptor> result;
    if (m_navigation) {
        result.push_back(*m_navigation);
    }
    for (const VkPreferenceBlock &entry : m_blocks) {
        if (entry.pageId == pageId) {
            result.push_back(entry.block);
        }
    }
    return result;
}

} // namespace vkui::vk::widgets
