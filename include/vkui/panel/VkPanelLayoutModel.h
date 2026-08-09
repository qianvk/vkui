#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QChar>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringView>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>

namespace vkui::panel {

struct VkPanelRectangleCache;

enum class VkPanelSplitAxis : std::uint8_t
{
    Horizontal,
    Vertical,
};

enum class VkSpatialDirection : std::uint8_t
{
    Left,
    Down,
    Up,
    Right,
};

enum class VkPanelRegionKind : std::uint8_t
{
    Panel,
    Group,
};

enum class VkPanelSnapshotMode : std::uint8_t
{
    /** Reflects current panel and group visibility. */
    Effective,
    /** Ignores collapse state for the complete <Leader>e layout diagram. */
    Expanded,
};

struct VkPanelSpec final
{
    QString id;
    QString title;
    QChar label;
    bool visible = true;
    /**
     * Stable provider family used to decide which leaves may form a chooser
     * group. Empty remains a valid family for SDK/tests that do not classify
     * panels; different non-equal values are never combined.
     */
    QString compositionKey{};

    friend bool operator==(const VkPanelSpec &,
                           const VkPanelSpec &) = default;
};

struct VkPanelLayoutRegion final
{
    QString id;
    QString title;
    QChar label;
    QRectF normalizedRect;
    VkPanelRegionKind kind = VkPanelRegionKind::Panel;
    bool visible = true;
    /** Exact descendant leaves; avoids reconstructing tree membership from QRectF. */
    QVector<QString> memberPanelIds;
    /** Common provider family for a composable panel/group. */
    QString compositionKey{};

    friend bool operator==(const VkPanelLayoutRegion &,
                           const VkPanelLayoutRegion &) = default;
};

/** User-owned state for one geometry-derived rectangular panel group. */
struct VkPanelCompositeState final
{
    QVector<QString> memberPanelIds;
    QChar label;
    /** True only for a deterministic chooser label that may be rebalanced. */
    bool automaticLabel = false;

    friend bool operator==(const VkPanelCompositeState &,
                           const VkPanelCompositeState &) = default;
};

/** Stable across tree rotations because identity depends only on leaf IDs. */
[[nodiscard]] QString stablePanelCompositeId(
    QVector<QString> memberPanelIds);

/** The nearest split divider affected by a directional resize. */
struct VkPanelResizeTarget final
{
    QString panelId;
    QString groupId;
    VkSpatialDirection direction = VkSpatialDirection::Right;
    VkPanelSplitAxis axis = VkPanelSplitAxis::Horizontal;
    bool panelOnFirstSide = true;
    double currentFirstRatio = 0.5;
    double proposedFirstRatio = 0.5;
    /** Caller-space signed edge delta; negative moves the edge inward. */
    double signedDelta = 0.0;

    friend bool operator==(const VkPanelResizeTarget &,
                           const VkPanelResizeTarget &) = default;
};

/** Nearest same-axis divider for Vim's grow/shrink extent semantics. */
struct VkPanelExtentResizeTarget final
{
    QString panelId;
    QString groupId;
    VkPanelSplitAxis axis = VkPanelSplitAxis::Horizontal;
    bool panelOnFirstSide = true;
    double currentFirstRatio = 0.5;
    double proposedFirstRatio = 0.5;
    double signedDelta = 0.0;

    friend bool operator==(const VkPanelExtentResizeTarget &,
                           const VkPanelExtentResizeTarget &) = default;
};

/**
 * Value snapshot consumed by overlays and persistence adapters.
 *
 * A snapshot has no back-reference to the mutable model. Snapshots from one
 * generation share a thread-safe, write-once rectangle cache, so the panel
 * chooser paints coherently without recomputing geometry on every frame.
 */
class VkPanelLayoutSnapshot final
{
public:
    VkPanelLayoutSnapshot() = default;

