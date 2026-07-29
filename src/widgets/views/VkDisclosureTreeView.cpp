// SPDX-License-Identifier: MIT

#include <vkui/widgets/views/VkDisclosureTreeView.h>

#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QTimeLine>

#include <vkui/core/VkThemeManager.h>

#include <algorithm>
#include <utility>

namespace vkui {
namespace {

constexpr int FinderDisclosureDurationMs = 200;

class DisclosureGroupOverlay final : public QWidget
{
public:
    explicit DisclosureGroupOverlay(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(
            QStringLiteral("vkDisclosureGroupTransition"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::NoFocus);
    }

    void setSurface(
        QPixmap surface,
        const int travel,
        const QColor &surfaceColor)
    {
        m_surface = std::move(surface);
        m_travel = std::max(0, travel);
        m_surfaceColor = surfaceColor;
        setProperty("travel", m_travel);
        setProperty("surfaceCount", 1);
        setProgress(0.0);
    }

    void setProgress(const qreal progress)
    {
        const qreal bounded =
            std::clamp(progress, 0.0, 1.0);
        if (qFuzzyCompare(
                m_progress + 1.0,
                bounded + 1.0)) {
            return;
        }
        m_progress = bounded;
        const int offset =
            qRound(m_progress * m_travel);
        setProperty("progress", m_progress);
        setProperty(
            "groupOffset", offset - m_travel);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.setClipRegion(event->region());
        painter.fillRect(
            event->rect(), m_surfaceColor);
        if (!m_surface.isNull()) {
            const int offset =
                qRound(m_progress * m_travel)
                - m_travel;
            painter.drawPixmap(
                QPointF(
                    0.0,
                    static_cast<qreal>(offset)),
                m_surface);
        }
    }

private:
    QPixmap m_surface;
    QColor m_surfaceColor;
    qreal m_progress = -1.0;
    int m_travel = 0;
};

} // namespace

VkTreeItemGeometry treeItemGeometry(
    const QStyleOptionViewItem &option,
    const VkFileIconMetrics &metrics,
    const int horizontalInset,
    const int pillHorizontalPadding)
{
    const bool rightToLeft =
        option.direction == Qt::RightToLeft;
    const QRect contentRect =
        option.rect.adjusted(
            horizontalInset,
            0,
            -horizontalInset,
            0);
    const int slotWidth =
        metrics.glyphSlotSize.width();
    const int iconLeft =
        rightToLeft
        ? contentRect.right() - slotWidth + 1
        : contentRect.left();
    const QRect iconRect(
        iconLeft,
        contentRect.top(),
        slotWidth,
        contentRect.height());
    const QRect availableTextRect =
        rightToLeft
        ? QRect(
              contentRect.left(),
              contentRect.top(),
              std::max(
                  0,
                  iconRect.left() - metrics.textGap
                      - contentRect.left()),
              contentRect.height())
        : QRect(
              iconRect.right() + metrics.textGap + 1,
              contentRect.top(),
              std::max(
                  0,
                  contentRect.right() - iconRect.right()
                      - metrics.textGap),
              contentRect.height());
    const QFontMetrics fontMetrics(option.font);
    const QString text =
        fontMetrics.elidedText(
            option.text,
            option.textElideMode,
            availableTextRect.width());
    const int textWidth =
        std::min(
            availableTextRect.width(),
            fontMetrics.horizontalAdvance(text));
    const QRect textRect =
        rightToLeft
        ? QRect(
              availableTextRect.right()
                  - textWidth + 1,
              availableTextRect.top(),
              textWidth,
              availableTextRect.height())
        : QRect(
              availableTextRect.left(),
              availableTextRect.top(),
              textWidth,
              availableTextRect.height());
    const int visualLeft =
        std::min(iconRect.left(), textRect.left());
    const int visualRight =
        std::max(iconRect.right(), textRect.right());
    return {
        iconRect,
        availableTextRect,
        textRect,
        QRect(
            visualLeft - pillHorizontalPadding,
            option.rect.top() + 2,
            visualRight - visualLeft + 1
                + 2 * pillHorizontalPadding,
            std::max(0, option.rect.height() - 4))};
}

VkDisclosureTreeView::VkDisclosureTreeView(QWidget *parent)
    : QTreeView(parent)
{
    m_disclosureTimeline =
        new QTimeLine(
            FinderDisclosureDurationMs, this);
    m_disclosureTimeline->setObjectName(
        QStringLiteral("vkDisclosureTimeline"));
    // Matches the Finder/NSOutlineView community reference used by VkUI's
    // filesystem: a 0.2 second NSAnimationEaseIn disclosure.
    m_disclosureTimeline->setEasingCurve(
        QEasingCurve::InSine);
    m_disclosureTimeline->setUpdateInterval(8);
    connect(
        m_disclosureTimeline,
        &QTimeLine::valueChanged,
        this,
        [this](const qreal value) {
            auto *overlay =
                static_cast<DisclosureGroupOverlay *>(
                    m_disclosureOverlay.data());
            if (overlay != nullptr) {
                overlay->setProgress(value);
            }
        });
    connect(
        m_disclosureTimeline,
        &QTimeLine::finished,
        this,
        &VkDisclosureTreeView::
            finishDisclosureAnimation);
    connect(
        VkThemeManager::instance(),
        &VkThemeManager::animationsEnabledChanged,
        this,
        [this](const bool enabled) {
            if (!enabled) {
                finishDisclosureAnimation();
            }
        });
    connect(
        VkThemeManager::instance(),
        &VkThemeManager::themeChanged,
        this,
        [this](quint64) {
            finishDisclosureAnimation();
        });
}

VkDisclosureTreeView::~VkDisclosureTreeView()
{
    finishDisclosureAnimation();
}

void VkDisclosureTreeView::setDisclosureSurfaceColor(
    const QColor &color)
{
    if (m_disclosureSurfaceColor == color) {
        return;
    }
    finishDisclosureAnimation();
    m_disclosureSurfaceColor = color;
}

QColor VkDisclosureTreeView::disclosureSurfaceColor()
    const
{
    return m_disclosureSurfaceColor.isValid()
        ? m_disclosureSurfaceColor
        : palette().color(QPalette::Base);
}

void VkDisclosureTreeView::setExpandedAnimated(
    const QModelIndex &index,
    const bool expanded)
{
    if (!index.isValid()
        || isExpanded(index) == expanded) {
        return;
    }
    auto *active =
        static_cast<DisclosureGroupOverlay *>(
            m_disclosureOverlay.data());
    if (active != nullptr
        && m_disclosureIndex == index) {
        QTreeView::setExpanded(index, expanded);
        doItemsLayout();
        m_disclosureTimeline->toggleDirection();
        return;
    }

    finishDisclosureAnimation();
    const QRect folderRect = visualRect(index);
    if (!VkThemeManager::instance()
             ->animationsEnabled()
        || !isVisible()
        || !folderRect.isValid()
        || viewport()->width() <= 0
        || viewport()->height() <= 0) {
        QTreeView::setExpanded(index, expanded);
        return;
    }
    const int affectedTop = folderRect.bottom() + 1;
    if (affectedTop < 0
        || affectedTop >= viewport()->height()) {
        QTreeView::setExpanded(index, expanded);
        return;
    }
    const QRect captureRect(
        0,
        affectedTop,
        viewport()->width(),
        viewport()->height() - affectedTop);
    const bool currentState = isExpanded(index);
    QModelIndex continuation;
    if (currentState) {
        continuation = indexBelow(index);
        while (continuation.isValid()
               && isDescendantOf(
                   continuation, index)) {
            continuation = indexBelow(continuation);
        }
    } else {
        continuation = indexBelow(index);
    }

    QPixmap collapsedSurface;
    QPixmap expandedSurface;
    int travel = 0;
    int collapsedContinuationY = -1;
    int expandedContinuationY = -1;
    if (currentState) {
        travel = expandedBranchHeight(index);
        if (continuation.isValid()) {
            expandedContinuationY =
                visualRect(continuation).top();
        }
        expandedSurface =
            viewport()->grab(captureRect);
    } else {
        if (continuation.isValid()) {
            collapsedContinuationY =
                visualRect(continuation).top();
        }
        collapsedSurface =
            viewport()->grab(captureRect);
    }

    QTreeView::setExpanded(index, expanded);
    doItemsLayout();
    if (expanded) {
        travel = expandedBranchHeight(index);
        if (continuation.isValid()) {
            expandedContinuationY =
                visualRect(continuation).top();
        }
        expandedSurface =
            viewport()->grab(captureRect);
    } else {
        if (continuation.isValid()) {
            collapsedContinuationY =
                visualRect(continuation).top();
        }
        collapsedSurface =
            viewport()->grab(captureRect);
    }
    if (collapsedContinuationY >= 0
        && expandedContinuationY >= 0) {
        travel =
            expandedContinuationY
            - collapsedContinuationY;
    }
    if (travel <= 0
        || collapsedSurface.isNull()
        || expandedSurface.isNull()) {
        viewport()->update(captureRect);
        return;
    }

    const qreal ratio =
        expandedSurface.devicePixelRatio();
    const QSize stripSize(
        captureRect.width(),
        captureRect.height() + travel);
    QPixmap strip(
        qCeil(stripSize.width() * ratio),
        qCeil(stripSize.height() * ratio));
    strip.setDevicePixelRatio(ratio);
    strip.fill(Qt::transparent);
    {
        QPainter painter(&strip);
        painter.drawPixmap(
            QPointF(0.0, 0.0),
            expandedSurface);
        painter.drawPixmap(
            QPointF(
                0.0,
                static_cast<qreal>(travel)),
            collapsedSurface);
    }
    auto *overlay =
        new DisclosureGroupOverlay(viewport());
    overlay->setGeometry(captureRect);
    overlay->setSurface(
        std::move(strip),
        travel,
        disclosureSurfaceColor());
    overlay->setProgress(
        currentState ? 1.0 : 0.0);
    overlay->show();
    overlay->raise();
    m_disclosureOverlay = overlay;
    m_disclosureIndex = index;
    m_disclosureTimeline->setCurrentTime(
        currentState
            ? m_disclosureTimeline->duration()
            : 0);
    m_disclosureTimeline->setDirection(
        expanded
            ? QTimeLine::Forward
            : QTimeLine::Backward);
    m_disclosureTimeline->start();
}

void VkDisclosureTreeView::finishDisclosureAnimation()
{
    if (m_disclosureTimeline != nullptr
        && m_disclosureTimeline->state()
               != QTimeLine::NotRunning) {
        m_disclosureTimeline->stop();
    }
    if (m_disclosureOverlay == nullptr) {
        m_disclosureIndex = QPersistentModelIndex();
        return;
    }
    const QRect dirty =
        m_disclosureOverlay->geometry();
    delete m_disclosureOverlay.data();
    m_disclosureOverlay = nullptr;
    m_disclosureIndex = QPersistentModelIndex();
    viewport()->update(dirty);
}

void VkDisclosureTreeView::changeEvent(QEvent *event)
{
    if (event != nullptr
        && (event->type() == QEvent::PaletteChange
            || event->type()
                == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::FontChange
            || event->type()
                == QEvent::ApplicationFontChange)) {
        finishDisclosureAnimation();
    }
    QTreeView::changeEvent(event);
}

void VkDisclosureTreeView::resizeEvent(
    QResizeEvent *event)
{
    const bool preserveHorizontalResize =
        m_disclosureOverlay != nullptr
        && event != nullptr
        && event->oldSize().height()
            == event->size().height();
    if (!preserveHorizontalResize) {
        finishDisclosureAnimation();
    }
    QTreeView::resizeEvent(event);
    if (preserveHorizontalResize
        && m_disclosureOverlay != nullptr) {
        QRect geometry =
            m_disclosureOverlay->geometry();
        geometry.setLeft(0);
        geometry.setWidth(viewport()->width());
        geometry.setHeight(
            std::max(
                0,
                viewport()->height()
                    - geometry.top()));
        m_disclosureOverlay->setGeometry(geometry);
    }
}

void VkDisclosureTreeView::scrollContentsBy(
    const int dx,
    const int dy)
{
    finishDisclosureAnimation();
    QTreeView::scrollContentsBy(dx, dy);
}

bool VkDisclosureTreeView::isDescendantOf(
    const QModelIndex &index,
    const QModelIndex &ancestor) const
{
    for (QModelIndex parent = index.parent();
         parent.isValid();
         parent = parent.parent()) {
        if (parent == ancestor) {
            return true;
        }
    }
    return false;
}

int VkDisclosureTreeView::expandedBranchHeight(
    const QModelIndex &index) const
{
    if (!index.isValid() || !isExpanded(index)) {
        return 0;
    }
    int height = 0;
    for (QModelIndex child = indexBelow(index);
         child.isValid()
         && isDescendantOf(child, index);
         child = indexBelow(child)) {
        const int measured =
            visualRect(child).height();
        height += measured > 0
            ? measured
            : std::max(1, sizeHintForIndex(child).height());
    }
    return height;
}

} // namespace vkui
