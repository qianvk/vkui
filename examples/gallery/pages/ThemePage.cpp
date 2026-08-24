// SPDX-License-Identifier: MIT

#include "ThemePage.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFontInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/VTextStyle.h>
#include <vkui/widgets/controls/VSlider.h>
#include <vkui/widgets/controls/VSwitch.h>
#include <vkui/widgets/effects/VLiquidGlass.h>

namespace {

class DefaultMarkerButton final : public QAbstractButton {
  public:
    explicit DefaultMarkerButton(const QString& text, QWidget* parent = nullptr)
        : QAbstractButton(parent) {
        setText(text);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        return {metrics.horizontalAdvance(text()) + 8, metrics.height() + 4};
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QColor foreground =
            palette().color(isEnabled() ? QPalette::ButtonText : QPalette::PlaceholderText);
        if (underMouse() && isEnabled()) {
            foreground = vkui::VkThemeManager::instance()->theme().colors().accent;
        }
        QPainter painter(this);
        painter.setPen(foreground);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextSingleLine, text());
    }

    void enterEvent(QEnterEvent* event) override {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override {
        QAbstractButton::leaveEvent(event);
        update();
    }
};

class TextSizePicker final : public QWidget {
  public:
    explicit TextSizePicker(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        smallerLabel_ = new QLabel(QStringLiteral("A"), this);
        smallerLabel_->setObjectName(QStringLiteral("textSizeSmallerLabel"));
        slider_ = new vkui::VSlider(Qt::Horizontal, this);
        slider_->setObjectName(QStringLiteral("interfaceTextSizeSlider"));
        slider_->setAccessibleName(ThemePage::tr("Text Size"));
        slider_->setRange(vkui::VkMinimumTextSizeLevel, vkui::VkMaximumTextSizeLevel);
        slider_->setSingleStep(1);
        slider_->setPageStep(1);
        slider_->setTickInterval(1);
        slider_->setTickPosition(QSlider::TicksBelow);
        slider_->setTracking(true);

        largerLabel_ = new QLabel(QStringLiteral("A"), this);
        largerLabel_->setObjectName(QStringLiteral("textSizeLargerLabel"));
        vkui::setTextStyle(*largerLabel_, vkui::VTextStyle::Title);
        defaultButton_ = new DefaultMarkerButton(ThemePage::tr("Default"), this);
        defaultButton_->setObjectName(QStringLiteral("defaultTextSizeButton"));
        vkui::setTextStyle(*defaultButton_, vkui::VTextStyle::Caption);

        connect(defaultButton_, &QAbstractButton::clicked, vkui::VkThemeManager::instance(),
                &vkui::VkThemeManager::resetTextSizeLevel);
        connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::themeChanged, this,
                [this](quint64, const vkui::VkThemeChanges changes) {
                    if (changes.testFlag(vkui::VkThemeChange::Metrics) ||
                        changes.testFlag(vkui::VkThemeChange::Typography)) {
                        updateGeometry();
                        layoutChildren();
                    }
                });
    }

    [[nodiscard]] vkui::VSlider* slider() const noexcept {
        return slider_;
    }

