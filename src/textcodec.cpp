// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "textcodec.h"

#include <QLocale>
#include <QStringDecoder>
#include <QStringEncoder>

// Enough bytes to judge a legacy encoding by; whole files can be large.
static constexpr qsizetype sampleSize = 256 * 1024;

static bool isValidUtf8(const QByteArray &data)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    // Decoding in one call reports a sequence cut at the end as an error too.
    const QString text = decoder.decode(data);
    Q_UNUSED(text);
    return !decoder.hasError();
}

static bool decodesCleanly(const QByteArray &encoding, QByteArrayView sample)
{
    QStringDecoder decoder(encoding.constData());
    if (!decoder.isValid())
        return false;
    const QString text = decoder.decode(sample);
    Q_UNUSED(text);
    return !decoder.hasError();
}

// A sample cut at a byte boundary may end in half a character.
static QByteArrayView trimmedSample(const QByteArray &data)
{
    if (data.size() <= sampleSize)
        return data;
    qsizetype end = sampleSize;
    while (end > sampleSize - 8 && quint8(data.at(end - 1)) >= 0x80)
        --end;
    return QByteArrayView(data).left(end);
}

// UTF-16 without a BOM: one byte of nearly every ASCII character is zero.
static QByteArray detectUtf16(QByteArrayView sample)
{
    const qsizetype count = qMin<qsizetype>(sample.size(), 4096) & ~qsizetype(1);
    if (count < 4)
        return {};
    int evenZero = 0;
    int oddZero = 0;
    for (qsizetype i = 0; i < count; i += 2) {
        evenZero += sample.at(i) == 0;
        oddZero += sample.at(i + 1) == 0;
    }
    const int pairs = int(count / 2);
    if (oddZero > pairs * 0.4 && evenZero < pairs * 0.05)
        return QByteArrayLiteral("UTF-16LE");
    if (evenZero > pairs * 0.4 && oddZero < pairs * 0.05)
        return QByteArrayLiteral("UTF-16BE");
    return {};
}

// How much the high bytes look like text in a double-byte encoding: common
// characters weigh 1, rare regions less. Returns the average weight.
static double doubleByteScore(QByteArrayView sample, const QByteArray &encoding)
{
    double weight = 0;
    int characters = 0;
    const qsizetype size = sample.size();
    for (qsizetype i = 0; i < size; ++i) {
        const quint8 lead = sample.at(i);
        if (lead < 0x80)
            continue;
        const quint8 trail = i + 1 < size ? quint8(sample.at(i + 1)) : 0;
        ++characters;

        if (encoding == "GB18030") {
            if (trail >= 0x30 && trail <= 0x39) {
                weight += 0.3;
                i += 3;
            } else {
                weight += lead >= 0xA1 && lead <= 0xF7 && trail >= 0xA1 ? 1.0 : 0.2;
                ++i;
            }
        } else if (encoding == "Big5") {
            weight += lead >= 0xA4 && lead <= 0xC6 ? 1.0 : 0.3;
            ++i;
        } else if (encoding == "Shift_JIS") {
            if (lead >= 0xA1 && lead <= 0xDF) {
                weight += 0.2;
            } else {
                weight += lead == 0x81 || lead == 0x82 || lead == 0x83 || (lead >= 0x88 && lead <= 0x9F)
                        ? 1.0 : 0.3;
                ++i;
            }
        } else if (encoding == "EUC-JP") {
            weight += lead == 0xA1 || lead == 0xA4 || lead == 0xA5 || (lead >= 0xB0 && lead <= 0xCF)
                    ? 1.0 : 0.3;
            ++i;
        } else if (encoding == "EUC-KR") {
            weight += (lead >= 0xB0 && lead <= 0xC8) || lead == 0xA1 ? 1.0 : 0.3;
            ++i;
        }
    }
    return characters > 0 ? weight / characters : 0;
}

