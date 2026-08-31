// SPDX-License-Identifier: MIT

#include "../animation/private/VkWidgetAnimation_p.h"
#include "private/VPanelEdgeHandleController_p.h"
#include "private/VPanelLayoutDialog_p.h"

#include <QHash>
#include <QPointer>
#include <QResizeEvent>
#include <QSplitterHandle>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vkui/widgets/controls/VSplitter.h>
#include <vkui/widgets/panels/VPanelManager.h>

namespace vkui {

namespace {

class VPanelAnimationSlot final : public QWidget {
  public:
    explicit VPanelAnimationSlot(QWidget* content) : QWidget(nullptr), content_(content) {
        Q_ASSERT(content_ != nullptr);
        setObjectName(QStringLiteral("vkuiPanelAnimationSlot"));
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(content_->sizePolicy());
    }

    void adoptContent() {
        if (content_ == nullptr) {
            return;
        }
        content_->setParent(this);
        content_->installEventFilter(this);
        content_->show();
        synchronizeMinimumConstraint();
        updateContentGeometry();
        updateGeometry();
    }

    void adoptContent(QWidget* content) {
        content_ = content;
        adoptContent();
    }

    [[nodiscard]] QWidget* releaseContent() {
        QWidget* content = content_.data();
        if (content == nullptr) {
            return nullptr;
        }
        content->removeEventFilter(this);
        content_.clear();
        content->hide();
        content->setParent(nullptr);
        setMinimumSize(0, 0);
        updateGeometry();
        return content;
    }

    void setConstraintRelaxed(const bool relaxed) {
        if (constraintRelaxed_ == relaxed) {
            return;
        }
        constraintRelaxed_ = relaxed;
        synchronizeMinimumConstraint();
        updateGeometry();
    }

    [[nodiscard]] QSize sizeHint() const override {
        if (content_ == nullptr) {
            return {};
        }
        QSize hint = content_->sizeHint();
        hint.setWidth(std::max(0, hint.width()));
        hint.setHeight(std::max(0, hint.height()));
        return hint.expandedTo(contentMinimumSize());
    }

    [[nodiscard]] QSize minimumSizeHint() const override {
        return constraintRelaxed_ ? QSize(0, 0) : contentMinimumSize();
    }

  protected:
    bool event(QEvent* event) override {
        const bool handled = QWidget::event(event);
        if (event != nullptr && event->type() == QEvent::LayoutRequest) {
            synchronizeMinimumConstraint();
            updateContentGeometry();
        }
        return handled;
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == content_ && event != nullptr) {
            switch (event->type()) {
            case QEvent::FontChange:
            case QEvent::StyleChange:
            case QEvent::LayoutRequest:
                synchronizeMinimumConstraint();
                updateContentGeometry();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        updateContentGeometry();
    }

  private:
    [[nodiscard]] QSize contentMinimumSize() const {
        if (content_ == nullptr) {
            return {};
        }
        QSize hint = content_->minimumSizeHint();
        hint.setWidth(std::max(0, hint.width()));
        hint.setHeight(std::max(0, hint.height()));
        return hint.expandedTo(content_->minimumSize());
    }

    void updateContentGeometry() {
        if (content_ == nullptr) {
            return;
        }
        // The slot supplies continuous splitter geometry below the content minimum. Keeping the
        // content at its own minimum and clipping it avoids mutating application-owned constraints.
        content_->setGeometry(QRect(QPoint(), size().expandedTo(contentMinimumSize())));
    }

    void synchronizeMinimumConstraint() {
        const QSize minimum = constraintRelaxed_ ? QSize(0, 0) : contentMinimumSize();
        if (minimumSize() != minimum) {
            setMinimumSize(minimum);
        }
    }

    QPointer<QWidget> content_;
    bool constraintRelaxed_ = false;
};

struct PanelBinding final {
    QString id;
    QString title;
    int number = 0;
    QPointer<QWidget> widget;
    QPointer<VPanelAnimationSlot> animationSlot;
    QRectF normalizedRect;
    bool expanded = true;
};

struct SplitterBinding final {
    QPointer<VSplitter> splitter;
    QList<int> expandedSizes;
    QMetaObject::Connection clickedConnection;
    QMetaObject::Connection movedConnection;
    QMetaObject::Connection destroyedConnection;
};

struct SplitterTransition final {
    QPointer<VSplitter> splitter;
    QList<int> startSizes;
    QList<int> targetSizes;
};

} // namespace

class VPanelManagerPrivate final {
  public:
    VPanelManagerPrivate(VPanelManager* owner, QWidget& host)
        : q(owner), window(&host), layoutAnimation(std::make_unique<VkWidgetAnimation>(&host)) {}

