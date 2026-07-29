// SPDX-License-Identifier: MIT

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QItemSelectionModel>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimeLine>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <utility>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VkDisclosureTreeView.h>

namespace vkui {
namespace {

constexpr int FinderDisclosureDurationMs = 200;

QEasingCurve finderDisclosureEasing() {
    // Frame captures of NSOutlineView's animator show immediate motion rather
    // than the near-zero initial velocity of InSine. This is the equivalent
    // cubic timing function used by AppKit for the disclosure transaction.
    QEasingCurve easing(QEasingCurve::BezierSpline);
    easing.addCubicBezierSegment(QPointF(0.25, 0.10), QPointF(0.25, 1.00), QPointF(1.00, 1.00));
    return easing;
}

struct DisclosureRow final {
    QPersistentModelIndex index;
    QRect rect;
};

class DisclosureGroupOverlay final : public QWidget {
  public:
    using RowPainter = std::function<void(QPainter&, const QPersistentModelIndex&, const QRect&)>;

    explicit DisclosureGroupOverlay(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("vkDisclosureGroupTransition"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::NoFocus);
    }

    void setRows(QVector<DisclosureRow> rows, QVector<DisclosureRow> followingRows,
                 RowPainter rowPainter, const int totalTravel, const QColor& surfaceColor) {
        m_rows = std::move(rows);
        m_followingRows = std::move(followingRows);
        m_rowPainter = std::move(rowPainter);
        m_totalTravel = std::max(0, totalTravel);
        m_surfaceColor = surfaceColor;
        setProperty("travel", m_totalTravel);
        setProperty("visualTravel", m_totalTravel);
        setProperty("totalTravel", m_totalTravel);
        setProperty("rowCount", m_rows.size());
        setProperty("trailingItemText",
                    m_rows.isEmpty() ? QString() : m_rows.constLast().index.data().toString());
        setProperty("followingItemText",
                    m_followingRows.isEmpty()
                        ? QString()
                        : m_followingRows.constFirst().index.data().toString());
        // Both groups are painted from persistent model indexes, so selection
        // pills retain delegate geometry and a long branch never needs a
        // branch-height pixmap.
        setProperty("surfaceCount", 2);
        setProgress(0.0);
    }

    void setProgress(const qreal progress) {
        const qreal bounded = std::clamp(progress, 0.0, 1.0);
        if (qFuzzyCompare(m_progress + 1.0, bounded + 1.0)) {
            return;
        }
        m_progress = bounded;
        const int siblingOffset = visibleReveal();
        const int groupOffset = siblingOffset - m_totalTravel;
        setProperty("progress", m_progress);
        setProperty("groupOffset", groupOffset);
        setProperty("siblingOffset", siblingOffset);
        setProperty("branchOffset", groupOffset);
        setProperty("firstChildTop", groupOffset);
        setProperty("lastChildBottom", siblingOffset);
        setProperty("branchRelativeSpan", siblingOffset - groupOffset);
        setProperty("seamGap", 0);
        setProperty("followingTop", siblingOffset);
        if (!m_rows.isEmpty() && siblingOffset > 0) {
            const int sourceTop = m_totalTravel - siblingOffset;
            const int sourceBottom = sourceTop + std::min(siblingOffset, height());
            auto first = std::lower_bound(m_rows.cbegin(), m_rows.cend(), sourceTop,
                                          [](const DisclosureRow& candidate, const int y) {
                                              return candidate.rect.bottom() < y;
                                          });
            auto last = first;
            while (last != m_rows.cend() && last->rect.top() < sourceBottom) {
                ++last;
            }
            setProperty("firstVisibleChildText",
                        first == m_rows.cend() ? QString() : first->index.data().toString());
            setProperty("lastVisibleChildText",
                        first == last ? QString() : std::prev(last)->index.data().toString());
        } else {
            setProperty("firstVisibleChildText", QString());
            setProperty("lastVisibleChildText", QString());
        }
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setClipRegion(event->region());
        painter.fillRect(event->rect(), m_surfaceColor);
        const int reveal = visibleReveal();
        if (!m_rows.isEmpty() && m_rowPainter && reveal > 0) {
            painter.save();
            const int visibleBranchHeight = std::min(reveal, height());
            painter.setClipRect(QRect(0, 0, width(), visibleBranchHeight), Qt::IntersectClip);
            const int groupOffset = reveal - m_totalTravel;
            const int sourceTop = -groupOffset;
            const int sourceBottom = sourceTop + visibleBranchHeight;
            auto row = std::lower_bound(m_rows.cbegin(), m_rows.cend(), sourceTop,
                                        [](const DisclosureRow& candidate, const int y) {
                                            return candidate.rect.bottom() < y;
                                        });
            for (; row != m_rows.cend() && row->rect.top() < sourceBottom; ++row) {
                QRect destination = row->rect.translated(0, groupOffset);
                m_rowPainter(painter, row->index, destination);
            }
            painter.restore();
        }
        if (!m_followingRows.isEmpty() && m_rowPainter && reveal < height()) {
            const int sourceBottom = height() - reveal;
            for (auto row = m_followingRows.cbegin();
                 row != m_followingRows.cend() && row->rect.top() < sourceBottom; ++row) {
                m_rowPainter(painter, row->index, row->rect.translated(0, reveal));
            }
        }
    }

