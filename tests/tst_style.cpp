// SPDX-License-Identifier: MIT

#include "widgets/animation/private/VkWidgetAnimation_p.h"
#include "widgets/style/private/VStylePainter_p.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QQueue>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>
#include <QStyleOptionToolButton>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTableView>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#include <cmath>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/VControlSize.h>
#include <vkui/widgets/VTextStyle.h>
#include <vkui/widgets/controls/VSegmentedControl.h>
#include <vkui/widgets/controls/VSlider.h>
#include <vkui/widgets/controls/VSwitch.h>
#include <vkui/widgets/effects/VLiquidGlass.h>
#include <vkui/widgets/style/VStyle.h>
#include <vkui/widgets/views/VTreeView.h>

#if defined(Q_OS_MACOS)
#include "private/MacWindowStackProbe.h"
#endif

namespace {

class InspectableComboBox final : public vkui::VCombobox {
  public:
    using VCombobox::initStyleOption;
    using VCombobox::VCombobox;
};

class InspectableSlider final : public vkui::VSlider {
  public:
    explicit InspectableSlider(Qt::Orientation orientation) : VSlider(orientation) {}
    using VSlider::initStyleOption;
};

class PolishProbeStyle final : public QProxyStyle {
  public:
    PolishProbeStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

    void polish(QWidget* widget) override {
        ++polishCount;
        QProxyStyle::polish(widget);
    }

    void unpolish(QWidget* widget) override {
        ++unpolishCount;
        QProxyStyle::unpolish(widget);
    }

    int polishCount = 0;
    int unpolishCount = 0;
};

class InspectableToolButton final : public QToolButton {
  public:
    using QToolButton::initStyleOption;
};

class PaintCounter final : public QObject {
  public:
    int paints = 0;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Paint) {
            ++paints;
        }
        return QObject::eventFilter(watched, event);
    }
};

QImage renderWidget(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image;
}

QImage renderPopupSurface(QWidget& popup) {
    QImage image(popup.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QStyleOption option;
    option.initFrom(&popup);
    option.rect = popup.rect();
    popup.style()->drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, &popup);
    return image;
}

qreal linearChannel(qreal channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

qreal contrastRatio(const QColor& first, const QColor& second) {
    const auto luminance = [](const QColor& color) {
        return 0.2126 * linearChannel(color.redF()) + 0.7152 * linearChannel(color.greenF()) +
               0.0722 * linearChannel(color.blueF());
    };
    const qreal lighter = std::max(luminance(first), luminance(second));
    const qreal darker = std::min(luminance(first), luminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

int matchingComponents(const QImage& image, const QRect& area, const QColor& target) {
    const QRect bounds = area.intersected(image.rect());
    QSet<QPoint> matching;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            // pixelColor() converts premultiplied storage back to straight-alpha RGB.
            const QColor pixel = image.pixelColor(x, y);
            const int distance = std::abs(pixel.red() - target.red()) +
                                 std::abs(pixel.green() - target.green()) +
                                 std::abs(pixel.blue() - target.blue());
            if (pixel.alpha() > 96 && distance < 150) {
                matching.insert(QPoint(x, y));
            }
        }
    }

    int components = 0;
    while (!matching.isEmpty()) {
        ++components;
        QQueue<QPoint> pending;
        pending.enqueue(*matching.constBegin());
        matching.remove(pending.head());
        while (!pending.isEmpty()) {
            const QPoint point = pending.dequeue();
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const QPoint neighbor = point + QPoint(dx, dy);
                    if (matching.remove(neighbor)) {
                        pending.enqueue(neighbor);
                    }
                }
            }
        }
    }
    return components;
}

QRect matchingColorBounds(const QImage& image, const QColor& target, const int tolerance = 8) {
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int distance = std::abs(pixel.red() - target.red()) +
                                 std::abs(pixel.green() - target.green()) +
                                 std::abs(pixel.blue() - target.blue());
            if (pixel.alpha() > 192 && distance <= tolerance) {
                bounds = bounds.united(QRect(x, y, 1, 1));
            }
        }
    }
    return bounds;
}

int colorDistance(const QColor& first, const QColor& second) {
    return std::abs(first.red() - second.red()) + std::abs(first.green() - second.green()) +
           std::abs(first.blue() - second.blue()) + std::abs(first.alpha() - second.alpha());
}

} // namespace

class StyleTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void everyAccentHasLegibleSelectedText();
    void comboBoxUsesTwoChevronGlyphs();
    void comboBoxCollapsedSurfaceAppearsOnlyOnHover();
    void comboBoxUsesQtMenuDelegateAndPreservesCustomDelegates();
    void comboBoxCollapsedUsesOwnerTypography();
    void comboBoxPopupUsesOwnerTypography();
    void comboPopupUsesMacStyleItems();
    void popupItemsShareTransparentMacStyleChrome();
    void comboPopupRealignsCommittedItemAfterHover();
    void comboPopupUsesOneRoundedSurface();
    void popupSurfacesFollowLiquidGlassPolicy();
    void menuRenderingUsesThemeTypography();
    void submenuStaysAboveItsRestackedParent();
    void colorChangesAvoidStructuralRepolish();
    void metricChangesTriggerOneCoalescedStructuralRefresh();
    void embeddedEditorsDoNotPaintASecondFrame();
    void comboBoxSizingAndElisionProtectTheChevronColumn();
    void fixedControlsHonorSizeClasses();
    void textSizeLevelResizesAllInheritedTextControlsAndIcons();
    void responsiveTypographyCoversEveryWidgetCategory();
    void selectedIndicatorsUseWhiteMarks();
    void segmentedControlHasNoHoverVisual();
    void switchShowsFocusOnlyForKeyboardNavigation();
    void sliderHandleDragPreservesCurrentValue();
    void discreteSliderUsesMacStyleTicksAndCapsuleHandle();
    void styleInteractionsAreEventDriven();
    void dialogButtonsUsePlatformOrder();
    void hiddenAnimationsSettleAtTheirTarget();

  private:
    bool animationsEnabled_ = true;
    vkui::VkAccentColor accentColor_ = vkui::VkAccentColor::Blue;
    vkui::VkAppearance appearance_ = vkui::VkAppearance::Auto;
    int textSizeLevel_ = vkui::VkDefaultTextSizeLevel;
    bool liquidGlassEnabled_ = true;
};

void StyleTest::initTestCase() {
    vkui::installVkUi(*qApp);
    auto* manager = vkui::VkThemeManager::instance();
    animationsEnabled_ = manager->animationsEnabled();
    accentColor_ = manager->accentColor();
    appearance_ = manager->appearance();
    textSizeLevel_ = manager->textSizeLevel();
    liquidGlassEnabled_ = manager->liquidGlassEnabled();
    manager->setAnimationsEnabled(false);
    manager->setLiquidGlassEnabled(true);
}

void StyleTest::cleanupTestCase() {
    auto* manager = vkui::VkThemeManager::instance();
    manager->setAccentColor(accentColor_);
    manager->setAppearance(appearance_);
    manager->setTextSizeLevel(textSizeLevel_);
    manager->setAnimationsEnabled(animationsEnabled_);
    manager->setLiquidGlassEnabled(liquidGlassEnabled_);
}

void StyleTest::everyAccentHasLegibleSelectedText() {
    auto* manager = vkui::VkThemeManager::instance();
    const QList<vkui::VkAccentColor> accents{
        vkui::VkAccentColor::Blue,  vkui::VkAccentColor::Purple,   vkui::VkAccentColor::Pink,
        vkui::VkAccentColor::Red,   vkui::VkAccentColor::Orange,   vkui::VkAccentColor::Yellow,
        vkui::VkAccentColor::Green, vkui::VkAccentColor::Graphite,
    };
    for (const vkui::VkAccentColor accent : accents) {
        manager->setAccentColor(accent);
        const QColor background = manager->theme().colors().accent;
        const QColor foreground = vkui::VStylePainter::contrastingText(background);
        QVERIFY2(contrastRatio(background, foreground) >= 4.5,
                 qPrintable(QStringLiteral("Insufficient contrast for accent %1")
                                .arg(static_cast<int>(accent))));
    }
}