    [[nodiscard]] PanelBinding* binding(const QStringView id) {
        const auto iterator = panels.find(id.toString());
        return iterator == panels.end() ? nullptr : &iterator.value();
    }

    [[nodiscard]] const PanelBinding* binding(const QStringView id) const {
        const auto iterator = panels.constFind(id.toString());
        return iterator == panels.cend() ? nullptr : &iterator.value();
    }

    [[nodiscard]] int liveExpandedPanelCount() const {
        return static_cast<int>(
            std::count_if(order.cbegin(), order.cend(), [this](const QString& id) {
                const PanelBinding& panel = panels[id];
                return panel.widget != nullptr && panel.expanded;
            }));
    }

    [[nodiscard]] PanelBinding* firstLiveCollapsedPanel(const PanelBinding* excluded = nullptr) {
        const auto iterator =
            std::find_if(order.cbegin(), order.cend(), [this, excluded](const QString& id) {
                PanelBinding& panel = panels[id];
                return &panel != excluded && panel.widget != nullptr && !panel.expanded;
            });
        return iterator == order.cend() ? nullptr : &panels[*iterator];
    }

    [[nodiscard]] PanelBinding* restoreExpandedInvariant() {
        if (liveExpandedPanelCount() > 0) {
            return nullptr;
        }
        PanelBinding* replacement = firstLiveCollapsedPanel();
        if (replacement != nullptr) {
            replacement->expanded = true;
        }
        return replacement;
    }

    void attachAnimationSlot(PanelBinding& panel) {
        if (panel.widget == nullptr || panel.animationSlot != nullptr) {
            return;
        }
        auto* splitter = qobject_cast<VSplitter*>(panel.widget->parentWidget());
        const int index = splitter != nullptr ? splitter->indexOf(panel.widget) : -1;
        if (index < 0) {
            return;
        }

        auto* slot = new VPanelAnimationSlot(panel.widget);
        QWidget* replaced = splitter->replaceWidget(index, slot);
        if (replaced != panel.widget) {
            delete slot;
            return;
        }
        slot->adoptContent();
        slot->show();
        panel.animationSlot = slot;
    }

    void detachAnimationSlot(PanelBinding& panel) {
        VPanelAnimationSlot* slot = panel.animationSlot.data();
        QWidget* content = panel.widget.data();
        auto* splitter = slot != nullptr ? qobject_cast<VSplitter*>(slot->parentWidget()) : nullptr;
        const int index = splitter != nullptr ? splitter->indexOf(slot) : -1;
        if (slot == nullptr || content == nullptr || index < 0) {
            return;
        }

        const bool visible = slot->isVisible();
        QWidget* released = slot->releaseContent();
        QWidget* replaced =
            released != nullptr ? splitter->replaceWidget(index, released) : nullptr;
        if (replaced != slot) {
            if (released != nullptr) {
                slot->adoptContent(released);
            }
            return;
        }
        panel.animationSlot.clear();
        delete slot;
        if (visible) {
            content->show();
        }
    }

    void detachAllAnimationSlots() {
        for (const QString& id : std::as_const(order)) {
            detachAnimationSlot(panels[id]);
        }
    }

    void setAnimationConstraintsRelaxed(const bool relaxed) {
        for (const QString& id : std::as_const(order)) {
            if (VPanelAnimationSlot* slot = panels[id].animationSlot.data()) {
                slot->setConstraintRelaxed(relaxed);
            }
        }
    }