  private:
    [[nodiscard]] int visibleReveal() const {
        if (m_progress <= 0.0 || m_totalTravel <= 0) {
            return 0;
        }
        if (m_progress >= 1.0) {
            return m_totalTravel;
        }
        return std::clamp(qRound(m_progress * m_totalTravel), 0, m_totalTravel);
    }

    QVector<DisclosureRow> m_rows;
    QVector<DisclosureRow> m_followingRows;
    RowPainter m_rowPainter;
    QColor m_surfaceColor;
    qreal m_progress = -1.0;
    int m_totalTravel = 0;
};

} // namespace

VkTreeItemGeometry treeItemGeometry(const QStyleOptionViewItem& option,
                                    const VkFileIconMetrics& metrics, const int horizontalInset,
                                    const int pillHorizontalPadding) {
    const bool rightToLeft = option.direction == Qt::RightToLeft;
    const QRect contentRect = option.rect.adjusted(horizontalInset, 0, -horizontalInset, 0);
    const int slotWidth = metrics.glyphSlotSize.width();
    const int iconLeft = rightToLeft ? contentRect.right() - slotWidth + 1 : contentRect.left();
    const QRect iconRect(iconLeft, contentRect.top(), slotWidth, contentRect.height());
    const QRect availableTextRect =
        rightToLeft ? QRect(contentRect.left(), contentRect.top(),
                            std::max(0, iconRect.left() - metrics.textGap - contentRect.left()),
                            contentRect.height())
                    : QRect(iconRect.right() + metrics.textGap + 1, contentRect.top(),
                            std::max(0, contentRect.right() - iconRect.right() - metrics.textGap),
                            contentRect.height());
    const QFontMetrics fontMetrics(option.font);
    const QString text =
        fontMetrics.elidedText(option.text, option.textElideMode, availableTextRect.width());
    const int textWidth = std::min(availableTextRect.width(), fontMetrics.horizontalAdvance(text));
    const QRect textRect =
        rightToLeft ? QRect(availableTextRect.right() - textWidth + 1, availableTextRect.top(),
                            textWidth, availableTextRect.height())
                    : QRect(availableTextRect.left(), availableTextRect.top(), textWidth,
                            availableTextRect.height());
    const int visualLeft = std::min(iconRect.left(), textRect.left());
    const int visualRight = std::max(iconRect.right(), textRect.right());
    // Keep the rounded selection surface inside the item viewport. Allowing
    // its nominal padding to extend past a root row's left edge clips away the
    // rounded corner and leaves a square block beside the icon.
    const int pillLeft = std::max(option.rect.left() + 2, visualLeft - pillHorizontalPadding);
    const int pillRight = std::min(option.rect.right() - 2, visualRight + pillHorizontalPadding);
    return {iconRect, availableTextRect, textRect,
            QRect(pillLeft, option.rect.top() + 2, std::max(0, pillRight - pillLeft + 1),
                  std::max(0, option.rect.height() - 4))};
}

VkDisclosureTreeView::VkDisclosureTreeView(QWidget* parent) : QTreeView(parent) {
    m_disclosureTimeline = new QTimeLine(FinderDisclosureDurationMs, this);
    m_disclosureTimeline->setObjectName(QStringLiteral("vkDisclosureTimeline"));
    m_disclosureTimeline->setEasingCurve(finderDisclosureEasing());
    m_disclosureTimeline->setUpdateInterval(8);
    connect(m_disclosureTimeline, &QTimeLine::valueChanged, this, [this](const qreal value) {
        auto* overlay = static_cast<DisclosureGroupOverlay*>(m_disclosureOverlay.data());
        if (overlay != nullptr) {
            overlay->setProgress(value);
            updateDisclosureScrollRange(value);
        }
    });
    connect(m_disclosureTimeline, &QTimeLine::finished, this,
            &VkDisclosureTreeView::finishDisclosureAnimation);
    connect(VkThemeManager::instance(), &VkThemeManager::animationsEnabledChanged, this,
            [this](const bool enabled) {
                if (!enabled) {
                    finishDisclosureAnimation();
                }
            });
    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
            [this](quint64) { finishDisclosureAnimation(); });
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
        auto* overlay = static_cast<DisclosureGroupOverlay*>(m_disclosureOverlay.data());
        if (overlay != nullptr) {
            updateDisclosureScrollRange(overlay->property("progress").toReal());
        }
    });
}

