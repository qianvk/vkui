// SPDX-License-Identifier: MIT

#include <QPainter>
#include <QSignalSpy>
#include <QtTest>
#include <cstdlib>
#include <memory>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/effects/VLiquidGlass.h>

namespace {

class SplitColorWidget final : public QWidget {
  public:
    explicit SplitColorWidget(QWidget* parent = nullptr) : QWidget(parent) {}

    void setColors(const QColor& leading, const QColor& trailing) {
        leading_ = leading;
        trailing_ = trailing;
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const int midpoint = width() / 2;
        painter.fillRect(QRect(0, 0, midpoint, height()), leading_);
        painter.fillRect(QRect(midpoint, 0, width() - midpoint, height()), trailing_);
    }

  private:
    QColor leading_{Qt::red};
    QColor trailing_{Qt::blue};
};

class StripeWidget final : public QWidget {
  public:
    explicit StripeWidget(QWidget* parent = nullptr) : QWidget(parent) {}

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        constexpr int stripeWidth = 8;
        for (int x = 0; x < width(); x += stripeWidth) {
            const bool light = (x / stripeWidth) % 2 != 0;
            painter.fillRect(QRect(x, 0, stripeWidth, height()),
                             light ? QColor(235, 235, 235) : QColor(24, 24, 24));
        }
    }
};

vkui::VLiquidGlassStyle exactBackdropStyle() {
    vkui::VLiquidGlassStyle style;
    style.cornerRadius = 0.0;
    style.blurRadius = 0.0;
    style.refractionHeight = 0.0;
    style.refractionAmount = 0.0;
    style.chromaticAberration = 0.0;
    style.saturation = 1.0;
    style.tintOpacity = 0.0;
    style.adaptiveLuminance = false;
    style.quality = vkui::VLiquidGlassQuality::High;
    return style;
}

QColor sampledColor(QWidget& widget, const QPoint& position) {
    const QImage image = widget.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QPoint devicePosition(qRound(position.x() * image.devicePixelRatio()),
                                qRound(position.y() * image.devicePixelRatio()));
    return image.pixelColor(devicePosition);
}

} // namespace

class LiquidGlassTest final : public QObject {
    Q_OBJECT

  private slots:
    void capturesBackdropWithoutRecursingIntoSurface();
    void sourcePaintInvalidatesEverySharedSurface();
    void sourceLifetimeIsSafe();
    void switchingSourceDisconnectsOldObservers();
    void disabledSurfaceDoesNotPaintMaterial();
    void materialPresetsHaveDistinctOptics();
    void regularMaterialRefractsBackdropAtEdge();
    void rimIsHorizontallySymmetric();
    void interiorLightingIsVerticallyBalanced();
    void themeTintPreferenceChangesRenderedMaterial();
    void styleValuesAreSanitized();
};

void LiquidGlassTest::capturesBackdropWithoutRecursingIntoSurface() {
    QWidget host;
    host.resize(260, 90);
    SplitColorWidget source(&host);
    source.setGeometry(10, 10, 240, 70);
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface surface(&host);
    surface.setGeometry(source.geometry());
    surface.setGlassStyle(exactBackdropStyle());
    surface.setBackdrop(&backdrop);
    surface.raise();
    host.show();
    QCoreApplication::processEvents();

    const QColor leading = sampledColor(surface, QPoint(24, surface.height() / 2));
    const QColor trailing =
        sampledColor(surface, QPoint(surface.width() - 24, surface.height() / 2));
    QVERIFY(leading.red() > 220);
    QVERIFY(leading.blue() < 35);
    QVERIFY(trailing.blue() > 220);
    QVERIFY(trailing.red() < 35);
}

void LiquidGlassTest::sourcePaintInvalidatesEverySharedSurface() {
    QWidget host;
    host.resize(300, 90);
    SplitColorWidget source(&host);
    source.setGeometry(0, 0, host.width(), host.height());
    vkui::VLiquidGlassBackdrop backdrop(&source);
    QSignalSpy invalidatedSpy(&backdrop, &vkui::VLiquidGlassBackdrop::invalidated);

    vkui::VLiquidGlassSurface first(&host);
    first.setGeometry(12, 12, 120, 60);
    first.setGlassStyle(exactBackdropStyle());
    first.setBackdrop(&backdrop);
    vkui::VLiquidGlassSurface second(&host);
    second.setGeometry(168, 12, 120, 60);
    second.setGlassStyle(exactBackdropStyle());
    second.setBackdrop(&backdrop);
    first.raise();
    second.raise();
    host.show();
    QCoreApplication::processEvents();

    invalidatedSpy.clear();
    source.setColors(Qt::green, Qt::yellow);
    source.repaint();
    QTRY_VERIFY(!invalidatedSpy.isEmpty());
    const QColor firstColor = sampledColor(first, QPoint(20, first.height() / 2));
    const QColor secondColor =
        sampledColor(second, QPoint(second.width() - 20, second.height() / 2));
    QVERIFY(firstColor.green() > 110);
    QVERIFY(secondColor.red() > 220);
    QVERIFY(secondColor.green() > 220);
}