    void attachAncestorSplitters(QWidget* panel) {
        for (QWidget* ancestor = panel != nullptr ? panel->parentWidget() : nullptr;
             ancestor != nullptr && ancestor != window; ancestor = ancestor->parentWidget()) {
            if (auto* splitter = qobject_cast<VSplitter*>(ancestor)) {
                attachSplitter(splitter);
            }
        }
        if (auto* splitter = qobject_cast<VSplitter*>(root.data())) {
            attachSplitter(splitter);
        }
    }

    void attachSplitter(VSplitter* splitter) {
        if (splitter == nullptr || splitters.contains(splitter)) {
            return;
        }
        SplitterBinding state;
        state.splitter = splitter;
        const QList<int> sizes = splitter->sizes();
        if (!sizes.isEmpty() &&
            std::all_of(sizes.cbegin(), sizes.cend(), [](const int size) { return size > 0; })) {
            state.expandedSizes = sizes;
        }
        state.clickedConnection =
            QObject::connect(splitter, &VSplitter::handleClicked, q,
                             [this, splitter](const int handleIndex, const Qt::MouseButton button) {
                                 if (button == Qt::RightButton) {
                                     q->showPanelChooser();
                                 } else if (button == Qt::LeftButton) {
                                     collapsePanelBeforeHandle(splitter, handleIndex);
                                 }
                             });
        state.movedConnection =
            QObject::connect(splitter, &QSplitter::splitterMoved, q,
                             [this, splitter](int, int) { handleSplitterMoved(splitter); });
        state.destroyedConnection = QObject::connect(
            splitter, &QObject::destroyed, q, [this, splitter] { splitters.remove(splitter); });
        splitters.insert(splitter, state);
    }

    [[nodiscard]] PanelBinding* directPanel(VSplitter* splitter, const int index) {
        QWidget* slot = splitter != nullptr ? splitter->widget(index) : nullptr;
        if (slot == nullptr) {
            return nullptr;
        }
        for (const QString& id : order) {
            PanelBinding& panel = panels[id];
            if (panel.animationSlot == slot ||
                (panel.animationSlot == nullptr && panel.widget == slot)) {
                return &panel;
            }
        }
        return nullptr;
    }

    void collapsePanelBeforeHandle(VSplitter* splitter, const int handleIndex) {
        if (splitter == nullptr || handleIndex <= 0 || handleIndex >= splitter->count()) {
            return;
        }

        const int firstIndex = handleIndex - 1;
        const int secondIndex = handleIndex;
        QWidget* first = splitter->widget(firstIndex);
        QWidget* second = splitter->widget(secondIndex);
        if (first == nullptr || second == nullptr) {
            return;
        }

        const auto centerOnAxis = [splitter](const QWidget* widget) {
            return splitter->orientation() == Qt::Horizontal ? widget->geometry().center().x()
                                                             : widget->geometry().center().y();
        };
        const int precedingIndex =
            centerOnAxis(first) <= centerOnAxis(second) ? firstIndex : secondIndex;
        if (PanelBinding* panel = directPanel(splitter, precedingIndex);
            panel != nullptr && panel->expanded) {
            q->setPanelExpanded(panel->id, false);
        }
    }

    void expandNearestPanel(const QPoint& globalPosition) {
        if (root == nullptr || root->width() <= 0 || root->height() <= 0) {
            return;
        }

        const QPoint point = root->mapFromGlobal(globalPosition);
        PanelBinding* nearest = nullptr;
        qreal nearestDistanceSquared = std::numeric_limits<qreal>::max();
        const QList<VPanelState> panelStates = states();
        for (const VPanelState& state : panelStates) {
            PanelBinding* panel = binding(state.id);
            if (panel == nullptr || panel->widget == nullptr || panel->expanded) {
                continue;
            }

            const QRectF panelRect(state.normalizedRect.x() * root->width(),
                                   state.normalizedRect.y() * root->height(),
                                   state.normalizedRect.width() * root->width(),
                                   state.normalizedRect.height() * root->height());
            const qreal dx =
                std::max({panelRect.left() - point.x(), 0.0, point.x() - panelRect.right()});
            const qreal dy =
                std::max({panelRect.top() - point.y(), 0.0, point.y() - panelRect.bottom()});
            const qreal distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                nearest = panel;
            }
        }
        if (nearest != nullptr) {
            q->setPanelExpanded(nearest->id, true);
        }
    }

