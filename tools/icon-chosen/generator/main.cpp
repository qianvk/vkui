// SPDX-License-Identifier: MIT

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainterPath>
#include <QRawFont>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace {

constexpr int SymbolsPerPack = 256;
constexpr qreal CanvasExtent = 24.0;
constexpr qreal CanvasPadding = 1.5;

struct Symbol final {
    quint32 codePoint = 0;
    QString name;
    QPainterPath path;
};

bool isPrivateUse(const quint32 codePoint) {
    return (codePoint >= 0xE000 && codePoint <= 0xF8FF) ||
           (codePoint >= 0xF0000 && codePoint <= 0xFFFFD) ||
           (codePoint >= 0x100000 && codePoint <= 0x10FFFD);
}

bool isNerdFontStandardSymbol(const quint32 codePoint) {
    switch (codePoint) {
    case 0x23FB:
    case 0x23FC:
    case 0x23FD:
    case 0x23FE:
    case 0x2665:
    case 0x26A1:
    case 0x2B58:
        return true;
    default:
        return false;
    }
}

QString number(const qreal value) {
    QString result = QString::number(value, 'f', 3);
    while (result.contains(u'.') && result.endsWith(u'0')) {
        result.chop(1);
    }
    if (result.endsWith(u'.')) {
        result.chop(1);
    }
    return result == QStringLiteral("-0") ? QStringLiteral("0") : result;
}

QString svgPath(const QPainterPath& source) {
    const QRectF bounds = source.boundingRect();
    if (bounds.isEmpty()) {
        return {};
    }

    const qreal available = CanvasExtent - 2.0 * CanvasPadding;
    const qreal scale = std::min(available / bounds.width(), available / bounds.height());
    const qreal xOffset = (CanvasExtent - bounds.width() * scale) / 2.0 - bounds.left() * scale;
    const qreal yOffset = (CanvasExtent - bounds.height() * scale) / 2.0 - bounds.top() * scale;
    const auto x = [scale, xOffset](const qreal value) { return number(value * scale + xOffset); };
    const auto y = [scale, yOffset](const qreal value) { return number(value * scale + yOffset); };

    QString result;
    QTextStream output(&result);
    for (int index = 0; index < source.elementCount(); ++index) {
        const QPainterPath::Element element = source.elementAt(index);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            output << 'M' << x(element.x) << ',' << y(element.y);
            break;
        case QPainterPath::LineToElement:
            output << 'L' << x(element.x) << ',' << y(element.y);
            break;
        case QPainterPath::CurveToElement: {
            if (index + 2 >= source.elementCount()) {
                return {};
            }
            const QPainterPath::Element control2 = source.elementAt(++index);
            const QPainterPath::Element end = source.elementAt(++index);
            output << 'C' << x(element.x) << ',' << y(element.y) << ' ' << x(control2.x) << ','
                   << y(control2.y) << ' ' << x(end.x) << ',' << y(end.y);
            break;
        }
        case QPainterPath::CurveToDataElement:
            return {};
        }
    }
    return result;
}

bool writePack(const QList<Symbol>& symbols, const int packIndex, const QDir& outputDirectory,
               QTextStream& catalog) {
    const QString fileName =
        QStringLiteral("symbols-%1.svg").arg(packIndex, 3, 10, QLatin1Char('0'));
    QFile file(outputDirectory.filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream output(&file);
    output << "<!-- SPDX-License-Identifier: OFL-1.1 -->\n"
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">\n";
    for (int index = 0; index < symbols.size(); ++index) {
        const Symbol& symbol = symbols.at(index);
        const QString path = svgPath(symbol.path);
        if (path.isEmpty()) {
            continue;
        }
        const QString id = QStringLiteral("i%1").arg(index);
        output << "<g id=\"" << id << "\"><path d=\"" << path << "\" fill=\"#000001\"/></g>\n";
        catalog << symbol.name << '\t' << QString::number(symbol.codePoint, 16).toUpper() << '\t'
                << fileName << '\t' << id << '\n';
    }
    output << "</svg>\n";
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    if (application.arguments().size() != 3) {
        qCritical("Usage: nerd-symbol-generator <font-file> <output-directory>");
        return 2;
    }

    const QString fontPath = application.arguments().at(1);
    QDir outputDirectory(application.arguments().at(2));
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        qCritical("Could not create the output directory");
        return 3;
    }

    const QRawFont font(fontPath, 2048.0, QFont::PreferNoHinting);
    if (!font.isValid()) {
        qCritical("Could not load the source font");
        return 4;
    }

    QFile catalogFile(outputDirectory.filePath(QStringLiteral("catalog.tsv")));
    if (!catalogFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical("Could not create the catalog index");
        return 5;
    }
    QTextStream catalog(&catalogFile);
    catalog << "# Nerd Fonts v3.4.0 / FiraCode Nerd Font\n";
    catalog << "# name\tcodepoint\tpack\telement\n";

    const QRegularExpression linePattern(QStringLiteral("^U\\+([0-9A-F]+)\\t(.+)$"));
    QTextStream input(stdin, QIODevice::ReadOnly);
    QList<Symbol> pack;
    pack.reserve(SymbolsPerPack);
    int packIndex = 0;
    int symbolCount = 0;

    while (!input.atEnd()) {
        const QRegularExpressionMatch match = linePattern.match(input.readLine());
        if (!match.hasMatch()) {
            continue;
        }

        bool validCodePoint = false;
        const quint32 codePoint = match.captured(1).toUInt(&validCodePoint, 16);
        if (!validCodePoint || (!isPrivateUse(codePoint) && !isNerdFontStandardSymbol(codePoint))) {
            continue;
        }

        const char32_t character = codePoint;
        const QList<quint32> glyphs = font.glyphIndexesForString(QString::fromUcs4(&character, 1));
        if (glyphs.isEmpty() || glyphs.constFirst() == 0) {
            continue;
        }
        QPainterPath path = font.pathForGlyph(glyphs.constFirst());
        if (path.isEmpty()) {
            continue;
        }

        pack.append(Symbol{codePoint, match.captured(2), std::move(path)});
        ++symbolCount;
        if (pack.size() == SymbolsPerPack) {
            if (!writePack(pack, packIndex++, outputDirectory, catalog)) {
                qCritical("Could not write an SVG pack");
                return 6;
            }
            pack.clear();
        }
    }

    if (!pack.isEmpty() && !writePack(pack, packIndex++, outputDirectory, catalog)) {
        qCritical("Could not write the final SVG pack");
        return 7;
    }

    qInfo("Generated %d symbols in %d SVG packs", symbolCount, packIndex);
    return symbolCount == 0 ? 8 : 0;
}
