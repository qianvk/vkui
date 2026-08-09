#include <vkui/panel/VkPanelLayoutModel.h>
#include <vkui/panel/VkSpatialNavigation.h>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <ranges>
#include <string_view>
#include <utility>

namespace vkui::panel {
namespace {

constexpr int kLayoutSchemaVersion = 1;
constexpr int kMaximumDepth = 64;
constexpr int kMaximumPanels = 512;
constexpr int kMaximumCompositeStates = 2'048;
constexpr int kMaximumIdentityLength = 128;
constexpr int kMaximumTitleLength = 512;
constexpr double kMinimumRatio = 0.08;
constexpr double kMaximumRatio = 0.92;
constexpr qint64 kNormalizedCoordinateScale = 1'000'000'000;
constexpr auto kPanelLabelAlphabet =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

struct CompressedPanelGeometry final
{
    QString id;
    QString compositionKey;
    qint64 left = 0;
    qint64 top = 0;
    qint64 right = 0;
    qint64 bottom = 0;
    int leftIndex = 0;
    int topIndex = 0;
    int rightIndex = 0;
    int bottomIndex = 0;
};

struct RectangularLeafUnion final
{
    QVector<QString> members;
    QRectF rect;
    QString compositionKey;
};

[[nodiscard]] qint64 normalizedCoordinate(const qreal value)
{
    return std::llround(
        std::clamp<qreal>(value, 0.0, 1.0)
        * static_cast<qreal>(kNormalizedCoordinateScale));
}

[[nodiscard]] int coordinateIndex(
    const QVector<qint64> &coordinates,
    const qint64 value)
{
    const auto found = std::ranges::lower_bound(
        coordinates, value);
    return found == coordinates.cend()
        ? -1
        : static_cast<int>(
              std::distance(coordinates.cbegin(), found));
}

/**
 * Enumerates every leaf union whose exact normalized coverage is a rectangle.
 *
 * A valid union cannot cut through the interior of a leaf on any of its four
 * edges. Candidate corners only need to come from a leaf's top-left and
 * another leaf's bottom-right corner, reducing the candidate set to O(n^2).
 * Coordinate-compressed crossing prefixes then reject a candidate in O(1).
 * Materializing member IDs is proportional to the returned data itself.
 */
[[nodiscard]] QVector<RectangularLeafUnion> rectangularLeafUnions(
    const QVector<VkPanelLayoutRegion> &regions)
{
    QVector<CompressedPanelGeometry> panels;
    QVector<qint64> xCoordinates;
    QVector<qint64> yCoordinates;
    for (const VkPanelLayoutRegion &region : regions) {
        if (region.kind != VkPanelRegionKind::Panel
            || region.normalizedRect.isEmpty()) {
            continue;
        }
        CompressedPanelGeometry panel;
        panel.id = region.id;
        panel.compositionKey = region.compositionKey;
        panel.left = normalizedCoordinate(
            region.normalizedRect.left());
        panel.top = normalizedCoordinate(
            region.normalizedRect.top());
        panel.right = normalizedCoordinate(
            region.normalizedRect.left()
            + region.normalizedRect.width());
        panel.bottom = normalizedCoordinate(
            region.normalizedRect.top()
            + region.normalizedRect.height());
        if (panel.left >= panel.right
            || panel.top >= panel.bottom) {
            continue;
        }
        xCoordinates.push_back(panel.left);
        xCoordinates.push_back(panel.right);
        yCoordinates.push_back(panel.top);
        yCoordinates.push_back(panel.bottom);
        panels.push_back(std::move(panel));
    }
    if (panels.isEmpty()) {
        return {};
    }

    std::ranges::sort(xCoordinates);
    xCoordinates.erase(
        std::ranges::unique(xCoordinates).begin(),
        xCoordinates.end());
    std::ranges::sort(yCoordinates);
    yCoordinates.erase(
        std::ranges::unique(yCoordinates).begin(),
        yCoordinates.end());
    for (CompressedPanelGeometry &panel : panels) {
        panel.leftIndex = coordinateIndex(
            xCoordinates, panel.left);
        panel.rightIndex = coordinateIndex(
            xCoordinates, panel.right);
        panel.topIndex = coordinateIndex(
            yCoordinates, panel.top);
        panel.bottomIndex = coordinateIndex(
            yCoordinates, panel.bottom);
    }

    const int xBoundaryCount = static_cast<int>(
        xCoordinates.size());
    const int yBoundaryCount = static_cast<int>(
        yCoordinates.size());
    const int xCellCount = std::max(0, xBoundaryCount - 1);
    const int yCellCount = std::max(0, yBoundaryCount - 1);
    if (xCellCount == 0 || yCellCount == 0) {
        return {};
    }

    // For each vertical boundary, accumulate the y-cells crossed by a leaf's
    // interior. The final per-boundary prefix answers a segment query in O(1).
    QVector<int> verticalDifference(
        xBoundaryCount * (yCellCount + 1), 0);
    for (const CompressedPanelGeometry &panel : panels) {
        for (int x = panel.leftIndex + 1;
             x < panel.rightIndex;
             ++x) {
            ++verticalDifference[
                x * (yCellCount + 1) + panel.topIndex];
            --verticalDifference[
                x * (yCellCount + 1) + panel.bottomIndex];
        }
    }
    QVector<int> verticalCrossingPrefix(
        xBoundaryCount * (yCellCount + 1), 0);
    for (int x = 0; x < xBoundaryCount; ++x) {
        int active = 0;
        for (int y = 0; y < yCellCount; ++y) {
            active += verticalDifference[
                x * (yCellCount + 1) + y];
            verticalCrossingPrefix[
                x * (yCellCount + 1) + y + 1]
                = verticalCrossingPrefix[
                      x * (yCellCount + 1) + y]
                + (active > 0 ? 1 : 0);
        }
    }

    QVector<int> horizontalDifference(
        yBoundaryCount * (xCellCount + 1), 0);
    for (const CompressedPanelGeometry &panel : panels) {
        for (int y = panel.topIndex + 1;
             y < panel.bottomIndex;
             ++y) {
            ++horizontalDifference[
                y * (xCellCount + 1) + panel.leftIndex];
            --horizontalDifference[
                y * (xCellCount + 1) + panel.rightIndex];
        }
    }
    QVector<int> horizontalCrossingPrefix(
        yBoundaryCount * (xCellCount + 1), 0);
    for (int y = 0; y < yBoundaryCount; ++y) {
        int active = 0;
        for (int x = 0; x < xCellCount; ++x) {
            active += horizontalDifference[
                y * (xCellCount + 1) + x];
            horizontalCrossingPrefix[
                y * (xCellCount + 1) + x + 1]
                = horizontalCrossingPrefix[
                      y * (xCellCount + 1) + x]
                + (active > 0 ? 1 : 0);
        }
    }

    const auto verticalEdgeIsClear = [
        &verticalCrossingPrefix,
        yCellCount](const int x, const int top, const int bottom) {
        const int stride = yCellCount + 1;
        return verticalCrossingPrefix[x * stride + bottom]
            == verticalCrossingPrefix[x * stride + top];
    };
    const auto horizontalEdgeIsClear = [
        &horizontalCrossingPrefix,
        xCellCount](const int y, const int left, const int right) {
        const int stride = xCellCount + 1;
        return horizontalCrossingPrefix[y * stride + right]
            == horizontalCrossingPrefix[y * stride + left];
    };

    QVector<RectangularLeafUnion> result;
    QSet<QString> admitted;
    for (const CompressedPanelGeometry &topLeft : panels) {
        for (const CompressedPanelGeometry &bottomRight : panels) {
            const int left = topLeft.leftIndex;
            const int top = topLeft.topIndex;
            const int right = bottomRight.rightIndex;
            const int bottom = bottomRight.bottomIndex;
            if (left >= right || top >= bottom
                || !verticalEdgeIsClear(left, top, bottom)
                || !verticalEdgeIsClear(right, top, bottom)
                || !horizontalEdgeIsClear(top, left, right)
                || !horizontalEdgeIsClear(bottom, left, right)) {
                continue;
            }

            QVector<QString> members;
            QString compositionKey;
            bool hasCompositionKey = false;
            bool mixedComposition = false;
            for (const CompressedPanelGeometry &panel : panels) {
                if (panel.leftIndex >= left
                    && panel.rightIndex <= right
                    && panel.topIndex >= top
                    && panel.bottomIndex <= bottom) {
                    members.push_back(panel.id);
                    if (!hasCompositionKey) {
                        compositionKey = panel.compositionKey;
                        hasCompositionKey = true;
                    } else if (compositionKey
                               != panel.compositionKey) {
                        mixedComposition = true;
                    }
                }
            }
            // The maximum/root rectangle is intentionally not an addressable
            // chooser target. Every proper rectangle, including one leaf, is.
            if (members.isEmpty() || mixedComposition
                || (panels.size() > 1
                    && members.size() == panels.size())) {
                continue;
            }
            std::ranges::sort(members);
            const QString key = members.join(QChar::Null);
            if (!admitted.contains(key)) {
                admitted.insert(key);
                result.push_back(RectangularLeafUnion{
                    std::move(members),
                    QRectF(
                        static_cast<qreal>(
                            xCoordinates[left])
                            / kNormalizedCoordinateScale,
                        static_cast<qreal>(
                            yCoordinates[top])
                            / kNormalizedCoordinateScale,
                        static_cast<qreal>(
                            xCoordinates[right]
                            - xCoordinates[left])
                            / kNormalizedCoordinateScale,
                        static_cast<qreal>(
                            yCoordinates[bottom]
                            - yCoordinates[top])
                            / kNormalizedCoordinateScale),
                    compositionKey});
            }
        }
    }
    std::ranges::sort(
        result,
        [](const RectangularLeafUnion &first,
           const RectangularLeafUnion &second) {
            if (first.members.size() != second.members.size()) {
                return first.members.size()
                    < second.members.size();
            }
            return std::lexicographical_compare(
                first.members.cbegin(), first.members.cend(),
                second.members.cbegin(), second.members.cend());
        });
    return result;
}

[[nodiscard]] bool validIdentity(const QStringView value)
{
    return !value.isEmpty()
        && value.size() <= kMaximumIdentityLength
        && !value.contains(QChar::Null);
}

[[nodiscard]] bool validCompositionKey(const QStringView value)
{
    return value.size() <= kMaximumIdentityLength
        && !value.contains(QChar::Null);
}

[[nodiscard]] QChar normalizedLabel(const QChar label)
{
    const ushort value = label.unicode();
    if (!((value >= u'a' && value <= u'z')
          || (value >= u'A' && value <= u'Z'))) {
        return {};
    }
    // VkCore distinguishes shifted character mappings, so lower- and
    // upper-case ASCII labels form 52 independent chooser targets.
    return label;
}

[[nodiscard]] QString directionName(const VkPanelSplitAxis axis)
{
    return axis == VkPanelSplitAxis::Horizontal
        ? QStringLiteral("horizontal")
        : QStringLiteral("vertical");
}

[[nodiscard]] std::optional<VkPanelSplitAxis> splitAxis(
    const QStringView value)
{
    if (value == QStringLiteral("horizontal")) {
        return VkPanelSplitAxis::Horizontal;
    }
    if (value == QStringLiteral("vertical")) {
        return VkPanelSplitAxis::Vertical;
    }
    return std::nullopt;
}

[[nodiscard]] bool horizontal(const VkSpatialDirection direction)
{
    return direction == VkSpatialDirection::Left
        || direction == VkSpatialDirection::Right;
}

} // namespace

struct VkPanelRectangleCache final
{
    std::once_flag once;
    QVector<VkPanelLayoutRegion> regions;
    QVector<VkPanelLayoutRegion> addressableRegions;
};

QString stablePanelCompositeId(
    QVector<QString> memberPanelIds)
{
    std::ranges::sort(memberPanelIds);
    memberPanelIds.erase(
        std::ranges::unique(memberPanelIds).begin(),
        memberPanelIds.end());
    QByteArray canonical;
    for (const QString &member : memberPanelIds) {
        const QByteArray encoded = member.toUtf8();
        canonical.append(QByteArray::number(encoded.size()));
        canonical.append(':');
        canonical.append(encoded);
        canonical.append(';');
    }
    return QStringLiteral("panel.rectangle.%1").arg(
        QString::fromLatin1(
            QCryptographicHash::hash(
                canonical,
                QCryptographicHash::Sha256)
                .toHex()));
}

struct VkPanelLayoutModel::Node final
{
    [[nodiscard]] bool isPanel() const noexcept
    {
        return first == nullptr && second == nullptr;
    }

