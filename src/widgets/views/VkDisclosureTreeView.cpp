// SPDX-License-Identifier: MIT

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimeLine>
#include <QWheelEvent>
#include <algorithm>
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

int firstContentReveal(const QPixmap& surface, const int boundary, const QColor& surfaceColor) {
    const QImage image = surface.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull() || boundary <= 0) {
        return 1;
    }
    const qreal ratio = std::max(1.0, surface.devicePixelRatio());
    const int physicalBoundary = std::clamp(qRound(boundary * ratio), 1, image.height());
    const int minimumPixels = std::max(4, qRound(6.0 * ratio));
    for (int depth = 1; depth <= physicalBoundary; ++depth) {
        const int y = physicalBoundary - depth;
        int contentPixels = 0;
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - surfaceColor.red()) <= 4 &&
                qAbs(pixel.green() - surfaceColor.green()) <= 4 &&
                qAbs(pixel.blue() - surfaceColor.blue()) <= 4 &&
                qAbs(pixel.alpha() - surfaceColor.alpha()) <= 4) {
                continue;
            }
            if (++contentPixels >= minimumPixels) {
                return std::max(1, qCeil(depth / ratio));
            }
        }
    }
    return 1;
}

class DisclosureGroupOverlay final : public QWidget {
  public:
    explicit DisclosureGroupOverlay(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("vkDisclosureGroupTransition"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::NoFocus);
    }

    void setSurface(QPixmap expandedSurface, QPixmap collapsedSurface, const int totalTravel,
                    const int visualTravel, const int contentRevealThreshold,
                    const QColor& surfaceColor) {
        m_expandedSurface = std::move(expandedSurface);
        m_collapsedSurface = std::move(collapsedSurface);
        m_totalTravel = std::max(0, totalTravel);
        m_visualTravel = std::clamp(visualTravel, 0, m_totalTravel);
        m_contentRevealThreshold =
            std::clamp(contentRevealThreshold, 1, std::max(1, m_visualTravel));
        m_surfaceColor = surfaceColor;
        setProperty("travel", m_visualTravel);
        setProperty("visualTravel", m_visualTravel);
        setProperty("totalTravel", m_totalTravel);
        // Match QTreeView's bounded animation strategy: expanded rows are
        // revealed above one moving boundary while the collapsed following
        // rows move below it. Memory stays independent of branch length.
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
        const int groupOffset = siblingOffset - m_visualTravel;
        setProperty("progress", m_progress);
        setProperty("groupOffset", groupOffset);
        setProperty("siblingOffset", siblingOffset);
        setProperty("branchOffset", groupOffset);
        setProperty("firstChildTop", groupOffset);
        setProperty("lastChildBottom", siblingOffset);
        setProperty("branchRelativeSpan", siblingOffset - groupOffset);
        setProperty("seamGap", 0);
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setClipRegion(event->region());
        painter.fillRect(event->rect(), m_surfaceColor);
        const int reveal = visibleReveal();
        if (!m_expandedSurface.isNull() && reveal > 0) {
            painter.save();
            painter.setClipRect(QRect(0, 0, width(), reveal), Qt::IntersectClip);
            painter.drawPixmap(QPoint(0, reveal - m_visualTravel), m_expandedSurface);
            painter.restore();
        }
        if (!m_collapsedSurface.isNull()) {
            painter.drawPixmap(QPoint(0, reveal), m_collapsedSurface);
        }
    }

  private:
    [[nodiscard]] int visibleReveal() const {
        if (m_progress <= 0.0 || m_visualTravel <= 0) {
            return 0;
        }
        if (m_progress >= 1.0) {
            return m_visualTravel;
        }
        const int reveal = std::clamp(qRound(m_progress * m_visualTravel), 0, m_visualTravel);
        return reveal < m_contentRevealThreshold ? 0 : reveal;
    }