// Western text puts accented letters one at a time between ASCII ones; text
// in a double-byte encoding read as Latin comes in runs.
static double singleByteScore(QByteArrayView sample)
{
    int runs = 0;
    int single = 0;
    qsizetype run = 0;
    for (qsizetype i = 0; i <= sample.size(); ++i) {
        if (i < sample.size() && quint8(sample.at(i)) >= 0x80) {
            ++run;
            continue;
        }
        if (run > 0) {
            ++runs;
            single += run == 1;
        }
        run = 0;
    }
    return runs > 0 ? double(single) / runs : 0;
}

static bool isDoubleByte(const QByteArray &encoding)
{
    return encoding == "GB18030" || encoding == "Big5" || encoding == "Shift_JIS"
            || encoding == "EUC-JP" || encoding == "EUC-KR";
}

QByteArray TextCodec::localeEncoding()
{
    const QLocale locale = QLocale::system();
    switch (locale.language()) {
    case QLocale::Chinese:
        return locale.script() == QLocale::TraditionalChineseScript
                        || locale.territory() == QLocale::Taiwan || locale.territory() == QLocale::HongKong
                        || locale.territory() == QLocale::Macao
                ? QByteArrayLiteral("Big5") : QByteArrayLiteral("GB18030");
    case QLocale::Japanese:
        return QByteArrayLiteral("Shift_JIS");
    case QLocale::Korean:
        return QByteArrayLiteral("EUC-KR");
    case QLocale::Russian:
    case QLocale::Ukrainian:
    case QLocale::Belarusian:
    case QLocale::Bulgarian:
    case QLocale::Macedonian:
    case QLocale::Serbian:
        return QByteArrayLiteral("windows-1251");
    case QLocale::Polish:
    case QLocale::Czech:
    case QLocale::Slovak:
    case QLocale::Hungarian:
    case QLocale::Slovenian:
    case QLocale::Croatian:
    case QLocale::Romanian:
        return QByteArrayLiteral("windows-1250");
    case QLocale::Greek:
        return QByteArrayLiteral("windows-1253");
    case QLocale::Turkish:
        return QByteArrayLiteral("windows-1254");
    case QLocale::Hebrew:
        return QByteArrayLiteral("windows-1255");
    case QLocale::Arabic:
    case QLocale::Persian:
        return QByteArrayLiteral("windows-1256");
    case QLocale::Lithuanian:
    case QLocale::Latvian:
    case QLocale::Estonian:
        return QByteArrayLiteral("windows-1257");
    case QLocale::Vietnamese:
        return QByteArrayLiteral("windows-1258");
    case QLocale::Thai:
        return QByteArrayLiteral("windows-874");
    default:
        return QByteArrayLiteral("windows-1252");
    }
}

QByteArray TextCodec::detect(const QByteArray &data, bool *bom)
{
    if (bom)
        *bom = true;
    if (data.startsWith("\xEF\xBB\xBF"))
        return QByteArrayLiteral("UTF-8");
    if (data.startsWith(QByteArrayView("\xFF\xFE\x00\x00", 4)))
        return QByteArrayLiteral("UTF-32LE");
    if (data.startsWith(QByteArrayView("\x00\x00\xFE\xFF", 4)))
        return QByteArrayLiteral("UTF-32BE");
    if (data.startsWith("\xFF\xFE"))
        return QByteArrayLiteral("UTF-16LE");
    if (data.startsWith("\xFE\xFF"))
        return QByteArrayLiteral("UTF-16BE");
    if (bom)
        *bom = false;

    const QByteArrayView sample = trimmedSample(data);
    const QByteArray utf16 = detectUtf16(sample);
    if (!utf16.isEmpty())
        return utf16;

    if (isValidUtf8(data.size() <= sampleSize ? data : sample.toByteArray()))
        return QByteArrayLiteral("UTF-8");

    const QByteArray preferred = localeEncoding();
    QList<QByteArray> candidates{preferred};
    for (const char *name : {"GB18030", "Big5", "Shift_JIS", "EUC-JP", "EUC-KR", "windows-1252"}) {
        if (preferred != name)
            candidates.append(name);
    }

    QByteArray best = QByteArrayLiteral("ISO-8859-1");
    double bestScore = -1;
    for (const QByteArray &candidate : std::as_const(candidates)) {
        if (!decodesCleanly(candidate, sample))
            continue;
        double score;
        if (isDoubleByte(candidate))
            score = doubleByteScore(sample, candidate);
        else if (candidate == "windows-1252")
            score = singleByteScore(sample);
        else
            score = 0.85;
        if (candidate == preferred)
            score += 0.15;
        if (score > bestScore) {
            best = candidate;
            bestScore = score;
        }
    }
    return best;
}

