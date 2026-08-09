#include <vkui/widgets/vk/VkPanelRegistry.h>
#include <vkui/panel/VkSpatialNavigation.h>
#include <vkui/core/VkDiagnostics.h>

#include <QCoreApplication>
#include <QApplication>
#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QRect>
#include <QStackedWidget>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vkui::vk::widgets {
namespace {

struct PanelEntry final
{
    std::uint64_t registration = 0;
    const QWidget *widgetIdentity = nullptr;
    VkPanelDescriptor descriptor;
    QMetaObject::Connection widgetDestroyed;
    std::shared_ptr<std::atomic_bool> active;
};

[[nodiscard]] QString panelKey(
    const QStringView providerId,
    const QStringView surfaceId)
{
    return providerId.toString()
        + QChar::Null
        + surfaceId.toString();
}

[[nodiscard]] bool descriptorVisible(
    const VkPanelDescriptor &descriptor)
{
    if (descriptor.widget == nullptr) {
        return false;
    }
    return descriptor.isVisible
        ? descriptor.isVisible()
        : descriptor.widget->isVisible();
}

[[nodiscard]] QRect globalPanelGeometry(
    const VkPanelDescriptor &descriptor)
{
    QWidget *widget = descriptor.widget.data();
    if (widget == nullptr) {
        return {};
    }
    return QRect(
        widget->mapToGlobal(QPoint(0, 0)),
        widget->size());
}

} // namespace