void StyleTest::comboBoxUsesTwoChevronGlyphs() {
    InspectableComboBox combo;
    combo.addItems({QStringLiteral("One"), QStringLiteral("Two")});
    combo.resize(180, 30);
    QStyleOptionComboBox option;
    combo.initStyleOption(&option);
    QImage image(combo.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    combo.style()->drawComplexControl(QStyle::CC_ComboBox, &option, &painter, &combo);
    painter.end();

    const QRect arrow = combo.style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                      QStyle::SC_ComboBoxArrow, &combo);
    const QRect glyphArea = arrow.adjusted(5, 3, -5, -3);
    QCOMPARE(matchingComponents(image, glyphArea,
                                vkui::VkThemeManager::instance()->theme().colors().symbolSecondary),
             2);
}

void StyleTest::comboBoxCollapsedSurfaceAppearsOnlyOnHover() {
    InspectableComboBox combo;
    combo.addItem(QStringLiteral("System"));
    combo.resize(180, 30);

    QStyleOptionComboBox option;
    combo.initStyleOption(&option);
    const auto render = [&combo, &option](const bool hovered, const bool keyboardFocused) {
        QStyleOptionComboBox frame = option;
        frame.state.setFlag(QStyle::State_MouseOver, hovered);
        frame.state.setFlag(QStyle::State_HasFocus, keyboardFocused);
        frame.state.setFlag(QStyle::State_KeyboardFocusChange, keyboardFocused);
        QImage image(combo.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        combo.style()->drawComplexControl(QStyle::CC_ComboBox, &frame, &painter, &combo);
        return image;
    };

    const QRect arrow = combo.style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                      QStyle::SC_ComboBoxArrow, &combo);
    const QPoint labelSurfacePoint(option.rect.left() + 4, option.rect.center().y());
    const QPoint arrowSurfacePoint(arrow.left() + 5, arrow.center().y());
    const QColor surface = vkui::VkThemeManager::instance()->theme().colors().controlFill;

    const QImage resting = render(false, false);
    QCOMPARE(QColor::fromRgba(resting.pixel(labelSurfacePoint)).alpha(), 0);
    QCOMPARE(QColor::fromRgba(resting.pixel(arrowSurfacePoint)), surface);

    const QImage hovered = render(true, false);
    QCOMPARE(QColor::fromRgba(hovered.pixel(labelSurfacePoint)), surface);
    QCOMPARE(QColor::fromRgba(hovered.pixel(arrowSurfacePoint)), surface);

    const QImage keyboardFocused = render(false, true);
    QCOMPARE(QColor::fromRgba(keyboardFocused.pixel(labelSurfacePoint)).alpha(), 0);
    QCOMPARE(QColor::fromRgba(keyboardFocused.pixel(arrowSurfacePoint)), surface);
}

void StyleTest::comboBoxUsesQtMenuDelegateAndPreservesCustomDelegates() {
    QComboBox qtCombo;
    qtCombo.ensurePolished();
    QCOMPARE(qtCombo.style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, &qtCombo), 0);

    InspectableComboBox defaultCombo;
    defaultCombo.ensurePolished();
    QCOMPARE(defaultCombo.style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, &defaultCombo), 1);
    QCOMPARE(defaultCombo.style()->styleHint(QStyle::SH_ComboBox_PopupFrameStyle, nullptr,
                                             &defaultCombo),
             static_cast<int>(QFrame::NoFrame));
    QVERIFY(defaultCombo.view()->itemDelegate() != nullptr);
    QVERIFY2(defaultCombo.view()->itemDelegate()->inherits("QComboMenuDelegate"),
             defaultCombo.view()->itemDelegate()->metaObject()->className());

    InspectableComboBox customizedCombo;
    auto* applicationDelegate = new QStyledItemDelegate(&customizedCombo);
    customizedCombo.setItemDelegate(applicationDelegate);
    customizedCombo.ensurePolished();
    QCOMPARE(customizedCombo.itemDelegate(), applicationDelegate);
}

void StyleTest::comboBoxPopupUsesOwnerTypography() {
    InspectableComboBox combo;
    combo.addItems({QStringLiteral("One"), QStringLiteral("Two")});
    QFont ownerFont = combo.font();
    ownerFont.setPointSizeF(19.0);
    combo.setFont(ownerFont);
    combo.resize(240, 36);
    combo.show();
    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());
    QWidget* popupViewport = combo.view()->viewport();
    QVERIFY(popupViewport != nullptr);

    QStyleOptionMenuItem ownerOption;
    ownerOption.initFrom(&combo);
    ownerOption.rect = QRect(0, 0, 240, 48);
    ownerOption.state |= QStyle::State_Enabled | QStyle::State_Active;
    ownerOption.menuItemType = QStyleOptionMenuItem::Normal;
    ownerOption.text = QStringLiteral("Owner typography");
    ownerOption.font = ownerFont;
    ownerOption.fontMetrics = QFontMetrics(ownerFont);

    QStyleOptionMenuItem staleOption(ownerOption);
    QFont staleMenuFont = ownerFont;
    staleMenuFont.setPointSizeF(10.0);
    staleOption.font = staleMenuFont;
    staleOption.fontMetrics = QFontMetrics(staleMenuFont);

    const auto renderItem = [&combo, popupViewport](const QStyleOptionMenuItem& option) {
        QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        combo.style()->drawControl(QStyle::CE_MenuItem, &option, &painter, popupViewport);
        return image;
    };
    QCOMPARE(renderItem(staleOption), renderItem(ownerOption));

    const QSize staleSize = combo.style()->sizeFromContents(QStyle::CT_MenuItem, &staleOption,
                                                            QSize(40, 10), popupViewport);
    const QSize ownerSize = combo.style()->sizeFromContents(QStyle::CT_MenuItem, &ownerOption,
                                                            QSize(40, 10), popupViewport);
    QCOMPARE(staleSize, ownerSize);
    combo.hidePopup();
}