    void handleSplitterMoved(VSplitter* splitter) {
        if (applying || splitter == nullptr) {
            return;
        }
        // Direct manipulation takes authority from an in-flight programmatic transition.
        layoutAnimation->stop();
        setAnimationConstraintsRelaxed(false);
        auto iterator = splitters.find(splitter);
        if (iterator == splitters.end()) {
            return;
        }
        const QList<int> sizes = splitter->sizes();
        bool allSlotsExpanded = !sizes.isEmpty();
        QHash<QString, bool> previousStates;
        previousStates.reserve(order.size());
        for (const QString& id : std::as_const(order)) {
            previousStates.insert(id, panels[id].expanded);
        }
        for (int index = 0; index < sizes.size(); ++index) {
            allSlotsExpanded = allSlotsExpanded && sizes.at(index) > 0;
            if (PanelBinding* panel = directPanel(splitter, index)) {
                panel->expanded = sizes.at(index) > 0;
            }
        }
        if (allSlotsExpanded) {
            iterator->expandedSizes = sizes;
        }
        if (restoreExpandedInvariant() != nullptr) {
            applyAllSplitterStates();
        }
        if (allPanelsExpanded()) {
            captureExpandedGeometry();
        }
        bool stateChanged = false;
        for (const QString& id : std::as_const(order)) {
            const PanelBinding& panel = panels[id];
            if (previousStates.value(id, panel.expanded) != panel.expanded) {
                stateChanged = true;
                emit q->panelExpandedChanged(id, panel.expanded);
            }
        }
        if (stateChanged || allSlotsExpanded) {
            emit q->panelLayoutChanged();
            refreshDialog();
        }
    }

    [[nodiscard]] bool allPanelsExpanded() const {
        if (order.isEmpty()) {
            return false;
        }
        return std::all_of(order.cbegin(), order.cend(), [this](const QString& id) {
            const auto iterator = panels.constFind(id);
            return iterator != panels.cend() && iterator->expanded && iterator->widget != nullptr;
        });
    }

    [[nodiscard]] QRectF logicalPanelGeometry(const PanelBinding& panel) const {
        if (panel.widget == nullptr || root == nullptr) {
            return {};
        }

        QWidget* geometryWidget =
            panel.animationSlot != nullptr ? panel.animationSlot.data() : panel.widget.data();
        const QPoint origin = geometryWidget->mapTo(root, QPoint(0, 0));
        QRectF geometry(origin, geometryWidget->size());
        auto* splitter = qobject_cast<VSplitter*>(geometryWidget->parentWidget());
        if (splitter == nullptr || !splitters.contains(splitter)) {
            return geometry;
        }

        const QRectF slotGeometry(geometryWidget->geometry());
        for (int index = 1; index < splitter->count(); ++index) {
            QSplitterHandle* handle = splitter->handle(index);
            if (handle == nullptr) {
                continue;
            }
            const QRectF handleGeometry(handle->geometry());
            const QPoint handleOrigin = handle->mapTo(root, QPoint(0, 0));
            const QRectF rootHandleGeometry(handleOrigin, handle->size());
            if (splitter->orientation() == Qt::Horizontal) {
                const qreal boundary = rootHandleGeometry.center().x() + 0.5;
                if (qFuzzyCompare(handleGeometry.right(), slotGeometry.left())) {
                    geometry.setLeft(boundary);
                } else if (qFuzzyCompare(slotGeometry.right(), handleGeometry.left())) {
                    geometry.setRight(boundary);
                }
            } else {
                const qreal boundary = rootHandleGeometry.center().y() + 0.5;
                if (qFuzzyCompare(handleGeometry.bottom(), slotGeometry.top())) {
                    geometry.setTop(boundary);
                } else if (qFuzzyCompare(slotGeometry.bottom(), handleGeometry.top())) {
                    geometry.setBottom(boundary);
                }
            }
        }
        return geometry;
    }