void LiquidGlassTest::sourceLifetimeIsSafe() {
    auto source = std::make_unique<QWidget>();
    vkui::VLiquidGlassBackdrop backdrop(source.get());
    QSignalSpy sourceChangedSpy(&backdrop, &vkui::VLiquidGlassBackdrop::sourceWidgetChanged);
    QSignalSpy invalidatedSpy(&backdrop, &vkui::VLiquidGlassBackdrop::invalidated);

    source.reset();
    QCOMPARE(backdrop.sourceWidget(), nullptr);
    QCOMPARE(sourceChangedSpy.size(), 1);
    QCOMPARE(invalidatedSpy.size(), 1);
}

void LiquidGlassTest::switchingSourceDisconnectsOldObservers() {
    auto first = std::make_unique<QWidget>();
    auto second = std::make_unique<QWidget>();
    vkui::VLiquidGlassBackdrop backdrop(first.get());

    backdrop.setSourceWidget(second.get());
    first.reset();
    QCOMPARE(backdrop.sourceWidget(), second.get());

    second.reset();
    QCOMPARE(backdrop.sourceWidget(), nullptr);
}

void LiquidGlassTest::disabledSurfaceDoesNotPaintMaterial() {
    SplitColorWidget source;
    source.resize(120, 50);
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface surface;
    surface.resize(source.size());
    surface.setBackdrop(&backdrop);
    surface.setGlassEnabled(false);
    source.show();
    surface.show();
    QCoreApplication::processEvents();

    const QImage image = surface.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(image.pixelColor(image.rect().center()).alpha(), 0);
}

void LiquidGlassTest::materialPresetsHaveDistinctOptics() {
    const vkui::VLiquidGlassStyle regular = vkui::VLiquidGlassStyle::regular();
    const vkui::VLiquidGlassStyle clear = vkui::VLiquidGlassStyle::clear();
    const vkui::VLiquidGlassStyle popup = vkui::VLiquidGlassStyle::popup();

    QCOMPARE(regular.blurRadius, 1.0);
    QCOMPARE(regular.refractionHeight, 12.0);
    QCOMPARE(regular.refractionAmount, 24.0);
    QVERIFY(!regular.drawsBorder);
    QVERIFY(clear.blurRadius < regular.blurRadius);
    QVERIFY(clear.refractionHeight > regular.refractionHeight);
    QVERIFY(clear.refractionAmount > regular.refractionAmount);
    QVERIFY(popup.blurRadius > regular.blurRadius);
    QVERIFY(popup.tintOpacity > regular.tintOpacity);
    QVERIFY(popup.refractionHeight < regular.refractionHeight);
    QVERIFY(popup.refractionAmount < regular.refractionAmount);
    QVERIFY(popup.saturation < regular.saturation);
    QVERIFY(popup.opticalEdgeIntensity < regular.opticalEdgeIntensity);
}

void LiquidGlassTest::regularMaterialRefractsBackdropAtEdge() {
    QWidget host;
    host.resize(240, 70);
    StripeWidget source(&host);
    source.setGeometry(host.rect());
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface surface(&host);
    surface.setGeometry(20, 14, 200, 42);
    surface.setBackdrop(&backdrop);
    surface.setGlassStyle(exactBackdropStyle());
    surface.raise();
    host.show();
    QCoreApplication::processEvents();

    const QColor unrefracted = sampledColor(surface, QPoint(3, surface.height() / 2));
    surface.setGlassStyle(vkui::VLiquidGlassStyle::regular());
    backdrop.invalidate();
    QCoreApplication::processEvents();
    const QColor refracted = sampledColor(surface, QPoint(3, surface.height() / 2));

    QVERIFY(std::abs(unrefracted.lightness() - refracted.lightness()) > 40);
}