void StyleTest::comboBoxCollapsedUsesOwnerTypography() {
    InspectableComboBox combo;
    combo.addItem(QStringLiteral("A label long enough to expose stale font metrics"));
    combo.resize(170, 44);
    QFont ownerFont = combo.font();
    ownerFont.setPointSizeF(19.0);
    combo.setFont(ownerFont);

    QStyleOptionComboBox ownerOption;
    combo.initStyleOption(&ownerOption);
    QStyleOptionComboBox staleOption(ownerOption);
    QFont staleFont(ownerFont);
    staleFont.setPointSizeF(9.0);
    staleOption.fontMetrics = QFontMetrics(staleFont);

    const auto renderLabel = [&combo](const QStyleOptionComboBox& option) {
        QImage image(combo.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        combo.style()->drawControl(QStyle::CE_ComboBoxLabel, &option, &painter, &combo);
        return image;
    };
    QCOMPARE(renderLabel(staleOption), renderLabel(ownerOption));

    const QSize staleSize =
        combo.style()->sizeFromContents(QStyle::CT_ComboBox, &staleOption, QSize(80, 12), &combo);
    const QSize ownerSize =
        combo.style()->sizeFromContents(QStyle::CT_ComboBox, &ownerOption, QSize(80, 12), &combo);
    QCOMPARE(staleSize, ownerSize);
}

void StyleTest::comboPopupUsesMacStyleItems() {
    InspectableComboBox combo;
    combo.addItem(QStringLiteral("Selected item"));
    combo.resize(180, 30);

    const auto renderMenuItem = [&combo](bool checked, QStyle::State extraState) {
        QImage image(QSize(180, 28), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QStyleOptionMenuItem option;
        option.initFrom(&combo);
        option.rect = image.rect();
        option.state |=
            QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Selected | extraState;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.checked = checked;
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.text = QStringLiteral("Selected item");
        option.font = combo.font();
        option.fontMetrics = QFontMetrics(option.font);

        QPainter painter(&image);
        combo.style()->drawControl(QStyle::CE_MenuItem, &option, &painter, &combo);
        return image;
    };

    const QImage selected = renderMenuItem(false, QStyle::State_None);
    const QImage selectedAndHovered =
        renderMenuItem(false, QStyle::State_MouseOver | QStyle::State_HasFocus);
    QCOMPARE(selectedAndHovered, selected);

    const auto& theme = vkui::VkThemeManager::instance()->theme();
    const QColor primary = theme.colors().accent;
    QCOMPARE(QColor::fromRgba(selected.pixel(4, selected.height() / 2)), primary);

    const QImage selectedAndChecked = renderMenuItem(true, QStyle::State_None);
    const int fontHeight = combo.fontMetrics().height();
    const int leadingMargin = std::max(qRound(theme.metrics().spacing4), qCeil(fontHeight * 0.28));
    const int checkColumnWidth = std::max(qCeil(fontHeight * 0.82), combo.iconSize().width());
    const int columnGap = std::max(qRound(theme.metrics().spacing2), qCeil(fontHeight * 0.18));
    const QRect checkColumn(leadingMargin, 0, checkColumnWidth, selectedAndChecked.height());
    QVERIFY(checkColumn.right() <
            qRound(theme.metrics().spacing8 + theme.metrics().fixedControlExtentRegular));
    QCOMPARE(matchingComponents(selectedAndChecked, checkColumn,
                                vkui::VStylePainter::contrastingText(primary)),
             1);

    QStyleOptionMenuItem uncheckedOption;
    uncheckedOption.initFrom(&combo);
    uncheckedOption.rect = selected.rect();
    uncheckedOption.state |= QStyle::State_Enabled | QStyle::State_Active;
    uncheckedOption.checkType = QStyleOptionMenuItem::NonExclusive;
    uncheckedOption.text = QStringLiteral("Selected item");
    uncheckedOption.font = combo.font();
    uncheckedOption.fontMetrics = QFontMetrics(uncheckedOption.font);
    QImage unchecked(selected.size(), QImage::Format_ARGB32_Premultiplied);
    unchecked.fill(Qt::transparent);
    QPainter uncheckedPainter(&unchecked);
    combo.style()->drawControl(QStyle::CE_MenuItem, &uncheckedOption, &uncheckedPainter, &combo);
    uncheckedPainter.end();

    QStyleOptionMenuItem checkedOption = uncheckedOption;
    checkedOption.checked = true;
    QImage checked(unchecked.size(), QImage::Format_ARGB32_Premultiplied);
    checked.fill(Qt::transparent);
    QPainter checkedPainter(&checked);
    combo.style()->drawControl(QStyle::CE_MenuItem, &checkedOption, &checkedPainter, &combo);
    checkedPainter.end();
    const int textStart = checkColumn.right() + 1 + columnGap;
    for (int y = 0; y < checked.height(); ++y) {
        for (int x = textStart; x < checked.width(); ++x) {
            QCOMPARE(checked.pixel(x, y), unchecked.pixel(x, y));
        }
    }
}

void StyleTest::popupItemsShareTransparentMacStyleChrome() {
    InspectableComboBox combo;
    QMenu menu;
    const auto renderItem = [](QWidget& owner, const bool highlighted) {
        QImage image(QSize(180, 32), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QStyleOptionMenuItem option;
        option.initFrom(&owner);
        option.rect = image.rect();
        option.state |= QStyle::State_Enabled | QStyle::State_Active;
        option.state.setFlag(QStyle::State_Selected, highlighted);
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.text = QStringLiteral("Popup item");
        option.font = owner.font();
        option.fontMetrics = QFontMetrics(option.font);

        QPainter painter(&image);
        owner.style()->drawControl(QStyle::CE_MenuItem, &option, &painter, &owner);
        return image;
    };

    const QPoint chromePoint(4, 16);
    QCOMPARE(renderItem(combo, false).pixelColor(chromePoint), QColor(Qt::transparent));
    QCOMPARE(renderItem(menu, false).pixelColor(chromePoint), QColor(Qt::transparent));

    const QColor accent = vkui::VkThemeManager::instance()->theme().colors().accent;
    QCOMPARE(renderItem(combo, true).pixelColor(chromePoint), accent);
    QCOMPARE(renderItem(menu, true).pixelColor(chromePoint), accent);
}

void StyleTest::comboPopupRealignsCommittedItemAfterHover() {
    InspectableComboBox combo;
    combo.addItems({QStringLiteral("Zero"), QStringLiteral("One"), QStringLiteral("Two"),
                    QStringLiteral("Three")});
    combo.setCurrentIndex(1);
    combo.resize(180, 30);
    combo.move(240, 180);
    combo.show();
    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());

    const QModelIndex hoveredIndex = combo.model()->index(3, combo.modelColumn());
    combo.view()->selectionModel()->setCurrentIndex(hoveredIndex,
                                                    QItemSelectionModel::ClearAndSelect);
    QCOMPARE(combo.view()->currentIndex(), hoveredIndex);
    QVERIFY(combo.view()->selectionModel()->isSelected(hoveredIndex));
    QCOMPARE(combo.currentIndex(), 1);

    combo.hidePopup();
    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());
    const QModelIndex committedIndex = combo.model()->index(1, combo.modelColumn());
    QCOMPARE(combo.view()->currentIndex(), committedIndex);
    QVERIFY(combo.view()->selectionModel()->isSelected(committedIndex));
    QVERIFY(!combo.view()->selectionModel()->isSelected(hoveredIndex));

    const QRect selectedGlobal(
        combo.view()->viewport()->mapToGlobal(combo.view()->visualRect(committedIndex).topLeft()),
        combo.view()->visualRect(committedIndex).size());
    const QRect comboGlobal(combo.mapToGlobal(QPoint(0, 0)), combo.size());
    QCOMPARE(selectedGlobal.top(), comboGlobal.top());
    combo.hidePopup();

    combo.setPlaceholderText(QStringLiteral("Choose an item"));
    combo.setCurrentIndex(-1);
    combo.view()->selectionModel()->setCurrentIndex(hoveredIndex,
                                                    QItemSelectionModel::ClearAndSelect);
    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());
    QVERIFY(!combo.view()->currentIndex().isValid());
    QVERIFY(combo.view()->selectionModel()->selectedIndexes().isEmpty());
    combo.hidePopup();
}