    [[nodiscard]] quint64 generation() const noexcept;
    [[nodiscard]] VkPanelSnapshotMode mode() const noexcept;
    [[nodiscard]] const QVector<VkPanelLayoutRegion> &regions()
        const noexcept;
    [[nodiscard]] std::optional<VkPanelLayoutRegion> region(
        QStringView id) const;
    /**
     * Every proper rectangular leaf union, including aligned unions that
     * cross structural split-tree branches.
     */
    [[nodiscard]] const QVector<VkPanelLayoutRegion> &
    rectangularPanelRegions() const &;
    /** Keeps references from escaping a temporary snapshot. */
    [[nodiscard]] QVector<VkPanelLayoutRegion>
    rectangularPanelRegions() &&;
    /** Rectangles that have a concrete single-key chooser address. */
    [[nodiscard]] const QVector<VkPanelLayoutRegion> &
    addressablePanelRegions() const &;
    [[nodiscard]] QVector<VkPanelLayoutRegion>
    addressablePanelRegions() &&;
    [[nodiscard]] QVector<QVector<QString>>
    rectangularPanelCombinations() const;

private:
    friend class VkPanelLayoutModel;

    quint64 m_generation = 0;
    VkPanelSnapshotMode m_mode = VkPanelSnapshotMode::Effective;
    QVector<VkPanelLayoutRegion> m_regions;
    QHash<QString, VkPanelCompositeState> m_compositeStates;
    mutable std::shared_ptr<VkPanelRectangleCache>
        m_rectangleCache;
};

/**
 * Renderer-independent workspace split tree.
 *
 * Panel and group IDs are stable semantic identities; physical QSplitter
 * indexes are deliberately absent. Geometry-derived chooser targets are
 * cached by immutable generation and invalidated only by a model mutation.
 */
class VkPanelLayoutModel final
{
public:
    VkPanelLayoutModel();
    ~VkPanelLayoutModel();

    VkPanelLayoutModel(const VkPanelLayoutModel &) = delete;
    VkPanelLayoutModel &operator=(const VkPanelLayoutModel &) = delete;

    /** Creates a value-identical model while preserving generation identity. */
    [[nodiscard]] std::unique_ptr<VkPanelLayoutModel> clone() const;

    [[nodiscard]] bool setRoot(VkPanelSpec panel, QString *error = nullptr);
    void clear();

    [[nodiscard]] bool split(
        QStringView existingPanelId,
        VkPanelSpec newPanel,
        VkPanelSplitAxis axis,
        bool placeAfter = true,
        QString groupId = {},
        QChar groupLabel = {},
        QString *error = nullptr);
    /** Wraps the complete current layout in one new outer split. */
    [[nodiscard]] bool splitRoot(
        VkPanelSpec newPanel,
        VkPanelSplitAxis axis,
        bool placeAfter = true,
        QString groupId = {},
        QChar groupLabel = {},
        QString *error = nullptr);
    [[nodiscard]] bool close(QStringView panelId);

    /**
     * Commits visibility to the exact addressed leaf set.
     *
     * Leaves are the sole persistent visibility authority. Structural and
     * geometric groups are commands over their member leaves, so a later
     * leaf toggle cannot inherit or clear a second ancestor/composite mask.
     */
    [[nodiscard]] bool setVisible(QStringView id, bool visible);
    [[nodiscard]] bool toggle(QStringView id);
    [[nodiscard]] bool isVisible(QStringView id) const;
    /**
     * Returns every leaf below an exact structural or geometric group.
     * Root is intentionally absent from chooser snapshots but remains a
     * valid renderer boundary, so resize projection must use this API.
     */
    [[nodiscard]] QVector<QString> memberPanelIds(
        QStringView id) const;

    /** Assigns one unique ASCII letter to a panel or non-root group. */
    [[nodiscard]] bool setLabel(
        QStringView id,
        QChar label,
        QString *error = nullptr);
    /** Assigns stable unused letters to new geometry-derived groups, up to 52. */
    void assignAutomaticRectangleLabels();
    /**
     * Releases only unused, visible synthetic labels before a topology edit.
     * Hidden composites represent an explicit user action and keep their
     * identity; panel and structural-group labels are never touched.
     */
    void clearAutomaticRectangleLabels();