VkDisclosureTreeView::~VkDisclosureTreeView() {
    finishDisclosureAnimation();
}

void VkDisclosureTreeView::setNativeBranchesVisible(const bool visible) {
    if (m_nativeBranchesVisible == visible) {
        return;
    }
    m_nativeBranchesVisible = visible;
    viewport()->update();
}

bool VkDisclosureTreeView::nativeBranchesVisible() const noexcept {
    return m_nativeBranchesVisible;
}

void VkDisclosureTreeView::setDisclosureSurfaceColor(const QColor& color) {
    if (m_disclosureSurfaceColor == color) {
        return;
    }
    finishDisclosureAnimation();
    m_disclosureSurfaceColor = color;
}

QColor VkDisclosureTreeView::disclosureSurfaceColor() const {
    return m_disclosureSurfaceColor.isValid() ? m_disclosureSurfaceColor
                                              : palette().color(QPalette::Base);
}

void VkDisclosureTreeView::setExpandedAnimated(const QModelIndex& index, const bool expanded) {
    if (!index.isValid() || isExpanded(index) == expanded) {
        return;
    }
    auto* active = static_cast<DisclosureGroupOverlay*>(m_disclosureOverlay.data());
    if (active != nullptr && m_disclosureIndex == index) {
        QTreeView::setExpanded(index, expanded);
        doItemsLayout();
        updateGeometries();
        updateDisclosureScrollRange(active->property("progress").toReal());
        m_disclosureTimeline->toggleDirection();
        return;
    }

    finishDisclosureAnimation();
    const QRect folderRect = visualRect(index);
    if (!VkThemeManager::instance()->animationsEnabled() || !isVisible() || !folderRect.isValid() ||
        viewport()->width() <= 0 || viewport()->height() <= 0) {
        QTreeView::setExpanded(index, expanded);
        return;
    }
    const int affectedTop = folderRect.bottom() + 1;
    if (affectedTop < 0 || affectedTop >= viewport()->height()) {
        QTreeView::setExpanded(index, expanded);
        return;
    }
    const QRect captureRect(0, affectedTop, viewport()->width(),
                            viewport()->height() - affectedTop);
    const int followingCaptureHeight =
        std::max(captureRect.height(), window() == nullptr ? 0 : window()->height());
    const bool currentState = isExpanded(index);
    updateGeometries();
    const int currentScrollMaximum =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->maximum();
    QModelIndex continuation;
    if (currentState) {
        continuation = indexBelow(index);
        while (continuation.isValid() && isDescendantOf(continuation, index)) {
            continuation = indexBelow(continuation);
        }
    } else {
        continuation = indexBelow(index);
    }

    QVector<DisclosureRow> expandedRows;
    QVector<DisclosureRow> followingRows;
    int totalTravel = 0;
    int collapsedContinuationY = -1;
    int expandedContinuationY = -1;
    const auto captureExpandedState = [this, &index, &continuation, affectedTop, &totalTravel,
                                       &expandedContinuationY, &expandedRows]() {
        expandedRows.clear();
        totalTravel = 0;
        for (QModelIndex descendant = indexBelow(index);
             descendant.isValid() && isDescendantOf(descendant, index);
             descendant = indexBelow(descendant)) {
            QRect rowRect = visualRect(descendant);
            if (rowRect.height() <= 0) {
                rowRect.setHeight(std::max(1, sizeHintForIndex(descendant).height()));
            }
            rowRect.translate(0, -affectedTop);
            expandedRows.append({QPersistentModelIndex(descendant), rowRect});
            totalTravel = std::max(totalTravel, rowRect.bottom() + 1);
        }
        if (continuation.isValid()) {
            expandedContinuationY = visualRect(continuation).top();
        }
    };
    const auto captureCollapsedState = [this, &index, &continuation, affectedTop,
                                        followingCaptureHeight, &collapsedContinuationY,
                                        &followingRows]() {
        followingRows.clear();
        if (continuation.isValid()) {
            collapsedContinuationY = visualRect(continuation).top();
        }
        for (QModelIndex following = indexBelow(index); following.isValid();
             following = indexBelow(following)) {
            QRect rowRect = visualRect(following);
            if (rowRect.height() <= 0) {
                rowRect.setHeight(std::max(1, sizeHintForIndex(following).height()));
            }
            rowRect.translate(0, -affectedTop);
            if (rowRect.top() >= followingCaptureHeight) {
                break;
            }
            followingRows.append({QPersistentModelIndex(following), rowRect});
        }
    };
    if (currentState) {
        captureExpandedState();
    } else {
        captureCollapsedState();
    }

    QTreeView::setExpanded(index, expanded);
    doItemsLayout();
    updateGeometries();
    const int targetScrollMaximum =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->maximum();
    m_disclosureCollapsedScrollMaximum = currentState ? targetScrollMaximum : currentScrollMaximum;
    m_disclosureExpandedScrollMaximum = currentState ? currentScrollMaximum : targetScrollMaximum;
    if (expanded) {
        captureExpandedState();
    } else {
        captureCollapsedState();
    }
    if (collapsedContinuationY >= 0 && expandedContinuationY >= 0) {
        totalTravel = expandedContinuationY - collapsedContinuationY;
    }
    if (!expandedRows.isEmpty()) {
        // The true trailing descendant defines the seam. This deliberately
        // rejects viewport-height approximations: the last child and the first
        // following sibling must share one boundary for the entire motion.
        totalTravel = expandedRows.constLast().rect.bottom() + 1;
    }
    if (totalTravel <= 0 || expandedRows.isEmpty()) {
        viewport()->update(captureRect);
        return;
    }

    QStyleOptionViewItem baseOption;
    initViewItemOption(&baseOption);
    baseOption.widget = this;
    const auto paintRow = [this, baseOption](QPainter& painter,
                                             const QPersistentModelIndex& persistentIndex,
                                             const QRect& rect) {
        if (!persistentIndex.isValid()) {
            return;
        }
        const QModelIndex rowIndex = persistentIndex;
        QStyleOptionViewItem option(baseOption);
        option.rect = rect;
        option.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus |
                          QStyle::State_MouseOver | QStyle::State_Open | QStyle::State_Children);
        if (selectionModel() != nullptr && selectionModel()->isSelected(rowIndex)) {
            option.state |= QStyle::State_Selected;
        }
        if (currentIndex() == rowIndex && hasFocus()) {
            option.state |= QStyle::State_HasFocus;
        }
        if (isExpanded(rowIndex)) {
            option.state |= QStyle::State_Open;
        }
        if (model() != nullptr && model()->hasChildren(rowIndex)) {
            option.state |= QStyle::State_Children;
        }
        if (auto* delegate = itemDelegateForIndex(rowIndex); delegate != nullptr) {
            delegate->paint(&painter, option, rowIndex);
        }
    };

    auto* overlay = new DisclosureGroupOverlay(viewport());
    overlay->setGeometry(captureRect);
    overlay->setRows(std::move(expandedRows), std::move(followingRows), paintRow, totalTravel,
                     disclosureSurfaceColor());
    overlay->setProgress(currentState ? 1.0 : 0.0);
    overlay->show();
    overlay->raise();
    m_disclosureOverlay = overlay;
    m_disclosureIndex = index;
    m_disclosureHorizontalScrollValue =
        horizontalScrollBar() == nullptr ? 0 : horizontalScrollBar()->value();
    m_disclosureVerticalScrollValue =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->value();
    const qreal branchScreens = static_cast<qreal>(totalTravel) / std::max(1, captureRect.height());
    const int adaptiveDuration =
        FinderDisclosureDurationMs +
        std::min(100, qRound(24.0 * std::log2(std::max(1.0, branchScreens))));
    m_disclosureTimeline->setDuration(adaptiveDuration);
    updateDisclosureScrollRange(currentState ? 1.0 : 0.0);
    m_disclosureTimeline->setCurrentTime(currentState ? m_disclosureTimeline->duration() : 0);
    m_disclosureTimeline->setDirection(expanded ? QTimeLine::Forward : QTimeLine::Backward);
    m_disclosureTimeline->start();
}

