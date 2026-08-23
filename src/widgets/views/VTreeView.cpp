// SPDX-License-Identifier: MIT

#include <QAbstractItemDelegate>
#include <QAbstractItemModel>
#include <QApplication>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimeLine>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <utility>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/views/VTreeView.h>

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

enum class MutationRowRole { Survivor, Entering, Leaving };

[[nodiscard]] QString mutationRowIdentity(QModelIndex index) {
    QStringList components;
    while (index.isValid()) {
        components.prepend(index.data(Qt::DisplayRole).toString());
        index = index.parent();
    }
    return components.join(QChar(0x001f));
}

struct MutationRow final {
    QPersistentModelIndex index;
    QString identity;
    QRect fromRect;
    QRect toRect;
    QPixmap pixmap;
    MutationRowRole role = MutationRowRole::Survivor;
    int clipTop = std::numeric_limits<int>::min();
};

struct CapturedMutationRow final {
    QPersistentModelIndex index;
    QString identity;
    QRect rect;
    QRect eventualRect;
    QPixmap pixmap;
    MutationRowRole role = MutationRowRole::Survivor;
    int clipTop = std::numeric_limits<int>::min();
};

class RowMutationOverlay final : public QWidget {
  public:
    explicit RowMutationOverlay(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("vkTreeMutationTransition"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::NoFocus);
    }

    void setRows(QVector<MutationRow> rows, const QRect& viewportDirtyRect, const int seam,
                 const QColor& surfaceColor, const QString& kind) {
        m_rows = std::move(rows);
        m_viewportOrigin = viewportDirtyRect.topLeft();
        m_viewportDirtyRect = viewportDirtyRect;
        m_seam = seam;
        m_surfaceColor = surfaceColor;
        setProperty("mutationKind", kind);
        setProperty("rowCount", m_rows.size());
        setProperty("dirtyRect", viewportDirtyRect);
        setProperty("surfaceArea", viewportDirtyRect.width() * viewportDirtyRect.height());
        setProperty("viewportArea", parentWidget() == nullptr
                                        ? 0
                                        : parentWidget()->width() * parentWidget()->height());
        setProperty("cachedPixelHeight",
                    std::accumulate(m_rows.cbegin(), m_rows.cend(), 0,
                                    [](const int total, const MutationRow& row) {
                                        return total + row.pixmap.height();
                                    }));
        setProperty("fromGeometry", geometryMap(0.0));
        setProperty("toGeometry", geometryMap(1.0));
        setProgress(0.0);
    }

    [[nodiscard]] QVector<CapturedMutationRow> currentRows() const {
        QVector<CapturedMutationRow> result;
        result.reserve(m_rows.size());
        for (const MutationRow& row : m_rows) {
            const QRect current = interpolatedRect(row, m_progress);
            if (!current.intersects(parentWidget()->rect())) {
                continue;
            }
            result.append(
                {row.index, row.identity, current, row.toRect, row.pixmap, row.role, row.clipTop});
        }
        return result;
    }

    void setProgress(const qreal progress) {
        const qreal bounded = std::clamp(progress, 0.0, 1.0);
        if (qFuzzyCompare(m_progress + 1.0, bounded + 1.0)) {
            return;
        }
        const QRect previous = frameBounds(m_progress);
        m_progress = bounded;
        setProperty("progress", m_progress);
        setProperty("currentGeometry", geometryMap(m_progress));
        updateSeamProperty();
        const QRect current = frameBounds(m_progress);
        const QRect dirty =
            previous.united(current).intersected(m_viewportDirtyRect).translated(-m_viewportOrigin);
        if (dirty.isValid()) {
            update(dirty);
        }
    }

  protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setClipRegion(event->region());
        painter.fillRect(event->rect(), m_surfaceColor);