    /**
     * Moves one exact physical edge of the target.
     *
     * The nearest matching ancestor divider is adjusted. delta is a
     * normalized fraction: positive moves outward, negative moves inward,
     * and the result is clamped so both sides remain usable.
     */
    [[nodiscard]] bool resize(
        QStringView panelId,
        VkSpatialDirection direction,
        double delta = 0.05);
    [[nodiscard]] std::optional<VkPanelResizeTarget> resizeTarget(
        QStringView panelId,
        VkSpatialDirection direction,
        double delta = 0.05) const;
    [[nodiscard]] std::optional<VkPanelExtentResizeTarget>
    extentResizeTarget(
        QStringView panelId,
        VkPanelSplitAxis axis,
        double signedDelta) const;
    [[nodiscard]] bool resizeExtent(
        QStringView panelId,
        VkPanelSplitAxis axis,
        double signedDelta);
    /** Equalizes every split by descendant leaf count. */
    [[nodiscard]] bool equalize();
    /** Maximizes one panel through every ancestor on the requested axis. */
    [[nodiscard]] bool maximizeExtent(
        QStringView panelId,
        VkPanelSplitAxis axis);
    /** Commits the exact renderer ratio at the LCA divider of two leaves. */
    [[nodiscard]] bool setSplitRatioBetween(
        QStringView firstPanelId,
        QStringView secondPanelId,
        VkPanelSplitAxis axis,
        double firstRatio);

    [[nodiscard]] std::optional<QString> adjacentPanel(
        QStringView panelId,
        VkSpatialDirection direction) const;
    [[nodiscard]] std::optional<VkSpatialDirection> collapseDirection(
        QStringView id) const;
    /** Returns the first leaf in the exact structural sibling subtree. */
    [[nodiscard]] std::optional<QString> siblingPanel(
        QStringView panelId) const;

    [[nodiscard]] bool containsPanel(QStringView panelId) const;
    [[nodiscard]] bool containsGroup(QStringView groupId) const;
    /** Reconciles a mounted leaf with its live provider during migration. */
    [[nodiscard]] bool setPanelCompositionKey(
        QStringView panelId,
        QString compositionKey);
    [[nodiscard]] qsizetype panelCount() const noexcept;
    /** Includes inactive persisted rectangle labels to prevent ABA clashes. */
    [[nodiscard]] QSet<QChar> assignedLabels() const;
    [[nodiscard]] quint64 generation() const noexcept;

    [[nodiscard]] VkPanelLayoutSnapshot snapshot(
        VkPanelSnapshotMode mode = VkPanelSnapshotMode::Effective) const;

    /** Versioned, bounded JSON intended for the workspace metadata store. */
    [[nodiscard]] QByteArray save() const;
    [[nodiscard]] bool restore(
        const QByteArray &serialized,
        QString *error = nullptr);

private:
    struct Node;

    [[nodiscard]] Node *find(QStringView id) const;
    [[nodiscard]] Node *findPanel(QStringView id) const;
    [[nodiscard]] std::unique_ptr<Node> *ownerOf(Node *node);
    [[nodiscard]] bool splitNode(
        Node *existing,
        VkPanelSpec newPanel,
        VkPanelSplitAxis axis,
        bool placeAfter,
        QString groupId,
        QChar groupLabel,
        QString *error);
    [[nodiscard]] QString nextGroupId();
    void pruneCompositeStates();
    void didMutate();

    std::unique_ptr<Node> m_root;
    QHash<QString, VkPanelCompositeState> m_compositeStates;
    quint64 m_generation = 0;
    quint64 m_nextGroup = 1;
    mutable std::shared_ptr<VkPanelRectangleCache>
        m_expandedRectangleCache;
    mutable std::shared_ptr<VkPanelRectangleCache>
        m_effectiveRectangleCache;
};

/**
 * Model/view projection of one immutable panel-layout snapshot.
 *
 * A delegate can draw the normalized rectangles directly and place each
 * single-letter target at its geometric center.
 */
class VkPanelChooserModel final : public QAbstractListModel
{
public:
    enum Role {
        IdentityRole = Qt::UserRole + 1,
        TitleRole,
        LabelRole,
        NormalizedRectRole,
        RegionKindRole,
        VisibleRole,
        GenerationRole,
    };

    explicit VkPanelChooserModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(
        const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index,
        int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setSnapshot(VkPanelLayoutSnapshot snapshot);
    [[nodiscard]] const VkPanelLayoutSnapshot &snapshot() const noexcept;

private:
    VkPanelLayoutSnapshot m_snapshot;
};

} // namespace vkui::panel

Q_DECLARE_METATYPE(vkui::panel::VkPanelRegionKind)