void LiquidGlassTest::rimIsHorizontallySymmetric() {
    QWidget host;
    host.resize(220, 70);
    SplitColorWidget source(&host);
    source.setColors(Qt::white, Qt::white);
    source.setGeometry(host.rect());
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface surface(&host);
    surface.setGeometry(20, 14, 180, 42);
    surface.setBackdrop(&backdrop);
    surface.raise();
    host.show();
    QCoreApplication::processEvents();

    const QColor leading = sampledColor(surface, QPoint(1, surface.height() / 2));
    const QColor trailing =
        sampledColor(surface, QPoint(surface.width() - 2, surface.height() / 2));
    QVERIFY(std::abs(leading.red() - trailing.red()) <= 2);
    QVERIFY(std::abs(leading.green() - trailing.green()) <= 2);
    QVERIFY(std::abs(leading.blue() - trailing.blue()) <= 2);
}

void LiquidGlassTest::interiorLightingIsVerticallyBalanced() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool originalEnabled = manager->liquidGlassEnabled();
    const int originalTint = manager->liquidGlassTintLevel();
    manager->setLiquidGlassEnabled(true);
    manager->setLiquidGlassTintLevel(vkui::VkMinimumLiquidGlassTintLevel);

    QWidget host;
    host.resize(240, 96);
    SplitColorWidget source(&host);
    source.setColors(Qt::white, Qt::white);
    source.setGeometry(host.rect());
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface surface(&host);
    surface.setGeometry(20, 16, 200, 64);
    auto style = vkui::VLiquidGlassStyle::regular();
    style.drawsBorder = false;
    surface.setGlassStyle(style);
    surface.setBackdrop(&backdrop);
    surface.raise();
    host.show();
    QCoreApplication::processEvents();

    const QColor upper = sampledColor(surface, QPoint(surface.width() / 2, surface.height() / 4));
    const QColor lower =
        sampledColor(surface, QPoint(surface.width() / 2, surface.height() * 3 / 4));
    QVERIFY(std::abs(upper.red() - lower.red()) <= 2);
    QVERIFY(std::abs(upper.green() - lower.green()) <= 2);
    QVERIFY(std::abs(upper.blue() - lower.blue()) <= 2);

    manager->setLiquidGlassTintLevel(originalTint);
    manager->setLiquidGlassEnabled(originalEnabled);
}

void LiquidGlassTest::themeTintPreferenceChangesRenderedMaterial() {
    auto* manager = vkui::VkThemeManager::instance();
    const bool originalEnabled = manager->liquidGlassEnabled();
    const int originalTint = manager->liquidGlassTintLevel();
    manager->setLiquidGlassEnabled(true);
    manager->setLiquidGlassTintLevel(vkui::VkMinimumLiquidGlassTintLevel);

    QWidget host;
    host.resize(220, 70);
    SplitColorWidget source(&host);
    source.setColors(QColor(20, 80, 220), QColor(245, 90, 30));
    source.setGeometry(host.rect());
    vkui::VLiquidGlassBackdrop backdrop(&source);
    vkui::VLiquidGlassSurface surface(&host);
    surface.setGeometry(20, 14, 180, 42);
    surface.setBackdrop(&backdrop);
    surface.raise();
    host.show();
    QCoreApplication::processEvents();
    const QImage clearImage = surface.grab().toImage();

    manager->setLiquidGlassTintLevel(vkui::VkMaximumLiquidGlassTintLevel);
    QCoreApplication::processEvents();
    const QImage tintedImage = surface.grab().toImage();
    QVERIFY(clearImage != tintedImage);

    manager->setLiquidGlassTintLevel(originalTint);
    manager->setLiquidGlassEnabled(originalEnabled);
}

void LiquidGlassTest::styleValuesAreSanitized() {
    vkui::VLiquidGlassSurface surface;
    vkui::VLiquidGlassStyle invalid;
    invalid.cornerRadius = -20.0;
    invalid.blurRadius = 1000.0;
    invalid.refractionHeight = -5.0;
    invalid.refractionAmount = 80.0;
    invalid.chromaticAberration = 80.0;
    invalid.saturation = 9.0;
    invalid.tintOpacity = -2.0;
    invalid.opticalEdgeIntensity = 9.0;
    surface.setGlassStyle(invalid);

    const vkui::VLiquidGlassStyle resolved = surface.glassStyle();
    QCOMPARE(resolved.cornerRadius, -1.0);
    QCOMPARE(resolved.blurRadius, 64.0);
    QCOMPARE(resolved.refractionHeight, 0.0);
    QCOMPARE(resolved.refractionAmount, 32.0);
    QCOMPARE(resolved.chromaticAberration, 8.0);
    QCOMPARE(resolved.saturation, 2.0);
    QCOMPARE(resolved.tintOpacity, 0.0);
    QCOMPARE(resolved.opticalEdgeIntensity, 1.0);
}

QTEST_MAIN(LiquidGlassTest)

#include "tst_liquidglass.moc"
