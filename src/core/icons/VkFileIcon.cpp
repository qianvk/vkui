// SPDX-License-Identifier: MIT

#include <QtCore/QSet>
#include <QtCore/QtMath>
#include <QtGui/QFontMetricsF>
#include <algorithm>
#include <vkui/core/VkFileIcon.h>

namespace vkui {

VkFileIconMetrics fileIconMetrics(const QFont& interfaceFont) {
    const QFontMetricsF metrics(interfaceFont);
    const qreal interfaceHeight = std::max<qreal>(1.0, metrics.height());
    const int iconExtent = std::max(1, qRound(interfaceHeight * 0.88));

    VkFileIconMetrics result;
    result.iconSize = QSize(iconExtent, iconExtent);
    result.textGap = std::max(2, qRound(interfaceHeight * 0.2));
    result.rowHeight =
        std::max(iconExtent, qCeil(interfaceHeight + std::max<qreal>(4.0, interfaceHeight * 0.38)));
    return result;
}

VkSymbol fileSymbolForPath(const QStringView path) {
    const qsizetype separator = std::max(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    const qsizetype dot = path.lastIndexOf(u'.');
    if (dot <= separator || dot + 1 >= path.size()) {
        return VkSymbol::FileGeneric;
    }

    const QString suffix = path.sliced(dot + 1).toString().toLower();
    static const QSet<QString> text{QStringLiteral("txt"), QStringLiteral("log"),
                                    QStringLiteral("rtf")};
    static const QSet<QString> code{
        QStringLiteral("c"),     QStringLiteral("cc"),   QStringLiteral("cpp"),
        QStringLiteral("cxx"),   QStringLiteral("h"),    QStringLiteral("hpp"),
        QStringLiteral("m"),     QStringLiteral("mm"),   QStringLiteral("rs"),
        QStringLiteral("go"),    QStringLiteral("py"),   QStringLiteral("js"),
        QStringLiteral("jsx"),   QStringLiteral("ts"),   QStringLiteral("tsx"),
        QStringLiteral("java"),  QStringLiteral("kt"),   QStringLiteral("swift"),
        QStringLiteral("lua"),   QStringLiteral("json"), QStringLiteral("xml"),
        QStringLiteral("yaml"),  QStringLiteral("yml"),  QStringLiteral("toml"),
        QStringLiteral("cmake"), QStringLiteral("sh")};
    static const QSet<QString> images{QStringLiteral("png"),  QStringLiteral("jpg"),
                                      QStringLiteral("jpeg"), QStringLiteral("gif"),
                                      QStringLiteral("webp"), QStringLiteral("bmp"),
                                      QStringLiteral("svg"),  QStringLiteral("heic"),
                                      QStringLiteral("tif"),  QStringLiteral("tiff")};
    static const QSet<QString> archives{
        QStringLiteral("zip"), QStringLiteral("7z"),  QStringLiteral("rar"), QStringLiteral("tar"),
        QStringLiteral("gz"),  QStringLiteral("bz2"), QStringLiteral("xz")};

    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")) {
        return VkSymbol::FileMarkdown;
    }
    if (text.contains(suffix)) {
        return VkSymbol::FileText;
    }
    if (code.contains(suffix)) {
        return VkSymbol::FileCode;
    }
    if (images.contains(suffix)) {
        return VkSymbol::FileImage;
    }
    if (suffix == QStringLiteral("pdf")) {
        return VkSymbol::FilePdf;
    }
    if (suffix == QStringLiteral("epub")) {
        return VkSymbol::FileBook;
    }
    if (archives.contains(suffix)) {
        return VkSymbol::FileArchive;
    }
    return VkSymbol::FileGeneric;
}

} // namespace vkui