    void captureExpandedGeometry() {
        if (!allPanelsExpanded() || root == nullptr || root->width() <= 0 || root->height() <= 0) {
            return;
        }
        const qreal rootWidth = root->width();
        const qreal rootHeight = root->height();
        for (const QString& id : order) {
            PanelBinding& panel = panels[id];
            QWidget* geometryWidget =
                panel.animationSlot != nullptr ? panel.animationSlot.data() : panel.widget.data();
            if (geometryWidget == nullptr || geometryWidget->width() <= 0 ||
                geometryWidget->height() <= 0) {
                return;
            }
            const QRectF logicalGeometry = logicalPanelGeometry(panel);
            panel.normalizedRect =
                QRectF(logicalGeometry.x() / rootWidth, logicalGeometry.y() / rootHeight,
                       logicalGeometry.width() / rootWidth, logicalGeometry.height() / rootHeight);
        }
    }

    [[nodiscard]] QList<VPanelState> states() const {
        QList<VPanelState> result;
        result.reserve(order.size());
        const int count = static_cast<int>(order.size());
        for (int index = 0; index < count; ++index) {
            const PanelBinding& panel = panels[order.at(index)];
            QRectF normalizedRect = panel.normalizedRect;
            if (!normalizedRect.isValid() || normalizedRect.isEmpty()) {
                normalizedRect = QRectF(static_cast<qreal>(index) / count, 0.0, 1.0 / count, 1.0);
            }
            result.append(
                VPanelState{panel.id, panel.title, panel.number, normalizedRect, panel.expanded});
        }
        return result;
    }

    [[nodiscard]] QList<int> targetSplitterSizes(SplitterBinding& binding) {
        VSplitter* splitter = binding.splitter.data();
        if (splitter == nullptr || splitter->count() == 0) {
            return {};
        }
        const QList<int> current = splitter->sizes();
        if (current.size() != splitter->count()) {
            return {};
        }
        if (binding.expandedSizes.size() != current.size() &&
            std::all_of(current.cbegin(), current.cend(),
                        [](const int extent) { return extent > 0; })) {
            // Capture the live fully-expanded geometry immediately before the first collapse.
            binding.expandedSizes = current;
        }

        QList<qreal> weights;
        weights.reserve(current.size());
        QList<bool> visible;
        visible.reserve(current.size());
        bool hasManagedSlot = false;
        for (int index = 0; index < current.size(); ++index) {
            PanelBinding* panel = directPanel(splitter, index);
            hasManagedSlot = hasManagedSlot || panel != nullptr;
            visible.append(panel == nullptr || panel->expanded);

            qreal weight = 0.0;
            if (binding.expandedSizes.size() == current.size() &&
                binding.expandedSizes.at(index) > 0) {
                weight = binding.expandedSizes.at(index);
            } else if (current.at(index) > 0) {
                weight = current.at(index);
            } else if (panel != nullptr && panel->normalizedRect.isValid()) {
                weight =
                    (splitter->orientation() == Qt::Horizontal ? panel->normalizedRect.width()
                                                               : panel->normalizedRect.height()) *
                    1000.0;
            }
            weights.append(std::max<qreal>(1.0, weight));
        }
        if (!hasManagedSlot) {
            return {};
        }

        int total = std::accumulate(current.cbegin(), current.cend(), 0);
        if (total <= 0) {
            const int extent =
                splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
            total = std::max(1, extent - splitter->handleWidth() * (splitter->count() - 1));
        }
        qreal totalWeight = 0.0;
        int lastVisible = -1;
        for (int index = 0; index < visible.size(); ++index) {
            if (visible.at(index)) {
                totalWeight += weights.at(index);
                lastVisible = index;
            }
        }
        if (lastVisible < 0 || totalWeight <= 0.0) {
            return {};
        }

        QList<int> target(current.size(), 0);
        int assigned = 0;
        for (int index = 0; index < visible.size(); ++index) {
            if (!visible.at(index)) {
                continue;
            }
            const int extent = index == lastVisible
                                   ? total - assigned
                                   : qRound(total * weights.at(index) / totalWeight);
            target[index] = std::max(1, extent);
            assigned += target.at(index);
        }
        return target;
    }