void VkDisclosureTreeView::updateDisclosureScrollRange(const qreal progress) {
    auto* scrollBar = verticalScrollBar();
    if (m_disclosureOverlay == nullptr || scrollBar == nullptr) {
        return;
    }
    const qreal bounded = std::clamp(progress, 0.0, 1.0);
    const int maximum =
        m_disclosureCollapsedScrollMaximum +
        qRound(bounded * (m_disclosureExpandedScrollMaximum - m_disclosureCollapsedScrollMaximum));
    const QSignalBlocker blocker(scrollBar);
    scrollBar->setRange(scrollBar->minimum(), std::max(scrollBar->minimum(), maximum));
    scrollBar->setValue(
        std::clamp(m_disclosureVerticalScrollValue, scrollBar->minimum(), scrollBar->maximum()));
    m_disclosureOverlay->setProperty("collapsedScrollMaximum", m_disclosureCollapsedScrollMaximum);
    m_disclosureOverlay->setProperty("expandedScrollMaximum", m_disclosureExpandedScrollMaximum);
    m_disclosureOverlay->setProperty("currentScrollMaximum", scrollBar->maximum());
}

void VkDisclosureTreeView::finishDisclosureAnimation() {
    if (m_disclosureTimeline != nullptr && m_disclosureTimeline->state() != QTimeLine::NotRunning) {
        m_disclosureTimeline->stop();
    }
    if (m_disclosureOverlay == nullptr) {
        m_disclosureIndex = QPersistentModelIndex();
        return;
    }
    const QRect dirty = m_disclosureOverlay->geometry();
    delete m_disclosureOverlay.data();
    m_disclosureOverlay = nullptr;
    m_disclosureIndex = QPersistentModelIndex();
    updateGeometries();
    viewport()->update(dirty);
}

