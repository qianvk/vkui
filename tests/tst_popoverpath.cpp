// SPDX-License-Identifier: MIT

#include "widgets/overlays/private/VkPopoverPath_p.h"
#include "widgets/overlays/private/VkPopoverShadowCache_p.h"

#include <QImage>
#include <QtTest>

#include <cmath>

class PopoverPathTest final : public QObject {
    Q_OBJECT

  private slots:
    void continuousCurvedArrowForEveryDirection_data();
    void continuousCurvedArrowForEveryDirection();
    void containsBodyCenterAndArrowTipBounds();
    void clampsArrowBaseAwayFromCorners();
    void invalidBodyProducesEmptyPath();
    void shadowIsDprCorrectAndSymmetric_data();
    void shadowIsDprCorrectAndSymmetric();
};

void PopoverPathTest::continuousCurvedArrowForEveryDirection_data() {
    QTest::addColumn<int>("placement");
    QTest::addColumn<QPointF>("tip");
    QTest::addColumn<QPointF>("base");
    QTest::newRow("below") << static_cast<int>(vkui::VkPopoverPlacement::Below)
                           << QPointF(120.0, 4.0) << QPointF(120.0, 20.0);
    QTest::newRow("above") << static_cast<int>(vkui::VkPopoverPlacement::Above)
                           << QPointF(120.0, 156.0) << QPointF(120.0, 140.0);
    QTest::newRow("right") << static_cast<int>(vkui::VkPopoverPlacement::Right)
                           << QPointF(4.0, 80.0) << QPointF(20.0, 80.0);
    QTest::newRow("left") << static_cast<int>(vkui::VkPopoverPlacement::Left)
                          << QPointF(236.0, 80.0) << QPointF(220.0, 80.0);
}

void PopoverPathTest::continuousCurvedArrowForEveryDirection() {
    QFETCH(int, placement);
    QFETCH(QPointF, tip);
    QFETCH(QPointF, base);
    const QRectF body(20.0, 20.0, 200.0, 120.0);
    const QPainterPath path = vkui::VkPopoverPath::create(
        body, static_cast<vkui::VkPopoverPlacement>(placement), tip, base, 20.0, 12.0);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.boundingRect().contains(body));
    QVERIFY(path.boundingRect().contains(tip));
    QCOMPARE(path.toSubpathPolygons().size(), 1);

    int curveElements = 0;
    for (int index = 0; index < path.elementCount(); ++index) {
        if (path.elementAt(index).type == QPainterPath::CurveToElement) {
            ++curveElements;
        }
    }
    QVERIFY(curveElements >= 2);
}

void PopoverPathTest::containsBodyCenterAndArrowTipBounds() {
    const QRectF body(20.0, 20.0, 200.0, 120.0);
    const QPointF tip(130.0, 3.0);
    const QPainterPath path = vkui::VkPopoverPath::create(body, vkui::VkPopoverPlacement::Below,
                                                          tip, QPointF(130.0, 20.0), 20.0, 12.0);
    QVERIFY(path.contains(body.center()));
    QVERIFY(path.boundingRect().contains(tip));
    QCOMPARE(path.boundingRect().left(), body.left());
    QCOMPARE(path.boundingRect().right(), body.right());
    QCOMPARE(path.boundingRect().bottom(), body.bottom());
}

void PopoverPathTest::clampsArrowBaseAwayFromCorners() {
    const QRectF body(20.0, 20.0, 200.0, 120.0);
    const qreal radius = 12.0;
    const qreal arrowWidth = 20.0;
    const QPainterPath path = vkui::VkPopoverPath::create(
        body, vkui::VkPopoverPlacement::Below, QPointF(body.left(), 3.0),
        QPointF(body.left(), body.top()), arrowWidth, radius);
    QVERIFY(path.elementCount() > 2);
    const QPainterPath::Element firstBase = path.elementAt(1);
    QVERIFY(firstBase.x >= body.left() + radius - 0.01);
    QVERIFY(firstBase.x + arrowWidth <= body.right() - radius + 0.01);
}

void PopoverPathTest::invalidBodyProducesEmptyPath() {
    QVERIFY(vkui::VkPopoverPath::create(QRectF{}, vkui::VkPopoverPlacement::Below, QPointF{},
                                        QPointF{}, 20.0, 12.0)
                .isEmpty());
}

void PopoverPathTest::shadowIsDprCorrectAndSymmetric_data() {
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::newRow("dpr-1") << 1.0;
    QTest::newRow("dpr-2") << 2.0;
}

void PopoverPathTest::shadowIsDprCorrectAndSymmetric() {
    QFETCH(qreal, devicePixelRatio);
    const QSize logicalSize(144, 112);
    const QRectF body(28.0, 24.0, 88.0, 56.0);
    QPainterPath path;
    path.addRoundedRect(body, 12.0, 12.0);

    vkui::VkPopoverShadowCache cache;
    const QPixmap& shadow =
        cache.shadow(path, logicalSize, devicePixelRatio, QColor(0, 0, 0, 112), 8.0,
                     QPointF(0.0, 2.0));
    QVERIFY(!shadow.isNull());
    QCOMPARE(shadow.devicePixelRatio(), devicePixelRatio);
    QCOMPARE(shadow.size(), QSize(qRound(logicalSize.width() * devicePixelRatio),
                                  qRound(logicalSize.height() * devicePixelRatio)));
    QCOMPARE(shadow.deviceIndependentSize(), QSizeF(logicalSize));

    const qint64 cacheKey = shadow.cacheKey();
    QCOMPARE(cache.shadow(path, logicalSize, devicePixelRatio, QColor(0, 0, 0, 112), 8.0,
                          QPointF(0.0, 2.0))
                 .cacheKey(),
             cacheKey);

    const QImage image = shadow.toImage();
    QRect alphaBounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 1) {
                alphaBounds |= QRect(x, y, 1, 1);
            }
        }
    }
    QVERIFY(!alphaBounds.isEmpty());

    const qreal bodyLeft = body.left() * devicePixelRatio;
    const qreal bodyRight = body.right() * devicePixelRatio;
    const qreal bodyTop = body.top() * devicePixelRatio;
    const qreal bodyBottom = body.bottom() * devicePixelRatio;
    const qreal leftExtent = bodyLeft - alphaBounds.left();
    const qreal rightExtent = alphaBounds.right() - bodyRight;
    const qreal topExtent = bodyTop - alphaBounds.top();
    const qreal bottomExtent = alphaBounds.bottom() - bodyBottom;
    QVERIFY(std::abs(leftExtent - rightExtent) <= 2.0);
    QVERIFY(std::abs((bottomExtent - topExtent) - 4.0 * devicePixelRatio) <= 3.0);
}

QTEST_MAIN(PopoverPathTest)
#include "tst_popoverpath.moc"