    void applySplitterState(SplitterBinding& binding) {
        VSplitter* splitter = binding.splitter.data();
        const QList<int> target = targetSplitterSizes(binding);
        if (splitter == nullptr || target.isEmpty()) {
            return;
        }
        splitter->setSizes(target);
    }

    void applyAllSplitterStates() {
        layoutAnimation->stop();
        setAnimationConstraintsRelaxed(false);
        applying = true;
        for (auto iterator = splitters.begin(); iterator != splitters.end(); ++iterator) {
            applySplitterState(iterator.value());
        }
        applying = false;
        scheduleSynchronize(false);
    }

    void animateAllSplitterStates(const VkMotionRole role) {
        layoutAnimation->stop();
        setAnimationConstraintsRelaxed(true);

        QList<SplitterTransition> transitions;
        transitions.reserve(splitters.size());
        for (auto iterator = splitters.begin(); iterator != splitters.end(); ++iterator) {
            VSplitter* splitter = iterator->splitter.data();
            const QList<int> target = targetSplitterSizes(iterator.value());
            if (splitter == nullptr || target.isEmpty()) {
                continue;
            }
            const QList<int> start = splitter->sizes();
            if (start == target || start.size() != target.size()) {
                continue;
            }
            transitions.append({splitter, start, target});
        }

        if (transitions.isEmpty()) {
            setAnimationConstraintsRelaxed(false);
            scheduleSynchronize(false);
            return;
        }

        layoutAnimation->start(
            0.0, 1.0, role,
            [this, transitions](const qreal progress) {
                applying = true;
                for (const SplitterTransition& transition : transitions) {
                    if (transition.splitter == nullptr) {
                        continue;
                    }
                    QList<int> sizes;
                    sizes.reserve(transition.startSizes.size());
                    for (int index = 0; index < transition.startSizes.size(); ++index) {
                        const qreal start = transition.startSizes.at(index);
                        const qreal target = transition.targetSizes.at(index);
                        sizes.append(qRound(std::lerp(start, target, progress)));
                    }
                    transition.splitter->setSizes(sizes);
                }
                applying = false;
            },
            [this] { applyAllSplitterStates(); });
    }

    void scheduleSynchronize(const bool applyState = true) {
        pendingApply = pendingApply || applyState;
        if (synchronizeScheduled) {
            return;
        }
        synchronizeScheduled = true;
        QTimer::singleShot(0, q, [this] {
            synchronizeScheduled = false;
            const bool shouldApply = std::exchange(pendingApply, false);
            if (shouldApply) {
                applyAllSplitterStates();
                return;
            }
            if (allPanelsExpanded()) {
                captureExpandedGeometry();
            }
            refreshDialog();
        });
    }

    void refreshDialog() {
        if (dialog != nullptr) {
            dialog->setPanelStates(states());
        }
    }