void VkDisclosureTreeView::changeEvent(QEvent* event) {
    if (event != nullptr &&
        (event->type() == QEvent::PaletteChange ||
         event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::FontChange ||
         event->type() == QEvent::ApplicationFontChange)) {
        finishDisclosureAnimation();
    }
    QTreeView::changeEvent(event);
}

void VkDisclosureTreeView::drawBranches(QPainter* painter, const QRect& rect,
                                        const QModelIndex& index) const {
    if (m_nativeBranchesVisible) {
        QTreeView::drawBranches(painter, rect, index);
    }
}

void VkDisclosureTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    if (!m_nativeBranchesVisible && painter) {
        painter->fillRect(QRect(viewport()->rect().left(), option.rect.top(),
                                viewport()->rect().width(), option.rect.height()),
                          disclosureSurfaceColor());
    }
    QTreeView::drawRow(painter, option, index);
    const int itemLeft = visualRect(index).left();
    if (!m_nativeBranchesVisible && painter && itemLeft > viewport()->rect().left()) {
        // QTreeView's native selected-row surface can extend through the
        // hierarchy branch rectangle even when drawBranches() is empty.
        // Clearing that strip after the native row pass keeps selection and
        // hover strictly inside the delegate's icon-and-label surface.
        const QRect branchRect(viewport()->rect().left(), option.rect.top(),
                               itemLeft - viewport()->rect().left(), option.rect.height());
        painter->fillRect(branchRect, disclosureSurfaceColor());
    }
}