    [[nodiscard]] QSize sizeHint() const override {
        const int topHeight =
            std::max({smallerLabel_->sizeHint().height(), slider_->sizeHint().height(),
                      largerLabel_->sizeHint().height()});
        return {320, topHeight + defaultButton_->sizeHint().height()};
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        layoutChildren();
    }

    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::LayoutDirectionChange ||
            event->type() == QEvent::StyleChange) {
            updateGeometry();
            layoutChildren();
        }
    }

  private:
    void layoutChildren() {
        const auto& metrics = vkui::VkThemeManager::instance()->theme().metrics();
        const int gap = std::max(4, qRound(metrics.spacing6));
        const QSize smallerSize = smallerLabel_->sizeHint();
        const QSize largerSize = largerLabel_->sizeHint();
        const int topHeight =
            std::max({smallerSize.height(), slider_->sizeHint().height(), largerSize.height()});

        const bool rightToLeft = layoutDirection() == Qt::RightToLeft;
        const int leadingWidth = rightToLeft ? largerSize.width() : smallerSize.width();
        const int trailingWidth = rightToLeft ? smallerSize.width() : largerSize.width();
        QWidget* leadingLabel = rightToLeft ? static_cast<QWidget*>(largerLabel_)
                                            : static_cast<QWidget*>(smallerLabel_);
        QWidget* trailingLabel = rightToLeft ? static_cast<QWidget*>(smallerLabel_)
                                             : static_cast<QWidget*>(largerLabel_);
        leadingLabel->setGeometry(0, (topHeight - leadingLabel->sizeHint().height()) / 2,
                                  leadingWidth, leadingLabel->sizeHint().height());
        trailingLabel->setGeometry(width() - trailingWidth,
                                   (topHeight - trailingLabel->sizeHint().height()) / 2,
                                   trailingWidth, trailingLabel->sizeHint().height());

        const int sliderLeft = leadingWidth + gap;
        const int sliderWidth = std::max(1, width() - leadingWidth - trailingWidth - gap * 2);
        slider_->setGeometry(sliderLeft, 0, sliderWidth, topHeight);

        QStyleOptionSlider option;
        option.initFrom(slider_);
        option.orientation = slider_->orientation();
        option.minimum = slider_->minimum();
        option.maximum = slider_->maximum();
        option.sliderPosition = vkui::VkDefaultTextSizeLevel;
        option.sliderValue = vkui::VkDefaultTextSizeLevel;
        option.singleStep = slider_->singleStep();
        option.pageStep = slider_->pageStep();
        option.tickInterval = slider_->tickInterval();
        option.tickPosition = slider_->tickPosition();
        bool upsideDown = slider_->invertedAppearance();
        if (rightToLeft) {
            upsideDown = !upsideDown;
        }
        option.upsideDown = upsideDown;
        const QRect defaultHandle = slider_->style()->subControlRect(
            QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider_);
        const QSize defaultSize = defaultButton_->sizeHint();
        const int defaultCenter = sliderLeft + defaultHandle.center().x();
        defaultButton_->setGeometry(defaultCenter - defaultSize.width() / 2, topHeight,
                                    defaultSize.width(), defaultSize.height());
    }

    QLabel* smallerLabel_ = nullptr;
    vkui::VSlider* slider_ = nullptr;
    QLabel* largerLabel_ = nullptr;
    DefaultMarkerButton* defaultButton_ = nullptr;
};

class LiquidGlassPreviewSource final : public QWidget {
  public:
    explicit LiquidGlassPreviewSource(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient background(rect().topLeft(), rect().topRight());
        background.setColorAt(0.0, QColor(46, 118, 246));
        background.setColorAt(0.48, QColor(177, 78, 219));
        background.setColorAt(1.0, QColor(255, 118, 77));
        painter.fillRect(rect(), background);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 224, 74, 220));
        painter.drawEllipse(QPointF(width() * 0.22, height() * 0.30), 34.0, 34.0);
        painter.setBrush(QColor(45, 214, 139, 220));
        painter.drawEllipse(QPointF(width() * 0.78, height() * 0.28), 42.0, 42.0);

        QFont sampleFont = font();
        sampleFont.setBold(true);
        sampleFont.setPixelSize(std::max(18, height() / 5));
        painter.setFont(sampleFont);
        painter.setPen(QColor(255, 255, 255, 205));
        painter.drawText(QRect(0, 4, width(), height() / 2), Qt::AlignCenter,
                         QStringLiteral("LIQUID  GLASS"));
    }
};

