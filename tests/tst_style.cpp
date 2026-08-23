// SPDX-License-Identifier: MIT

#include "widgets/animation/private/VkWidgetAnimation_p.h"
#include "widgets/style/private/VStylePainter_p.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QProxyStyle>
#include <QQueue>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>
#include <QStyleOptionToolButton>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QtTest>
#include <cmath>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/VControlSize.h>
#include <vkui/widgets/controls/VSegmentedControl.h>
#include <vkui/widgets/controls/VSlider.h>
#include <vkui/widgets/controls/VSwitch.h>
#include <vkui/widgets/style/VStyle.h>

#if defined(Q_OS_MACOS)
#include "private/MacWindowStackProbe.h"
#endif

namespace {

class InspectableComboBox final : public vkui::VCombobox {
  public:
    using VCombobox::initStyleOption;
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
    void comboPopupUsesMacStyleItems();
    void comboPopupUsesOneRoundedSurface();
    void submenuStaysAboveItsRestackedParent();
    void colorChangesAvoidStructuralRepolish();
    void embeddedEditorsDoNotPaintASecondFrame();
    void comboBoxSizingAndElisionProtectTheChevronColumn();
    void fixedControlsHonorSizeClasses();
    void selectedIndicatorsUseWhiteMarks();
    void segmentedControlHasNoHoverVisual();
    void switchShowsFocusOnlyForKeyboardNavigation();
    void sliderHandleDragPreservesCurrentValue();
    void styleInteractionsAreEventDriven();
    void dialogButtonsUsePlatformOrder();
    void hiddenAnimationsSettleAtTheirTarget();

  private:
    bool animationsEnabled_ = true;
    vkui::VkAccentColor accentColor_ = vkui::VkAccentColor::Blue;
    vkui::VkAppearance appearance_ = vkui::VkAppearance::Auto;
};

void StyleTest::initTestCase() {
    vkui::installVkUi(*qApp);
    auto* manager = vkui::VkThemeManager::instance();
    animationsEnabled_ = manager->animationsEnabled();
    accentColor_ = manager->accentColor();
    appearance_ = manager->appearance();
    manager->setAnimationsEnabled(false);
}

void StyleTest::cleanupTestCase() {
    auto* manager = vkui::VkThemeManager::instance();
    manager->setAccentColor(accentColor_);
    manager->setAppearance(appearance_);
    manager->setAnimationsEnabled(animationsEnabled_);
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

void StyleTest::comboPopupUsesOneRoundedSurface() {
    InspectableComboBox combo;
    combo.addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    combo.setCurrentIndex(1);
    combo.resize(180, 30);
    combo.move(240, 180);
    combo.show();
    QCoreApplication::processEvents();

    const QPalette viewPalette = combo.view()->palette();
    const QPalette viewportPalette = combo.view()->viewport()->palette();
    const bool viewAutoFillBackground = combo.view()->autoFillBackground();
    const bool viewportAutoFillBackground = combo.view()->viewport()->autoFillBackground();

    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());
    QWidget* popup = combo.view()->window();
    QVERIFY(popup != nullptr);
    QVERIFY(popup->inherits("QComboBoxPrivateContainer"));
    auto* popupFrame = qobject_cast<QFrame*>(popup);
    QVERIFY(popupFrame != nullptr);
    QCOMPARE(popupFrame->frameStyle(), static_cast<int>(QFrame::NoFrame));
    QVERIFY(popup->testAttribute(Qt::WA_TranslucentBackground));
    QVERIFY(popup->mask().isEmpty());
    QCOMPARE(combo.view()->frameShape(), QFrame::NoFrame);
    QCOMPARE(combo.view()->palette(), viewPalette);
    QCOMPARE(combo.view()->viewport()->palette(), viewportPalette);
    QCOMPARE(combo.view()->autoFillBackground(), viewAutoFillBackground);
    QCOMPARE(combo.view()->viewport()->autoFillBackground(), viewportAutoFillBackground);

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
    const int cornerMargin = combo.style()->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &combo);
    QVERIFY(QColor::fromRgba(popupImage.pixel(0, 0)).alpha() < 64);
    QVERIFY(QColor::fromRgba(popupImage.pixel(0, cornerMargin)).alpha() < 96);
    QVERIFY(QColor::fromRgba(popupImage.pixel(popupImage.width() - 1, cornerMargin)).alpha() < 96);
    QVERIFY(QColor::fromRgba(popupImage.pixel(popupImage.rect().center())).alpha() > 192);

    const auto& theme = vkui::VkThemeManager::instance()->theme();
    const QPoint selectedLocal = popup->mapFromGlobal(selectedGlobal.topLeft());
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
    combo.hidePopup();
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
    largeItem.font = largeFont;
    largeItem.fontMetrics = QFontMetrics(largeFont);
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
