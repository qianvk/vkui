#include <vkui/widgets/vk/VkWidgetBlockNavigator.h>

#include <vkui/widgets/views/VTreeView.h>
#include <vkui/widgets/controls/VSegmentedControl.h>

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QLayout>
#include <QModelIndex>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTextEdit>
#include <QTabBar>
#include <QTreeView>
#include <QWidget>

#include <algorithm>
#include <limits>
#include <tuple>

namespace vkui::vk::widgets {
namespace {

[[nodiscard]] bool belongsTo(
    const QWidget *const candidate,
    const QWidget *const root)
{
    return candidate != nullptr && root != nullptr
        && (candidate == root || root->isAncestorOf(candidate));
}

[[nodiscard]] bool hasCompoundControlAncestor(
    const QWidget *widget,
    const QWidget *root)
{
    for (const QWidget *ancestor = widget == nullptr
             ? nullptr
             : widget->parentWidget();
         ancestor != nullptr && ancestor != root;
         ancestor = ancestor->parentWidget()) {
        if (qobject_cast<const QComboBox *>(ancestor) != nullptr
            || qobject_cast<const QAbstractSpinBox *>(ancestor) != nullptr
            || qobject_cast<const vkui::VSegmentedControl *>(ancestor)
                != nullptr) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool keyboardAddressable(
    QWidget *const widget,
    QWidget *const root)
{
    return widget != nullptr && widget != root
        && root != nullptr && widget->isEnabled()
        // The popover root can still be hidden while its semantic window is
        // being mounted. isVisibleTo(root) deliberately ignores that root,
        // but rejects controls inside a hidden conditional row (for example
        // PDF-only appearance controls in a text book).
        && widget->isVisibleTo(root)
        && !hasCompoundControlAncestor(widget, root)
        && qobject_cast<QScrollBar *>(widget) == nullptr
        && (qobject_cast<QAbstractButton *>(widget) != nullptr
            || qobject_cast<QAbstractSlider *>(widget) != nullptr
            || qobject_cast<QComboBox *>(widget) != nullptr
            || qobject_cast<QAbstractSpinBox *>(widget) != nullptr
            || qobject_cast<QLineEdit *>(widget) != nullptr
            || qobject_cast<QTextEdit *>(widget) != nullptr
            || qobject_cast<QPlainTextEdit *>(widget) != nullptr
            || qobject_cast<QTabBar *>(widget) != nullptr
            || qobject_cast<vkui::VSegmentedControl *>(widget) != nullptr);
}

[[nodiscard]] QVector<QWidget *> controls(QWidget *const root)
{
    QVector<QWidget *> result;
    if (root == nullptr) {
        return result;
    }
    root->ensurePolished();
    if (root->layout() != nullptr) {
        root->layout()->activate();
    }
    for (QWidget *const child : root->findChildren<QWidget *>()) {
        if (keyboardAddressable(child, root)) {
            result.push_back(child);
        }
    }
    std::ranges::sort(
        result,
        [root](const QWidget *left, const QWidget *right) {
            const QPoint a = left->mapTo(root, QPoint());
            const QPoint b = right->mapTo(root, QPoint());
            return std::tuple(a.y(), a.x(), left->objectName())
                < std::tuple(b.y(), b.x(), right->objectName());
        });
    return result;
}

[[nodiscard]] QAbstractItemView *itemView(QWidget *const root)
{
    if (auto *const direct = qobject_cast<QAbstractItemView *>(root)) {
        return direct;
    }
    if (root == nullptr) {
        return nullptr;
    }
    const QList<QAbstractItemView *> descendants =
        root->findChildren<QAbstractItemView *>();
    const auto explicitView = std::ranges::find_if(
        descendants,
        [root](QAbstractItemView *const view) {
            // QComboBox owns a private popup view. It is an implementation
            // detail of that one control, never the semantic list for the
            // surrounding block.
            return view != nullptr
                && !hasCompoundControlAncestor(view, root);
        });
    return explicitView == descendants.end()
        ? nullptr
        : *explicitView;
}

[[nodiscard]] QModelIndex firstEnabledIndex(
    QAbstractItemView *const view)
{
    if (view == nullptr || view->model() == nullptr) {
        return {};
    }
    QAbstractItemModel *const model = view->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        if (index.isValid()
            && (model->flags(index) & Qt::ItemIsEnabled)) {
            return index;
        }
    }
    return {};
}

[[nodiscard]] bool activateItem(
    QAbstractItemView *const view,
    const QModelIndex &index)
{
    if (view == nullptr || !index.isValid()
        || view->model() == nullptr
        || !(view->model()->flags(index) & Qt::ItemIsEnabled)) {
        return false;
    }
    if (QMetaObject::invokeMethod(
            view,
            "clicked",
            Qt::DirectConnection,
            Q_ARG(QModelIndex, index))) {
        return true;
    }
    return QMetaObject::invokeMethod(
        view,
        "activated",
        Qt::DirectConnection,
        Q_ARG(QModelIndex, index));
}

[[nodiscard]] bool moveItem(
    QAbstractItemView *const view,
    const int horizontal,
    const int vertical)
{
    if (view == nullptr || view->model() == nullptr) {
        return false;
    }
    QModelIndex current = view->currentIndex();
    if (!current.isValid()) {
        current = firstEnabledIndex(view);
        if (!current.isValid()) {
            return false;
        }
        view->setCurrentIndex(current);
    }

    if (auto *const tree = qobject_cast<QTreeView *>(view);
        tree != nullptr && horizontal != 0) {
        const auto setExpanded = [tree](
                                     const QModelIndex &index,
                                     const bool expanded) {
            if (auto *const disclosure =
                    qobject_cast<vkui::VTreeView *>(tree)) {
                disclosure->setExpandedAnimated(index, expanded);
            } else {
                tree->setExpanded(index, expanded);
            }
        };
        if (horizontal > 0) {
            if (tree->model()->hasChildren(current)
                && !tree->isExpanded(current)) {
                setExpanded(current, true);
                return true;
            }
            if (tree->isExpanded(current)
                && tree->model()->rowCount(current) > 0) {
                const QModelIndex child =
                    tree->model()->index(0, 0, current);
                if (child.isValid()) {
                    tree->setCurrentIndex(child);
                    tree->scrollTo(
                        child, QAbstractItemView::EnsureVisible);
                    return true;
                }
            }
            if (view->property("vkRightActivates").toBool()
                && !tree->model()->hasChildren(current)) {
                return activateItem(view, current);
            }
        } else {
            if (tree->isExpanded(current)) {
                setExpanded(current, false);
                return true;
            }
            const QModelIndex parent = current.parent();
            if (parent.isValid()) {
                tree->setCurrentIndex(parent);
                tree->scrollTo(
                    parent, QAbstractItemView::EnsureVisible);
                return true;
            }
        }
        return false;
    }

    if (horizontal > 0
        && view->property("vkRightActivates").toBool()) {
        return activateItem(view, current);
    }

    if (vertical == 0 && horizontal == 0) {
        return false;
    }
    const int delta = vertical != 0 ? vertical : horizontal;
    QModelIndex next = current;
    bool moved = false;
    const int steps = std::max(1, std::abs(delta));
    for (int step = 0; step < steps; ++step) {
        QModelIndex candidate;
        if (auto *const tree = qobject_cast<QTreeView *>(view)) {
            candidate = delta > 0
                ? tree->indexBelow(next)
                : tree->indexAbove(next);
        } else {
            const int row = std::clamp(
                next.row() + (delta > 0 ? 1 : -1),
                0,
                std::max(0, view->model()->rowCount(next.parent()) - 1));
            candidate = view->model()->index(
                row, next.column(), next.parent());
        }
        if (!candidate.isValid() || candidate == next) {
            break;
        }
        next = candidate;
        moved = true;
    }
    if (!moved) {
        return false;
    }
    view->setCurrentIndex(next);
    view->scrollTo(next, QAbstractItemView::EnsureVisible);
    return true;
}

[[nodiscard]] QWidget *focusedControl(QWidget *const root)
{
    const QVector<QWidget *> candidates = controls(root);
    // The semantic highlight identifies the control itself and survives
    // relayouts. A numeric position is only a fallback for older surfaces:
    // inserting a heading or hiding native chrome can reorder the geometric
    // control list without changing the user's current control.
    const auto highlighted = std::ranges::find_if(
        candidates,
        [](const QWidget *const candidate) {
            return candidate->property(
                "vkKeyboardCurrent").toBool();
        });
    if (highlighted != candidates.cend()) {
        return *highlighted;
    }
    const QVariant semantic = root == nullptr
        ? QVariant{}
        : root->property("vkNavigatorControlIndex");
    const int semanticIndex = semantic.isValid()
        ? semantic.toInt()
        : -1;
    if (semanticIndex >= 0 && semanticIndex < candidates.size()) {
        return candidates.at(semanticIndex);
    }
    QWidget *const focused = QApplication::focusWidget();
    return belongsTo(focused, root)
            && candidates.contains(focused)
        ? focused
        : nullptr;
}

[[nodiscard]] bool setFocusedControl(
    QWidget *const root,
    QWidget *const widget)
{
    if (root == nullptr || widget == nullptr) {
        return false;
    }
    const QVector<QWidget *> candidates = controls(root);
    const int semanticIndex = static_cast<int>(
        candidates.indexOf(widget));
    if (semanticIndex >= 0) {
        root->setProperty("vkNavigatorControlIndex", semanticIndex);
        for (QWidget *const host :
             root->findChildren<QWidget *>()) {
            if (host->property(
                        "vkKeyboardHighlightHost")
                    .toBool()
                && host->property(
                           "vkKeyboardCurrent")
                       .toBool()) {
                host->setProperty("vkKeyboardCurrent", false);
                host->style()->unpolish(host);
                host->style()->polish(host);
                host->update();
            }
        }
        for (QWidget *const candidate : candidates) {
            const bool current = candidate == widget;
            if (candidate->property("vkKeyboardCurrent").toBool()
                != current) {
                candidate->setProperty("vkKeyboardCurrent", current);
                candidate->style()->unpolish(candidate);
                candidate->style()->polish(candidate);
                candidate->update();
            }
        }
        for (QWidget *host = widget->parentWidget();
             host != nullptr && host != root;
             host = host->parentWidget()) {
            if (!host->property(
                         "vkKeyboardHighlightHost")
                     .toBool()) {
                continue;
            }
            host->setProperty("vkKeyboardCurrent", true);
            host->style()->unpolish(host);
            host->style()->polish(host);
            host->update();
            break;
        }
    }
    QWidget *const topLevel = widget->window();
    if (topLevel != nullptr && topLevel != widget) {
        topLevel->setFocusProxy(widget);
        topLevel->activateWindow();
    }
    widget->setFocus(Qt::PopupFocusReason);
    // Native transient windows may retain physical focus on their top-level
    // surface. The semantic cursor above remains authoritative in that case.
    return widget->isEnabled();
}

[[nodiscard]] bool moveControl(
    QWidget *const root,
    const int horizontal,
    const int vertical)
{
    QVector<QWidget *> candidates = controls(root);
    if (candidates.isEmpty()) {
        return false;
    }
    QWidget *const focused = focusedControl(root);
    if (auto *const combo = qobject_cast<QComboBox *>(focused);
        combo != nullptr && combo->view() != nullptr
        && combo->view()->isVisible() && vertical != 0) {
        const int current = combo->view()->currentIndex().isValid()
            ? combo->view()->currentIndex().row()
            : std::max(0, combo->currentIndex());
        const int target = std::clamp(
            current + vertical,
            0,
            std::max(0, combo->count() - 1));
        combo->view()->setCurrentIndex(
            combo->model()->index(target, combo->modelColumn()));
        combo->view()->scrollTo(
            combo->view()->currentIndex(),
            QAbstractItemView::EnsureVisible);
        return target != current;
    }
    if (horizontal != 0) {
        if (auto *const segments =
                qobject_cast<vkui::VSegmentedControl *>(focused)) {
            const int current = segments->currentIndex();
            const int target = std::clamp(
                current + (horizontal > 0 ? 1 : -1),
                0, std::max(0, segments->count() - 1));
            segments->setCurrentIndex(target);
            return target != current;
        }
        if (auto *const slider = qobject_cast<QAbstractSlider *>(focused)) {
            slider->setValue(
                std::clamp(
                    slider->value()
                        + (horizontal > 0
                               ? slider->singleStep()
                               : -slider->singleStep()),
                    slider->minimum(), slider->maximum()));
            return true;
        }
        if (auto *const combo = qobject_cast<QComboBox *>(focused)) {
            combo->setCurrentIndex(std::clamp(
                combo->currentIndex() + (horizontal > 0 ? 1 : -1),
                0,
                std::max(0, combo->count() - 1)));
            return true;
        }
    }

    const qsizetype current =
        std::max<qsizetype>(0, candidates.indexOf(focused));
    const int requestedSteps = std::max(
        1, std::abs(vertical != 0 ? vertical : horizontal));
    if (vertical != 0 && requestedSteps > 1) {
        const qsizetype target = std::clamp<qsizetype>(
            current + (vertical > 0 ? requestedSteps : -requestedSteps),
            0,
            candidates.size() - 1);
        return setFocusedControl(root, candidates.at(target));
    }
    QWidget *const currentWidget = candidates.at(current);
    const QPoint origin = currentWidget->mapToGlobal(
        currentWidget->rect().center());
    const QRect originRect(
        currentWidget->mapToGlobal(QPoint()),
        currentWidget->size());
    QWidget *best = nullptr;
    qint64 bestScore = std::numeric_limits<qint64>::max();
    bool bestSharesNavigationAxis = false;
    for (QWidget *const candidate : candidates) {
        if (candidate == candidates.at(current)) {
            continue;
        }
        const QPoint point = candidate->mapToGlobal(
            candidate->rect().center());
        const QRect candidateRect(
            candidate->mapToGlobal(QPoint()),
            candidate->size());
        const int dx = point.x() - origin.x();
        const int dy = point.y() - origin.y();
        const bool forward = horizontal < 0
            ? dx < 0
            : horizontal > 0
            ? dx > 0
            : vertical < 0
            ? dy < 0
            : dy > 0;
        if (!forward) {
            continue;
        }
        const bool sharesNavigationAxis =
            horizontal != 0
            ? std::min(originRect.bottom(), candidateRect.bottom())
                    >= std::max(originRect.top(), candidateRect.top())
            : std::min(originRect.right(), candidateRect.right())
                    >= std::max(originRect.left(), candidateRect.left());
        // CSS Spatial Navigation and native focus engines prefer candidates
        // in the same navigation corridor before considering another row or
        // column. Without this corridor rule a wide action below a compact
        // icon row can steal `l` merely because its center is closer.
        if (best != nullptr
            && bestSharesNavigationAxis
            && !sharesNavigationAxis) {
            continue;
        }
        const qint64 primary = horizontal != 0
            ? std::abs(dx)
            : std::abs(dy);
        const qint64 secondary = horizontal != 0
            ? std::abs(dy)
            : std::abs(dx);
        const qint64 score = primary * 1'024 + secondary;
        if (best == nullptr
            || (sharesNavigationAxis
                && !bestSharesNavigationAxis)
            || (sharesNavigationAxis
                    == bestSharesNavigationAxis
                && score < bestScore)) {
            best = candidate;
            bestScore = score;
            bestSharesNavigationAxis = sharesNavigationAxis;
        }
    }
    if (best == nullptr && vertical != 0) {
        const qsizetype next = (current + (vertical > 0 ? 1 : -1)
                          + candidates.size())
            % candidates.size();
        best = candidates.at(next);
    }
    return setFocusedControl(root, best);
}

} // namespace

bool VkWidgetBlockNavigator::focus(QWidget *const root)
{
    if (root == nullptr || !root->isEnabled()) {
        return false;
    }
    if (QAbstractItemView *const view = itemView(root)) {
        if (!view->currentIndex().isValid()) {
            view->setCurrentIndex(firstEnabledIndex(view));
        }
        return setFocusedControl(root, view);
    }
    const QVector<QWidget *> candidates = controls(root);
    if (candidates.isEmpty()) {
        root->setFocus(Qt::PopupFocusReason);
        return root->isEnabled();
    }
    QWidget *const current = focusedControl(root);
    return setFocusedControl(
        root,
        current != nullptr
            ? current
            : candidates.constFirst());
}

bool VkWidgetBlockNavigator::focus(
    QWidget *const root,
    QWidget *const preferredControl)
{
    if (root == nullptr || preferredControl == nullptr
        || !root->isEnabled()
        || !belongsTo(preferredControl, root)) {
        return false;
    }
    return setFocusedControl(root, preferredControl);
}

bool VkWidgetBlockNavigator::navigate(
    QWidget *const root,
    const int horizontal,
    const int vertical)
{
    if (root == nullptr || (horizontal == 0 && vertical == 0)) {
        return false;
    }
    if (QAbstractItemView *const view = itemView(root)) {
        return moveItem(view, horizontal, vertical);
    }
    if (auto *const tabs = qobject_cast<QTabBar *>(focusedControl(root));
        tabs != nullptr && horizontal != 0 && tabs->count() > 0) {
        tabs->setCurrentIndex(std::clamp(
            tabs->currentIndex() + (horizontal > 0 ? 1 : -1),
            0,
            tabs->count() - 1));
        return true;
    }
    return moveControl(root, horizontal, vertical);
}

bool VkWidgetBlockNavigator::navigatePage(
    QWidget *const root,
    const int direction,
    const int rows)
{
    if (root == nullptr || direction == 0 || rows <= 0) {
        return false;
    }
    return navigate(
        root,
        0,
        (direction > 0 ? 1 : -1) * rows);
}

bool VkWidgetBlockNavigator::navigateBoundary(
    QWidget *const root,
    const bool last,
    const int count,
    const bool countWasExplicit)
{
    if (root == nullptr) {
        return false;
    }
    if (QAbstractItemView *const view = itemView(root)) {
        if (view->model() == nullptr) {
            return false;
        }
        QModelIndex target = firstEnabledIndex(view);
        if (!target.isValid()) {
            return false;
        }
        const int ordinal = countWasExplicit
            ? std::max(0, count - 1)
            : last
            ? std::numeric_limits<int>::max()
            : 0;
        for (int step = 0; step < ordinal; ++step) {
            QModelIndex next;
            if (auto *const tree = qobject_cast<QTreeView *>(view)) {
                next = tree->indexBelow(target);
            } else {
                const int row = target.row() + 1;
                if (row < view->model()->rowCount(target.parent())) {
                    next = view->model()->index(
                        row, target.column(), target.parent());
                }
            }
            if (!next.isValid()) {
                break;
            }
            target = next;
        }
        view->setCurrentIndex(target);
        view->scrollTo(target, QAbstractItemView::EnsureVisible);
        return true;
    }

    const QVector<QWidget *> candidates = controls(root);
    if (candidates.isEmpty()) {
        return false;
    }
    QWidget *const focused = focusedControl(root);
    if (auto *const combo = qobject_cast<QComboBox *>(focused);
        combo != nullptr && combo->view() != nullptr
        && combo->view()->isVisible() && combo->count() > 0) {
        const int row = countWasExplicit
            ? std::clamp(count - 1, 0, combo->count() - 1)
            : last ? combo->count() - 1 : 0;
        combo->view()->setCurrentIndex(
            combo->model()->index(row, combo->modelColumn()));
        combo->view()->scrollTo(
            combo->view()->currentIndex(),
            QAbstractItemView::EnsureVisible);
        return true;
    }
    const int lastIndex = static_cast<int>(
        std::min<qsizetype>(
            candidates.size() - 1,
            std::numeric_limits<int>::max()));
    const int index = countWasExplicit
        ? std::clamp(count - 1, 0, lastIndex)
        : last ? lastIndex : 0;
    return setFocusedControl(root, candidates.at(index));
}

bool VkWidgetBlockNavigator::activate(QWidget *const root)
{
    if (root == nullptr) {
        return false;
    }
    if (QAbstractItemView *const view = itemView(root)) {
        QModelIndex current = view->currentIndex();
        if (!current.isValid()) {
            current = firstEnabledIndex(view);
            view->setCurrentIndex(current);
        }
        return activateItem(view, current);
    }
    QWidget *const focused = focusedControl(root);
    if (auto *const button = qobject_cast<QAbstractButton *>(focused)) {
        button->click();
        return true;
    }
    if (auto *const combo = qobject_cast<QComboBox *>(focused)) {
        if (combo->view() != nullptr
            && combo->view()->isVisible()) {
            const QModelIndex selected =
                combo->view()->currentIndex();
            if (selected.isValid()) {
                combo->setCurrentIndex(selected.row());
            }
            combo->hidePopup();
            return true;
        }
        combo->showPopup();
        if (combo->view() != nullptr) {
            combo->view()->setCurrentIndex(
                combo->model()->index(
                    std::max(0, combo->currentIndex()),
                    combo->modelColumn()));
        }
        return true;
    }
    if (auto *const segments =
            qobject_cast<vkui::VSegmentedControl *>(focused)) {
        // Selection is already authoritative; Enter intentionally reapplies
        // it through the public setter so a segmented control remains one
        // compound block rather than exposing its private buttons.
        segments->setCurrentIndex(segments->currentIndex());
        return segments->currentIndex() >= 0;
    }
    if (qobject_cast<QLineEdit *>(focused) != nullptr
        || qobject_cast<QTextEdit *>(focused) != nullptr
        || qobject_cast<QPlainTextEdit *>(focused) != nullptr
        || qobject_cast<QAbstractSpinBox *>(focused) != nullptr) {
        return setFocusedControl(root, focused);
    }
    return focus(root);
}

int VkWidgetBlockNavigator::viewportRows(QWidget *const root)
{
    if (QWidget *const focused = focusedControl(root)) {
        if (auto *const combo = qobject_cast<QComboBox *>(focused);
            combo != nullptr && combo->view() != nullptr
            && combo->view()->isVisible()) {
            const int rowHeight = std::max(
                1,
                combo->view()->sizeHintForRow(
                    std::max(0, combo->currentIndex())));
            return std::max(
                1,
                combo->view()->viewport()->height() / rowHeight);
        }
    }
    if (QAbstractItemView *const view = itemView(root)) {
        int rowHeight = view->sizeHintForRow(
            std::max(0, view->currentIndex().row()));
        if (rowHeight <= 0) {
            rowHeight = view->fontMetrics().height() + 6;
        }
        return std::max(
            1,
            view->viewport()->height() / std::max(1, rowHeight));
    }
    return std::max(1, static_cast<int>(controls(root).size()));
}

} // namespace vkui::vk::widgets