void StyleTest::comboPopupUsesOneRoundedSurface() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool originalGlassEnabled = manager->liquidGlassEnabled();
    manager->setLiquidGlassEnabled(false);

    InspectableComboBox combo;
    combo.addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    combo.setCurrentIndex(1);
    combo.resize(180, 30);
    combo.move(240, 180);
    combo.show();
    QCoreApplication::processEvents();

    const QColor hostileBackground(255, 0, 180);
    const auto makeOpaque = [hostileBackground](QWidget& widget) {
        QPalette palette = widget.palette();
        palette.setColor(QPalette::Base, hostileBackground);
        palette.setColor(QPalette::AlternateBase, hostileBackground);
        palette.setColor(QPalette::Window, hostileBackground);
        widget.setPalette(palette);
        widget.setBackgroundRole(QPalette::Base);
        widget.setAutoFillBackground(true);
        widget.setAttribute(Qt::WA_OpaquePaintEvent, true);
    };
    makeOpaque(*combo.view());
    makeOpaque(*combo.view()->viewport());

    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());
    QWidget* popup = combo.view()->window();
    QVERIFY(popup != nullptr);
    QVERIFY(popup->inherits("QComboBoxPrivateContainer"));
    auto* popupFrame = qobject_cast<QFrame*>(popup);
    QVERIFY(popupFrame != nullptr);
    QCOMPARE(popupFrame->frameStyle(), static_cast<int>(QFrame::NoFrame));
    QVERIFY(popup->testAttribute(Qt::WA_TranslucentBackground));
    QVERIFY(popup->windowFlags().testFlag(Qt::NoDropShadowWindowHint));
    QVERIFY(popup->mask().isEmpty());
    QCOMPARE(combo.view()->frameShape(), QFrame::NoFrame);
    for (QWidget* content : {static_cast<QWidget*>(combo.view()), combo.view()->viewport()}) {
        QCOMPARE(content->palette().color(QPalette::Base), QColor(Qt::transparent));
        QCOMPARE(content->palette().color(QPalette::AlternateBase), QColor(Qt::transparent));
        QCOMPARE(content->palette().color(QPalette::Window), QColor(Qt::transparent));
        QCOMPARE(content->palette().color(content->backgroundRole()), QColor(Qt::transparent));
        QVERIFY(!content->autoFillBackground());
        QVERIFY(content->testAttribute(Qt::WA_NoSystemBackground));
        QVERIFY(!content->testAttribute(Qt::WA_OpaquePaintEvent));
    }

    const QModelIndex selectedIndex = combo.model()->index(combo.currentIndex(), 0);
    const QRect selectedRect = combo.view()->visualRect(selectedIndex);
    const QRect selectedVisible = selectedRect.intersected(combo.view()->viewport()->rect());
    const QRect selectedGlobal(combo.view()->viewport()->mapToGlobal(selectedVisible.topLeft()),
                               selectedVisible.size());
    const QRect comboGlobal(combo.mapToGlobal(QPoint(0, 0)), combo.size());
    QCOMPARE(selectedGlobal.top(), comboGlobal.top());
    QCOMPARE(selectedGlobal.left(), comboGlobal.left());
    QCOMPARE(selectedGlobal.width(), comboGlobal.width());

    QImage popupImage(popup->size(), QImage::Format_ARGB32_Premultiplied);
    popupImage.fill(Qt::transparent);
    popup->render(&popupImage);
    const auto& theme = vkui::VkThemeManager::instance()->theme();
    const int shadowMargin = qCeil(theme.metrics().spacing8 + std::abs(theme.metrics().spacing2));
    const int contentMargin = qRound(theme.metrics().spacing6);
    const int layoutMargin = combo.style()->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &combo);
    QCOMPARE(layoutMargin, shadowMargin + contentMargin);
    QVERIFY(QColor::fromRgba(popupImage.pixel(0, 0)).alpha() < 64);
    QVERIFY(QColor::fromRgba(popupImage.pixel(0, shadowMargin)).alpha() < 96);
    QVERIFY(QColor::fromRgba(popupImage.pixel(popupImage.width() - 1, shadowMargin)).alpha() < 96);
    QVERIFY(QColor::fromRgba(popupImage.pixel(popupImage.rect().center())).alpha() > 192);

    const QPoint selectedLocal = popup->mapFromGlobal(selectedGlobal.topLeft());
    QCOMPARE(selectedLocal.x(), layoutMargin);
    const QRect surfaceRect =
        popup->rect().adjusted(shadowMargin, shadowMargin, -shadowMargin, -shadowMargin);
    QCOMPARE(selectedLocal.x() - surfaceRect.left(), contentMargin);
    QCOMPARE(surfaceRect.right() - (selectedLocal.x() + selectedGlobal.width() - 1), contentMargin);
    QCOMPARE(QColor::fromRgba(popupImage.pixel(selectedLocal.x() + 4,
                                               selectedLocal.y() + selectedGlobal.height() / 2)),
             theme.colors().accent);
    const int fontHeight = combo.fontMetrics().height();
    const int leadingMargin = std::max(qRound(theme.metrics().spacing4), qCeil(fontHeight * 0.28));
    const int checkColumnWidth = std::max(qCeil(fontHeight * 0.82), combo.iconSize().width());
    const QRect selectedCheckColumn(selectedLocal.x() + leadingMargin, selectedLocal.y(),
                                    checkColumnWidth, selectedGlobal.height());
    QCOMPARE(matchingComponents(popupImage, selectedCheckColumn,
                                vkui::VStylePainter::contrastingText(theme.colors().accent)),
             1);

    const QModelIndex normalIndex = combo.model()->index(0, 0);
    const QRect normalRect = combo.view()->visualRect(normalIndex);
    const QPoint normalBackgroundGlobal =
        combo.view()->viewport()->mapToGlobal(QPoint(4, normalRect.center().y()));
    const QPoint normalBackgroundLocal = popup->mapFromGlobal(normalBackgroundGlobal);
    QCOMPARE(popupImage.pixelColor(normalBackgroundLocal), theme.colors().elevatedBackground);
    QVERIFY(popupImage.pixelColor(normalBackgroundLocal) != hostileBackground);

    combo.view()->selectionModel()->setCurrentIndex(combo.model()->index(0, combo.modelColumn()),
                                                    QItemSelectionModel::ClearAndSelect);
    combo.view()->viewport()->update();
    QCoreApplication::processEvents();
    const QImage hoveredPopupImage = renderWidget(*popup);
    const QRect hoverBounds = matchingColorBounds(hoveredPopupImage, theme.colors().accent);
    QVERIFY(!hoverBounds.isEmpty());
    const QRect contentRect =
        surfaceRect.adjusted(contentMargin, contentMargin, -contentMargin, -contentMargin);
    QVERIFY2(contentRect.contains(hoverBounds),
             qPrintable(QStringLiteral("Hover bounds %1,%2 %3x%4 escaped content rect %5,%6 %7x%8")
                            .arg(hoverBounds.x())
                            .arg(hoverBounds.y())
                            .arg(hoverBounds.width())
                            .arg(hoverBounds.height())
                            .arg(contentRect.x())
                            .arg(contentRect.y())
                            .arg(contentRect.width())
                            .arg(contentRect.height())));
    combo.hidePopup();
    manager->setLiquidGlassEnabled(originalGlassEnabled);
}

void StyleTest::popupSurfacesFollowLiquidGlassPolicy() {
    auto* manager = vkui::VkThemeManager::instance();
    manager->setLiquidGlassEnabled(true);

    QWidget owner;
    owner.resize(360, 240);
    InspectableComboBox combo(&owner);
    combo.addItems({QStringLiteral("One"), QStringLiteral("Two")});
    combo.setGeometry(80, 80, 180, 32);
    owner.show();
    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());

    QWidget* comboPopup = combo.view()->window();
    auto* comboGlass = comboPopup->findChild<vkui::VLiquidGlassSurface*>(
        QStringLiteral("vkuiPopupLiquidGlassSurface"));
    QVERIFY(comboGlass != nullptr);
    QVERIFY(comboGlass->isHidden());
    QVERIFY(comboGlass->backdrop() != nullptr);
    QCOMPARE(comboGlass->backdrop()->sourceWidget(), &owner);
    QCOMPARE(comboGlass->glassStyle().blurRadius, vkui::VLiquidGlassStyle::popup().blurRadius);
    const auto& metrics = manager->theme().metrics();
    const int shadowMargin = qCeil(metrics.spacing8 + std::abs(metrics.spacing2));
    const int contentMargin = qRound(metrics.spacing6);
    QCOMPARE(combo.style()->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &combo),
             shadowMargin + contentMargin);
    QCOMPARE(comboGlass->geometry(),
             comboPopup->rect().adjusted(shadowMargin, shadowMargin, -shadowMargin, -shadowMargin));
    QCOMPARE(comboGlass->glassStyle().cornerRadius, metrics.comboBoxPopupCornerRadius);
    QCOMPARE(comboGlass->glassStyle().refractionHeight, 0.0);
    QCOMPARE(comboGlass->glassStyle().opticalEdgeIntensity, 0.0);

    // A missing backdrop selects the uniform material fallback. Any pixel discontinuity between
    // the popup-only render and a normal row can then only come from an extra content layer.
    comboGlass->setBackdrop(nullptr);
    const QImage comboSurfaceImage = renderPopupSurface(*comboPopup);
    const QImage comboImage = renderWidget(*comboPopup);
    const QRect normalRow = combo.view()->visualRect(combo.model()->index(1, combo.modelColumn()));
    const QRect normalVisible = normalRow.intersected(combo.view()->viewport()->rect());
    const QPoint normalSample = combo.view()->viewport()->mapTo(
        comboPopup, QPoint(normalVisible.right() - contentMargin, normalVisible.center().y()));
    QVERIFY2(comboImage.rect().contains(normalSample),
             qPrintable(QStringLiteral("image=%1,%2 %3x%4 row=%5,%6 %7x%8 sample=%9,%10")
                            .arg(comboImage.rect().x())
                            .arg(comboImage.rect().y())
                            .arg(comboImage.width())
                            .arg(comboImage.height())
                            .arg(normalVisible.x())
                            .arg(normalVisible.y())
                            .arg(normalVisible.width())
                            .arg(normalVisible.height())
                            .arg(normalSample.x())
                            .arg(normalSample.y())));
    const QColor comboItemColor = comboImage.pixelColor(normalSample);
    const QColor comboSurfaceColor = comboSurfaceImage.pixelColor(normalSample);
    QVERIFY2(colorDistance(comboItemColor, comboSurfaceColor) <= 4,
             qPrintable(QStringLiteral("Combo item %1,%2,%3,%4 differs from surface %5,%6,%7,%8")
                            .arg(comboItemColor.red())
                            .arg(comboItemColor.green())
                            .arg(comboItemColor.blue())
                            .arg(comboItemColor.alpha())
                            .arg(comboSurfaceColor.red())
                            .arg(comboSurfaceColor.green())
                            .arg(comboSurfaceColor.blue())
                            .arg(comboSurfaceColor.alpha())));

    manager->setLiquidGlassEnabled(false);
    QVERIFY(comboGlass->isHidden());
    manager->setLiquidGlassEnabled(true);
    QVERIFY(comboGlass->isHidden());
    combo.hidePopup();

    QMenu menu(&owner);
    const QColor markerColor(255, 0, 180);
    QPixmap marker(12, 12);
    marker.fill(markerColor);
    QAction* menuAction = menu.addAction(QIcon(marker), QStringLiteral("Menu item"));
    menu.popup(owner.mapToGlobal(QPoint(24, 24)));
    QTRY_VERIFY(menu.isVisible());
    auto* menuGlass =
        menu.findChild<vkui::VLiquidGlassSurface*>(QStringLiteral("vkuiPopupLiquidGlassSurface"));
    QVERIFY(menuGlass != nullptr);
    QVERIFY(menuGlass->isHidden());
    QCOMPARE(menuGlass->backdrop()->sourceWidget(), &owner);
    QCOMPARE(menuGlass->glassStyle().blurRadius, vkui::VLiquidGlassStyle::popup().blurRadius);
    QCOMPARE(menu.style()->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &menu),
             shadowMargin + contentMargin);
    QCOMPARE(menuGlass->geometry(),
             menu.rect().adjusted(shadowMargin, shadowMargin, -shadowMargin, -shadowMargin));
    QCOMPARE(menuGlass->glassStyle().cornerRadius, metrics.menuCornerRadius);
    QVERIFY(menu.windowFlags().testFlag(Qt::NoDropShadowWindowHint));

    menuGlass->setBackdrop(nullptr);
    const QImage menuSurfaceImage = renderPopupSurface(menu);
    QImage menuImage = renderWidget(menu);
    const QRect actionRect = menu.actionGeometry(menuAction);
    const QPoint menuItemSample(actionRect.right() - contentMargin, actionRect.center().y());
    QVERIFY(menuImage.rect().contains(menuItemSample));
    QVERIFY2(colorDistance(menuImage.pixelColor(menuItemSample),
                           menuSurfaceImage.pixelColor(menuItemSample)) <= 4,
             "Menu actions must reveal the popup surface without an extra background layer");

    menu.setActiveAction(menuAction);
    menu.update();
    QCoreApplication::processEvents();
    menuImage = renderWidget(menu);
    const QRect menuSurface = menuGlass->geometry();
    const QRect menuContent =
        menuSurface.adjusted(contentMargin, contentMargin, -contentMargin, -contentMargin);
    const QRect menuHoverBounds = matchingColorBounds(menuImage, manager->theme().colors().accent);
    QVERIFY(!menuHoverBounds.isEmpty());
    QVERIFY(menuContent.contains(menuHoverBounds));
    QCOMPARE(matchingComponents(menuImage, menu.actionGeometry(menuAction), markerColor), 1);
    menu.close();
}