    VPanelManager* q = nullptr;
    QPointer<QWidget> window;
    std::unique_ptr<VkWidgetAnimation> layoutAnimation;
    QPointer<QWidget> root;
    QHash<QString, PanelBinding> panels;
    QList<QString> order;
    QHash<VSplitter*, SplitterBinding> splitters;
    std::unique_ptr<VPanelEdgeHandleController> edgeHandles;
    QPointer<VPanelLayoutDialog> dialog;
    bool applying = false;
    bool synchronizeScheduled = false;
    bool pendingApply = false;
};

VPanelManager::VPanelManager(QWidget& window)
    : QObject(&window), d_(std::make_unique<VPanelManagerPrivate>(this, window)) {}

VPanelManager::~VPanelManager() {
    closePanelChooser();
    d_->layoutAnimation->stop();
    d_->setAnimationConstraintsRelaxed(false);
    d_->detachAllAnimationSlots();
}

QWidget* VPanelManager::window() const noexcept {
    return d_->window.data();
}

QWidget* VPanelManager::layoutRoot() const noexcept {
    return d_->root.data();
}

QList<QWidget*> VPanelManager::windowEdgeHandles() const {
    return d_->edgeHandles != nullptr ? d_->edgeHandles->handles() : QList<QWidget*>{};
}

bool VPanelManager::setLayoutRoot(QWidget* root) {
    if (root != nullptr && root != d_->window && !d_->window->isAncestorOf(root)) {
        return false;
    }
    if (d_->root == root) {
        return true;
    }
    closePanelChooser();
    d_->layoutAnimation->stop();
    d_->setAnimationConstraintsRelaxed(false);
    d_->detachAllAnimationSlots();
    for (auto iterator = d_->splitters.begin(); iterator != d_->splitters.end(); ++iterator) {
        QObject::disconnect(iterator->clickedConnection);
        QObject::disconnect(iterator->movedConnection);
        QObject::disconnect(iterator->destroyedConnection);
    }
    d_->splitters.clear();
    d_->root = root;
    if (root != nullptr && d_->edgeHandles == nullptr) {
        d_->edgeHandles = std::make_unique<VPanelEdgeHandleController>(
            *d_->window,
            [this](const Qt::MouseButton button, const QPoint& globalPosition) {
                if (button == Qt::RightButton) {
                    showPanelChooser();
                } else if (button == Qt::LeftButton) {
                    d_->expandNearestPanel(globalPosition);
                }
            },
            this);
    } else if (root == nullptr) {
        d_->edgeHandles.reset();
    }
    if (auto* splitter = qobject_cast<VSplitter*>(root)) {
        d_->attachSplitter(splitter);
    }
    for (const QString& id : std::as_const(d_->order)) {
        PanelBinding& panel = d_->panels[id];
        if (panel.widget != nullptr && root != nullptr &&
            (panel.widget == root || root->isAncestorOf(panel.widget))) {
            d_->attachAnimationSlot(panel);
            d_->attachAncestorSplitters(panel.widget);
        }
    }
    // Wait for Qt's layout pass before capturing application-provided splitter sizes.
    d_->scheduleSynchronize(false);
    return true;
}

bool VPanelManager::registerPanel(QString id, QString title, QWidget* panel, const int number) {
    id = id.trimmed();
    if (id.isEmpty() || panel == nullptr || panel->window() != d_->window) {
        return false;
    }
    if (d_->root != nullptr && panel != d_->root && !d_->root->isAncestorOf(panel)) {
        return false;
    }

    PanelBinding* existing = d_->binding(id);
    if (existing != nullptr && existing->widget != nullptr && existing->widget != panel) {
        return false;
    }
    if (number > 0) {
        const bool numberInUse = std::any_of(
            d_->order.cbegin(), d_->order.cend(), [this, number, &id](const QString& otherId) {
                const PanelBinding& candidate = d_->panels[otherId];
                return otherId != id && candidate.number == number;
            });
        if (numberInUse) {
            return false;
        }
    }

    const bool isRebinding = existing != nullptr;
    if (!isRebinding) {
        int resolvedNumber = number;
        if (resolvedNumber <= 0) {
            for (const QString& existingId : std::as_const(d_->order)) {
                resolvedNumber = std::max(resolvedNumber, d_->panels[existingId].number);
            }
            ++resolvedNumber;
        }
        d_->order.append(id);
        d_->panels.insert(id,
                          PanelBinding{id, std::move(title), resolvedNumber, panel, {}, {}, true});
        existing = d_->binding(id);
    } else {
        existing->title = std::move(title);
        existing->widget = panel;
        if (number > 0) {
            existing->number = number;
        }
    }

    connect(panel, &QObject::destroyed, this, [this, id, panel] {
        if (PanelBinding* binding = d_->binding(id);
            binding != nullptr && binding->widget.data() == panel) {
            binding->widget.clear();
            VPanelAnimationSlot* slot = binding->animationSlot.data();
            binding->animationSlot.clear();
            if (slot != nullptr) {
                slot->deleteLater();
            }
        }
    });
    d_->attachAnimationSlot(*existing);
    d_->attachAncestorSplitters(panel);
    // Rebinds preserve an existing collapse state; new panels keep application sizes.
    const bool restoresCollapsedPanel =
        isRebinding &&
        std::any_of(d_->order.cbegin(), d_->order.cend(),
                    [this](const QString& panelId) { return !d_->panels[panelId].expanded; });
    d_->scheduleSynchronize(restoresCollapsedPanel);
    emit panelLayoutChanged();
    return true;
}

bool VPanelManager::unregisterPanel(const QStringView id) {
    const QString key = id.toString();
    PanelBinding* panel = d_->binding(key);
    if (panel == nullptr) {
        return false;
    }
    d_->layoutAnimation->stop();
    d_->setAnimationConstraintsRelaxed(false);
    d_->detachAnimationSlot(*panel);
    d_->panels.remove(key);
    d_->order.removeAll(key);
    if (PanelBinding* replacement = d_->restoreExpandedInvariant()) {
        emit panelExpandedChanged(replacement->id, true);
        d_->applyAllSplitterStates();
    } else {
        d_->refreshDialog();
    }
    emit panelLayoutChanged();
    return true;
}

void VPanelManager::clearPanels() {
    if (d_->panels.isEmpty()) {
        return;
    }
    closePanelChooser();
    d_->layoutAnimation->stop();
    d_->setAnimationConstraintsRelaxed(false);
    d_->detachAllAnimationSlots();
    d_->panels.clear();
    d_->order.clear();
    emit panelLayoutChanged();
}

QList<VPanelState> VPanelManager::panelStates() const {
    return d_->states();
}

bool VPanelManager::containsPanel(const QStringView id) const {
    return d_->binding(id) != nullptr;
}

bool VPanelManager::isPanelExpanded(const QStringView id) const {
    const PanelBinding* panel = d_->binding(id);
    return panel != nullptr && panel->expanded;
}

bool VPanelManager::setPanelExpanded(const QStringView id, const bool expanded) {
    PanelBinding* panel = d_->binding(id);
    if (panel == nullptr || panel->widget == nullptr) {
        return false;
    }
    if (panel->expanded == expanded) {
        return true;
    }

    QList<std::pair<QString, bool>> changes;
    if (!expanded) {
        if (d_->liveExpandedPanelCount() <= 1) {
            PanelBinding* replacement = d_->firstLiveCollapsedPanel(panel);
            if (replacement == nullptr) {
                return false;
            }
            replacement->expanded = true;
            changes.emplaceBack(replacement->id, true);
        }
    }

    panel->expanded = expanded;
    changes.emplaceBack(panel->id, expanded);
    d_->animateAllSplitterStates(expanded ? VkMotionRole::EmphasizedEnter
                                          : VkMotionRole::EmphasizedExit);
    for (const auto& [panelId, isExpanded] : changes) {
        emit panelExpandedChanged(panelId, isExpanded);
    }
    emit panelLayoutChanged();
    d_->refreshDialog();
    return true;
}

bool VPanelManager::togglePanel(const QStringView id) {
    const PanelBinding* panel = d_->binding(id);
    return panel != nullptr && setPanelExpanded(id, !panel->expanded);
}

void VPanelManager::showPanelChooser() {
    if (d_->order.isEmpty() || d_->window == nullptr) {
        return;
    }
    if (d_->allPanelsExpanded()) {
        d_->captureExpandedGeometry();
    }
    if (d_->dialog == nullptr) {
        d_->dialog =
            new VPanelLayoutDialog(d_->window, [this](const QString& id) { togglePanel(id); });
    }
    d_->refreshDialog();
    d_->dialog->present();
}

void VPanelManager::closePanelChooser() {
    if (d_->dialog == nullptr) {
        return;
    }
    delete d_->dialog.data();
    d_->dialog.clear();
}

} // namespace vkui