struct VkPanelLease::State final
    : std::enable_shared_from_this<VkPanelLease::State>
{
    QHash<std::uint64_t, PanelEntry> entries;
    QHash<QString, std::uint64_t> identities;
    QHash<vkui::vk::WindowId, std::uint64_t> windows;
    QHash<const QWidget *, std::uint64_t> widgets;
    std::uint64_t nextRegistration = 1;
    QThread *thread = QThread::currentThread();
    QMutex lifecycleMutex;
    QObject *executor = new QObject;
    bool shuttingDown = false;

    State()
    {
        Q_ASSERT_X(
            QCoreApplication::instance() == nullptr
                || thread
                    == QCoreApplication::instance()->thread(),
            "VkPanelRegistry",
            "The panel registry must be created on the GUI thread.");
        executor->setObjectName(
            QStringLiteral("vkPanelRegistryExecutor"));
    }

    ~State()
    {
        // VkPanelRegistry::shutdown() owns the thread-affine QObject. State
        // itself may be released by the last lease on any thread afterwards.
        Q_ASSERT(executor == nullptr);
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return QThread::currentThread() == thread;
    }

    void removeOnOwnerThread(
        const std::uint64_t registration)
    {
        Q_ASSERT(isOwnerThread());
        const auto found = entries.find(registration);
        if (found == entries.end()) {
            return;
        }
        const VkPanelDescriptor descriptor = found->descriptor;
        found->active->store(
            false,
            std::memory_order_release);
        QObject::disconnect(found->widgetDestroyed);
        identities.remove(panelKey(
            found->descriptor.providerId,
            found->descriptor.surfaceId));
        windows.remove(found->descriptor.window);
        widgets.remove(found->widgetIdentity);
        entries.erase(found);
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Info,
            QStringLiteral("vkpanel.registry"),
            QStringLiteral("unregister"),
            QStringLiteral("Panel registration released"),
            QJsonObject{
                {QStringLiteral("registration"),
                 static_cast<qint64>(registration)},
                {QStringLiteral("window"),
                 static_cast<qint64>(descriptor.window)},
                {QStringLiteral("provider"), descriptor.providerId},
                {QStringLiteral("surface"), descriptor.surfaceId},
            });
    }

    void requestRemove(
        const std::uint64_t registration)
    {
        if (isOwnerThread()) {
            removeOnOwnerThread(registration);
            return;
        }

        const std::weak_ptr<State> weak =
            weak_from_this();
        QMutexLocker lock(&lifecycleMutex);
        if (shuttingDown || executor == nullptr) {
            return;
        }
        // The lifecycle mutex prevents the GUI thread from deleting executor
        // between this check and Qt taking ownership of the queued functor.
        (void)QMetaObject::invokeMethod(
            executor,
            [weak, registration] {
                if (const auto state = weak.lock()) {
                    state->removeOnOwnerThread(
                        registration);
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
            ownedExecutor =
                std::exchange(executor, nullptr);
        }

        // Deleting the context also discards queued removals. The maps are
        // then cleared exactly once below, on their owning GUI thread.
        delete ownedExecutor;
        for (const auto &entry :
             std::as_const(entries)) {
            entry.active->store(
                false,
                std::memory_order_release);
            QObject::disconnect(
                entry.widgetDestroyed);
        }
        entries.clear();
        identities.clear();
        windows.clear();
        widgets.clear();
    }
};

VkPanelLease::VkPanelLease(
    std::weak_ptr<State> state,
    const std::uint64_t registration,
    std::shared_ptr<std::atomic_bool> active)
    : m_state(std::move(state))
    , m_registration(registration)
    , m_active(std::move(active))
{
}

VkPanelLease::~VkPanelLease()
{
    reset();
}

VkPanelLease::VkPanelLease(
    VkPanelLease &&other) noexcept
    : m_state(std::move(other.m_state))
    , m_registration(std::exchange(
          other.m_registration, 0))
    , m_active(std::move(other.m_active))
{
}

VkPanelLease &VkPanelLease::operator=(
    VkPanelLease &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    m_state = std::move(other.m_state);
    m_registration = std::exchange(
        other.m_registration, 0);
    m_active = std::move(other.m_active);
    return *this;
}

void VkPanelLease::reset()
{
    if (m_registration == 0) {
        return;
    }
    if (m_active != nullptr) {
        m_active->store(
            false,
            std::memory_order_release);
    }
    if (const auto state = m_state.lock()) {
        state->requestRemove(m_registration);
    }
    m_registration = 0;
    m_state.reset();
    m_active.reset();
}

VkPanelLease::operator bool() const noexcept
{
    return m_registration != 0
        && !m_state.expired()
        && m_active != nullptr
        && m_active->load(
            std::memory_order_acquire);
}

VkPanelRegistry::VkPanelRegistry()
    : m_state(std::make_shared<VkPanelLease::State>())
{
}

VkPanelRegistry::~VkPanelRegistry()
{
    if (m_state != nullptr) {
        m_state->shutdown();
    }
}

VkPanelLease VkPanelRegistry::registerPanel(
    VkPanelDescriptor descriptor,
    QString *const error)
{
    const QJsonObject identityFields{
        {QStringLiteral("window"),
         static_cast<qint64>(descriptor.window)},
        {QStringLiteral("provider"), descriptor.providerId},
        {QStringLiteral("surface"), descriptor.surfaceId},
    };
    const auto fail =
        [error, identityFields](const QString &message) {
            if (error != nullptr) {
                *error = message;
            }
            vkui::writeDiagnostic(
                vkui::DiagnosticLevel::Error,
                QStringLiteral("vkpanel.registry"),
                QStringLiteral("register.rejected"),
                message,
                identityFields);
            return VkPanelLease{};
        };
    if (!m_state->isOwnerThread()) {
        Q_ASSERT_X(
            false,
            "VkPanelRegistry::registerPanel",
            "Panels must be registered on the registry's GUI thread.");
        return fail(QStringLiteral(
            "Panels must be registered on the registry's GUI thread."));
    }
    if (descriptor.window == 0) {
        return fail(QStringLiteral(
            "A panel requires a non-zero VkCore WindowId."));
    }
    if (descriptor.providerId.isEmpty()
        || descriptor.surfaceId.isEmpty()
        || descriptor.providerId.contains(QChar::Null)
        || descriptor.surfaceId.contains(QChar::Null)) {
        return fail(QStringLiteral(
            "A panel requires non-empty provider and surface identifiers "
            "without null characters."));
    }
    if (descriptor.widget == nullptr) {
        return fail(QStringLiteral(
            "A panel requires a live host widget."));
    }
    const QString identity = panelKey(
        descriptor.providerId,
        descriptor.surfaceId);
    if (const auto existingIdentity =
            m_state->identities.constFind(identity);
        existingIdentity != m_state->identities.cend()) {
        const auto existing =
            m_state->entries.constFind(*existingIdentity);
        if (existing != m_state->entries.cend()
            && (existing->descriptor.widget == nullptr
                || !existing->active->load(
                    std::memory_order_acquire))) {
            m_state->removeOnOwnerThread(
                *existingIdentity);
        } else {
            return fail(QStringLiteral(
                "The panel identity is already registered."));
        }
    }
    if (const auto existingWindow =
            m_state->windows.constFind(descriptor.window);
        existingWindow != m_state->windows.cend()) {
        const auto existing =
            m_state->entries.constFind(*existingWindow);
        if (existing != m_state->entries.cend()
            && (existing->descriptor.widget == nullptr
                || !existing->active->load(
                    std::memory_order_acquire))) {
            m_state->removeOnOwnerThread(
                *existingWindow);
        } else {
            return fail(QStringLiteral(
                "The VkCore window is already mounted by another panel."));
        }
    }
    if (const auto existingWidget =
            m_state->widgets.constFind(
                descriptor.widget.data());
        existingWidget != m_state->widgets.cend()) {
        const auto existing =
            m_state->entries.constFind(
                *existingWidget);
        if (existing != m_state->entries.cend()
            && (existing->descriptor.widget == nullptr
                || !existing->active->load(
                    std::memory_order_acquire))) {
            m_state->removeOnOwnerThread(
                *existingWidget);
        } else {
            return fail(QStringLiteral(
                "The host widget is already mounted by another panel."));
        }
    }

    std::uint64_t registration = 0;
    do {
        registration =
            m_state->nextRegistration++;
        if (m_state->nextRegistration == 0) {
            m_state->nextRegistration = 1;
        }
    } while (registration == 0
             || m_state->entries.contains(
                 registration));
    auto active =
        std::make_shared<std::atomic_bool>(
            true);
    auto inserted = m_state->entries.insert(
        registration,
        PanelEntry{
            registration,
            descriptor.widget.data(),
            std::move(descriptor),
            {},
            active});
    auto &storedEntry = inserted.value();
    const auto &stored = storedEntry.descriptor;
    m_state->identities.insert(
        panelKey(stored.providerId, stored.surfaceId),
        registration);
    m_state->windows.insert(
        stored.window,
        registration);
    m_state->widgets.insert(
        storedEntry.widgetIdentity,
        registration);
    const std::weak_ptr<VkPanelLease::State> weak =
        m_state;
    storedEntry.widgetDestroyed = QObject::connect(
        stored.widget.data(),
        &QObject::destroyed,
        m_state->executor,
        [weak, registration] {
            if (const auto state = weak.lock()) {
                state->removeOnOwnerThread(
                    registration);
            }
        });
    if (error != nullptr) {
        error->clear();
    }
    vkui::writeDiagnostic(
        vkui::DiagnosticLevel::Info,
        QStringLiteral("vkpanel.registry"),
        QStringLiteral("register"),
        QStringLiteral("Panel registered"),
        QJsonObject{
            {QStringLiteral("registration"),
             static_cast<qint64>(registration)},
            {QStringLiteral("window"),
             static_cast<qint64>(stored.window)},
            {QStringLiteral("provider"), stored.providerId},
            {QStringLiteral("surface"), stored.surfaceId},
        });
    return VkPanelLease{
        m_state,
        registration,
        std::move(active)};
}

bool VkPanelRegistry::rebindPanel(
    const vkui::vk::WindowId window,
    VkPanelDescriptor descriptor,
    QString *const error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!m_state->isOwnerThread()) {
        return fail(QStringLiteral(
            "Panels must be rebound on the registry's GUI thread."));
    }
    const auto registration = m_state->windows.constFind(window);
    if (window == 0 || registration == m_state->windows.cend()) {
        return fail(QStringLiteral(
            "The semantic panel window is not registered."));
    }
    auto found = m_state->entries.find(*registration);
    if (found == m_state->entries.end()
        || !found->active->load(std::memory_order_acquire)) {
        return fail(QStringLiteral(
            "The semantic panel registration is no longer active."));
    }
    if (descriptor.widget == nullptr) {
        return fail(QStringLiteral(
            "A rebound panel requires a live host widget."));
    }

    // Identity belongs to the lease, not to the replaceable renderer. Ignore
    // caller-provided identity fields so a projection swap cannot silently
    // mutate plugin ownership or collide with another surface.
    descriptor.window = window;
    descriptor.providerId = found->descriptor.providerId;
    descriptor.surfaceId = found->descriptor.surfaceId;
    if (descriptor.title.isEmpty()) {
        descriptor.title = found->descriptor.title;
    }
    const QWidget *const replacement = descriptor.widget.data();
    if (const auto owner = m_state->widgets.constFind(replacement);
        owner != m_state->widgets.cend() && *owner != *registration) {
        return fail(QStringLiteral(
            "The replacement widget already belongs to another panel."));
    }

    QObject::disconnect(found->widgetDestroyed);
    m_state->widgets.remove(found->widgetIdentity);
    found->widgetIdentity = replacement;
    found->descriptor = std::move(descriptor);
    const std::weak_ptr<VkPanelLease::State> weak = m_state;
    found->widgetDestroyed = QObject::connect(
        found->descriptor.widget.data(),
        &QObject::destroyed,
        m_state->executor,
        [weak, registration = *registration] {
            if (const auto state = weak.lock()) {
                state->removeOnOwnerThread(registration);
            }
        });
    m_state->widgets.insert(replacement, *registration);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::optional<VkPanelDescriptor>
VkPanelRegistry::panel(
    const QStringView providerId,
    const QStringView surfaceId) const
{
    if (!m_state->isOwnerThread()) {
        Q_ASSERT_X(
            false,
            "VkPanelRegistry::panel",
            "Panel queries must run on the registry's GUI thread.");
        return std::nullopt;
    }
    const auto identity = m_state->identities.constFind(
        panelKey(providerId, surfaceId));
    if (identity == m_state->identities.cend()) {
        return std::nullopt;
    }
    const auto entry =
        m_state->entries.constFind(*identity);
    return entry == m_state->entries.cend()
        || entry->descriptor.widget == nullptr
        || !entry->active->load(
            std::memory_order_acquire)
        ? std::nullopt
        : std::optional<VkPanelDescriptor>(
              entry->descriptor);
}

std::optional<VkPanelDescriptor>
VkPanelRegistry::panel(const vkui::vk::WindowId window) const
{
    if (!m_state->isOwnerThread()) {
        Q_ASSERT_X(
            false,
            "VkPanelRegistry::panel",
            "Panel queries must run on the registry's GUI thread.");
        return std::nullopt;
    }
    const auto registration =
        m_state->windows.constFind(window);
    if (registration == m_state->windows.cend()) {
        return std::nullopt;
    }
    const auto entry =
        m_state->entries.constFind(*registration);
    return entry == m_state->entries.cend()
        || entry->descriptor.widget == nullptr
        || !entry->active->load(
            std::memory_order_acquire)
        ? std::nullopt
        : std::optional<VkPanelDescriptor>(
              entry->descriptor);
}

std::optional<VkPanelDescriptor>
VkPanelRegistry::panelFor(
    const QObject *object) const
{
    if (!m_state->isOwnerThread()) {
        Q_ASSERT_X(
            false,
            "VkPanelRegistry::panelFor",
            "Panel queries must run on the registry's GUI thread.");
        return std::nullopt;
    }
    for (const QObject *candidate = object;
         candidate != nullptr;
         candidate = candidate->parent()) {
        const auto *candidateWidget =
            qobject_cast<const QWidget *>(candidate);
        if (candidateWidget == nullptr) {
            continue;
        }
        const auto registration =
            m_state->widgets.constFind(
                candidateWidget);
        if (registration
            == m_state->widgets.cend()) {
            continue;
        }
        const auto entry =
            m_state->entries.constFind(
                *registration);
        if (entry != m_state->entries.cend()
            && entry->descriptor.widget != nullptr
            && entry->active->load(
                std::memory_order_acquire)) {
            return entry->descriptor;
        }
    }
    return std::nullopt;
}

QVector<VkPanelDescriptor>
VkPanelRegistry::panels() const
{
    if (!m_state->isOwnerThread()) {
        Q_ASSERT_X(
            false,
            "VkPanelRegistry::panels",
            "Panel queries must run on the registry's GUI thread.");
        return {};
    }
    QVector<VkPanelDescriptor> result;
    result.reserve(m_state->entries.size());
    for (const auto &entry :
         std::as_const(m_state->entries)) {
        if (entry.descriptor.widget != nullptr
            && entry.active->load(
                std::memory_order_acquire)) {
            result.push_back(entry.descriptor);
        }
    }
    std::ranges::sort(
        result,
        [](const VkPanelDescriptor &left,
           const VkPanelDescriptor &right) {
            if (left.providerId != right.providerId) {
                return left.providerId < right.providerId;
            }
            return left.surfaceId < right.surfaceId;
        });
    return result;
}

bool VkPanelRegistry::toggle(
    const QStringView providerId,
    const QStringView surfaceId)
{
    const auto descriptor =
        panel(providerId, surfaceId);
    if (!descriptor || descriptor->widget == nullptr) {
        return false;
    }
    const bool currentlyVisible =
        descriptorVisible(*descriptor);
    if (descriptor->widget == nullptr) {
        return false;
    }
    const bool requested = !currentlyVisible;
    const bool committed = descriptor->setVisible
        ? descriptor->setVisible(requested)
        : (descriptor->widget->setVisible(requested), true);
    vkui::writeDiagnostic(
        committed
            ? vkui::DiagnosticLevel::Debug
            : vkui::DiagnosticLevel::Error,
        QStringLiteral("vkpanel.registry"),
        QStringLiteral("toggle"),
        committed
            ? QStringLiteral("Panel visibility toggled")
            : QStringLiteral("Panel visibility toggle rejected"),
        QJsonObject{
            {QStringLiteral("window"),
             static_cast<qint64>(descriptor->window)},
            {QStringLiteral("provider"), descriptor->providerId},
            {QStringLiteral("surface"), descriptor->surfaceId},
            {QStringLiteral("visible"), requested},
        });
    return committed;
}

bool VkPanelRegistry::setVisible(
    const QStringView providerId,
    const QStringView surfaceId,
    const bool visible)
{
    const auto descriptor =
        panel(providerId, surfaceId);
    if (!descriptor || descriptor->widget == nullptr) {
        return false;
    }
    if (descriptorVisible(*descriptor) == visible) {
        return true;
    }
    if (descriptor->setVisible) {
        return descriptor->setVisible(visible);
    }
    descriptor->widget->setVisible(visible);
    return descriptor->widget != nullptr
        && descriptor->widget->isVisible() == visible;
}

bool VkPanelRegistry::focus(
    const QStringView providerId,
    const QStringView surfaceId)
{
    const auto descriptor =
        panel(providerId, surfaceId);
    return descriptor
        ? focusWindow(descriptor->window)
        : false;
}

bool VkPanelRegistry::focusWindow(
    const vkui::vk::WindowId window)
{
    const auto descriptor = panel(window);
    const auto widgetFields = [window](
                                  const VkPanelDescriptor *const panel,
                                  const QString &stage,
                                  const QString &reason = {}) {
        QWidget *const widget = panel == nullptr
            ? nullptr
            : panel->widget.data();
        QWidget *const focused = QApplication::focusWidget();
        QJsonObject fields{
            {QStringLiteral("window"), static_cast<qint64>(window)},
            {QStringLiteral("stage"), stage},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("provider"),
             panel == nullptr ? QString{} : panel->providerId},
            {QStringLiteral("surface"),
             panel == nullptr ? QString{} : panel->surfaceId},
            {QStringLiteral("widget"),
             widget == nullptr ? QString{} : widget->objectName()},
            {QStringLiteral("widget_class"),
             widget == nullptr
                 ? QString{}
                 : QString::fromLatin1(
                       widget->metaObject()->className())},
            {QStringLiteral("widget_visible"),
             widget != nullptr && widget->isVisible()},
            {QStringLiteral("widget_enabled"),
             widget != nullptr && widget->isEnabled()},
            {QStringLiteral("focus_callback"),
             panel != nullptr
                 && static_cast<bool>(panel->focus)},
            {QStringLiteral("focused_widget"),
             focused == nullptr ? QString{} : focused->objectName()},
            {QStringLiteral("focused_class"),
             focused == nullptr
                 ? QString{}
                 : QString::fromLatin1(
                       focused->metaObject()->className())},
        };
        if (auto *const stack = qobject_cast<QStackedWidget *>(widget)) {
            QWidget *const renderer = stack->currentWidget();
            fields.insert(
                QStringLiteral("renderer"),
                renderer == nullptr ? QString{} : renderer->objectName());
            fields.insert(
                QStringLiteral("renderer_class"),
                renderer == nullptr
                    ? QString{}
                    : QString::fromLatin1(
                          renderer->metaObject()->className()));
            fields.insert(
                QStringLiteral("renderer_visible"),
                renderer != nullptr && renderer->isVisible());
            fields.insert(
                QStringLiteral("renderer_enabled"),
                renderer != nullptr && renderer->isEnabled());
        }
        return fields;
    };
    const auto reject = [&widgetFields, descriptor](
                            const QString &stage,
                            const QString &reason) {
        vkui::writeDiagnostic(
            vkui::DiagnosticLevel::Warning,
            QStringLiteral("vkpanel.focus"),
            QStringLiteral("rejected"),
            QStringLiteral("Panel focus transaction rejected"),
            widgetFields(
                descriptor ? &*descriptor : nullptr,
                stage,
                reason));
        return false;
    };
    if (!descriptor || descriptor->widget == nullptr) {
        return reject(
            QStringLiteral("lookup"),
            QStringLiteral("descriptor-or-widget-missing"));
    }
    if (!descriptorVisible(*descriptor)
        && (descriptor->setVisible == nullptr
            || !descriptor->setVisible(true))) {
        return reject(
            QStringLiteral("visibility"),
            QStringLiteral("visibility-restore-rejected"));
    }
    if (descriptor->widget == nullptr) {
        return reject(
            QStringLiteral("visibility"),
            QStringLiteral("widget-destroyed-during-restore"));
    }
    if (!descriptor->widget->isVisible()
        || !descriptor->widget->isEnabled()) {
        return reject(
            QStringLiteral("readiness"),
            QStringLiteral("widget-not-visible-or-enabled"));
    }
    if (descriptor->focus) {
        const bool committed = descriptor->focus();
        vkui::writeDiagnostic(
            committed
                ? vkui::DiagnosticLevel::Debug
                : vkui::DiagnosticLevel::Warning,
            QStringLiteral("vkpanel.focus"),
            committed
                ? QStringLiteral("committed")
                : QStringLiteral("rejected"),
            committed
                ? QStringLiteral("Panel focus callback committed")
                : QStringLiteral("Panel focus callback rejected"),
            widgetFields(
                &*descriptor,
                QStringLiteral("callback"),
                committed
                    ? QString{}
                    : QStringLiteral("focus-callback-returned-false")));
        return committed;
    }
    descriptor->widget->setFocus(
        Qt::ShortcutFocusReason);
    // FocusIn is asynchronous on several native window systems and is not
    // available at all in Qt's offscreen platform. A live focusable widget
    // accepting setFocus() is the command commit point; FocusIn later
    // confirms renderer state without deciding VkCore's active window.
    const bool committed = descriptor->widget != nullptr
        && descriptor->widget->isVisible()
        && descriptor->widget->isEnabled();
    vkui::writeDiagnostic(
        committed
            ? vkui::DiagnosticLevel::Debug
            : vkui::DiagnosticLevel::Warning,
        QStringLiteral("vkpanel.focus"),
        committed
            ? QStringLiteral("committed")
            : QStringLiteral("rejected"),
        committed
            ? QStringLiteral("Panel widget focus committed")
            : QStringLiteral("Panel widget focus rejected"),
        widgetFields(
            &*descriptor,
            QStringLiteral("widget"),
            committed
                ? QString{}
                : QStringLiteral("widget-not-ready-after-focus")));
    return committed;
}