    Node *parent = nullptr;
    QString id;
    QString title;
    QString compositionKey;
    QChar label;
    bool visible = true;
    VkPanelSplitAxis axis = VkPanelSplitAxis::Horizontal;
    double firstRatio = 0.5;
    std::unique_ptr<Node> first;
    std::unique_ptr<Node> second;
};

quint64 VkPanelLayoutSnapshot::generation() const noexcept
{
    return m_generation;
}

VkPanelSnapshotMode VkPanelLayoutSnapshot::mode() const noexcept
{
    return m_mode;
}

const QVector<VkPanelLayoutRegion> &
VkPanelLayoutSnapshot::regions() const noexcept
{
    return m_regions;
}

std::optional<VkPanelLayoutRegion> VkPanelLayoutSnapshot::region(
    const QStringView id) const
{
    const auto found = std::find_if(
        m_regions.cbegin(), m_regions.cend(),
        [id](const VkPanelLayoutRegion &candidate) {
            return candidate.id == id;
        });
    if (found != m_regions.cend()) {
        return *found;
    }
    const QString requested = id.toString();
    if (!requested.startsWith(
            QStringLiteral("panel.rectangle."))
        && !m_compositeStates.contains(requested)) {
        return std::nullopt;
    }
    const QVector<VkPanelLayoutRegion> &rectangles =
        rectangularPanelRegions();
    const auto rectangle = std::ranges::find_if(
        rectangles,
        [id](const VkPanelLayoutRegion &candidate) {
            return candidate.id == id;
        });
    return rectangle == rectangles.cend()
        ? std::nullopt
        : std::optional<VkPanelLayoutRegion>(*rectangle);
}

const QVector<VkPanelLayoutRegion> &
VkPanelLayoutSnapshot::rectangularPanelRegions() const &
{
    if (m_rectangleCache == nullptr) {
        m_rectangleCache =
            std::make_shared<VkPanelRectangleCache>();
    }
    std::call_once(
        m_rectangleCache->once,
        [this] {
            const QVector<RectangularLeafUnion> unions =
                rectangularLeafUnions(m_regions);
            QHash<QString, VkPanelLayoutRegion>
                structuralByMembers;
            for (const VkPanelLayoutRegion &region : m_regions) {
                QVector<QString> members =
                    region.memberPanelIds;
                std::ranges::sort(members);
                structuralByMembers.insert(
                    members.join(QChar::Null), region);
            }

            auto &result = m_rectangleCache->regions;
            result.reserve(unions.size());
            for (const RectangularLeafUnion &rectangle : unions) {
                const QString memberKey =
                    rectangle.members.join(QChar::Null);
                const auto structural =
                    structuralByMembers.constFind(memberKey);
                if (structural
                    != structuralByMembers.cend()) {
                    VkPanelLayoutRegion region =
                        structural.value();
                    // Effective snapshots may promote a structural node
                    // after one child disappears. Geometry is authoritative
                    // for this immutable generation.
                    region.normalizedRect = rectangle.rect;
                    region.memberPanelIds = rectangle.members;
                    region.compositionKey =
                        rectangle.compositionKey;
                    result.push_back(std::move(region));
                    continue;
                }

                const QString identity =
                    stablePanelCompositeId(rectangle.members);
                const auto state =
                    m_compositeStates.constFind(identity);
                const bool membersVisible = std::ranges::all_of(
                    rectangle.members,
                    [this](const QString &member) {
                        const auto panel = std::ranges::find_if(
                            m_regions,
                            [&member](
                                const VkPanelLayoutRegion &region) {
                                return region.kind
                                        == VkPanelRegionKind::Panel
                                    && region.id == member;
                            });
                        return panel != m_regions.cend()
                            && panel->visible;
                    });
                result.push_back(VkPanelLayoutRegion{
                    identity,
                    QStringLiteral("Panel group"),
                    state == m_compositeStates.cend()
                        ? QChar{}
                        : state->label,
                    rectangle.rect,
                    VkPanelRegionKind::Group,
                    membersVisible,
                    rectangle.members,
                    rectangle.compositionKey});
            }
            auto &addressable =
                m_rectangleCache->addressableRegions;
            addressable.reserve(result.size());
            std::ranges::copy_if(
                result,
                std::back_inserter(addressable),
                [](const VkPanelLayoutRegion &region) {
                    return !region.label.isNull();
                });
        });
    return m_rectangleCache->regions;
}

QVector<VkPanelLayoutRegion>
VkPanelLayoutSnapshot::rectangularPanelRegions() &&
{
    return static_cast<const VkPanelLayoutSnapshot &>(*this)
        .rectangularPanelRegions();
}

const QVector<VkPanelLayoutRegion> &
VkPanelLayoutSnapshot::addressablePanelRegions() const &
{
    static_cast<void>(rectangularPanelRegions());
    return m_rectangleCache->addressableRegions;
}

QVector<VkPanelLayoutRegion>
VkPanelLayoutSnapshot::addressablePanelRegions() &&
{
    return static_cast<const VkPanelLayoutSnapshot &>(*this)
        .addressablePanelRegions();
}

QVector<QVector<QString>>
VkPanelLayoutSnapshot::rectangularPanelCombinations() const
{
    QVector<QVector<QString>> result;
    const auto &rectangles = rectangularPanelRegions();
    result.reserve(rectangles.size());
    for (const VkPanelLayoutRegion &rectangle : rectangles) {
        result.push_back(rectangle.memberPanelIds);
    }
    return result;
}

VkPanelLayoutModel::VkPanelLayoutModel() = default;
VkPanelLayoutModel::~VkPanelLayoutModel() = default;

std::unique_ptr<VkPanelLayoutModel> VkPanelLayoutModel::clone() const
{
    auto result = std::make_unique<VkPanelLayoutModel>();
    const auto copyNode = [](
                              const auto &self,
                              const Node *const source,
                              Node *const parent) -> std::unique_ptr<Node> {
        if (source == nullptr) {
            return nullptr;
        }
        auto node = std::make_unique<Node>();
        node->parent = parent;
        node->id = source->id;
        node->title = source->title;
        node->compositionKey = source->compositionKey;
        node->label = source->label;
        node->visible = source->visible;
        node->axis = source->axis;
        node->firstRatio = source->firstRatio;
        node->first = self(self, source->first.get(), node.get());
        node->second = self(self, source->second.get(), node.get());
        return node;
    };
    result->m_root = copyNode(copyNode, m_root.get(), nullptr);
    result->m_compositeStates = m_compositeStates;
    result->m_generation = m_generation;
    result->m_nextGroup = m_nextGroup;
    return result;
}

bool VkPanelLayoutModel::setRoot(
    VkPanelSpec panel,
    QString *const error)
{
    if (m_root != nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("The panel layout already has a root.");
        }
        return false;
    }
    if (!validIdentity(panel.id)
        || !validCompositionKey(panel.compositionKey)
        || panel.title.size() > kMaximumTitleLength
        || (!panel.label.isNull()
            && normalizedLabel(panel.label).isNull())) {
        if (error != nullptr) {
            *error = QStringLiteral("The root panel descriptor is invalid.");
        }
        return false;
    }
    panel.label = normalizedLabel(panel.label);
    m_root = std::make_unique<Node>();
    m_root->id = std::move(panel.id);
    m_root->title = std::move(panel.title);
    m_root->compositionKey = std::move(panel.compositionKey);
    m_root->label = panel.label;
    m_root->visible = panel.visible;
    didMutate();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void VkPanelLayoutModel::clear()
{
    if (m_root == nullptr) {
        return;
    }
    m_root.reset();
    m_compositeStates.clear();
    m_nextGroup = 1;
    didMutate();
}

bool VkPanelLayoutModel::split(
    const QStringView existingPanelId,
    VkPanelSpec newPanel,
    const VkPanelSplitAxis axis,
    const bool placeAfter,
    QString groupId,
    QChar groupLabel,
    QString *const error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    Node *const existing = findPanel(existingPanelId);
    if (existing == nullptr) {
        return fail(QStringLiteral("The source panel does not exist."));
    }
    return splitNode(
        existing,
        std::move(newPanel),
        axis,
        placeAfter,
        std::move(groupId),
        groupLabel,
        error);
}

bool VkPanelLayoutModel::splitRoot(
    VkPanelSpec newPanel,
    const VkPanelSplitAxis axis,
    const bool placeAfter,
    QString groupId,
    const QChar groupLabel,
    QString *const error)
{
    if (m_root == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "The panel layout has no root to split.");
        }
        return false;
    }
    return splitNode(
        m_root.get(),
        std::move(newPanel),
        axis,
        placeAfter,
        std::move(groupId),
        groupLabel,
        error);
}