void VkDisclosureTreeView::resizeEvent(QResizeEvent* event) {
    const bool preserveDisclosure = m_disclosureOverlay != nullptr && event != nullptr;
    if (!preserveDisclosure) {
        finishDisclosureAnimation();
    }
    QTreeView::resizeEvent(event);
    if (preserveDisclosure && m_disclosureOverlay != nullptr) {
        QRect geometry = m_disclosureOverlay->geometry();
        geometry.setLeft(0);
        geometry.setWidth(viewport()->width());
        geometry.setHeight(std::max(0, viewport()->height() - geometry.top()));
        if (geometry.height() <= 0) {
            finishDisclosureAnimation();
        } else {
            m_disclosureOverlay->setGeometry(geometry);
        }
    }
}

void VkDisclosureTreeView::scrollContentsBy(const int dx, const int dy) {
    if (m_disclosureOverlay != nullptr && !m_restoringDisclosureScroll &&
        QApplication::mouseButtons() == Qt::NoButton) {
        // QTreeView may asynchronously keep the previous current index
        // visible after a branch changes size. Applying that internal scroll
        // halfway through the captured transition would invalidate both
        // surfaces and make following rows jump to their final positions.
        // Hold the clicked folder's scroll anchor for this short transaction.
        m_restoringDisclosureScroll = true;
        if (auto* horizontal = horizontalScrollBar();
            horizontal != nullptr && horizontal->value() != m_disclosureHorizontalScrollValue) {
            const QSignalBlocker blocker(horizontal);
            horizontal->setValue(m_disclosureHorizontalScrollValue);
        }
        if (auto* vertical = verticalScrollBar();
            vertical != nullptr && vertical->value() != m_disclosureVerticalScrollValue) {
            const QSignalBlocker blocker(vertical);
            vertical->setValue(m_disclosureVerticalScrollValue);
        }
        m_restoringDisclosureScroll = false;
        return;
    }
    finishDisclosureAnimation();
    QTreeView::scrollContentsBy(dx, dy);
}

void VkDisclosureTreeView::updateGeometries() {
    QTreeView::updateGeometries();
    auto* overlay = static_cast<DisclosureGroupOverlay*>(m_disclosureOverlay.data());
    if (overlay != nullptr) {
        updateDisclosureScrollRange(overlay->property("progress").toReal());
    }
}

void VkDisclosureTreeView::wheelEvent(QWheelEvent* event) {
    // A real wheel gesture takes precedence over the disclosure transaction;
    // direct scrollbar drags are distinguished by QApplication::mouseButtons
    // in scrollContentsBy().
    finishDisclosureAnimation();
    QTreeView::wheelEvent(event);
}

bool VkDisclosureTreeView::isDescendantOf(const QModelIndex& index,
                                          const QModelIndex& ancestor) const {
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
        if (parent == ancestor) {
            return true;
        }
    }
    return false;
}

} // namespace vkui