        for (const MutationRow& row : m_rows) {
            const QRect viewportRect = interpolatedRect(row, m_progress);
            if (!viewportRect.intersects(m_viewportDirtyRect)) {
                continue;
            }
            painter.save();
            if (row.clipTop != std::numeric_limits<int>::min()) {
                const int seamY = std::clamp(row.clipTop - m_viewportOrigin.y(), 0, height());
                painter.setClipRect(QRect(0, seamY, width(), height() - seamY), Qt::IntersectClip);
            }
            const QRect localRect = viewportRect.translated(-m_viewportOrigin);
            painter.drawPixmap(localRect.topLeft(), row.pixmap);
            painter.restore();
        }
    }

  private:
    [[nodiscard]] static QRect interpolatedRect(const MutationRow& row, const qreal progress) {
        const auto coordinate = [progress](const int from, const int to) {
            return qRound(from + (to - from) * progress);
        };
        return {coordinate(row.fromRect.x(), row.toRect.x()),
                coordinate(row.fromRect.y(), row.toRect.y()),
                coordinate(row.fromRect.width(), row.toRect.width()),
                coordinate(row.fromRect.height(), row.toRect.height())};
    }

    [[nodiscard]] QRect frameBounds(const qreal progress) const {
        QRect result;
        for (const MutationRow& row : m_rows) {
            const QRect current = interpolatedRect(row, progress);
            if (current.intersects(m_viewportDirtyRect)) {
                result = result.isNull() ? current : result.united(current);
            }
        }
        return result;
    }

    [[nodiscard]] QVariantMap geometryMap(const qreal progress) const {
        QVariantMap result;
        for (const MutationRow& row : m_rows) {
            if (!row.index.isValid()) {
                continue;
            }
            const QString key = row.index.data(Qt::DisplayRole).toString();
            if (!key.isEmpty()) {
                result.insert(key, interpolatedRect(row, progress));
            }
        }
        return result;
    }

    void updateSeamProperty() {
        int groupBottom = std::numeric_limits<int>::min();
        int trailingTop = std::numeric_limits<int>::max();
        for (const MutationRow& row : m_rows) {
            const QRect current = interpolatedRect(row, m_progress);
            if (row.role == MutationRowRole::Entering || row.role == MutationRowRole::Leaving) {
                groupBottom = std::max(groupBottom, current.bottom() + 1);
            } else if (row.fromRect.top() >= m_seam || row.toRect.top() >= m_seam) {
                trailingTop = std::min(trailingTop, current.top());
            }
        }
        const int seamGap = groupBottom == std::numeric_limits<int>::min() ||
                                    trailingTop == std::numeric_limits<int>::max()
                                ? 0
                                : trailingTop - groupBottom;
        setProperty("seamGap", seamGap);
        setProperty("groupBottom", groupBottom);
        setProperty("trailingTop", trailingTop);
    }

    QVector<MutationRow> m_rows;
    QPoint m_viewportOrigin;
    QRect m_viewportDirtyRect;
    QColor m_surfaceColor;
    qreal m_progress = -1.0;
    int m_seam = 0;
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

struct VTreeView::RowMutationTransaction final {
    RowMutationKind kind = RowMutationKind::Insert;
    QPersistentModelIndex sourceParent;
    QPersistentModelIndex destinationParent;
    QPersistentModelIndex continuation;
    QString continuationIdentity;
    QVector<CapturedMutationRow> before;
    QRect continuationRect;
    int first = 0;
    int last = -1;
    int destinationRow = -1;
    int seam = 0;
    int startScrollMaximum = 0;
    int startScrollValue = 0;
};

VTreeView::VTreeView(QWidget* parent) : QTreeView(parent) {
    setHeaderHidden(true);
    setFrameShape(QFrame::NoFrame);
    setRootIsDecorated(false);
    setAlternatingRowColors(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAllColumnsShowFocus(false);
    setExpandsOnDoubleClick(false);
    setAnimated(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setIndentation(20);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover, true);
    setTreeItemDelegate(new VTreeItemDelegate(this, this));

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
            &VTreeView::finishDisclosureAnimation);
    connect(VkThemeManager::instance(), &VkThemeManager::animationsEnabledChanged, this,
            [this](const bool enabled) {
                if (!enabled) {
                    finishDisclosureAnimation();
                    finishRowMutationAnimation();
                }
            });
    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
            [this](quint64, const VkThemeChanges changes) {
                if (changes.testFlag(VkThemeChange::Colors) ||
                    changes.testFlag(VkThemeChange::Metrics) ||
                    changes.testFlag(VkThemeChange::Typography)) {
                    // Animation overlays contain captured rows and must not retain stale tokens.
                    finishDisclosureAnimation();
                    finishRowMutationAnimation();
                    viewport()->update();
                }
            });
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
        auto* overlay = static_cast<DisclosureGroupOverlay*>(m_disclosureOverlay.data());
        if (overlay != nullptr) {
            updateDisclosureScrollRange(overlay->property("progress").toReal());
        }
    });

    m_mutationTimeline = new QTimeLine(FinderDisclosureDurationMs, this);
    m_mutationTimeline->setObjectName(QStringLiteral("vkTreeMutationTimeline"));
    m_mutationTimeline->setEasingCurve(finderDisclosureEasing());
    m_mutationTimeline->setUpdateInterval(8);
    connect(m_mutationTimeline, &QTimeLine::valueChanged, this, [this](const qreal value) {
        auto* overlay = static_cast<RowMutationOverlay*>(m_mutationOverlay.data());
        if (overlay != nullptr) {
            overlay->setProgress(value);
            updateMutationScrollRange(value);
        }
    });
    connect(m_mutationTimeline, &QTimeLine::finished, this,
            &VTreeView::finishRowMutationAnimation);
}

VTreeView::~VTreeView() {
    // A model may itself be a QObject child of the view. Disconnect before
    // QObject tears down child objects: timelines can be destroyed before a
    // child model emits destroyed(), and its lifecycle callback must never
    // dereference those raw timer pointers during base-class teardown.
    for (const QMetaObject::Connection& connection : std::as_const(m_modelConnections)) {
        disconnect(connection);
    }
    m_modelConnections.clear();
    finishDisclosureAnimation();
    finishRowMutationAnimation();
    m_disclosureTimeline = nullptr;
    m_mutationTimeline = nullptr;
}