bool TextCodec::isBinary(const QByteArray &data)
{
    const qsizetype count = qMin<qsizetype>(data.size(), 64 * 1024);
    int control = 0;
    for (qsizetype i = 0; i < count; ++i) {
        const quint8 byte = data.at(i);
        if (byte == 0)
            return true;
        if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f'
                && byte != '\v' && byte != 0x1B && byte != 0x08)
            ++control;
    }
    return count > 0 && control > count / 10;
}

bool TextCodec::isSupported(const QByteArray &encoding)
{
    return QStringDecoder(encoding.constData()).isValid();
}

QList<QString> TextCodec::splitLines(QStringView text, LineEnding *dominant, bool *mixed)
{
    QList<QString> lines;
    lines.reserve(text.size() / 40 + 1);
    qsizetype lf = 0;
    qsizetype crlf = 0;
    qsizetype cr = 0;

    const QChar *data = text.data();
    const qsizetype size = text.size();
    qsizetype start = 0;
    for (qsizetype i = 0; i < size; ++i) {
        const char16_t c = data[i].unicode();
        if (c != u'\n' && c != u'\r')
            continue;
        lines.append(QString(data + start, i - start));
        if (c == u'\r' && i + 1 < size && data[i + 1] == u'\n') {
            ++crlf;
            ++i;
        } else if (c == u'\r') {
            ++cr;
        } else {
            ++lf;
        }
        start = i + 1;
    }
    lines.append(QString(data + start, size - start));

    if (dominant) {
        *dominant = LineEnding::Unix;
        if (crlf > lf && crlf >= cr)
            *dominant = LineEnding::Windows;
        else if (cr > lf && cr > crlf)
            *dominant = LineEnding::ClassicMac;
    }
    if (mixed)
        *mixed = (lf > 0) + (crlf > 0) + (cr > 0) > 1;
    return lines;
}

DecodedText TextCodec::decode(const QByteArray &data, const QByteArray &encoding)
{
    DecodedText result;
    bool bom = false;
    result.encoding = encoding.isEmpty() ? detect(data, &bom) : encoding;
    if (!encoding.isEmpty()) {
        bool detectedBom = false;
        const QByteArray detected = detect(data, &detectedBom);
        bom = detectedBom && detected == encoding;
    }
    result.bom = bom;
    result.binary = !result.encoding.startsWith("UTF-16") && !result.encoding.startsWith("UTF-32")
            && isBinary(data);

    QStringDecoder decoder(result.encoding.constData());
    if (!decoder.isValid()) {
        result.encoding = QByteArrayLiteral("UTF-8");
        decoder = QStringDecoder(QStringDecoder::Utf8);
    }
    const QString text = decoder.decode(data);
    result.malformed = decoder.hasError();
    result.lines = splitLines(text, &result.lineEnding, &result.mixedLineEndings);
    return result;
}

QString TextCodec::lineEndingText(LineEnding lineEnding)
{
    switch (lineEnding) {
    case LineEnding::Windows:
        return QStringLiteral("\r\n");
    case LineEnding::ClassicMac:
        return QStringLiteral("\r");
    case LineEnding::Unix:
        break;
    }
    return QStringLiteral("\n");
}