class LiquidGlassPreview final : public QWidget {
  public:
    explicit LiquidGlassPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("liquidGlassPreview"));
        setMinimumHeight(150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        source_ = new LiquidGlassPreviewSource(this);
        backdrop_ = new vkui::VLiquidGlassBackdrop(source_, this);
        regular_ = createSurface(ThemePage::tr("Regular"), ThemePage::tr("Balanced refraction"),
                                 vkui::VLiquidGlassStyle::regular());
        clear_ = createSurface(ThemePage::tr("Clear"), ThemePage::tr("Maximum backdrop detail"),
                               vkui::VLiquidGlassStyle::clear());
        source_->lower();
    }

    [[nodiscard]] QSize sizeHint() const override {
        return {520, 150};
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        source_->setGeometry(rect());
        constexpr int outerMargin = 20;
        constexpr int gap = 16;
        const int surfaceWidth = std::max(1, (width() - outerMargin * 2 - gap) / 2);
        const int surfaceHeight = std::max(72, height() - outerMargin * 2);
        const int top = (height() - surfaceHeight) / 2;
        regular_->setGeometry(outerMargin, top, surfaceWidth, surfaceHeight);
        clear_->setGeometry(outerMargin + surfaceWidth + gap, top, surfaceWidth, surfaceHeight);
    }

  private:
    vkui::VLiquidGlassSurface* createSurface(const QString& title, const QString& detail,
                                             vkui::VLiquidGlassStyle style) {
        auto* surface = new vkui::VLiquidGlassSurface(this);
        surface->setBackdrop(backdrop_);
        style.cornerRadius = 18.0;
        surface->setGlassStyle(style);
        auto* layout = new QVBoxLayout(surface);
        layout->setContentsMargins(16, 10, 16, 10);
        layout->setSpacing(2);
        auto* titleLabel = new QLabel(title, surface);
        vkui::setTextStyle(*titleLabel, vkui::VTextStyle::BodyEmphasized);
        auto* detailLabel = new QLabel(detail, surface);
        vkui::setTextStyle(*detailLabel, vkui::VTextStyle::Caption);
        layout->addStretch();
        layout->addWidget(titleLabel);
        layout->addWidget(detailLabel);
        return surface;
    }

    LiquidGlassPreviewSource* source_ = nullptr;
    vkui::VLiquidGlassBackdrop* backdrop_ = nullptr;
    vkui::VLiquidGlassSurface* regular_ = nullptr;
    vkui::VLiquidGlassSurface* clear_ = nullptr;
};

QString accentColorName(vkui::VkAccentColor accentColor) {
    switch (accentColor) {
    case vkui::VkAccentColor::Blue:
        return ThemePage::tr("Blue");
    case vkui::VkAccentColor::Purple:
        return ThemePage::tr("Purple");
    case vkui::VkAccentColor::Pink:
        return ThemePage::tr("Pink");
    case vkui::VkAccentColor::Red:
        return ThemePage::tr("Red");
    case vkui::VkAccentColor::Orange:
        return ThemePage::tr("Orange");
    case vkui::VkAccentColor::Yellow:
        return ThemePage::tr("Yellow");
    case vkui::VkAccentColor::Green:
        return ThemePage::tr("Green");
    case vkui::VkAccentColor::Graphite:
        return ThemePage::tr("Graphite");
    }
    return {};
}

} // namespace