void VTreeView::setModel(QAbstractItemModel* model) {
    finishDisclosureAnimation();
    finishRowMutationAnimation();
    for (const QMetaObject::Connection& connection : std::as_const(m_modelConnections)) {
        disconnect(connection);
    }
    m_modelConnections.clear();
    QTreeView::setModel(model);
    reconnectMutationModel(model);
}

void VTreeView::setTreeItemDelegate(VTreeItemDelegate* delegate) {
    Q_ASSERT(delegate != nullptr);
    if (delegate == nullptr || itemDelegate() == delegate) {
        return;
    }
    setItemDelegate(delegate);
    scheduleDelayedItemsLayout();
    viewport()->update();
}

VTreeItemDelegate* VTreeView::treeItemDelegate() const noexcept {
    return qobject_cast<VTreeItemDelegate*>(itemDelegate());
}

VTreeItemLayout VTreeView::itemLayout(const QModelIndex& index) const {
    if (!index.isValid()) {
        return {};
    }
    auto* delegate = qobject_cast<VTreeItemDelegate*>(itemDelegateForIndex(index));
    if (delegate == nullptr) {
        return {};
    }
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.widget = this;
    option.rect = visualRect(index);
    if (!option.rect.isValid()) {
        return {};
    }
    return delegate->itemLayout(option, index);
}

QRect VTreeView::expansionToggleRect(const QModelIndex& index) const {
    if (!index.isValid() || model() == nullptr || !model()->hasChildren(index)) {
        return {};
    }
    return itemLayout(index).leadingRect;
}

void VTreeView::setBranchLinesVisible(const bool visible) {
    if (m_branchLinesVisible == visible) {
        return;
    }
    m_branchLinesVisible = visible;
    viewport()->update();
}

bool VTreeView::branchLinesVisible() const noexcept {
    return m_branchLinesVisible;
}

void VTreeView::setDisclosureSurfaceColor(const QColor& color) {
    if (m_disclosureSurfaceColor == color) {
        return;
    }
    finishDisclosureAnimation();
    m_disclosureSurfaceColor = color;
}

QColor VTreeView::disclosureSurfaceColor() const {
    return m_disclosureSurfaceColor.isValid()
               ? m_disclosureSurfaceColor
               : viewport()->palette().color(viewport()->backgroundRole());
}

void VTreeView::reconnectMutationModel(QAbstractItemModel* model) {
    if (model == nullptr) {
        return;
    }
    m_modelConnections.append(
        connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this,
                [this](const QModelIndex& parent, const int first, const int last) {
                    beginRowMutation(RowMutationKind::Insert, parent, first, last);
                }));
    m_modelConnections.append(connect(model, &QAbstractItemModel::rowsInserted, this,
                                      [this](const QModelIndex&, const int, const int) {
                                          completeRowMutation(RowMutationKind::Insert);
                                      }));
    m_modelConnections.append(
        connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [this](const QModelIndex& parent, const int first, const int last) {
                    beginRowMutation(RowMutationKind::Remove, parent, first, last);
                }));
    m_modelConnections.append(connect(model, &QAbstractItemModel::rowsRemoved, this,
                                      [this](const QModelIndex&, const int, const int) {
                                          completeRowMutation(RowMutationKind::Remove);
                                      }));
    m_modelConnections.append(
        connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this,
                [this](const QModelIndex& sourceParent, const int first, const int last,
                       const QModelIndex& destinationParent, const int destinationRow) {
                    beginRowMutation(RowMutationKind::Move, sourceParent, first, last,
                                     destinationParent, destinationRow);
                }));
    m_modelConnections.append(
        connect(model, &QAbstractItemModel::rowsMoved, this,
                [this](const QModelIndex&, const int, const int, const QModelIndex&, const int) {
                    completeRowMutation(RowMutationKind::Move);
                }));
    m_modelConnections.append(
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
            finishDisclosureAnimation();
            finishRowMutationAnimation();
            m_pendingMutation.reset();
        }));
    m_modelConnections.append(connect(model, &QObject::destroyed, this, [this] {
        finishDisclosureAnimation();
        finishRowMutationAnimation();
        m_pendingMutation.reset();
        m_modelConnections.clear();
    }));
}

QRect VTreeView::mutationRowRect(const QModelIndex& index) const {
    if (!index.isValid() || viewport() == nullptr) {
        return {};
    }
    const QRect itemRect = visualRect(index);
    if (!itemRect.isValid()) {
        return {};
    }
    // QTreeView paints drawRow() with a FullRow rectangle. Retain that same
    // coordinate system in the transition so hierarchy indentation never
    // becomes a second horizontal translation.
    return QRect(viewport()->rect().left(), itemRect.top(), viewport()->width(), itemRect.height());
}

QPixmap VTreeView::captureMutationSurface() const {
    return viewport() == nullptr ? QPixmap{} : viewport()->grab();
}