void StyleTest::menuRenderingUsesThemeTypography() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    manager->setTextSizeLevel(vkui::VkMinimumTextSizeLevel);

    QMenu menu;
    menu.addAction(QStringLiteral("Menu item"));
    QStyleOptionMenuItem themeOption;
    themeOption.initFrom(&menu);
    themeOption.rect = QRect(0, 0, 220, 40);
    themeOption.state |= QStyle::State_Enabled | QStyle::State_Active;
    themeOption.menuItemType = QStyleOptionMenuItem::Normal;
    themeOption.text = QStringLiteral("Menu item");
    themeOption.font = manager->theme().typography().body;
    themeOption.fontMetrics = QFontMetrics(themeOption.font);

    QStyleOptionMenuItem staleOption(themeOption);
    staleOption.font.setPointSizeF(19.0);
    staleOption.fontMetrics = QFontMetrics(staleOption.font);
    const auto renderItem = [&menu](const QStyleOptionMenuItem& option) {
        QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        menu.style()->drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        return image;
    };
    QCOMPARE(renderItem(staleOption), renderItem(themeOption));

    const QSize staleSize =
        menu.style()->sizeFromContents(QStyle::CT_MenuItem, &staleOption, QSize(40, 10), &menu);
    const QSize themeSize =
        menu.style()->sizeFromContents(QStyle::CT_MenuItem, &themeOption, QSize(40, 10), &menu);
    QCOMPARE(staleSize, themeSize);
    manager->setTextSizeLevel(originalLevel);
}

void StyleTest::submenuStaysAboveItsRestackedParent() {
#if defined(Q_OS_MACOS)
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) {
        QSKIP("Native window stacking is only observable through the Cocoa platform plugin");
    }

    QMenu root;
    QMenu* submenu = root.addMenu(QStringLiteral("Image Appearance"));
    submenu->addAction(QStringLiteral("Square"));
    submenu->addAction(QStringLiteral("No Crop"));
    root.addSeparator();
    root.addAction(QStringLiteral("Reset Rows"));

    root.popup(QPoint(200, 200));
    QTRY_VERIFY(root.isVisible());
    root.setActiveAction(submenu->menuAction());
    QTRY_VERIFY(submenu->isVisible());
    QVERIFY(submenu->windowHandle());
    QCOMPARE(submenu->windowHandle()->transientParent(), root.windowHandle());
    QTRY_VERIFY(vkuiNativeWindowIsInFrontOf(submenu->winId(), root.winId()));

    // A pressed parent item can cause the platform popup surface to be restacked. The active
    // submenu must remain in front of its parent after that native ordering change.
    root.raise();
    QCoreApplication::processEvents();
    QTRY_VERIFY(vkuiNativeWindowIsInFrontOf(submenu->winId(), root.winId()));

    root.close();
#else
    QSKIP("Native window stacking probe is only available on macOS");
#endif
}

void StyleTest::colorChangesAvoidStructuralRepolish() {
    auto* manager = vkui::VkThemeManager::instance();
    manager->setAppearance(vkui::VkAppearance::Light);

    QWidget probe;
    auto* probeStyle = new PolishProbeStyle;
    probeStyle->setParent(&probe);
    probe.setStyle(probeStyle);
    probe.resize(80, 40);
    probe.show();
    probe.ensurePolished();
    QCoreApplication::processEvents();

    const int polishCount = probeStyle->polishCount;
    const int unpolishCount = probeStyle->unpolishCount;
    manager->setAppearance(vkui::VkAppearance::Dark);
    manager->setAccentColor(manager->accentColor() == vkui::VkAccentColor::Purple
                                ? vkui::VkAccentColor::Blue
                                : vkui::VkAccentColor::Purple);
    QCoreApplication::processEvents();

    QCOMPARE(probeStyle->unpolishCount, unpolishCount);
    QCOMPARE(probeStyle->polishCount, polishCount);
    QCOMPARE(qApp->palette().color(QPalette::Base), manager->theme().colors().contentBackground);
    QCOMPARE(qApp->palette().color(QPalette::Accent), manager->theme().colors().accent);
}

void StyleTest::metricChangesTriggerOneCoalescedStructuralRefresh() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    manager->resetTextSizeLevel();

    QWidget probe;
    auto* probeStyle = new PolishProbeStyle;
    probeStyle->setParent(&probe);
    probe.setStyle(probeStyle);
    probe.resize(80, 40);
    probe.show();
    probe.ensurePolished();
    QCoreApplication::processEvents();

    const int polishCount = probeStyle->polishCount;
    const int unpolishCount = probeStyle->unpolishCount;
    manager->setTextSizeLevel(vkui::VkMaximumTextSizeLevel);
    QCoreApplication::processEvents();

    QCOMPARE(probeStyle->unpolishCount, unpolishCount + 1);
    QCOMPARE(probeStyle->polishCount, polishCount + 1);
    manager->setTextSizeLevel(originalLevel);
    QCoreApplication::processEvents();
}

void StyleTest::embeddedEditorsDoNotPaintASecondFrame() {
    QSpinBox spinBox;
    spinBox.resize(140, 30);
    spinBox.show();
    spinBox.ensurePolished();
    QLineEdit* editor = spinBox.findChild<QLineEdit*>();
    QVERIFY(editor != nullptr);

    QStyleOptionFrame option;
    option.initFrom(editor);
    option.rect = editor->rect();
    QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    editor->style()->drawPrimitive(QStyle::PE_PanelLineEdit, &option, &painter, editor);
    painter.end();

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QCOMPARE(QColor::fromRgba(image.pixel(x, y)).alpha(), 0);
        }
    }
}