    QPixmap m_expandedSurface;
    QPixmap m_collapsedSurface;
    QColor m_surfaceColor;
    qreal m_progress = -1.0;
    int m_totalTravel = 0;
    int m_visualTravel = 0;
    int m_contentRevealThreshold = 1;
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
    return {iconRect, availableTextRect, textRect,
            QRect(visualLeft - pillHorizontalPadding, option.rect.top() + 2,
                  visualRight - visualLeft + 1 + 2 * pillHorizontalPadding,
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
}

VkDisclosureTreeView::~VkDisclosureTreeView() {
    finishDisclosureAnimation();
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
    const bool currentState = isExpanded(index);
    QModelIndex continuation;
    if (currentState) {
        continuation = indexBelow(index);
        while (continuation.isValid() && isDescendantOf(continuation, index)) {
            continuation = indexBelow(continuation);
        }
    } else {
        continuation = indexBelow(index);
    }

    QPixmap expandedSurface;
    QPixmap collapsedSurface;
    int totalTravel = 0;
    int collapsedContinuationY = -1;
    int expandedContinuationY = -1;
    const auto captureExpandedState = [this, &index, &continuation, &captureRect, &totalTravel,
                                       &expandedContinuationY, &expandedSurface]() {
        totalTravel = expandedBranchHeight(index);
        if (continuation.isValid()) {
            expandedContinuationY = visualRect(continuation).top();
        }
        expandedSurface = viewport()->grab(captureRect);
    };
    const auto captureCollapsedState = [this, &continuation, &captureRect, &collapsedContinuationY,
                                        &collapsedSurface]() {
        if (continuation.isValid()) {
            collapsedContinuationY = visualRect(continuation).top();
        }
        collapsedSurface = viewport()->grab(captureRect);
    };
    if (currentState) {
        captureExpandedState();
    } else {
        captureCollapsedState();
    }

    QTreeView::setExpanded(index, expanded);
    doItemsLayout();
    if (expanded) {
        captureExpandedState();
    } else {
        captureCollapsedState();
    }
    if (collapsedContinuationY >= 0 && expandedContinuationY >= 0) {
        totalTravel = expandedContinuationY - collapsedContinuationY;
    }
    int visualTravel = std::min(totalTravel, captureRect.height());
    if (totalTravel > captureRect.height()) {
        // End the moving descendant surface on a complete item boundary.
        // A viewport grab commonly ends in the middle of a row; using that
        // arbitrary cut would make its text/content gap visibly separate from
        // the first following row. Qt's own QTreeView animation likewise sums
        // complete item heights when choosing the animated extent.
        QModelIndex descendant = indexBelow(index);
        while (descendant.isValid() && isDescendantOf(descendant, index)) {
            const QRect descendantRect = visualRect(descendant);
            const int rowBoundary = descendantRect.bottom() + 1 - affectedTop;
            if (rowBoundary >= captureRect.height()) {
                visualTravel = std::min(totalTravel, rowBoundary);
                break;
            }
            descendant = indexBelow(descendant);
        }
    }
    if (totalTravel <= 0 || visualTravel <= 0 || expandedSurface.isNull() ||
        collapsedSurface.isNull()) {
        viewport()->update(captureRect);
        return;
    }

    const int expandedSurfaceHeight = qRound(expandedSurface.deviceIndependentSize().height());
    if (expandedSurfaceHeight < visualTravel) {
        // The aligned boundary may be at most one row below the viewport.
        // Paint only that small offscreen tail. This avoids allocating or
        // traversing the full branch while preserving an exact row-to-row
        // seam for long volumes and directories.
        const qreal ratio = std::max(1.0, expandedSurface.devicePixelRatio());
        QPixmap extendedSurface(
            QSize(qRound(captureRect.width() * ratio), qRound(visualTravel * ratio)));
        extendedSurface.setDevicePixelRatio(ratio);
        extendedSurface.fill(disclosureSurfaceColor());
        QPainter surfacePainter(&extendedSurface);
        surfacePainter.drawPixmap(QPoint(0, 0), expandedSurface);
        surfacePainter.setClipRect(QRect(0, expandedSurfaceHeight, captureRect.width(),
                                         visualTravel - expandedSurfaceHeight));

        QStyleOptionViewItem baseOption;
        initViewItemOption(&baseOption);
        baseOption.widget = this;
        QModelIndex descendant = indexBelow(index);
        while (descendant.isValid() && isDescendantOf(descendant, index)) {
            const QRect viewportRect = visualRect(descendant);
            const QRect surfaceRect = viewportRect.translated(0, -affectedTop);
            if (surfaceRect.top() >= visualTravel) {
                break;
            }
            if (surfaceRect.bottom() >= expandedSurfaceHeight) {
                QStyleOptionViewItem option(baseOption);
                option.rect = surfaceRect;
                option.state &=
                    ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver |
                      QStyle::State_Open | QStyle::State_Children);
                if (selectionModel() != nullptr && selectionModel()->isSelected(descendant)) {
                    option.state |= QStyle::State_Selected;
                }
                if (currentIndex() == descendant && hasFocus()) {
                    option.state |= QStyle::State_HasFocus;
                }
                if (isExpanded(descendant)) {
                    option.state |= QStyle::State_Open;
                }
                if (model() != nullptr && model()->hasChildren(descendant)) {
                    option.state |= QStyle::State_Children;
                }
                if (auto* delegate = itemDelegateForIndex(descendant); delegate != nullptr) {
                    delegate->paint(&surfacePainter, option, descendant);
                }
            }
            descendant = indexBelow(descendant);
        }
        surfacePainter.end();
        expandedSurface = std::move(extendedSurface);
    }

    auto* overlay = new DisclosureGroupOverlay(viewport());
    overlay->setGeometry(captureRect);
    const int contentRevealThreshold =
        firstContentReveal(expandedSurface, visualTravel, disclosureSurfaceColor());
    overlay->setSurface(std::move(expandedSurface), std::move(collapsedSurface), totalTravel,
                        visualTravel, contentRevealThreshold, disclosureSurfaceColor());
    overlay->setProgress(currentState ? 1.0 : 0.0);
    overlay->show();
    overlay->raise();
    m_disclosureOverlay = overlay;
    m_disclosureIndex = index;
    m_disclosureHorizontalScrollValue =
        horizontalScrollBar() == nullptr ? 0 : horizontalScrollBar()->value();
    m_disclosureVerticalScrollValue =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->value();
    m_disclosureTimeline->setCurrentTime(currentState ? m_disclosureTimeline->duration() : 0);
    m_disclosureTimeline->setDirection(expanded ? QTimeLine::Forward : QTimeLine::Backward);
    m_disclosureTimeline->start();
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

void VkDisclosureTreeView::resizeEvent(QResizeEvent* event) {
    const bool preserveHorizontalResize = m_disclosureOverlay != nullptr && event != nullptr &&
                                          event->oldSize().height() == event->size().height();
    if (!preserveHorizontalResize) {
        finishDisclosureAnimation();
    }
    QTreeView::resizeEvent(event);
    if (preserveHorizontalResize && m_disclosureOverlay != nullptr) {
        QRect geometry = m_disclosureOverlay->geometry();
        geometry.setLeft(0);
        geometry.setWidth(viewport()->width());
        geometry.setHeight(std::max(0, viewport()->height() - geometry.top()));
        m_disclosureOverlay->setGeometry(geometry);
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

int VkDisclosureTreeView::expandedBranchHeight(const QModelIndex& index) const {
    if (!index.isValid() || !isExpanded(index)) {
        return 0;
    }
    int height = 0;
    for (QModelIndex child = indexBelow(index); child.isValid() && isDescendantOf(child, index);
         child = indexBelow(child)) {
        const int measured = visualRect(child).height();
        height += measured > 0 ? measured : std::max(1, sizeHintForIndex(child).height());
    }
    return height;
}

} // namespace vkui