bool VkPanelLayoutModel::splitNode(
    Node *const existing,
    VkPanelSpec newPanel,
    const VkPanelSplitAxis axis,
    const bool placeAfter,
    QString groupId,
    QChar groupLabel,
    QString *const error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (existing == nullptr) {
        return fail(QStringLiteral(
            "The split target does not exist."));
    }
    if (!validIdentity(newPanel.id)
        || !validCompositionKey(newPanel.compositionKey)
        || newPanel.title.size() > kMaximumTitleLength
        || find(newPanel.id) != nullptr
        || m_compositeStates.contains(newPanel.id)) {
        return fail(QStringLiteral("The new panel identity is invalid or already used."));
    }
    const QChar originalPanelLabel = newPanel.label;
    newPanel.label = normalizedLabel(newPanel.label);
    if (!originalPanelLabel.isNull() && newPanel.label.isNull()) {
        return fail(QStringLiteral("Panel labels must be ASCII letters."));
    }
    if (!newPanel.label.isNull()) {
        if (assignedLabels().contains(newPanel.label)) {
            return fail(QStringLiteral("The panel label is already assigned."));
        }
    }
    if (groupId.isEmpty()) {
        groupId = nextGroupId();
    }
    if (!validIdentity(groupId) || find(groupId) != nullptr
        || m_compositeStates.contains(groupId)
        || groupId == newPanel.id) {
        return fail(QStringLiteral("The split group identity is invalid or already used."));
    }
    const Node *const originalParent = existing->parent;
    const QChar originalGroupLabel = groupLabel;
    groupLabel = normalizedLabel(groupLabel);
    if (!originalGroupLabel.isNull() && groupLabel.isNull()) {
        return fail(QStringLiteral("Group labels must be ASCII letters."));
    }
    // The maximum window group is not a chooser target and therefore never
    // consumes one of the user's single-letter panel targets.
    if (originalParent == nullptr) {
        groupLabel = {};
    }
    if (!groupLabel.isNull()) {
        if (assignedLabels().contains(groupLabel)
            || groupLabel == newPanel.label) {
            return fail(QStringLiteral("The group label is already assigned."));
        }
    }
    if (panelCount() >= kMaximumPanels) {
        return fail(QStringLiteral("The panel layout limit has been reached."));
    }

    std::unique_ptr<Node> *const owner = ownerOf(existing);
    if (owner == nullptr) {
        return fail(QStringLiteral("The panel layout is inconsistent."));
    }
    Node *const oldParent = existing->parent;
    std::unique_ptr<Node> previous = std::move(*owner);
    auto inserted = std::make_unique<Node>();
    inserted->id = std::move(newPanel.id);
    inserted->title = std::move(newPanel.title);
    inserted->compositionKey = std::move(
        newPanel.compositionKey);
    inserted->label = newPanel.label;
    inserted->visible = newPanel.visible;

    auto branch = std::make_unique<Node>();
    branch->parent = oldParent;
    branch->id = std::move(groupId);
    branch->title = QStringLiteral("Panel group");
    branch->label = groupLabel;
    branch->axis = axis;
    if (placeAfter) {
        branch->first = std::move(previous);
        branch->second = std::move(inserted);
    } else {
        branch->first = std::move(inserted);
        branch->second = std::move(previous);
    }
    branch->first->parent = branch.get();
    branch->second->parent = branch.get();
    *owner = std::move(branch);
    didMutate();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool VkPanelLayoutModel::close(const QStringView panelId)
{
    Node *const leaf = findPanel(panelId);
    if (leaf == nullptr || leaf->parent == nullptr) {
        return false;
    }
    Node *const branch = leaf->parent;
    std::unique_ptr<Node> *const branchOwner = ownerOf(branch);
    if (branchOwner == nullptr) {
        return false;
    }
    std::unique_ptr<Node> sibling =
        branch->first.get() == leaf
        ? std::move(branch->second)
        : std::move(branch->first);
    sibling->parent = branch->parent;
    *branchOwner = std::move(sibling);
    pruneCompositeStates();
    didMutate();
    return true;
}

bool VkPanelLayoutModel::setVisible(
    const QStringView id,
    const bool visible)
{
    QVector<QString> members;
    if (Node *const node = find(id)) {
        const auto collect = [&members](
                                 const auto &self,
                                 Node *const candidate) -> void {
            if (candidate == nullptr) {
                return;
            }
            if (candidate->isPanel()) {
                members.push_back(candidate->id);
                return;
            }
            self(self, candidate->first.get());
            self(self, candidate->second.get());
        };
        collect(collect, node);
    } else {
        const auto rectangle = snapshot(
            VkPanelSnapshotMode::Expanded).region(id);
        if (!rectangle
            || rectangle->kind != VkPanelRegionKind::Group) {
            return false;
        }
        members = rectangle->memberPanelIds;
        auto state = m_compositeStates.find(id.toString());
        if (state == m_compositeStates.end()) {
            if (m_compositeStates.size()
                >= kMaximumCompositeStates) {
                return false;
            }
            state = m_compositeStates.insert(
                id.toString(),
                VkPanelCompositeState{members, {}, false});
        } else if (state->memberPanelIds != members) {
            return false;
        }
    }
    if (members.isEmpty()) {
        return false;
    }

    // Leaf visibility is the only persistent authority. Groups are commands
    // over an exact leaf set, never a second hidden bit layered over their
    // children. This makes partial -> expanded -> collapsed deterministic
    // and prevents one panel from silently reopening an unrelated sibling.
    bool changed = false;
    for (const QString &member : std::as_const(members)) {
        Node *const leaf = findPanel(member);
        if (leaf == nullptr) {
            return false;
        }
        if (leaf->visible != visible) {
            leaf->visible = visible;
            changed = true;
        }
    }
    if (changed) {
        didMutate();
    }
    return true;
}

bool VkPanelLayoutModel::toggle(const QStringView id)
{
    if (Node *const node = find(id)) {
        static_cast<void>(node);
        // A group is visible only when every addressed leaf is visible. A
        // partial group therefore expands in one press before it collapses.
        return setVisible(id, !isVisible(id));
    }
    const auto rectangle = snapshot(
        VkPanelSnapshotMode::Expanded)
                               .region(id);
    if (!rectangle
        || rectangle->kind != VkPanelRegionKind::Group) {
        return false;
    }
    // Exact member visibility gives a partially hidden rectangle
    // deterministic tri-state toggle semantics:
    // one press expands every member; the next collapses the combination.
    return setVisible(id, !isVisible(id));
}

bool VkPanelLayoutModel::isVisible(const QStringView id) const
{
    const Node *node = find(id);
    if (node != nullptr) {
        const auto allVisible = [](const auto &self,
                                   const Node *candidate) -> bool {
            if (candidate == nullptr) {
                return false;
            }
            if (candidate->isPanel()) {
                return candidate->visible;
            }
            return self(self, candidate->first.get())
                && self(self, candidate->second.get());
        };
        return allVisible(allVisible, node);
    }
    const auto rectangle = snapshot(
        VkPanelSnapshotMode::Expanded)
                               .region(id);
    if (!rectangle
        || rectangle->kind != VkPanelRegionKind::Group) {
        return false;
    }
    return std::ranges::all_of(
            rectangle->memberPanelIds,
            [this](const QString &member) {
                return isVisible(member);
            });
}

QVector<QString> VkPanelLayoutModel::memberPanelIds(
    const QStringView id) const
{
    QVector<QString> result;
    if (const Node *const node = find(id)) {
        const auto collect = [&result](const auto &self,
                                       const Node *candidate) -> void {
            if (candidate == nullptr) {
                return;
            }
            if (candidate->isPanel()) {
                result.push_back(candidate->id);
                return;
            }
            self(self, candidate->first.get());
            self(self, candidate->second.get());
        };
        collect(collect, node);
    } else {
        const auto rectangle = snapshot(
            VkPanelSnapshotMode::Expanded).region(id);
        if (rectangle
            && rectangle->kind == VkPanelRegionKind::Group) {
            result = rectangle->memberPanelIds;
        }
    }
    std::ranges::sort(result);
    return result;
}

bool VkPanelLayoutModel::setLabel(
    const QStringView id,
    QChar label,
    QString *const error)
{
    Node *const node = find(id);
    const auto rectangle = node == nullptr
        ? snapshot(VkPanelSnapshotMode::Expanded).region(id)
        : std::optional<VkPanelLayoutRegion>{};
    const QChar original = label;
    label = normalizedLabel(label);
    const auto commonCompositionKey = [](
        const auto &self,
        const Node *const candidate) -> std::optional<QString> {
        if (candidate == nullptr) {
            return std::nullopt;
        }
        if (candidate->isPanel()) {
            return candidate->compositionKey;
        }
        const auto first = self(self, candidate->first.get());
        const auto second = self(self, candidate->second.get());
        return first && second && *first == *second
            ? first
            : std::nullopt;
    };
    if ((node == nullptr
         && (!rectangle
             || rectangle->kind != VkPanelRegionKind::Group))
        || (node == m_root.get() && !node->isPanel())
        || (node != nullptr && !node->isPanel()
            && !label.isNull()
            && !commonCompositionKey(
                commonCompositionKey, node))
        || (!original.isNull() && label.isNull())) {
        if (error != nullptr) {
            *error = QStringLiteral("The target or ASCII panel label is invalid.");
        }
        return false;
    }
    bool labelOwnedByOther = false;
    const auto findLabelOwner = [
        id,
        label,
        &labelOwnedByOther](const auto &self,
                            const Node *candidate) -> void {
        if (candidate == nullptr || labelOwnedByOther) {
            return;
        }
        if (candidate->id != id
            && candidate->label == label) {
            labelOwnedByOther = true;
            return;
        }
        self(self, candidate->first.get());
        self(self, candidate->second.get());
    };
    if (!label.isNull()) {
        findLabelOwner(findLabelOwner, m_root.get());
        if (!labelOwnedByOther) {
            labelOwnedByOther = std::ranges::any_of(
                m_compositeStates.asKeyValueRange(),
                [id, label](const auto &entry) {
                    return entry.first != id
                        && entry.second.label == label;
                });
        }
    }
    if (labelOwnedByOther) {
        if (error != nullptr) {
            *error = QStringLiteral("The panel label is already assigned.");
        }
        return false;
    }
    if (node != nullptr && node->label != label) {
        node->label = label;
        didMutate();
    } else if (node == nullptr) {
        auto state = m_compositeStates.find(id.toString());
        if (state == m_compositeStates.end()) {
            if (m_compositeStates.size()
                >= kMaximumCompositeStates) {
                if (error != nullptr) {
                    *error = QStringLiteral(
                        "The panel rectangle state limit has been reached.");
                }
                return false;
            }
            state = m_compositeStates.insert(
                id.toString(),
                VkPanelCompositeState{
                    rectangle->memberPanelIds, {}, false});
        }
        if (state->memberPanelIds
                != rectangle->memberPanelIds) {
            if (error != nullptr) {
                *error = QStringLiteral(
                    "The panel rectangle identity conflicts with another member set.");
            }
            return false;
        }
        if (state->label != label) {
            state->label = label;
            state->automaticLabel = false;
            didMutate();
        } else if (state->automaticLabel) {
            // Explicitly choosing the generated letter turns it into
            // user-owned state, so a later structural split cannot reclaim
            // it behind the user's back.
            state->automaticLabel = false;
            didMutate();
        }
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void VkPanelLayoutModel::clearAutomaticRectangleLabels()
{
    bool changed = false;
    for (auto state = m_compositeStates.begin();
         state != m_compositeStates.end();) {
        const bool membersVisible = std::ranges::all_of(
            state->memberPanelIds,
            [this](const QString &member) {
                const Node *const leaf = findPanel(member);
                return leaf != nullptr && leaf->visible;
            });
        if (!state->automaticLabel || !membersVisible) {
            ++state;
            continue;
        }
        changed = true;
        state = m_compositeStates.erase(state);
    }
    if (changed) {
        didMutate();
    }
}

void VkPanelLayoutModel::assignAutomaticRectangleLabels()
{
    VkPanelLayoutSnapshot expanded = snapshot(
        VkPanelSnapshotMode::Expanded);
    QVector<VkPanelLayoutRegion> rectangles =
        expanded.rectangularPanelRegions();

    // A split can invalidate a synthetic rectangle without removing any of
    // its leaves. Prune by exact current geometry, not merely by member
    // existence, so dead identities cannot reserve one of the 52 keys.
    QSet<QString> validRectangles;
    validRectangles.reserve(rectangles.size());
    for (const VkPanelLayoutRegion &region : rectangles) {
        validRectangles.insert(region.id);
    }
    bool pruned = false;
    for (auto state = m_compositeStates.begin();
         state != m_compositeStates.end();) {
        if (!validRectangles.contains(state.key())) {
            state = m_compositeStates.erase(state);
            pruned = true;
        } else {
            ++state;
        }
    }
    if (pruned) {
        didMutate();
        expanded = snapshot(VkPanelSnapshotMode::Expanded);
        rectangles = expanded.rectangularPanelRegions();
    }
    QSet<QChar> used = assignedLabels();
    QString available;
    for (const char value :
         std::string_view(kPanelLabelAlphabet)) {
        const QChar label = QLatin1Char(value);
        if (!used.contains(label)) {
            available.push_back(label);
        }
    }

    qsizetype nextLabel = 0;
    bool changed = false;
    for (const VkPanelLayoutRegion &region : rectangles) {
        if (!region.label.isNull()
            || nextLabel >= available.size()) {
            continue;
        }
        const QChar label = available.at(nextLabel++);
        if (Node *const structural = find(region.id)) {
            structural->label = label;
            changed = true;
            continue;
        }
        auto state = m_compositeStates.find(region.id);
        if (state == m_compositeStates.end()) {
            if (m_compositeStates.size()
                >= kMaximumCompositeStates) {
                break;
            }
            state = m_compositeStates.insert(
                region.id,
                VkPanelCompositeState{
                    region.memberPanelIds,
                    label,
                    true});
            changed = true;
            continue;
        }
        if (state->label.isNull()) {
            state->label = label;
            state->automaticLabel = true;
            changed = true;
        }
    }
    if (changed) {
        didMutate();
    }
}

bool VkPanelLayoutModel::resize(
    const QStringView panelId,
    const VkSpatialDirection direction,
    const double delta)
{
    const auto target = resizeTarget(panelId, direction, delta);
    if (!target) {
        return false;
    }
    Node *const branch = find(target->groupId);
    if (branch == nullptr || branch->isPanel()
        || !qFuzzyCompare(
            branch->firstRatio, target->currentFirstRatio)) {
        return false;
    }
    branch->firstRatio = target->proposedFirstRatio;
    didMutate();
    return true;
}

std::optional<VkPanelResizeTarget>
VkPanelLayoutModel::resizeTarget(
    const QStringView panelId,
    const VkSpatialDirection direction,
    const double delta) const
{
    const Node *node = findPanel(panelId);
    if (node == nullptr || !std::isfinite(delta)
        || qFuzzyIsNull(delta)) {
        return std::nullopt;
    }
    const VkPanelSplitAxis wanted = horizontal(direction)
        ? VkPanelSplitAxis::Horizontal
        : VkPanelSplitAxis::Vertical;
    while (node->parent != nullptr) {
        const Node *const branch = node->parent;
        const bool isFirst = branch->first.get() == node;
        const bool touchesRequestedDivider =
            (direction == VkSpatialDirection::Right
                || direction == VkSpatialDirection::Down)
                ? isFirst
                : !isFirst;
        if (branch->axis == wanted && touchesRequestedDivider) {
            const double proposed = std::clamp(
                branch->firstRatio
                    + (isFirst ? delta : -delta),
                kMinimumRatio,
                kMaximumRatio);
            if (qFuzzyCompare(proposed, branch->firstRatio)) {
                return std::nullopt;
            }
            return VkPanelResizeTarget{
                panelId.toString(),
                branch->id,
                direction,
                branch->axis,
                isFirst,
                branch->firstRatio,
                proposed,
                delta};
        }
        node = branch;
    }
    return std::nullopt;
}

std::optional<VkPanelExtentResizeTarget>
VkPanelLayoutModel::extentResizeTarget(
    const QStringView panelId,
    const VkPanelSplitAxis axis,
    const double signedDelta) const
{
    const Node *node = findPanel(panelId);
    if (node == nullptr || !std::isfinite(signedDelta)
        || qFuzzyIsNull(signedDelta)) {
        return std::nullopt;
    }
    while (node->parent != nullptr) {
        const Node *const branch = node->parent;
        if (branch->axis == axis) {
            const bool onFirst =
                branch->first.get() == node;
            const double proposed = std::clamp(
                branch->firstRatio
                    + (onFirst
                           ? signedDelta
                           : -signedDelta),
                kMinimumRatio,
                kMaximumRatio);
            if (qFuzzyCompare(
                    proposed, branch->firstRatio)) {
                return std::nullopt;
            }
            return VkPanelExtentResizeTarget{
                panelId.toString(),
                branch->id,
                axis,
                onFirst,
                branch->firstRatio,
                proposed,
                signedDelta};
        }
        node = branch;
    }
    return std::nullopt;
}

bool VkPanelLayoutModel::resizeExtent(
    const QStringView panelId,
    const VkPanelSplitAxis axis,
    const double signedDelta)
{
    const auto target = extentResizeTarget(
        panelId, axis, signedDelta);
    if (!target) {
        return false;
    }
    Node *const branch = find(target->groupId);
    if (branch == nullptr || branch->isPanel()
        || !qFuzzyCompare(
            branch->firstRatio,
            target->currentFirstRatio)) {
        return false;
    }
    branch->firstRatio = target->proposedFirstRatio;
    didMutate();
    return true;
}

bool VkPanelLayoutModel::equalize()
{
    if (m_root == nullptr) {
        return false;
    }
    const auto countLeaves = [](
        const auto &self,
        const Node *node) -> qsizetype {
        if (node == nullptr) {
            return 0;
        }
        return node->isPanel()
            ? qsizetype{1}
            : self(self, node->first.get())
                + self(self, node->second.get());
    };
    bool changed = false;
    const auto apply = [
        &countLeaves,
        &changed](const auto &self, Node *node) -> void {
        if (node == nullptr || node->isPanel()) {
            return;
        }
        const qsizetype first = countLeaves(
            countLeaves, node->first.get());
        const qsizetype total = first + countLeaves(
            countLeaves, node->second.get());
        if (total > 0) {
            const double ratio = std::clamp(
                static_cast<double>(first)
                    / static_cast<double>(total),
                kMinimumRatio,
                kMaximumRatio);
            if (!qFuzzyCompare(node->firstRatio, ratio)) {
                node->firstRatio = ratio;
                changed = true;
            }
        }
        self(self, node->first.get());
        self(self, node->second.get());
    };
    apply(apply, m_root.get());
    if (changed) {
        didMutate();
    }
    return true;
}

bool VkPanelLayoutModel::maximizeExtent(
    const QStringView panelId,
    const VkPanelSplitAxis axis)
{
    Node *node = findPanel(panelId);
    if (node == nullptr) {
        return false;
    }
    bool changed = false;
    while (node->parent != nullptr) {
        Node *const branch = node->parent;
        if (branch->axis == axis) {
            const bool onFirst =
                branch->first.get() == node;
            const double ratio = onFirst
                ? kMaximumRatio
                : kMinimumRatio;
            if (!qFuzzyCompare(branch->firstRatio, ratio)) {
                branch->firstRatio = ratio;
                changed = true;
            }
        }
        node = branch;
    }
    if (changed) {
        didMutate();
    }
    // Idempotent maximize, including an already full-height/full-width leaf,
    // is still a successfully handled Vim window command.
    return true;
}

bool VkPanelLayoutModel::setSplitRatioBetween(
    const QStringView firstPanelId,
    const QStringView secondPanelId,
    const VkPanelSplitAxis axis,
    const double firstRatio)
{
    Node *const firstLeaf = findPanel(firstPanelId);
    Node *const secondLeaf = findPanel(secondPanelId);
    if (firstLeaf == nullptr || secondLeaf == nullptr
        || firstLeaf == secondLeaf
        || !std::isfinite(firstRatio)) {
        return false;
    }

    QSet<Node *> firstAncestors;
    for (Node *node = firstLeaf;
         node != nullptr;
         node = node->parent) {
        firstAncestors.insert(node);
    }
    Node *common = secondLeaf;
    while (common != nullptr
           && !firstAncestors.contains(common)) {
        common = common->parent;
    }
    if (common == nullptr || common->isPanel()
        || common->axis != axis) {
        return false;
    }

    Node *firstChild = firstLeaf;
    while (firstChild->parent != common) {
        firstChild = firstChild->parent;
    }
    Node *secondChild = secondLeaf;
    while (secondChild->parent != common) {
        secondChild = secondChild->parent;
    }
    if (common->first.get() != firstChild
        || common->second.get() != secondChild) {
        return false;
    }

    const double bounded = std::clamp(
        firstRatio, kMinimumRatio, kMaximumRatio);
    if (!qFuzzyCompare(common->firstRatio, bounded)) {
        common->firstRatio = bounded;
        didMutate();
    }
    return true;
}

std::optional<QString> VkPanelLayoutModel::adjacentPanel(
    const QStringView panelId,
    const VkSpatialDirection direction) const
{
    const VkPanelLayoutSnapshot current = snapshot();
    const auto origin = current.region(panelId);
    if (!origin || origin->kind != VkPanelRegionKind::Panel
        || !origin->visible) {
        return std::nullopt;
    }
    const QRectF a = origin->normalizedRect;
    const int horizontal =
        direction == VkSpatialDirection::Left
        ? -1
        : direction == VkSpatialDirection::Right
        ? 1
        : 0;
    const int vertical =
        direction == VkSpatialDirection::Up
        ? -1
        : direction == VkSpatialDirection::Down
        ? 1
        : 0;
    std::optional<VkSpatialNavigationRank> best;
    std::optional<QString> result;
    for (const VkPanelLayoutRegion &candidate : current.regions()) {
        if (candidate.kind != VkPanelRegionKind::Panel
            || candidate.id == panelId || !candidate.visible) {
            continue;
        }
        const QRectF b = candidate.normalizedRect;
        const auto rank = spatialNavigationRank(
            a, b, horizontal, vertical);
        if (!rank) {
            continue;
        }
        const bool equalRank = best
            && !(*rank < *best)
            && !(*best < *rank);
        if (!best || *rank < *best
            || (equalRank
                && (!result
                    || candidate.id < *result))) {
            best = *rank;
            result = candidate.id;
        }
    }
    return result;
}

std::optional<VkSpatialDirection>
VkPanelLayoutModel::collapseDirection(const QStringView id) const
{
    const Node *const node = find(id);
    if (node == nullptr || node->parent == nullptr) {
        return std::nullopt;
    }
    const Node *const parent = node->parent;
    const bool first = parent->first.get() == node;
    if (parent->axis == VkPanelSplitAxis::Horizontal) {
        return first
            ? VkSpatialDirection::Left
            : VkSpatialDirection::Right;
    }
    return first
        ? VkSpatialDirection::Up
        : VkSpatialDirection::Down;
}

std::optional<QString> VkPanelLayoutModel::siblingPanel(
    const QStringView panelId) const
{
    const Node *const leaf = findPanel(panelId);
    if (leaf == nullptr || leaf->parent == nullptr) {
        return std::nullopt;
    }
    const Node *node = leaf->parent->first.get() == leaf
        ? leaf->parent->second.get()
        : leaf->parent->first.get();
    while (node != nullptr && !node->isPanel()) {
        node = node->first.get();
    }
    return node == nullptr
        ? std::nullopt
        : std::optional<QString>(node->id);
}

bool VkPanelLayoutModel::containsPanel(const QStringView panelId) const
{
    return findPanel(panelId) != nullptr;
}

bool VkPanelLayoutModel::containsGroup(const QStringView groupId) const
{
    const Node *const node = find(groupId);
    if (node != nullptr) {
        return !node->isPanel();
    }
    const auto rectangle = snapshot(
        VkPanelSnapshotMode::Expanded)
                               .region(groupId);
    return rectangle
        && rectangle->kind == VkPanelRegionKind::Group;
}

bool VkPanelLayoutModel::setPanelCompositionKey(
    const QStringView panelId,
    QString compositionKey)
{
    Node *const panel = findPanel(panelId);
    if (panel == nullptr
        || !validCompositionKey(compositionKey)) {
        return false;
    }
    if (panel->compositionKey == compositionKey) {
        return true;
    }
    panel->compositionKey = std::move(compositionKey);
    didMutate();
    return true;
}

qsizetype VkPanelLayoutModel::panelCount() const noexcept
{
    qsizetype count = 0;
    const auto visit = [&count](const auto &self, const Node *node) -> void {
        if (node == nullptr) {
            return;
        }
        if (node->isPanel()) {
            ++count;
            return;
        }
        self(self, node->first.get());
        self(self, node->second.get());
    };
    visit(visit, m_root.get());
    return count;
}

QSet<QChar> VkPanelLayoutModel::assignedLabels() const
{
    QSet<QChar> result;
    const auto collect = [&result](const auto &self,
                                   const Node *node) -> void {
        if (node == nullptr) {
            return;
        }
        if (!node->label.isNull()) {
            result.insert(node->label);
        }
        self(self, node->first.get());
        self(self, node->second.get());
    };
    collect(collect, m_root.get());
    for (const VkPanelCompositeState &state :
         m_compositeStates) {
        if (!state.label.isNull()) {
            result.insert(state.label);
        }
    }
    return result;
}

quint64 VkPanelLayoutModel::generation() const noexcept
{
    return m_generation;
}

VkPanelLayoutSnapshot VkPanelLayoutModel::snapshot(
    const VkPanelSnapshotMode mode) const
{
    VkPanelLayoutSnapshot result;
    result.m_generation = m_generation;
    result.m_mode = mode;
    result.m_compositeStates = m_compositeStates;
    auto &sharedRectangleCache =
        mode == VkPanelSnapshotMode::Expanded
        ? m_expandedRectangleCache
        : m_effectiveRectangleCache;
    if (sharedRectangleCache == nullptr) {
        sharedRectangleCache =
            std::make_shared<VkPanelRectangleCache>();
    }
    result.m_rectangleCache = sharedRectangleCache;
    if (m_root == nullptr) {
        return result;
    }

    const bool expanded = mode == VkPanelSnapshotMode::Expanded;
    const auto nodeAdmitted = [
        expanded](const Node *node) {
        return node != nullptr
            && (!node->isPanel()
                || expanded || node->visible);
    };
    const auto hasVisible = [
        &nodeAdmitted](const auto &self, const Node *node) -> bool {
        if (!nodeAdmitted(node)) {
            return false;
        }
        return node->isPanel()
            || self(self, node->first.get())
            || self(self, node->second.get());
    };
    const auto collectPanels =
        [&nodeAdmitted](const auto &self,
                        const Node *node,
                        QVector<QString> &panels) -> void {
        if (!nodeAdmitted(node)) {
            return;
        }
        if (node->isPanel()) {
            panels.push_back(node->id);
            return;
        }
        self(self, node->first.get(), panels);
        self(self, node->second.get(), panels);
    };
    const auto allVisible = [](const auto &self,
                               const Node *node) -> bool {
        if (node == nullptr) {
            return false;
        }
        if (node->isPanel()) {
            return node->visible;
        }
        return self(self, node->first.get())
            && self(self, node->second.get());
    };
    const auto append =
        [&result, &nodeAdmitted, &hasVisible,
         &collectPanels, &allVisible](
            const auto &self,
            const Node *node,
            const QRectF &rect,
            const bool root) -> void {
        if (!nodeAdmitted(node)) {
            return;
        }
        if (node->isPanel()) {
            result.m_regions.push_back({
                node->id,
                node->title,
                node->label,
                rect,
                VkPanelRegionKind::Panel,
                node->visible,
                {node->id},
                node->compositionKey});
            return;
        }
        const bool firstVisible = hasVisible(hasVisible, node->first.get());
        const bool secondVisible = hasVisible(hasVisible, node->second.get());
        if (!firstVisible && !secondVisible) {
            return;
        }
        if (!root) {
            QVector<QString> members;
            collectPanels(
                collectPanels, node, members);
            result.m_regions.push_back({
                node->id,
                node->title,
                node->label,
                rect,
                VkPanelRegionKind::Group,
                allVisible(allVisible, node),
                std::move(members),
                {}});
        }
        if (!firstVisible) {
            self(self, node->second.get(), rect, false);
            return;
        }
        if (!secondVisible) {
            self(self, node->first.get(), rect, false);
            return;
        }
        QRectF firstRect = rect;
        QRectF secondRect = rect;
        if (node->axis == VkPanelSplitAxis::Horizontal) {
            const qreal split = rect.left()
                + rect.width() * node->firstRatio;
            firstRect.setRight(split);
            secondRect.setLeft(split);
        } else {
            const qreal split = rect.top()
                + rect.height() * node->firstRatio;
            firstRect.setBottom(split);
            secondRect.setTop(split);
        }
        self(self, node->first.get(), firstRect, false);
        self(self, node->second.get(), secondRect, false);
    };
    append(append, m_root.get(), QRectF(0.0, 0.0, 1.0, 1.0), true);
    return result;
}

QByteArray VkPanelLayoutModel::save() const
{
    const auto encode = [](const auto &self, const Node *node) -> QJsonObject {
        if (node == nullptr) {
            return {};
        }
        QJsonObject object{
            {QStringLiteral("kind"),
             node->isPanel() ? QStringLiteral("panel") : QStringLiteral("group")},
            {QStringLiteral("id"), node->id},
            {QStringLiteral("title"), node->title},
            {QStringLiteral("compositionKey"),
             node->isPanel() ? node->compositionKey : QString{}},
            {QStringLiteral("label"),
             node->label.isNull() ? QString{} : QString(node->label)},
            {QStringLiteral("visible"), node->visible},
        };
        if (!node->isPanel()) {
            // Since schema v1 groups are geometry only. Keep the field for
            // backward compatibility, but leaf visibility is authoritative.
            object.insert(QStringLiteral("visible"), true);
            object.insert(QStringLiteral("axis"), directionName(node->axis));
            object.insert(QStringLiteral("ratio"), node->firstRatio);
            object.insert(QStringLiteral("first"), self(self, node->first.get()));
            object.insert(QStringLiteral("second"), self(self, node->second.get()));
        }
        return object;
    };
    QJsonArray composites;
    QStringList compositeIds = m_compositeStates.keys();
    compositeIds.sort(Qt::CaseSensitive);
    for (const QString &identity : compositeIds) {
        const auto state = m_compositeStates.constFind(identity);
        if (state == m_compositeStates.cend()) {
            continue;
        }
        QJsonArray members;
        for (const QString &member : state->memberPanelIds) {
            members.push_back(member);
        }
        composites.push_back(QJsonObject{
            {QStringLiteral("id"), identity},
            {QStringLiteral("members"), members},
            {QStringLiteral("label"),
             state->label.isNull()
                 ? QString{}
                 : QString(state->label)},
            {QStringLiteral("visible"), true},
            {QStringLiteral("automaticLabel"),
             state->automaticLabel},
        });
    }
    const QJsonObject document{
        {QStringLiteral("schemaVersion"), kLayoutSchemaVersion},
        {QStringLiteral("nextGroup"), static_cast<qint64>(m_nextGroup)},
        {QStringLiteral("root"), encode(encode, m_root.get())},
        {QStringLiteral("rectangles"), composites},
    };
    return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

bool VkPanelLayoutModel::restore(
    const QByteArray &serialized,
    QString *const error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        serialized, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return fail(QStringLiteral("The panel layout JSON is invalid."));
    }
    const QJsonObject top = document.object();
    if (top.value(QStringLiteral("schemaVersion")).toInt(-1)
        != kLayoutSchemaVersion) {
        return fail(QStringLiteral("The panel layout schema is unsupported."));
    }
    QSet<QString> identities;
    QSet<QString> panelIdentities;
    QSet<QChar> labels;
    int panels = 0;
    QString validationError;
    const auto decode =
        [&identities,
         &panelIdentities,
         &labels,
         &panels,
         &validationError](
            const auto &self,
            const QJsonValue &value,
            Node *parent,
            const int depth) -> std::unique_ptr<Node> {
        if (!value.isObject() || depth > kMaximumDepth) {
            validationError = QStringLiteral("The panel layout tree is malformed or too deep.");
            return nullptr;
        }
        const QJsonObject object = value.toObject();
        const QString kind = object.value(QStringLiteral("kind")).toString();
        const bool isPanel = kind == QStringLiteral("panel");
        if (!isPanel && kind != QStringLiteral("group")) {
            validationError = QStringLiteral("The panel layout node kind is invalid.");
            return nullptr;
        }
        auto node = std::make_unique<Node>();
        node->parent = parent;
        node->id = object.value(QStringLiteral("id")).toString();
        node->title = object.value(QStringLiteral("title")).toString();
        node->compositionKey = object.value(
            QStringLiteral("compositionKey")).toString();
        const QString labelText = object.value(QStringLiteral("label")).toString();
        node->label = labelText.isEmpty() ? QChar{} : normalizedLabel(labelText.front());
        node->visible = object.value(QStringLiteral("visible")).toBool(true);
        if (!validIdentity(node->id) || identities.contains(node->id)
            || (isPanel
                && !validCompositionKey(
                    node->compositionKey))
            || node->title.size() > kMaximumTitleLength
            || labelText.size() > 1
            || (!labelText.isEmpty() && node->label.isNull())
            || (!node->label.isNull() && labels.contains(node->label))) {
            validationError = QStringLiteral("The panel layout contains an invalid or duplicate identity/label.");
            return nullptr;
        }
        identities.insert(node->id);
        if (!node->label.isNull()) {
            labels.insert(node->label);
        }
        if (isPanel) {
            if (++panels > kMaximumPanels) {
                validationError = QStringLiteral("The panel layout contains too many panels.");
                return nullptr;
            }
            panelIdentities.insert(node->id);
            return node;
        }
        node->compositionKey.clear();
        const auto axis = splitAxis(object.value(QStringLiteral("axis")).toString());
        const double ratio = object.value(QStringLiteral("ratio")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        if (!axis || !std::isfinite(ratio)
            || ratio < kMinimumRatio || ratio > kMaximumRatio) {
            validationError = QStringLiteral("The panel split geometry is invalid.");
            return nullptr;
        }
        node->axis = *axis;
        node->firstRatio = ratio;
        node->first = self(self, object.value(QStringLiteral("first")), node.get(), depth + 1);
        if (node->first == nullptr) {
            return nullptr;
        }
        node->second = self(self, object.value(QStringLiteral("second")), node.get(), depth + 1);
        if (node->second == nullptr) {
            return nullptr;
        }
        return node;
    };

    const QJsonValue rootValue = top.value(QStringLiteral("root"));
    std::unique_ptr<Node> restored;
    if (rootValue.isObject() && !rootValue.toObject().isEmpty()) {
        restored = decode(decode, rootValue, nullptr, 0);
        if (restored == nullptr) {
            return fail(validationError.isEmpty()
                    ? QStringLiteral("The panel layout could not be restored.")
                    : validationError);
        }
    } else if (!rootValue.isObject()) {
        return fail(QStringLiteral("The panel layout root is invalid."));
    }
    const qint64 nextGroup = top.value(QStringLiteral("nextGroup")).toInteger(1);
    if (nextGroup < 1) {
        return fail(QStringLiteral("The panel group sequence is invalid."));
    }

    QHash<QString, VkPanelCompositeState>
        restoredCompositeStates;
    QSet<QString> restoredHiddenComposites;
    const QJsonValue rectanglesValue = top.value(
        QStringLiteral("rectangles"));
    if (!rectanglesValue.isUndefined()
        && !rectanglesValue.isArray()) {
        return fail(QStringLiteral(
            "The panel rectangle state is invalid."));
    }
    const QJsonArray rectangles = rectanglesValue.toArray();
    if (rectangles.size() > kMaximumCompositeStates) {
        return fail(QStringLiteral(
            "The panel layout contains too many rectangle states."));
    }
    for (const QJsonValue &value : rectangles) {
        if (!value.isObject()) {
            return fail(QStringLiteral(
                "The panel rectangle state is malformed."));
        }
        const QJsonObject object = value.toObject();
        const QString identity = object.value(
            QStringLiteral("id")).toString();
        const QJsonValue membersValue = object.value(
            QStringLiteral("members"));
        const QString labelText = object.value(
            QStringLiteral("label")).toString();
        const QChar label = labelText.isEmpty()
            ? QChar{}
            : normalizedLabel(labelText.front());
        if (!validIdentity(identity)
            || identities.contains(identity)
            || restoredCompositeStates.contains(identity)
            || !membersValue.isArray()
            || labelText.size() > 1
            || (!labelText.isEmpty() && label.isNull())
            || (!label.isNull() && labels.contains(label))) {
            return fail(QStringLiteral(
                "The panel rectangle identity or label is invalid."));
        }
        QVector<QString> members;
        for (const QJsonValue &memberValue :
             membersValue.toArray()) {
            if (!memberValue.isString()) {
                return fail(QStringLiteral(
                    "The panel rectangle member list is invalid."));
            }
            members.push_back(memberValue.toString());
        }
        std::ranges::sort(members);
        const auto uniqueEnd = std::ranges::unique(members);
        if (members.size() < 2
            || uniqueEnd.begin() != members.cend()
            || std::ranges::any_of(
                members,
                [&panelIdentities](const QString &member) {
                    return !panelIdentities.contains(member);
                })
            || stablePanelCompositeId(members) != identity) {
            return fail(QStringLiteral(
                "The panel rectangle member set is invalid."));
        }
        if (!label.isNull()) {
            labels.insert(label);
        }
        if (!object.value(QStringLiteral("visible"))
                 .toBool(true)) {
            restoredHiddenComposites.insert(identity);
        }
        restoredCompositeStates.insert(
            identity,
            VkPanelCompositeState{
                std::move(members),
                label,
                object.value(QStringLiteral("automaticLabel"))
                    .toBool(false)});
    }

    // Migrate the former layered visibility representation in-place. A
    // hidden structural group or geometric mask becomes explicit hidden
    // leaves once; every group bit is then retired as a visibility source.
    QHash<QString, Node *> restoredLeaves;
    const auto normalizeVisibility = [
        &restoredLeaves](const auto &self,
                         Node *const node,
                         const bool ancestorsVisible) -> void {
        if (node == nullptr) {
            return;
        }
        if (node->isPanel()) {
            node->visible = node->visible && ancestorsVisible;
            restoredLeaves.insert(node->id, node);
            return;
        }
        const bool descendantsVisible =
            ancestorsVisible && node->visible;
        node->visible = true;
        self(self, node->first.get(), descendantsVisible);
        self(self, node->second.get(), descendantsVisible);
    };
    normalizeVisibility(
        normalizeVisibility, restored.get(), true);
    for (auto state = restoredCompositeStates.cbegin();
         state != restoredCompositeStates.cend(); ++state) {
        if (restoredHiddenComposites.contains(state.key())) {
            for (const QString &member : state->memberPanelIds) {
                if (Node *const leaf = restoredLeaves.value(member)) {
                    leaf->visible = false;
                }
            }
        }
    }
    if (restored != nullptr && !restored->isPanel()) {
        restored->label = {};
    }
    m_root = std::move(restored);
    m_compositeStates = std::move(
        restoredCompositeStates);
    m_nextGroup = static_cast<quint64>(nextGroup);
    pruneCompositeStates();
    didMutate();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

VkPanelLayoutModel::Node *VkPanelLayoutModel::find(
    const QStringView id) const
{
    const auto visit = [id](const auto &self, Node *node) -> Node * {
        if (node == nullptr || node->id == id) {
            return node;
        }
        if (Node *const found = self(self, node->first.get())) {
            return found;
        }
        return self(self, node->second.get());
    };
    return visit(visit, m_root.get());
}

VkPanelLayoutModel::Node *VkPanelLayoutModel::findPanel(
    const QStringView id) const
{
    Node *const node = find(id);
    return node != nullptr && node->isPanel()
        ? node
        : nullptr;
}

std::unique_ptr<VkPanelLayoutModel::Node> *
VkPanelLayoutModel::ownerOf(Node *const node)
{
    if (node == nullptr) {
        return nullptr;
    }
    if (node->parent == nullptr) {
        return &m_root;
    }
    if (node->parent->first.get() == node) {
        return &node->parent->first;
    }
    if (node->parent->second.get() == node) {
        return &node->parent->second;
    }
    return nullptr;
}

QString VkPanelLayoutModel::nextGroupId()
{
    QString candidate;
    do {
        candidate = QStringLiteral("panel-group.%1").arg(m_nextGroup++);
        if (m_nextGroup == 0) {
            m_nextGroup = 1;
        }
    } while (find(candidate) != nullptr);
    return candidate;
}

void VkPanelLayoutModel::pruneCompositeStates()
{
    QSet<QString> panels;
    const auto collect = [&panels](
                             const auto &self,
                             const Node *node) -> void {
        if (node == nullptr) {
            return;
        }
        if (node->isPanel()) {
            panels.insert(node->id);
            return;
        }
        self(self, node->first.get());
        self(self, node->second.get());
    };
    collect(collect, m_root.get());
    for (auto state = m_compositeStates.begin();
         state != m_compositeStates.end();) {
        const bool invalid = state->memberPanelIds.size() < 2
            || stablePanelCompositeId(
                   state->memberPanelIds)
                != state.key()
            || std::ranges::any_of(
                state->memberPanelIds,
                [&panels](const QString &member) {
                    return !panels.contains(member);
                });
        state = invalid
            ? m_compositeStates.erase(state)
            : std::next(state);
    }
}

void VkPanelLayoutModel::didMutate()
{
    m_expandedRectangleCache.reset();
    m_effectiveRectangleCache.reset();
    ++m_generation;
    if (m_generation == 0) {
        m_generation = 1;
    }
}

VkPanelChooserModel::VkPanelChooserModel(QObject *const parent)
    : QAbstractListModel(parent)
{
}

int VkPanelChooserModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid()
        ? 0
        : static_cast<int>(std::min<qsizetype>(
              m_snapshot.addressablePanelRegions().size(),
              std::numeric_limits<int>::max()));
}

QVariant VkPanelChooserModel::data(
    const QModelIndex &index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= rowCount()) {
        return {};
    }
    const VkPanelLayoutRegion &region =
        m_snapshot.addressablePanelRegions().at(
            index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return region.title;
    case IdentityRole:
        return region.id;
    case LabelRole:
        return QString(region.label);
    case NormalizedRectRole:
        return region.normalizedRect;
    case RegionKindRole:
        return QVariant::fromValue(region.kind);
    case VisibleRole:
        return region.visible;
    case GenerationRole:
        return m_snapshot.generation();
    default:
        return {};
    }
}

QHash<int, QByteArray> VkPanelChooserModel::roleNames() const
{
    return {
        {IdentityRole, QByteArrayLiteral("identity")},
        {TitleRole, QByteArrayLiteral("title")},
        {LabelRole, QByteArrayLiteral("label")},
        {NormalizedRectRole, QByteArrayLiteral("normalizedRect")},
        {RegionKindRole, QByteArrayLiteral("regionKind")},
        {VisibleRole, QByteArrayLiteral("visible")},
        {GenerationRole, QByteArrayLiteral("generation")},
    };
}

void VkPanelChooserModel::setSnapshot(VkPanelLayoutSnapshot snapshot)
{
    beginResetModel();
    m_snapshot = std::move(snapshot);
    endResetModel();
}

const VkPanelLayoutSnapshot &
VkPanelChooserModel::snapshot() const noexcept
{
    return m_snapshot;
}

} // namespace vkui::panel