void StyleTest::comboBoxSizingAndElisionProtectTheChevronColumn() {
    InspectableComboBox compactLabelCombo;
    compactLabelCombo.addItems({QStringLiteral("System"), QStringLiteral("Automatic")});
    compactLabelCombo.setSizeAdjustPolicy(QComboBox::AdjustToContents);
    compactLabelCombo.setCurrentIndex(1);
    compactLabelCombo.resize(compactLabelCombo.sizeHint());
    QCOMPARE(compactLabelCombo.sizePolicy().horizontalPolicy(), QSizePolicy::Minimum);

    QStyleOptionComboBox compactLabelOption;
    compactLabelCombo.initStyleOption(&compactLabelOption);
    const QRect compactLabelRect = compactLabelCombo.style()->subControlRect(
        QStyle::CC_ComboBox, &compactLabelOption, QStyle::SC_ComboBoxEditField, &compactLabelCombo);
    QVERIFY(compactLabelRect.width() >=
            compactLabelCombo.fontMetrics().horizontalAdvance(QStringLiteral("Automatic")));

    InspectableComboBox combo;
    const QString label = QStringLiteral("A deliberately long combo-box value");
    combo.addItem(label);
    combo.setSizeAdjustPolicy(QComboBox::AdjustToContents);
    combo.setElideMode(Qt::ElideMiddle);
    QCOMPARE(combo.elideMode(), Qt::ElideMiddle);
    QVERIFY(combo.sizeHint().width() > combo.fontMetrics().horizontalAdvance(label));

    combo.resize(128, 30);
    QStyleOptionComboBox option;
    combo.initStyleOption(&option);
    QImage labelImage(combo.size(), QImage::Format_ARGB32_Premultiplied);
    labelImage.fill(Qt::transparent);
    QPainter painter(&labelImage);
    combo.style()->drawControl(QStyle::CE_ComboBoxLabel, &option, &painter, &combo);
    painter.end();

    const QRect arrow = combo.style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                      QStyle::SC_ComboBoxArrow, &combo);
    for (int y = arrow.top(); y <= arrow.bottom(); ++y) {
        for (int x = arrow.left(); x <= arrow.right(); ++x) {
            QCOMPARE(QColor::fromRgba(labelImage.pixel(x, y)).alpha(), 0);
        }
    }

    QStyleOptionMenuItem regularItem;
    regularItem.initFrom(&combo);
    regularItem.font = combo.font();
    regularItem.fontMetrics = QFontMetrics(regularItem.font);
    regularItem.text = QStringLiteral("A reasonably long item label");
    const QSize regularItemSize =
        combo.style()->sizeFromContents(QStyle::CT_MenuItem, &regularItem, QSize(), &combo);

    QFont largeFont = combo.font();
    largeFont.setPointSizeF(largeFont.pointSizeF() + 10.0);
    QStyleOptionMenuItem largeItem = regularItem;
    combo.setFont(largeFont);
    // The private popup view can still report its stale platform-menu font. Sizing follows the
    // public combo-box owner, matching the painting contract exercised above.
    const QSize largeItemSize =
        combo.style()->sizeFromContents(QStyle::CT_MenuItem, &largeItem, QSize(), &combo);
    QVERIFY(largeItemSize.width() > regularItemSize.width());
    QVERIFY(largeItemSize.height() > regularItemSize.height());

    InspectableComboBox expandingCombo;
    expandingCombo.addItem(QStringLiteral("Short"));
    expandingCombo.addItem(
        QStringLiteral("A long menu value that should expand the popup before it is truncated"));
    expandingCombo.setCurrentIndex(0);
    expandingCombo.resize(140, 30);
    expandingCombo.move(220, 160);
    expandingCombo.show();
    expandingCombo.showPopup();
    QTRY_VERIFY(expandingCombo.view()->isVisible());
    QWidget* popup = expandingCombo.view()->window();
    QVERIFY(popup->width() > expandingCombo.width());
    const QModelIndex current = expandingCombo.model()->index(expandingCombo.currentIndex(), 0);
    const QRect currentRect = expandingCombo.view()->visualRect(current);
    const QPoint rowTopLeft = expandingCombo.view()->viewport()->mapToGlobal(currentRect.topLeft());
    QCOMPARE(rowTopLeft.x(), expandingCombo.mapToGlobal(QPoint()).x());
    QCOMPARE(rowTopLeft.y(), expandingCombo.mapToGlobal(QPoint()).y());
    expandingCombo.hidePopup();

    InspectableComboBox compactCombo;
    QFont explicitFont = compactCombo.font();
    compactCombo.setFont(explicitFont);
    compactCombo.addItems(
        {QStringLiteral("One"), QStringLiteral("System"), QStringLiteral("Graphite")});
    compactCombo.resize(58, 30);
    compactCombo.move(220, 160);
    compactCombo.show();
    compactCombo.showPopup();
    QTRY_VERIFY(compactCombo.view()->isVisible());
    QVERIFY(compactCombo.view()->window()->width() > compactCombo.width());

    const auto& compactMetrics = vkui::VkThemeManager::instance()->theme().metrics();
    const int compactFontHeight = compactCombo.fontMetrics().height();
    const int leadingMargin =
        std::max(qRound(compactMetrics.spacing4), qCeil(compactFontHeight * 0.28));
    const int stateColumn =
        std::max(qCeil(compactFontHeight * 0.82), compactCombo.iconSize().width());
    const int columnGap =
        std::max(qRound(compactMetrics.spacing2), qCeil(compactFontHeight * 0.18));
    const int rightMargin =
        std::max(qRound(compactMetrics.spacing8), qCeil(compactFontHeight * 0.48));
    const int horizontalPadding = leadingMargin + stateColumn + columnGap + rightMargin;
    for (int row = 0; row < compactCombo.count(); ++row) {
        const QModelIndex index = compactCombo.model()->index(row, compactCombo.modelColumn());
        const int textWidth = compactCombo.view()->visualRect(index).width() - horizontalPadding;
        QCOMPARE(compactCombo.fontMetrics().elidedText(compactCombo.itemText(row),
                                                       compactCombo.elideMode(), textWidth),
                 compactCombo.itemText(row));
    }
    compactCombo.hidePopup();
}

void StyleTest::fixedControlsHonorSizeClasses() {
    QCheckBox checkBox;
    QRadioButton radioButton;
    vkui::setControlSize(checkBox, vkui::VControlSize::Small);
    vkui::setControlSize(radioButton, vkui::VControlSize::Small);
    const int smallCheck =
        checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox);
    const int smallRadio =
        radioButton.style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, &radioButton);

    vkui::setControlSize(checkBox, vkui::VControlSize::Large);
    vkui::setControlSize(radioButton, vkui::VControlSize::Large);
    QVERIFY(checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox) >
            smallCheck);
    QVERIFY(radioButton.style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr,
                                             &radioButton) > smallRadio);

    vkui::setControlExtent(checkBox, 27);
    vkui::setControlExtent(radioButton, 27);
    QCOMPARE(checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox), 27);
    QCOMPARE(
        radioButton.style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, &radioButton),
        27);
    vkui::resetControlExtent(checkBox);
    QCOMPARE(checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox),
             vkui::controlExtent(vkui::VControlSize::Large));
}