QPixmap VTreeView::captureMutationRow(const QRect& rect, const QPixmap& surface) const {
    if (!rect.isValid() || surface.isNull() || viewport() == nullptr) {
        return {};
    }
    const qreal ratio = surface.devicePixelRatio();
    QPixmap pixmap(qMax(1, qCeil(rect.width() * ratio)), qMax(1, qCeil(rect.height() * ratio)));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(disclosureSurfaceColor());

    // Never call QTreeView::drawRow() out of drawTree(). Qt's implementation
    // obtains indentation from the private current view-item cursor, which is
    // only advanced by drawTree(); a manual call therefore paints rows at an
    // unrelated hierarchy depth. One real viewport render followed by cheap
    // row slices is both exact and O(visible pixels) per mutation.
    QPainter painter(&pixmap);
    painter.drawPixmap(QPoint(-rect.left(), -rect.top()), surface);
    return pixmap;
}

void VTreeView::beginRowMutation(const RowMutationKind kind,
                                           const QModelIndex& sourceParent, const int first,
                                           const int last, const QModelIndex& destinationParent,
                                           const int destinationRow) {
    if (model() == nullptr) {
        return;
    }

    const auto parentCanMoveVisibleRows = [this](const QModelIndex& parent) {
        if (!parent.isValid()) {
            return !rootIndex().isValid();
        }
        if (parent == rootIndex()) {
            return true;
        }
        return isExpanded(parent) && visualRect(parent).isValid();
    };
    const auto parentTouchesDisclosure = [this](const QModelIndex& parent) {
        if (!m_disclosureIndex.isValid()) {
            return false;
        }
        if (!parent.isValid()) {
            return !rootIndex().isValid();
        }
        return parent == m_disclosureIndex || isDescendantOf(parent, m_disclosureIndex) ||
               isDescendantOf(m_disclosureIndex, parent);
    };
    const bool sourceVisible = parentCanMoveVisibleRows(sourceParent);
    const bool destinationVisible =
        kind == RowMutationKind::Move && parentCanMoveVisibleRows(destinationParent);
    const bool related =
        parentTouchesDisclosure(sourceParent) ||
        (kind == RowMutationKind::Move && parentTouchesDisclosure(destinationParent));
    const bool preserveDisclosure =
        m_disclosureOverlay != nullptr && !related && !sourceVisible && !destinationVisible;
    if (preserveDisclosure) {
        // A collapsed, unrelated subtree has no rows in QTreeView's visible
        // geometry. Its asynchronous load/eviction may update the folder's
        // disclosure affordance, but cannot move any row captured by the
        // active transaction. Starting a second overlay here used to stop the
        // current disclosure at an arbitrary frame and made its children
        // flash in or disappear. Let the model update underneath instead.
        m_pendingMutation.reset();
        return;
    }
    finishDisclosureAnimation();

    auto transaction = std::make_unique<RowMutationTransaction>();
    transaction->kind = kind;
    transaction->sourceParent = sourceParent;
    transaction->destinationParent = destinationParent;
    transaction->first = first;
    transaction->last = last;
    transaction->destinationRow = destinationRow;
    transaction->startScrollMaximum =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->maximum();
    transaction->startScrollValue =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->value();

    QRect coveredByPriorTransition;
    if (auto* active = static_cast<RowMutationOverlay*>(m_mutationOverlay.data());
        active != nullptr) {
        transaction->before = active->currentRows();
        coveredByPriorTransition = active->geometry();
        m_mutationTimeline->stop();
        delete active;
        m_mutationOverlay = nullptr;
    }

    doItemsLayout();
    updateGeometries();
    const QPixmap beforeSurface = captureMutationSurface();
    QModelIndex firstVisible;
    for (int y = 0; y < viewport()->height() && !firstVisible.isValid(); ++y) {
        firstVisible = indexAt(QPoint(viewport()->width() / 2, y));
    }
    if (firstVisible.isValid() && firstVisible.column() != 0) {
        firstVisible = firstVisible.sibling(firstVisible.row(), 0);
    }
    for (QModelIndex index = firstVisible; index.isValid(); index = indexBelow(index)) {
        const QRect rect = mutationRowRect(index);
        if (rect.top() >= viewport()->height()) {
            break;
        }
        if (!rect.isValid() || rect.bottom() < 0 || rect.intersects(coveredByPriorTransition)) {
            continue;
        }
        transaction->before.append({QPersistentModelIndex(index), mutationRowIdentity(index), rect,
                                    rect, captureMutationRow(rect, beforeSurface),
                                    MutationRowRole::Survivor});
    }

    const auto currentRect = [&transaction, this](const QModelIndex& index) {
        const QPersistentModelIndex persistent(index);
        for (const CapturedMutationRow& row : std::as_const(transaction->before)) {
            if (row.index.isValid() && row.index == persistent) {
                return row.rect;
            }
        }
        return mutationRowRect(index);
    };
    const auto afterRemovedRoots = [this, sourceParent, first, last](QModelIndex index) {
        while (index.isValid()) {
            QModelIndex root = index;
            while (root.parent().isValid() && root.parent() != sourceParent) {
                root = root.parent();
            }
            if (root.parent() != sourceParent || root.row() < first || root.row() > last) {
                return index;
            }
            index = indexBelow(index);
        }
        return QModelIndex{};
    };

    if (kind == RowMutationKind::Insert) {
        if (sourceParent.isValid() && !isExpanded(sourceParent)) {
            transaction->seam = -1;
        } else {
            QModelIndex continuation = model()->index(first, 0, sourceParent);
            if (!continuation.isValid()) {
                if (first > 0) {
                    QModelIndex previous = model()->index(first - 1, 0, sourceParent);
                    QModelIndex following = indexBelow(previous);
                    while (following.isValid() && isDescendantOf(following, previous)) {
                        previous = following;
                        following = indexBelow(following);
                    }
                    continuation = following;
                    transaction->seam = continuation.isValid() ? currentRect(continuation).top()
                                                               : currentRect(previous).bottom() + 1;
                } else if (sourceParent.isValid()) {
                    continuation = indexBelow(sourceParent);
                    transaction->seam = continuation.isValid()
                                            ? currentRect(continuation).top()
                                            : currentRect(sourceParent).bottom() + 1;
                } else {
                    transaction->seam = 0;
                }
            } else if (continuation.isValid()) {
                transaction->seam = currentRect(continuation).top();
            }
            transaction->continuation = continuation;
            transaction->continuationIdentity = mutationRowIdentity(continuation);
            transaction->continuationRect = currentRect(continuation);
        }
    } else {
        const QModelIndex firstRemoved = model()->index(first, 0, sourceParent);
        const QRect firstRect = currentRect(firstRemoved);
        transaction->seam = firstRect.isValid() ? firstRect.top() : -1;
        QModelIndex continuation = afterRemovedRoots(firstRemoved);
        transaction->continuation = continuation;
        transaction->continuationIdentity = mutationRowIdentity(continuation);
        transaction->continuationRect = currentRect(continuation);
        if (kind == RowMutationKind::Move && destinationRow >= 0 &&
            (!destinationParent.isValid() || isExpanded(destinationParent))) {
            const QModelIndex destination = model()->index(destinationRow, 0, destinationParent);
            const QRect destinationRect = currentRect(destination);
            if (destinationRect.isValid()) {
                transaction->seam = transaction->seam < 0
                                        ? destinationRect.top()
                                        : std::min(transaction->seam, destinationRect.top());
            }
        }
    }
    m_pendingMutation = std::move(transaction);
}

