// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTCODEC_H
#define TEXTCODEC_H

#include <QByteArray>
#include <QList>
#include <QString>

enum class LineEnding {
    Unix,
    Windows,
    ClassicMac,
};

struct DecodedText
{
    QList<QString> lines;
    // A name QStringDecoder accepts, e.g. "UTF-8", "UTF-16LE", "GB18030".
    QByteArray encoding;
    bool bom = false;
    LineEnding lineEnding = LineEnding::Unix;
    bool mixedLineEndings = false;
    // NUL bytes or mostly control characters.
    bool binary = false;
    // Some bytes did not decode and became U+FFFD.
    bool malformed = false;
};

struct EncodingInfo
{
    QByteArray name;
    // Untranslated; translated with the "Encoding" context.
    const char *label;
};

namespace TextCodec
{
// Guesses the encoding from a BOM, the UTF-16 NUL pattern, UTF-8 validity and
// the byte statistics of the legacy encodings, preferring the locale's own.
QByteArray detect(const QByteArray &data, bool *bom = nullptr);

// Decodes with the given encoding, or the detected one when it is empty.
DecodedText decode(const QByteArray &data, const QByteArray &encoding = QByteArray());

// Joins the lines with the line ending and encodes them; lossy is set when a
// character has no representation in the encoding.
QByteArray encode(const QList<QString> &lines, const QByteArray &encoding, bool bom,
                  LineEnding lineEnding, bool *lossy = nullptr);

bool isBinary(const QByteArray &data);
bool isSupported(const QByteArray &encoding);

// Splits at \n, \r\n and lone \r, and reports which of them the text uses most.
QList<QString> splitLines(QStringView text, LineEnding *dominant = nullptr, bool *mixed = nullptr);

QString lineEndingText(LineEnding lineEnding);

// The legacy encoding text in the system locale's language most likely uses.
QByteArray localeEncoding();

// What the encoding menu offers, in display order.
const QList<EncodingInfo> &encodings();
}

#endif