void StyleTest::textSizeLevelResizesAllInheritedTextControlsAndIcons() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    manager->resetTextSizeLevel();

    QPushButton button(QStringLiteral("Settings"));
    QCheckBox checkBox(QStringLiteral("Option"));
    QLabel bodyLabel(QStringLiteral("Body label"));
    QGroupBox groupBox(QStringLiteral("Group title"));
    QLineEdit lineEdit(QStringLiteral("Editable text"));
    QComboBox comboBox;
    comboBox.addItem(QStringLiteral("Menu text"));
    vkui::VSegmentedControl segmented;
    segmented.addSegment(QStringLiteral("Segment text"));
    vkui::VSwitch control;
    QLabel title(QStringLiteral("Title"));
    vkui::setTextStyle(title, vkui::VTextStyle::Title);

    QCheckBox exactCheckBox(QStringLiteral("Exact"));
    vkui::setControlExtent(exactCheckBox, 27);
    const int defaultFontHeight = button.fontMetrics().height();
    const QList<QWidget*> inheritedTextWidgets{&button,   &checkBox, &bodyLabel, &groupBox,
                                               &lineEdit, &comboBox, &segmented};
    const int defaultButtonHeight = button.sizeHint().height();
    const int defaultIndicator =
        checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox);
    const QSize defaultSwitchSize = control.sizeHint();
    const int defaultIconExtent =
        button.style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, &button);
    const qreal defaultTitleSize = title.font().pointSizeF();

    manager->setTextSizeLevel(vkui::VkMaximumTextSizeLevel);
    QCoreApplication::processEvents();

    QVERIFY(button.fontMetrics().height() > defaultFontHeight);
    for (QWidget* widget : inheritedTextWidgets) {
        QCOMPARE(widget->font(), QApplication::font());
        QVERIFY(widget->fontMetrics().height() > defaultFontHeight);
    }
    QVERIFY(button.sizeHint().height() > defaultButtonHeight);
    QVERIFY(checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox) >
            defaultIndicator);
    QVERIFY(control.sizeHint().height() > defaultSwitchSize.height());
    QVERIFY(button.style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, &button) >
            defaultIconExtent);
    QVERIFY(title.font().pointSizeF() > defaultTitleSize);
    QCOMPARE(title.font(), vkui::textStyleFont(vkui::VTextStyle::Title));
    QCOMPARE(exactCheckBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &exactCheckBox),
             27);

    vkui::resetTextStyle(title);
    QVERIFY(!vkui::textStyle(title));
    manager->setTextSizeLevel(originalLevel);
}

void StyleTest::responsiveTypographyCoversEveryWidgetCategory() {
    auto* manager = vkui::VkThemeManager::instance();
    const int originalLevel = manager->textSizeLevel();
    manager->setTextSizeLevel(vkui::VkMinimumTextSizeLevel);

    QWidget window;
    QPushButton button(QStringLiteral("Button"), &window);
    QToolButton toolButton(&window);
    toolButton.setText(QStringLiteral("Tool"));
    QCheckBox checkBox(QStringLiteral("Check"), &window);
    QRadioButton radioButton(QStringLiteral("Radio"), &window);
    QLabel label(QStringLiteral("Label"), &window);
    QGroupBox groupBox(QStringLiteral("Group"), &window);
    QLineEdit lineEdit(QStringLiteral("Line edit"), &window);
    QSpinBox spinBox(&window);
    QDoubleSpinBox doubleSpinBox(&window);
    QProgressBar progressBar(&window);
    QPlainTextEdit plainTextEdit(QStringLiteral("Plain text"), &window);
    QTextEdit textEdit(QStringLiteral("Rich text"), &window);
    QTabBar tabBar(&window);
    tabBar.addTab(QStringLiteral("Tab"));
    QListView listView(&window);
    QTableView tableView(&window);
    QToolBar toolBar(&window);
    toolBar.addAction(QStringLiteral("Toolbar action"));
    InspectableComboBox comboBox;
    comboBox.setParent(&window);
    comboBox.addItems({QStringLiteral("One"), QStringLiteral("Two")});
    vkui::VSegmentedControl segmented(&window);
    segmented.addSegment(QStringLiteral("Segment"));
    vkui::VTreeView treeView(&window);
    QStandardItemModel treeModel;
    treeModel.appendRow(new QStandardItem(QStringLiteral("Tree item")));
    treeView.setModel(&treeModel);

    QLabel semanticTitle(QStringLiteral("Semantic title"), &window);
    vkui::setTextStyle(semanticTitle, vkui::VTextStyle::Title);
    QLabel explicitLabel(QStringLiteral("Explicit application font"), &window);
    QFont explicitFont = explicitLabel.font();
    explicitFont.setPointSizeF(9.0);
    explicitLabel.setFont(explicitFont);
    QLabel runtimeExplicitLabel(QStringLiteral("Runtime application font"), &window);

    QMenu menu(&button);
    menu.addAction(QStringLiteral("Menu action"));

    window.resize(640, 480);
    window.show();
    menu.ensurePolished();
    QCoreApplication::processEvents();
    QFont runtimeExplicitFont = runtimeExplicitLabel.font();
    runtimeExplicitFont.setPointSizeF(10.0);
    runtimeExplicitLabel.setFont(runtimeExplicitFont);

    const QList<QWidget*> bodyWidgets{
        &button,    &toolButton,    &checkBox,    &radioButton,   &label,    &groupBox, &lineEdit,
        &spinBox,   &doubleSpinBox, &progressBar, &plainTextEdit, &textEdit, &tabBar,   &listView,
        &tableView, &toolBar,       &comboBox,    &segmented,     &treeView, &menu,
    };
    const int smallBodyHeight = label.fontMetrics().height();
    const int smallRadioIndicator = radioButton.style()->pixelMetric(
        QStyle::PM_ExclusiveIndicatorHeight, nullptr, &radioButton);
    QStyleOptionViewItem smallTreeOption;
    smallTreeOption.initFrom(&treeView);
    smallTreeOption.widget = &treeView;
    const QSize smallTreeRow =
        treeView.itemDelegate()->sizeHint(smallTreeOption, treeModel.index(0, 0));
    const qreal smallTitleSize = semanticTitle.font().pointSizeF();

    manager->setTextSizeLevel(vkui::VkMaximumTextSizeLevel);
    QCoreApplication::processEvents();

    QCOMPARE(window.font(), manager->theme().typography().body);
    for (QWidget* widget : bodyWidgets) {
        QCOMPARE(widget->font(), window.font());
        QVERIFY2(widget->fontMetrics().height() > smallBodyHeight,
                 widget->metaObject()->className());
    }
    QVERIFY(radioButton.style()->pixelMetric(QStyle::PM_ExclusiveIndicatorHeight, nullptr,
                                             &radioButton) > smallRadioIndicator);
    QStyleOptionViewItem largeTreeOption;
    largeTreeOption.initFrom(&treeView);
    largeTreeOption.widget = &treeView;
    const QSize largeTreeRow =
        treeView.itemDelegate()->sizeHint(largeTreeOption, treeModel.index(0, 0));
    QVERIFY(largeTreeRow.height() > smallTreeRow.height());
    QVERIFY(semanticTitle.font().pointSizeF() > smallTitleSize);
    QCOMPARE(semanticTitle.font(), vkui::textStyleFont(vkui::VTextStyle::Title));
    QCOMPARE(explicitLabel.font().pointSizeF(), 9.0);
    QCOMPARE(runtimeExplicitLabel.font().pointSizeF(), 10.0);

    manager->setTextSizeLevel(originalLevel);
    QCoreApplication::processEvents();
}

void StyleTest::selectedIndicatorsUseWhiteMarks() {
    auto hasWhitePixel = [](const QImage& image, const QRect& area) {
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const QColor pixel = QColor::fromRgba(image.pixel(x, y));
                if (pixel.alpha() > 180 && pixel.red() > 235 && pixel.green() > 235 &&
                    pixel.blue() > 235) {
                    return true;
                }
            }
        }
        return false;
    };

    QCheckBox checkBox;
    checkBox.setChecked(true);
    checkBox.resize(checkBox.sizeHint());
    QStyleOptionButton checkOption;
    checkOption.initFrom(&checkBox);
    checkOption.rect = checkBox.rect();
    checkOption.state.setFlag(QStyle::State_On, true);
    const QRect checkIndicator =
        checkBox.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &checkOption, &checkBox);
    QVERIFY(hasWhitePixel(renderWidget(checkBox), checkIndicator));

    QRadioButton radioButton;
    radioButton.setChecked(true);
    radioButton.resize(radioButton.sizeHint());
    QStyleOptionButton radioOption;
    radioOption.initFrom(&radioButton);
    radioOption.rect = radioButton.rect();
    radioOption.state.setFlag(QStyle::State_On, true);
    const QRect radioIndicator = radioButton.style()->subElementRect(
        QStyle::SE_RadioButtonIndicator, &radioOption, &radioButton);
    QVERIFY(hasWhitePixel(renderWidget(radioButton), radioIndicator));
}