void VTreeView::completeRowMutation(const RowMutationKind kind) {
    if (m_pendingMutation == nullptr || m_pendingMutation->kind != kind || model() == nullptr) {
        m_pendingMutation.reset();
        return;
    }
    std::unique_ptr<RowMutationTransaction> transaction = std::move(m_pendingMutation);
    doItemsLayout();
    QTreeView::updateGeometries();
    const QPixmap afterSurface = captureMutationSurface();

    m_mutationStartScrollMaximum = transaction->startScrollMaximum;
    m_mutationTargetScrollMaximum =
        verticalScrollBar() == nullptr ? 0 : verticalScrollBar()->maximum();
    m_mutationVerticalScrollValue = transaction->startScrollValue;
    m_mutationHorizontalScrollValue =
        horizontalScrollBar() == nullptr ? 0 : horizontalScrollBar()->value();
    if (!VkThemeManager::instance()->animationsEnabled() || !isVisible() || transaction->seam < 0 ||
        viewport()->width() <= 0 || viewport()->height() <= 0) {
        return;
    }

    QVector<CapturedMutationRow> after;
    QModelIndex firstVisible;
    for (int y = 0; y < viewport()->height() && !firstVisible.isValid(); ++y) {
        firstVisible = indexAt(QPoint(viewport()->width() / 2, y));
    }
    if (firstVisible.isValid() && firstVisible.column() != 0) {
        firstVisible = firstVisible.sibling(firstVisible.row(), 0);
    }
    for (QModelIndex index = firstVisible; index.isValid(); index = indexBelow(index)) {
        const QRect rect = mutationRowRect(index);
        if (rect.top() >= viewport()->height()) {
            break;
        }
        if (!rect.isValid() || rect.bottom() < 0) {
            continue;
        }
        after.append({QPersistentModelIndex(index), mutationRowIdentity(index), rect, rect,
                      captureMutationRow(rect, afterSurface), MutationRowRole::Survivor});
    }

    const auto beforeRectFor = [&transaction](const QPersistentModelIndex& index,
                                              const QString& identity) {
        for (const CapturedMutationRow& row : std::as_const(transaction->before)) {
            if ((row.index.isValid() && index.isValid() && row.index == index) ||
                (!identity.isEmpty() && row.identity == identity)) {
                return row.rect;
            }
        }
        return QRect{};
    };
    int translation = 0;
    if (transaction->continuation.isValid() || !transaction->continuationIdentity.isEmpty()) {
        const QRect from =
            beforeRectFor(transaction->continuation, transaction->continuationIdentity);
        QRect to = transaction->continuation.isValid() ? mutationRowRect(transaction->continuation)
                                                       : QRect{};
        if (!to.isValid()) {
            for (const CapturedMutationRow& row : std::as_const(after)) {
                if (row.identity == transaction->continuationIdentity) {
                    to = row.rect;
                    break;
                }
            }
        }
        const QRect effectiveFrom = from.isValid() ? from : transaction->continuationRect;
        if (effectiveFrom.isValid() && to.isValid()) {
            translation = to.top() - effectiveFrom.top();
        }
    }
    if (translation == 0 && kind == RowMutationKind::Insert) {
        int top = std::numeric_limits<int>::max();
        int bottom = std::numeric_limits<int>::min();
        for (int row = transaction->first; row <= transaction->last; ++row) {
            const QModelIndex inserted = model()->index(row, 0, transaction->sourceParent);
            if (!inserted.isValid()) {
                continue;
            }
            QModelIndex cursor = inserted;
            do {
                const QRect rect = mutationRowRect(cursor);
                if (rect.isValid()) {
                    top = std::min(top, rect.top());
                    bottom = std::max(bottom, rect.bottom() + 1);
                }
                cursor = indexBelow(cursor);
            } while (cursor.isValid() && isDescendantOf(cursor, inserted));
        }
        if (top != std::numeric_limits<int>::max() && bottom > top) {
            translation = bottom - top;
        }
    }
    if (translation == 0 && kind == RowMutationKind::Remove) {
        int bottom = transaction->seam;
        for (const CapturedMutationRow& row : std::as_const(transaction->before)) {
            if (!row.index.isValid()) {
                bottom = std::max(bottom, row.rect.bottom() + 1);
            }
        }
        translation = transaction->seam - bottom;
    }

    const auto insertedRoot = [&transaction](QModelIndex index) {
        while (index.isValid() && index.parent() != transaction->sourceParent) {
            index = index.parent();
        }
        return index.isValid() && index.parent() == transaction->sourceParent &&
               index.row() >= transaction->first && index.row() <= transaction->last;
    };

    QVector<MutationRow> rows;
    rows.reserve(transaction->before.size() + after.size());
    QVector<bool> afterUsed(after.size(), false);
    for (const CapturedMutationRow& before : std::as_const(transaction->before)) {
        int match = -1;
        for (int index = 0; index < after.size(); ++index) {
            if (afterUsed[index]) {
                continue;
            }
            const bool persistentMatch = before.index.isValid() && after[index].index.isValid() &&
                                         after[index].index == before.index;
            const bool identityMatch =
                !before.identity.isEmpty() && after[index].identity == before.identity;
            if (persistentMatch || identityMatch) {
                match = index;
                break;
            }
        }
        if (match >= 0) {
            afterUsed[match] = true;
            rows.append({after[match].index, after[match].identity, before.rect, after[match].rect,
                         before.pixmap.isNull() ? after[match].pixmap : before.pixmap,
                         MutationRowRole::Survivor, before.clipTop});
            continue;
        }
        if (before.index.isValid()) {
            const QRect target = mutationRowRect(before.index);
            if (target.isValid()) {
                rows.append({before.index, before.identity, before.rect, target, before.pixmap,
                             MutationRowRole::Survivor, before.clipTop});
                continue;
            }
        }
        QRect target = before.eventualRect;
        if (before.role != MutationRowRole::Leaving || !target.isValid() || target == before.rect) {
            target = before.rect.translated(0, translation);
        }
        rows.append({before.index, before.identity, before.rect, target, before.pixmap,
                     MutationRowRole::Leaving, std::max(before.clipTop, transaction->seam)});
    }
    for (int index = 0; index < after.size(); ++index) {
        if (afterUsed[index]) {
            continue;
        }
        const CapturedMutationRow& target = after[index];
        const bool entering = kind == RowMutationKind::Insert && insertedRoot(target.index);
        QRect from = target.rect;
        if (kind == RowMutationKind::Move) {
            from.moveTop(transaction->seam);
        } else {
            from.translate(0, -translation);
        }
        rows.append({target.index, target.identity, from, target.rect, target.pixmap,
                     entering ? MutationRowRole::Entering : MutationRowRole::Survivor,
                     entering ? transaction->seam : std::numeric_limits<int>::min()});
    }

    QRect dirty;
    for (const MutationRow& row : std::as_const(rows)) {
        if (row.fromRect == row.toRect && row.role == MutationRowRole::Survivor) {
            continue;
        }
        QRect bounds = row.fromRect.united(row.toRect);
        if (row.clipTop != std::numeric_limits<int>::min()) {
            bounds.setTop(std::max(bounds.top(), row.clipTop));
        }
        dirty = dirty.isNull() ? bounds : dirty.united(bounds);
    }
    if (dirty.isNull()) {
        updateMutationScrollRange(1.0);
        return;
    }
    dirty.setLeft(0);
    dirty.setRight(viewport()->width() - 1);
    dirty = dirty.intersected(viewport()->rect());
    if (!dirty.isValid()) {
        updateMutationScrollRange(1.0);
        return;
    }

    QVector<MutationRow> visibleRows;
    visibleRows.reserve(rows.size());
    for (MutationRow& row : rows) {
        if (row.fromRect.intersects(dirty) || row.toRect.intersects(dirty)) {
            visibleRows.append(std::move(row));
        }
    }
    if (visibleRows.isEmpty()) {
        updateMutationScrollRange(1.0);
        return;
    }

    const QString kindName = kind == RowMutationKind::Insert   ? QStringLiteral("insert")
                             : kind == RowMutationKind::Remove ? QStringLiteral("remove")
                                                               : QStringLiteral("move");
    auto* overlay = new RowMutationOverlay(viewport());
    overlay->setGeometry(dirty);
    overlay->setRows(std::move(visibleRows), dirty, transaction->seam, disclosureSurfaceColor(),
                     kindName);
    overlay->show();
    overlay->raise();
    m_mutationOverlay = overlay;
    updateMutationScrollRange(0.0);
    m_mutationTimeline->setDuration(FinderDisclosureDurationMs);
    m_mutationTimeline->setDirection(QTimeLine::Forward);
    m_mutationTimeline->setCurrentTime(0);
    m_mutationTimeline->start();
}