QByteArray TextCodec::encode(const QList<QString> &lines, const QByteArray &encoding, bool bom,
                             LineEnding lineEnding, bool *lossy)
{
    const QString separator = lineEndingText(lineEnding);
    qsizetype size = 0;
    for (const QString &line : lines)
        size += line.size() + separator.size();
    QString text;
    text.reserve(size);
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            text += separator;
        text += lines.at(i);
    }

    const bool unicode = encoding.startsWith("UTF-");
    QStringEncoder encoder(encoding.constData(),
                           unicode && bom ? QStringEncoder::Flag::WriteBom : QStringEncoder::Flag::Default);
    if (!encoder.isValid())
        encoder = QStringEncoder(QStringEncoder::Utf8);
    QByteArray bytes = encoder.encode(text);

    if (lossy) {
        *lossy = encoder.hasError();
        // Not every converter reports unmappable characters; a round trip does.
        if (!*lossy && !unicode) {
            QStringDecoder decoder(encoding.constData());
            *lossy = decoder.decode(bytes) != text;
        }
    }
    return bytes;
}

const QList<EncodingInfo> &TextCodec::encodings()
{
    static const QList<EncodingInfo> list{
        {"UTF-8", QT_TRANSLATE_NOOP("Encoding", "Unicode (UTF-8)")},
        {"UTF-16LE", QT_TRANSLATE_NOOP("Encoding", "Unicode (UTF-16 LE)")},
        {"UTF-16BE", QT_TRANSLATE_NOOP("Encoding", "Unicode (UTF-16 BE)")},
        {"GB18030", QT_TRANSLATE_NOOP("Encoding", "Chinese Simplified (GB18030)")},
        {"Big5", QT_TRANSLATE_NOOP("Encoding", "Chinese Traditional (Big5)")},
        {"Big5-HKSCS", QT_TRANSLATE_NOOP("Encoding", "Chinese Traditional (Big5-HKSCS)")},
        {"Shift_JIS", QT_TRANSLATE_NOOP("Encoding", "Japanese (Shift_JIS)")},
        {"EUC-JP", QT_TRANSLATE_NOOP("Encoding", "Japanese (EUC-JP)")},
        {"EUC-KR", QT_TRANSLATE_NOOP("Encoding", "Korean (EUC-KR)")},
        {"windows-1252", QT_TRANSLATE_NOOP("Encoding", "Western (Windows-1252)")},
        {"ISO-8859-1", QT_TRANSLATE_NOOP("Encoding", "Western (ISO-8859-1)")},
        {"ISO-8859-15", QT_TRANSLATE_NOOP("Encoding", "Western (ISO-8859-15)")},
        {"windows-1250", QT_TRANSLATE_NOOP("Encoding", "Central European (Windows-1250)")},
        {"ISO-8859-2", QT_TRANSLATE_NOOP("Encoding", "Central European (ISO-8859-2)")},
        {"windows-1251", QT_TRANSLATE_NOOP("Encoding", "Cyrillic (Windows-1251)")},
        {"KOI8-R", QT_TRANSLATE_NOOP("Encoding", "Cyrillic (KOI8-R)")},
        {"KOI8-U", QT_TRANSLATE_NOOP("Encoding", "Cyrillic (KOI8-U)")},
        {"windows-1253", QT_TRANSLATE_NOOP("Encoding", "Greek (Windows-1253)")},
        {"windows-1254", QT_TRANSLATE_NOOP("Encoding", "Turkish (Windows-1254)")},
        {"windows-1255", QT_TRANSLATE_NOOP("Encoding", "Hebrew (Windows-1255)")},
        {"windows-1256", QT_TRANSLATE_NOOP("Encoding", "Arabic (Windows-1256)")},
        {"windows-1257", QT_TRANSLATE_NOOP("Encoding", "Baltic (Windows-1257)")},
        {"windows-1258", QT_TRANSLATE_NOOP("Encoding", "Vietnamese (Windows-1258)")},
        {"windows-874", QT_TRANSLATE_NOOP("Encoding", "Thai (Windows-874)")},
    };
    return list;
}
