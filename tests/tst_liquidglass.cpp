// SPDX-License-Identifier: MIT

#include <QPainter>
#include <QSignalSpy>
#include <QtTest>
#include <memory>
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
    surface.setGlassStyle(invalid);

    const vkui::VLiquidGlassStyle resolved = surface.glassStyle();
    QCOMPARE(resolved.cornerRadius, -1.0);
    QCOMPARE(resolved.blurRadius, 64.0);
    QCOMPARE(resolved.refractionHeight, 0.0);
    QCOMPARE(resolved.refractionAmount, 32.0);
    QCOMPARE(resolved.chromaticAberration, 8.0);
    QCOMPARE(resolved.saturation, 2.0);
    QCOMPARE(resolved.tintOpacity, 0.0);
}

QTEST_MAIN(LiquidGlassTest)

#include "tst_liquidglass.moc"
