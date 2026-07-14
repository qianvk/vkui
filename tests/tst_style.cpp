// SPDX-License-Identifier: MIT

#include "widgets/style/private/VkStylePainter_p.h"
#include "widgets/style/private/VkWidgetAnimationManager_p.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QProxyStyle>
#include <QQueue>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QTemporaryFile>
#include <QToolButton>
#include <QtTest>
#include <cmath>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VkComboBox.h>
#include <vkui/widgets/VkControlSize.h>
#include <vkui/widgets/controls/VkSegmentedControl.h>
#include <vkui/widgets/controls/VkSwitch.h>
#include <vkui/widgets/style/VkStyle.h>
#include <vkui/widgets/style/VkStyleSheet.h>

namespace {

class InspectableComboBox final : public QComboBox {
  public:
    using QComboBox::initStyleOption;
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
            const QColor pixel = QColor::fromRgba(image.pixel(x, y));
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
    void comboPopupUsesOneRoundedSurface();
    void existingWidgetsAreRepolishedAfterThemeChange();
    void optionalStyleSheetOverlayTracksPalette();
    void embeddedEditorsDoNotPaintASecondFrame();
    void comboBoxSizingAndElisionProtectTheChevronColumn();
    void fixedControlsHonorSizeClasses();
    void selectedIndicatorsUseWhiteMarks();
    void segmentedControlHasNoHoverVisual();
    void switchShowsFocusOnlyForKeyboardNavigation();
    void paintQueriesDoNotRetargetInputAnimations();

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
        const QColor foreground = vkui::VkStylePainter::contrastingText(background);
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

void StyleTest::comboPopupUsesOneRoundedSurface() {
    InspectableComboBox combo;
    combo.addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    combo.resize(180, 30);
    combo.show();
    combo.showPopup();
    QTRY_VERIFY(combo.view()->isVisible());
    QWidget* popup = combo.view()->window();
    QVERIFY(popup != nullptr);
    QVERIFY(popup->inherits("QComboBoxPrivateContainer"));
    QVERIFY(popup->testAttribute(Qt::WA_TranslucentBackground));
    QVERIFY(popup->mask().isEmpty());
    QCOMPARE(combo.view()->frameShape(), QFrame::NoFrame);
    QCOMPARE(combo.view()->viewport()->palette().color(QPalette::Base), QColor(Qt::transparent));
    QImage popupImage(popup->size(), QImage::Format_ARGB32_Premultiplied);
    popupImage.fill(Qt::transparent);
    popup->render(&popupImage);
    QVERIFY(QColor::fromRgba(popupImage.pixel(0, 0)).alpha() < 64);
    QVERIFY(QColor::fromRgba(popupImage.pixel(popupImage.rect().center())).alpha() > 192);
    combo.hidePopup();
}

void StyleTest::existingWidgetsAreRepolishedAfterThemeChange() {
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

    QTRY_VERIFY(probeStyle->unpolishCount > unpolishCount);
    QTRY_VERIFY(probeStyle->polishCount > polishCount);
    QCOMPARE(probeStyle->unpolishCount, unpolishCount + 1);
    QCOMPARE(probeStyle->polishCount, polishCount + 1);
    QCOMPARE(qApp->palette().color(QPalette::Base), manager->theme().colors().contentBackground);
}

void StyleTest::optionalStyleSheetOverlayTracksPalette() {
    QTemporaryFile file;
    QVERIFY(file.open());
    QVERIFY(file.write("QWidget#QssPaletteProbe { background: palette(base); }") > 0);
    file.flush();

    QString error;
    const bool applied = vkui::applyStyleSheetFile(*qApp, file.fileName(), &error);
    QWidget probe;
    probe.setObjectName(QStringLiteral("QssPaletteProbe"));
    probe.resize(48, 32);
    probe.show();

    auto* manager = vkui::VkThemeManager::instance();
    manager->setAppearance(vkui::VkAppearance::Light);
    QCoreApplication::processEvents();
    const QColor lightPixel = QColor::fromRgba(renderWidget(probe).pixel(probe.rect().center()));
    const QColor lightBase = manager->theme().colors().contentBackground;

    manager->setAppearance(vkui::VkAppearance::Dark);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const QColor darkPixel = QColor::fromRgba(renderWidget(probe).pixel(probe.rect().center()));
    const QColor darkBase = manager->theme().colors().contentBackground;
    vkui::clearStyleSheet(*qApp);

    QVERIFY2(applied, qPrintable(error));
    QCOMPARE(lightPixel.toRgb(), lightBase.toRgb());
    QCOMPARE(darkPixel.toRgb(), darkBase.toRgb());
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
    InspectableComboBox combo;
    const QString label = QStringLiteral("A deliberately long combo-box value");
    combo.addItem(label);
    combo.setSizeAdjustPolicy(QComboBox::AdjustToContents);
    vkui::setComboBoxElideMode(combo, Qt::ElideMiddle);
    QCOMPARE(vkui::comboBoxElideMode(combo), Qt::ElideMiddle);
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
}

void StyleTest::fixedControlsHonorSizeClasses() {
    QCheckBox checkBox;
    QRadioButton radioButton;
    vkui::setControlSize(checkBox, vkui::VkControlSize::Small);
    vkui::setControlSize(radioButton, vkui::VkControlSize::Small);
    const int smallCheck =
        checkBox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkBox);
    const int smallRadio =
        radioButton.style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, &radioButton);

    vkui::setControlSize(checkBox, vkui::VkControlSize::Large);
    vkui::setControlSize(radioButton, vkui::VkControlSize::Large);
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
             vkui::controlExtent(vkui::VkControlSize::Large));
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
    vkui::VkSegmentedControl control;
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
    vkui::VkSwitch control;
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

void StyleTest::paintQueriesDoNotRetargetInputAnimations() {
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

    QWidget host;
    QToolButton button(&host);
    host.resize(120, 60);
    button.setGeometry(10, 10, 32, 32);
    host.show();
    QCoreApplication::processEvents();

    vkui::VkWidgetAnimationManager manager;
    manager.watch(&button);

    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(&button, &enter);
    QTRY_VERIFY_WITH_TIMEOUT(manager.progress(&button, false, false, false, false).hover > 0.99,
                             1000);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&button, &leave);
    QTRY_VERIFY_WITH_TIMEOUT(manager.progress(&button, true, false, false, false).hover < 0.01,
                             1000);
}

QTEST_MAIN(StyleTest)
#include "tst_style.moc"