void VTreeView::updateMutationScrollRange(const qreal progress) {
    auto* scrollBar = verticalScrollBar();
    if (m_mutationOverlay == nullptr || scrollBar == nullptr) {
        return;
    }
    const qreal bounded = std::clamp(progress, 0.0, 1.0);
    const int maximum =
        m_mutationStartScrollMaximum +
        qRound(bounded * (m_mutationTargetScrollMaximum - m_mutationStartScrollMaximum));
    const QSignalBlocker blocker(scrollBar);
    scrollBar->setRange(scrollBar->minimum(), std::max(scrollBar->minimum(), maximum));
    scrollBar->setValue(
        std::clamp(m_mutationVerticalScrollValue, scrollBar->minimum(), scrollBar->maximum()));
    m_mutationOverlay->setProperty("startScrollMaximum", m_mutationStartScrollMaximum);
    m_mutationOverlay->setProperty("targetScrollMaximum", m_mutationTargetScrollMaximum);
    m_mutationOverlay->setProperty("currentScrollMaximum", scrollBar->maximum());
}

void VTreeView::finishRowMutationAnimation() {
    if (m_mutationTimeline != nullptr && m_mutationTimeline->state() != QTimeLine::NotRunning) {
        m_mutationTimeline->stop();
    }
    if (m_mutationOverlay == nullptr) {
        m_pendingMutation.reset();
        return;
    }
    const QRect dirty = m_mutationOverlay->geometry();
    delete m_mutationOverlay.data();
    m_mutationOverlay = nullptr;
    m_pendingMutation.reset();
    QTreeView::updateGeometries();
    viewport()->update(dirty);
}