void StyleTest::segmentedControlHasNoHoverVisual() {
    vkui::VSegmentedControl control;
    control.addSegment(QStringLiteral("One"));
    control.addSegment(QStringLiteral("Two"));
    control.addSegment(QStringLiteral("Three"));
    control.setCurrentIndex(0);
    control.resize(control.sizeHint());
    control.show();
    QCoreApplication::processEvents();

    const auto buttons =
        control.findChildren<QAbstractButton*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(buttons.size(), 3);
    const QImage before = renderWidget(control);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(buttons.at(1), &enter);
    QCoreApplication::processEvents();
    const QImage after = renderWidget(control);
    QCOMPARE(after, before);

    const QRect selectedRect = buttons.at(0)->geometry();
    const QPoint sample(selectedRect.left() + 6, selectedRect.center().y());
    const QColor sampleColor = QColor::fromRgba(after.pixel(sample));
    const QColor accent = vkui::VkThemeManager::instance()->theme().colors().accent;
    QVERIFY(std::abs(sampleColor.red() - accent.red()) <= 2);
    QVERIFY(std::abs(sampleColor.green() - accent.green()) <= 2);
    QVERIFY(std::abs(sampleColor.blue() - accent.blue()) <= 2);
}

void StyleTest::switchShowsFocusOnlyForKeyboardNavigation() {
    vkui::VSwitch control;
    control.setChecked(true);
    control.resize(control.sizeHint());
    control.show();
    control.setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    QVERIFY(control.hasFocus());
    const QImage mouseFocus = renderWidget(control);

    control.clearFocus();
    QCoreApplication::processEvents();
    const QImage noFocus = renderWidget(control);
    QCOMPARE(mouseFocus, noFocus);

    control.setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    QVERIFY(control.hasFocus());
    const QImage keyboardFocus = renderWidget(control);
    QVERIFY(keyboardFocus != noFocus);
}

void StyleTest::sliderHandleDragPreservesCurrentValue() {
    InspectableSlider slider(Qt::Horizontal);
    slider.setRange(0, 3200);
    slider.setValue(2371);
    slider.resize(260, 30);
    slider.show();
    QCoreApplication::processEvents();

    QStyleOptionSlider option;
    slider.initStyleOption(&option);
    const QRect handle = slider.style()->subControlRect(QStyle::CC_Slider, &option,
                                                        QStyle::SC_SliderHandle, &slider);
    const QPoint pressPoint(handle.right() + 1, handle.center().y());
    QVERIFY(slider.rect().contains(pressPoint));
    QVERIFY(!handle.contains(pressPoint));
    QCOMPARE(slider.style()->hitTestComplexControl(QStyle::CC_Slider, &option, pressPoint, &slider),
             QStyle::SC_SliderHandle);

    const int initialValue = slider.value();
    QSignalSpy valueChanged(&slider, &QSlider::valueChanged);
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, pressPoint);
    QCOMPARE(slider.value(), initialValue);
    QCOMPARE(valueChanged.count(), 0);
    QVERIFY(slider.isSliderDown());

    // Qt sends an initial move at the press position on some platforms. A consistent
    // press-time anchor must preserve the exact value on an initial zero-distance move.
    QTest::mouseMove(&slider, pressPoint);
    QCOMPARE(slider.value(), initialValue);
    QCOMPARE(valueChanged.count(), 0);

    QTest::mouseMove(&slider, pressPoint + QPoint(8, 0));
    QVERIFY(slider.value() > initialValue);
    QVERIFY(slider.value() < initialValue + 150);

    QStyleOptionSlider movedOption;
    slider.initStyleOption(&movedOption);
    const QRect movedHandle = slider.style()->subControlRect(QStyle::CC_Slider, &movedOption,
                                                             QStyle::SC_SliderHandle, &slider);
    QVERIFY(std::abs((movedHandle.center().x() - handle.center().x()) - 8) <= 1);

    QTest::mouseMove(&slider, pressPoint);
    QCOMPARE(slider.value(), initialValue);
    QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, pressPoint);
    QVERIFY(!slider.isSliderDown());
}

void StyleTest::discreteSliderUsesMacStyleTicksAndCapsuleHandle() {
    InspectableSlider slider(Qt::Horizontal);
    slider.setRange(vkui::VkMinimumTextSizeLevel, vkui::VkMaximumTextSizeLevel);
    slider.setValue(vkui::VkDefaultTextSizeLevel);
    slider.setSingleStep(1);
    slider.setTickInterval(1);
    slider.setTickPosition(QSlider::TicksBelow);
    slider.resize(260, slider.sizeHint().height());
    slider.show();
    QCoreApplication::processEvents();

    QStyleOptionSlider option;
    slider.initStyleOption(&option);
    const QRect handle = slider.style()->subControlRect(QStyle::CC_Slider, &option,
                                                        QStyle::SC_SliderHandle, &slider);
    const QRect groove = slider.style()->subControlRect(QStyle::CC_Slider, &option,
                                                        QStyle::SC_SliderGroove, &slider);
    QVERIFY(handle.height() > handle.width());
    QVERIFY(groove.height() <= 2);
    QCOMPARE(
        slider.style()->hitTestComplexControl(QStyle::CC_Slider, &option, handle.center(), &slider),
        QStyle::SC_SliderHandle);

    const QImage rendered = renderWidget(slider);
    const int firstSampleX = groove.left() + qRound(groove.width() * 0.45);
    const int secondSampleX = groove.left() + qRound(groove.width() * 0.80);
    QCOMPARE(rendered.pixelColor(firstSampleX, groove.center().y()),
             rendered.pixelColor(secondSampleX, groove.center().y()));

    InspectableSlider continuous(Qt::Horizontal);
    continuous.setRange(0, 100);
    continuous.resize(260, 30);
    QStyleOptionSlider continuousOption;
    continuous.initStyleOption(&continuousOption);
    const QRect continuousHandle = continuous.style()->subControlRect(
        QStyle::CC_Slider, &continuousOption, QStyle::SC_SliderHandle, &continuous);
    QCOMPARE(continuousHandle.width(), continuousHandle.height());
}

void StyleTest::styleInteractionsAreEventDriven() {
    auto* themeManager = vkui::VkThemeManager::instance();
    const bool animationsWereEnabled = themeManager->animationsEnabled();
    struct AnimationSettingGuard {
        vkui::VkThemeManager* manager;
        bool enabled;
        ~AnimationSettingGuard() {
            manager->setAnimationsEnabled(enabled);
        }
    } restore{themeManager, animationsWereEnabled};
    themeManager->setAnimationsEnabled(true);

    InspectableToolButton button;
    button.setAutoRaise(true);
    button.resize(36, 36);

    QStyleOptionToolButton normal;
    button.initStyleOption(&normal);
    normal.state &= ~QStyle::State_MouseOver;
    QStyleOptionToolButton hovered = normal;
    hovered.state |= QStyle::State_MouseOver;
    const auto render = [&button](const QStyleOptionToolButton& option) {
        QImage image(button.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        button.style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, &button);
        return image;
    };
    QVERIFY2(render(normal) != render(hovered),
             "Hover must remain visible through the QStyleOption state");

    button.show();
    QCoreApplication::processEvents();

    PaintCounter counter;
    button.installEventFilter(&counter);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(&button, &enter);
    QCoreApplication::processEvents();
    const int paintsAfterStateChange = counter.paints;
    QTest::qWait(220);
    QCOMPARE(counter.paints, paintsAfterStateChange);
}

void StyleTest::dialogButtonsUsePlatformOrder() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QCOMPARE(qApp->style()->styleHint(QStyle::SH_DialogButtonLayout),
             static_cast<int>(QDialogButtonBox::MacLayout));
#else
    QVERIFY(qApp->style()->styleHint(QStyle::SH_DialogButtonLayout) >= 0);
#endif
}

void StyleTest::hiddenAnimationsSettleAtTheirTarget() {
    auto* themeManager = vkui::VkThemeManager::instance();
    const bool animationsWereEnabled = themeManager->animationsEnabled();
    struct AnimationSettingGuard {
        vkui::VkThemeManager* manager;
        bool enabled;
        ~AnimationSettingGuard() {
            manager->setAnimationsEnabled(enabled);
        }
    } restore{themeManager, animationsWereEnabled};
    themeManager->setAnimationsEnabled(true);

    QWidget owner;
    owner.show();
    QCoreApplication::processEvents();
    vkui::VkWidgetAnimation animation(&owner);
    qreal value = 0.0;
    bool completed = false;
    animation.start(
        0.0, 1.0, vkui::VkMotionRole::EmphasizedEnter, [&value](qreal frame) { value = frame; },
        [&completed] { completed = true; });
    QVERIFY(animation.isRunning());

    owner.hide();
    QVERIFY(!animation.isRunning());
    QCOMPARE(value, 1.0);
    QVERIFY(completed);
}

QTEST_MAIN(StyleTest)
#include "tst_style.moc"