ThemePage::ThemePage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Theme and Appearance"), this);
    vkui::setTextStyle(*title, vkui::VTextStyle::Title);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("Auto follows the platform color scheme. A resolved immutable theme supplies semantic "
           "colors, metrics, typography, and motion to every subsystem."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearanceLayout = new QHBoxLayout(appearanceGroup);
    auto* buttons = new QButtonGroup(appearanceGroup);
    const QList<QPair<QString, vkui::VkAppearance>> choices{
        {tr("System"), vkui::VkAppearance::Auto},
        {tr("Light"), vkui::VkAppearance::Light},
        {tr("Dark"), vkui::VkAppearance::Dark},
    };
    for (const auto& choice : choices) {
        auto* button = new QRadioButton(choice.first, appearanceGroup);
        const int id = static_cast<int>(choice.second);
        buttons->addButton(button, id);
        button->setChecked(vkui::VkThemeManager::instance()->appearance() == choice.second);
        appearanceLayout->addWidget(button);
    }
    appearanceLayout->addStretch();
    connect(buttons, &QButtonGroup::idClicked, this, [](int id) {
        vkui::VkThemeManager::instance()->setAppearance(static_cast<vkui::VkAppearance>(id));
    });
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::appearanceChanged, this,
            [buttons](vkui::VkAppearance appearance) {
                if (auto* button = buttons->button(static_cast<int>(appearance))) {
                    button->setChecked(true);
                }
            });
    layout->addWidget(appearanceGroup);

    auto* accentGroup = new QGroupBox(tr("Accent color"), this);
    auto* accentLayout = new QGridLayout(accentGroup);
    auto* accentButtons = new QButtonGroup(accentGroup);
    const QList<vkui::VkAccentColor> accents{
        vkui::VkAccentColor::Blue,  vkui::VkAccentColor::Purple,   vkui::VkAccentColor::Pink,
        vkui::VkAccentColor::Red,   vkui::VkAccentColor::Orange,   vkui::VkAccentColor::Yellow,
        vkui::VkAccentColor::Green, vkui::VkAccentColor::Graphite,
    };
    for (int index = 0; index < accents.size(); ++index) {
        const vkui::VkAccentColor accent = accents.at(index);
        auto* button = new QRadioButton(accentColorName(accent), accentGroup);
        const int id = static_cast<int>(accent);
        accentButtons->addButton(button, id);
        button->setChecked(vkui::VkThemeManager::instance()->accentColor() == accent);
        accentLayout->addWidget(button, index / 4, index % 4);
    }
    connect(accentButtons, &QButtonGroup::idClicked, this, [](int id) {
        vkui::VkThemeManager::instance()->setAccentColor(static_cast<vkui::VkAccentColor>(id));
    });
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::accentColorChanged, this,
            [accentButtons](vkui::VkAccentColor accent) {
                if (auto* button = accentButtons->button(static_cast<int>(accent))) {
                    button->setChecked(true);
                }
            });
    layout->addWidget(accentGroup);

    auto* glassGroup = new QGroupBox(tr("Liquid Glass"), this);
    glassGroup->setObjectName(QStringLiteral("liquidGlassGroup"));
    auto* glassLayout = new QVBoxLayout(glassGroup);
    auto* glassControlRow = new QHBoxLayout;
    auto* glassLabel = new QLabel(tr("Enable Liquid Glass for supported surfaces"), glassGroup);
    auto* glassSwitch = new vkui::VSwitch(glassGroup);
    glassSwitch->setObjectName(QStringLiteral("liquidGlassEnabledSwitch"));
    glassSwitch->setAccessibleName(tr("Enable Liquid Glass"));
    glassSwitch->setChecked(vkui::VkThemeManager::instance()->liquidGlassEnabled());
    glassLabel->setBuddy(glassSwitch);
    glassControlRow->addWidget(glassLabel);
    glassControlRow->addWidget(glassSwitch);
    glassControlRow->addStretch();
    glassLayout->addLayout(glassControlRow);
    auto* glassExplanation = new QLabel(
        tr("The same cached Qt renderer is used by controls, menus, combobox popups, and "
           "popovers. Disabling it selects the opaque semantic fallback."),
        glassGroup);
    glassExplanation->setWordWrap(true);
    glassLayout->addWidget(glassExplanation);
    glassLayout->addWidget(new LiquidGlassPreview(glassGroup));
    connect(glassSwitch, &vkui::VSwitch::toggled, vkui::VkThemeManager::instance(),
            &vkui::VkThemeManager::setLiquidGlassEnabled);
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::liquidGlassEnabledChanged,
            glassSwitch, &vkui::VSwitch::setChecked);
    layout->addWidget(glassGroup);

    auto* textSizeGroup = new QGroupBox(tr("Text Size"), this);
    textSizeGroup->setObjectName(QStringLiteral("interfaceTextSizeGroup"));
    auto* textSizeLayout = new QVBoxLayout(textSizeGroup);
    auto* textSizeExplanation = new QLabel(
        tr("Standard Qt widgets inherit the application font automatically. Semantic headings, "
           "controls, meaningful icons, and spacing respond without dedicated label subclasses."),
        textSizeGroup);
    textSizeExplanation->setWordWrap(true);
    textSizeLayout->addWidget(textSizeExplanation);

    auto* textSizePicker = new TextSizePicker(textSizeGroup);
    auto* textSizeSlider = textSizePicker->slider();
    textSizeSlider->setValue(vkui::VkThemeManager::instance()->textSizeLevel());
    textSizeLayout->addWidget(textSizePicker);

    auto* previewRow = new QHBoxLayout;
    auto* previewButton =
        new QPushButton(vkui::icon(vkui::VkSymbol::Settings), tr("Settings"), textSizeGroup);
    auto* previewCheck = new QCheckBox(tr("Option"), textSizeGroup);
    previewCheck->setChecked(true);
    auto* previewCombo = new vkui::VCombobox(textSizeGroup);
    previewCombo->addItems({tr("System"), tr("Automatic")});
    previewCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* previewSwitch = new vkui::VSwitch(textSizeGroup);
    previewSwitch->setAccessibleName(tr("Preview switch"));
    previewSwitch->setChecked(true);
    previewRow->addWidget(previewButton);
    previewRow->addWidget(previewCheck);
    previewRow->addWidget(previewCombo);
    previewRow->addWidget(previewSwitch);
    previewRow->addStretch();
    textSizeLayout->addLayout(previewRow);

    connect(textSizeSlider, &QSlider::valueChanged, vkui::VkThemeManager::instance(),
            &vkui::VkThemeManager::setTextSizeLevel);
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::textSizeLevelChanged,
            textSizeSlider, [textSizeSlider](const int level) {
                const QSignalBlocker blocker(textSizeSlider);
                textSizeSlider->setValue(level);
            });
    layout->addWidget(textSizeGroup);

    auto* motionGroup = new QGroupBox(tr("Motion policy"), this);
    auto* motionLayout = new QHBoxLayout(motionGroup);
    auto* motionLabel = new QLabel(tr("Enable interface animations"), motionGroup);
    auto* motionSwitch = new vkui::VSwitch(motionGroup);
    motionSwitch->setAccessibleName(tr("Enable interface animations"));
    motionSwitch->setChecked(vkui::VkThemeManager::instance()->animationsEnabled());
    motionLabel->setBuddy(motionSwitch);
    motionLayout->addWidget(motionLabel);
    motionLayout->addWidget(motionSwitch);
    motionLayout->addStretch();
    connect(motionSwitch, &vkui::VSwitch::toggled, vkui::VkThemeManager::instance(),
            &vkui::VkThemeManager::setAnimationsEnabled);
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::animationsEnabledChanged,
            motionSwitch, &vkui::VSwitch::setChecked);
    layout->addWidget(motionGroup);

    auto* diagnostics = new QGroupBox(tr("Resolved theme"), this);
    auto* diagnosticsLayout = new QFormLayout(diagnostics);
    effectiveLabel_ = new QLabel(diagnostics);
    accentLabel_ = new QLabel(diagnostics);
    textSizeValueLabel_ = new QLabel(diagnostics);
    vkui::setTextStyle(*textSizeValueLabel_, vkui::VTextStyle::BodyEmphasized);
    typographyLabel_ = new QLabel(diagnostics);
    generationLabel_ = new QLabel(diagnostics);
    diagnosticsLayout->addRow(tr("Effective appearance"), effectiveLabel_);
    diagnosticsLayout->addRow(tr("Accent color"), accentLabel_);
    diagnosticsLayout->addRow(tr("Text size"), textSizeValueLabel_);
    diagnosticsLayout->addRow(tr("Responsive metrics"), typographyLabel_);
    diagnosticsLayout->addRow(tr("Theme generation"), generationLabel_);
    layout->addWidget(diagnostics);

    auto* note = new QLabel(
        tr("Theme changes update the application palette and invalidate generation-keyed icon and "
           "paint caches. VStyle itself is not recreated."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::themeChanged, this,
            [this] { updateSummary(); });
    updateSummary();
}

void ThemePage::updateSummary() {
    const auto* manager = vkui::VkThemeManager::instance();
    const auto appearance = manager->effectiveAppearance();
    effectiveLabel_->setText(appearance == vkui::VkAppearance::Dark ? tr("Dark") : tr("Light"));
    accentLabel_->setText(accentColorName(manager->accentColor()));

    const vkui::VkTheme& theme = manager->theme();
    const int level = manager->textSizeLevel();
    const qreal bodyPoints = QFontInfo(theme.typography().body).pointSizeF();
    textSizeValueLabel_->setText(
        bodyPoints > 0.0
            ? tr("Level %1 · %2 pt body").arg(level).arg(bodyPoints, 0, 'f', 1)
            : tr("Level %1 · %2 px body").arg(level).arg(theme.typography().body.pixelSize()));
    const int controlHeight = qRound(theme.metrics().controlHeightRegular);
    const int iconExtent = qRound(theme.metrics().controlHeightSmall * 0.67);
    typographyLabel_->setText(tr("%1 px control · %2 px icon").arg(controlHeight).arg(iconExtent));
    generationLabel_->setText(QString::number(theme.generation()));
}