void VTreeView::setExpandedAnimated(const QModelIndex& index, const bool expanded) {
    finishRowMutationAnimation();
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

void VTreeView::updateDisclosureScrollRange(const qreal progress) {
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

void VTreeView::finishDisclosureAnimation() {
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

void VTreeView::changeEvent(QEvent* event) {
    if (event != nullptr &&
        (event->type() == QEvent::PaletteChange ||
         event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::FontChange ||
         event->type() == QEvent::ApplicationFontChange)) {
        finishDisclosureAnimation();
        finishRowMutationAnimation();
    }
    QTreeView::changeEvent(event);
}

void VTreeView::drawBranches(QPainter* painter, const QRect& rect,
                             const QModelIndex& index) const {
    if (!m_branchLinesVisible || painter == nullptr || model() == nullptr ||
        !index.isValid() || !index.parent().isValid()) {
        return;
    }

    const QModelIndex parent = index.parent();
    const bool lastChild = index.row() == model()->rowCount(parent) - 1;
    const QRect itemRect = visualRect(index);
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
    const int itemEdge = rightToLeft ? itemRect.right() : itemRect.left();
    const qreal centerY = rect.center().y() + 0.5;
    qreal lineX = itemEdge + (rightToLeft ? indentation() * 0.5 : -indentation() * 0.5);

    painter->save();
    QPen pen(VkThemeManager::instance()->theme().colors().separator, 0.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->drawLine(QPointF(lineX, rect.top()),
                      QPointF(lineX, lastChild ? centerY : rect.bottom() + 1.0));
    const qreal horizontalEnd =
        itemEdge + (rightToLeft ? VTreeItemDelegate::HorizontalInset + 2.0
                                : -VTreeItemDelegate::HorizontalInset - 2.0);
    painter->drawLine(QPointF(lineX, centerY), QPointF(horizontalEnd, centerY));

    // Continue non-terminal ancestor branches through this row. Immediate
    // and ancestor connectors share the same indentation grid at every depth.
    for (QModelIndex ancestor = parent; ancestor.parent().isValid();
         ancestor = ancestor.parent()) {
        lineX += rightToLeft ? indentation() : -indentation();
        const QModelIndex grandParent = ancestor.parent();
        if (ancestor.row() < model()->rowCount(grandParent) - 1) {
            painter->drawLine(QPointF(lineX, rect.top()),
                              QPointF(lineX, rect.bottom() + 1.0));
        }
    }
    painter->restore();
}

void VTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const {
    if (painter != nullptr) {
        painter->fillRect(QRect(viewport()->rect().left(), option.rect.top(),
                                viewport()->rect().width(), option.rect.height()),
                          disclosureSurfaceColor());
    }
    QTreeView::drawRow(painter, option, index);
    const QRect itemRect = visualRect(index);
    if (painter != nullptr && !m_branchLinesVisible &&
        ((layoutDirection() == Qt::LeftToRight &&
          itemRect.left() > viewport()->rect().left()) ||
         (layoutDirection() == Qt::RightToLeft &&
          itemRect.right() < viewport()->rect().right()))) {
        // QTreeView's native selected-row surface can extend through the
        // hierarchy branch rectangle even though native branches are hidden.
        // Clearing that strip after the native row pass keeps selection and
        // hover strictly inside the delegate's icon-and-label surface.
        const QRect branchRect =
            layoutDirection() == Qt::RightToLeft
                ? QRect(itemRect.right() + 1, option.rect.top(),
                        viewport()->rect().right() - itemRect.right(), option.rect.height())
                : QRect(viewport()->rect().left(), option.rect.top(),
                        itemRect.left() - viewport()->rect().left(), option.rect.height());
        painter->fillRect(branchRect, disclosureSurfaceColor());
    }
}

void VTreeView::armExpansionIconPress(const QPoint& position, const Qt::MouseButton button) {
    m_expansionIconPressed = false;
    m_pressedExpansionIndex = QPersistentModelIndex();
    if (button != Qt::LeftButton) {
        return;
    }
    const QModelIndex index = indexAt(position);
    if (expansionToggleRect(index).contains(position)) {
        m_expansionIconPressed = true;
        m_pressedExpansionIndex = index;
    }
}

void VTreeView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event == nullptr) {
        return;
    }
    armExpansionIconPress(event->position().toPoint(), event->button());
    if (m_expansionIconPressed) {
        // A real rapid second click arrives as MouseButtonDblClick instead of
        // MouseButtonPress. Arm its following release without letting
        // QTreeView reinterpret the icon as an edit or native disclosure.
        event->accept();
        return;
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void VTreeView::mousePressEvent(QMouseEvent* event) {
    if (event != nullptr) {
        armExpansionIconPress(event->position().toPoint(), event->button());
    }
    QTreeView::mousePressEvent(event);
}

void VTreeView::mouseReleaseEvent(QMouseEvent* event) {
    const QPersistentModelIndex pressedIndex = m_pressedExpansionIndex;
    const bool activate = event != nullptr && event->button() == Qt::LeftButton &&
                          m_expansionIconPressed && pressedIndex.isValid() &&
                          indexAt(event->position().toPoint()) == pressedIndex &&
                          expansionToggleRect(pressedIndex).contains(event->position().toPoint());
    m_expansionIconPressed = false;
    m_pressedExpansionIndex = QPersistentModelIndex();

    QTreeView::mouseReleaseEvent(event);
    if (activate) {
        const QModelIndex index = pressedIndex;
        setExpandedAnimated(index, !isExpanded(index));
        emit expansionIconClicked(index);
    }
}

void VTreeView::resizeEvent(QResizeEvent* event) {
    finishRowMutationAnimation();
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

void VTreeView::scrollContentsBy(const int dx, const int dy) {
    if (m_mutationOverlay != nullptr && !m_restoringMutationScroll &&
        QApplication::mouseButtons() == Qt::NoButton) {
        m_restoringMutationScroll = true;
        if (auto* horizontal = horizontalScrollBar();
            horizontal != nullptr && horizontal->value() != m_mutationHorizontalScrollValue) {
            const QSignalBlocker blocker(horizontal);
            horizontal->setValue(m_mutationHorizontalScrollValue);
        }
        if (auto* vertical = verticalScrollBar();
            vertical != nullptr && vertical->value() != m_mutationVerticalScrollValue) {
            const QSignalBlocker blocker(vertical);
            vertical->setValue(m_mutationVerticalScrollValue);
        }
        m_restoringMutationScroll = false;
        return;
    }
    if (m_mutationOverlay != nullptr) {
        finishRowMutationAnimation();
    }
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

void VTreeView::updateGeometries() {
    QTreeView::updateGeometries();
    auto* overlay = static_cast<DisclosureGroupOverlay*>(m_disclosureOverlay.data());
    if (overlay != nullptr) {
        updateDisclosureScrollRange(overlay->property("progress").toReal());
    }
    auto* mutationOverlay = static_cast<RowMutationOverlay*>(m_mutationOverlay.data());
    if (mutationOverlay != nullptr) {
        updateMutationScrollRange(mutationOverlay->property("progress").toReal());
    }
}

void VTreeView::wheelEvent(QWheelEvent* event) {
    // A real wheel gesture takes precedence over the disclosure transaction;
    // direct scrollbar drags are distinguished by QApplication::mouseButtons
    // in scrollContentsBy().
    finishDisclosureAnimation();
    finishRowMutationAnimation();
    QTreeView::wheelEvent(event);
}

bool VTreeView::isDescendantOf(const QModelIndex& index,
                                         const QModelIndex& ancestor) const {
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
        if (parent == ancestor) {
            return true;
        }
    }
    return false;
}

} // namespace vkui