bool VkPanelRegistry::navigateWindow(
    const vkui::vk::WindowId window,
    const int horizontalDirection,
    const int verticalDirection)
{
    const auto descriptor = panel(window);
    if (!descriptor
        || descriptor->widget == nullptr
        || !descriptor->navigate
        || (horizontalDirection == 0
            && verticalDirection == 0)) {
        return false;
    }
    return descriptor->navigate(
        std::clamp(horizontalDirection, -1, 1),
        std::clamp(verticalDirection, -1, 1));
}

std::optional<vkui::vk::WindowId>
VkPanelRegistry::adjacentWindow(
    const vkui::vk::WindowId origin,
    const int horizontalDirection,
    const int verticalDirection) const
{
    const auto originPanel = panel(origin);
    if (!originPanel
        || originPanel->widget == nullptr
        || (horizontalDirection == 0
            && verticalDirection == 0)) {
        return std::nullopt;
    }
    const QRect originGeometry =
        globalPanelGeometry(*originPanel);
    if (!originGeometry.isValid()) {
        return std::nullopt;
    }
    QVector<vkui::panel::VkSpatialWindow> candidates;
    candidates.reserve(panels().size());
    for (const auto &candidate : panels()) {
        if (candidate.window == origin
            || !descriptorVisible(candidate)) {
            continue;
        }
        const QRect geometry =
            globalPanelGeometry(candidate);
        if (!geometry.isValid()) {
            continue;
        }
        candidates.push_back(
            vkui::panel::VkSpatialWindow{
                candidate.window,
                QRectF(geometry)});
    }
    return vkui::panel::nearestSpatialWindow(
        origin,
        QRectF(originGeometry),
        candidates,
        horizontalDirection,
        verticalDirection);
}

} // namespace vkui::vk::widgets
