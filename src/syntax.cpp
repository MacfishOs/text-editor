// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "syntax.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>

struct Language
{
    enum Kind {
        Code,
        Markdown,
        Diff,
        Ini,
        Yaml,
        Markup,
        Json,
        Log,
    };

    QString id;
    const char *name;
    Kind kind = Code;
    QStringList patterns;
    QStringList interpreters;
    QStringList mimeTypes;
    QStringList lineComments;
    QList<QPair<QString, QString>> blockComments;
    QString quotes;
    QStringList multilineQuotes;
    QSet<QString> keywords;
    QSet<QString> types;
    QSet<QString> constants;
    bool caseInsensitive = false;
    bool hashPreprocessor = false;
    bool dollarVariables = false;
    bool decorators = false;
    bool hexColors = false;
    bool functions = true;
};

static QSet<QString> words(const char *list)
{
    QSet<QString> result;
    for (const QString &word : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        result.insert(word);
    return result;
}

static QList<Language> makeLanguages()
{
    QList<Language> list;
    const QPair<QString, QString> cComment{QStringLiteral("/*"), QStringLiteral("*/")};
    const QStringList slashes{QStringLiteral("//")};
    const QStringList hash{QStringLiteral("#")};
    const QSet<QString> cTypes = words("void bool char short int long float double signed unsigned auto size_t ssize_t "
                                       "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t wchar_t "
                                       "char8_t char16_t char32_t intptr_t uintptr_t ptrdiff_t FILE");

    {
        Language l;
        l.id = QStringLiteral("c");
        l.name = QT_TRANSLATE_NOOP("Language", "C");
        l.patterns = {QStringLiteral("*.c"), QStringLiteral("*.h")};
        l.mimeTypes = {QStringLiteral("text/x-csrc"), QStringLiteral("text/x-chdr")};
        l.lineComments = slashes;
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"'");
        l.keywords = words("break case const continue default do else enum extern for goto if inline register restrict "
                           "return sizeof static struct switch typedef union volatile while _Bool _Atomic _Generic "
                           "_Noreturn _Static_assert _Thread_local alignas alignof static_assert thread_local typeof");
        l.types = cTypes;
        l.constants = words("NULL true false EOF stdin stdout stderr nullptr");
        l.hashPreprocessor = true;
        list.append(l);
    }
    {
        Language l = list.last();
        l.id = QStringLiteral("cpp");
        l.name = QT_TRANSLATE_NOOP("Language", "C++");
        l.patterns = {QStringLiteral("*.cpp"), QStringLiteral("*.cc"), QStringLiteral("*.cxx"), QStringLiteral("*.c++"),
                      QStringLiteral("*.hpp"), QStringLiteral("*.hh"), QStringLiteral("*.hxx"), QStringLiteral("*.h++"),
                      QStringLiteral("*.ipp"), QStringLiteral("*.tpp"), QStringLiteral("*.inl")};
        l.mimeTypes = {QStringLiteral("text/x-c++src"), QStringLiteral("text/x-c++hdr")};
        l.keywords += words("alignas and asm catch class co_await co_return co_yield concept consteval constexpr constinit "
                            "const_cast decltype delete dynamic_cast explicit export final friend mutable namespace new "
                            "noexcept not operator or override private protected public reinterpret_cast requires "
                            "static_cast template this throw try typeid typename using virtual xor emit signals slots "
                            "Q_OBJECT Q_PROPERTY Q_INVOKABLE Q_SIGNALS Q_SLOTS Q_EMIT");
        l.types += words("std string wstring vector map set unordered_map unique_ptr shared_ptr QString QStringList "
                         "QList QByteArray QVariant QObject qint64 quint64 qint32 quint32 qreal");
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("python");
        l.name = QT_TRANSLATE_NOOP("Language", "Python");
        l.patterns = {QStringLiteral("*.py"), QStringLiteral("*.pyw"), QStringLiteral("*.pyi"),
                      QStringLiteral("SConstruct"), QStringLiteral("SConscript")};
        l.interpreters = {QStringLiteral("python"), QStringLiteral("python2"), QStringLiteral("python3")};
        l.mimeTypes = {QStringLiteral("text/x-python"), QStringLiteral("text/x-python3")};
        l.lineComments = hash;
        l.quotes = QStringLiteral("\"'");
        l.multilineQuotes = {QStringLiteral("\"\"\""), QStringLiteral("'''")};
        l.keywords = words("and as assert async await break class continue def del elif else except finally for from "
                           "global if import in is lambda nonlocal not or pass raise return try while with yield match case");
        l.types = words("int float str bytes bool list dict set tuple object type complex frozenset bytearray");
        l.constants = words("None True False self cls __name__ __main__ __init__ print len range open super isinstance");
        l.decorators = true;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("javascript");
        l.name = QT_TRANSLATE_NOOP("Language", "JavaScript");
        l.patterns = {QStringLiteral("*.js"), QStringLiteral("*.mjs"), QStringLiteral("*.cjs"), QStringLiteral("*.jsx")};
        l.interpreters = {QStringLiteral("node"), QStringLiteral("nodejs"), QStringLiteral("gjs")};
        l.mimeTypes = {QStringLiteral("application/javascript"), QStringLiteral("text/javascript")};
        l.lineComments = slashes;
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"'");
        l.multilineQuotes = {QStringLiteral("`")};
        l.keywords = words("async await break case catch class const continue debugger default delete do else export "
                           "extends finally for from function get if import in instanceof let new of return set static "
                           "super switch this throw try typeof var void while with yield");
        l.constants = words("true false null undefined NaN Infinity console window document globalThis");
        list.append(l);
    }
    {
        Language l = list.last();
        l.id = QStringLiteral("typescript");
        l.name = QT_TRANSLATE_NOOP("Language", "TypeScript");
        l.patterns = {QStringLiteral("*.ts"), QStringLiteral("*.tsx"), QStringLiteral("*.mts"), QStringLiteral("*.cts")};
        l.interpreters = {QStringLiteral("deno"), QStringLiteral("ts-node")};
        l.mimeTypes = {QStringLiteral("application/x-typescript"), QStringLiteral("text/x-typescript")};
        l.keywords += words("abstract as declare enum implements interface keyof namespace private protected public "
                            "readonly type satisfies infer is");
        l.types = words("any boolean number string symbol bigint never unknown object void");
        list.append(l);
    }
    {
        Language l = list.at(3);
        l.id = QStringLiteral("qml");
        l.name = QT_TRANSLATE_NOOP("Language", "QML");
        l.patterns = {QStringLiteral("*.qml"), QStringLiteral("qmldir")};
        l.interpreters = {};
        l.mimeTypes = {QStringLiteral("text/x-qml")};
        l.keywords += words("property readonly required signal alias component pragma on default");
        l.types = words("int real double bool string var url color point size rect list date font");
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("sh");
        l.name = QT_TRANSLATE_NOOP("Language", "Shell");
        l.patterns = {QStringLiteral("*.sh"), QStringLiteral("*.bash"), QStringLiteral("*.zsh"), QStringLiteral("*.ksh"),
                      QStringLiteral(".bashrc"), QStringLiteral(".bash_profile"), QStringLiteral(".bash_aliases"),
                      QStringLiteral(".profile"), QStringLiteral(".zshrc"), QStringLiteral(".zprofile"),
                      QStringLiteral("PKGBUILD"), QStringLiteral("*.ebuild"), QStringLiteral("APKBUILD")};
        l.interpreters = {QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("dash"),
                          QStringLiteral("ksh"), QStringLiteral("ash")};
        l.mimeTypes = {QStringLiteral("application/x-shellscript"), QStringLiteral("text/x-sh")};
        l.lineComments = hash;
        l.quotes = QStringLiteral("\"'`");
        l.keywords = words("if then else elif fi case esac for select while until do done in function time coproc "
                           "return exit break continue local export readonly declare typeset unset shift source alias "
                           "set trap eval exec");
        l.constants = words("true false echo printf read cd test");
        l.dollarVariables = true;
        l.functions = false;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("rust");
        l.name = QT_TRANSLATE_NOOP("Language", "Rust");
        l.patterns = {QStringLiteral("*.rs")};
        l.mimeTypes = {QStringLiteral("text/rust"), QStringLiteral("text/x-rust")};
        l.lineComments = slashes;
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"");
        l.keywords = words("as async await break const continue crate dyn else enum extern fn for if impl in let loop "
                           "match mod move mut pub ref return static struct super trait type unsafe use where while "
                           "macro_rules union");
        l.types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option "
                        "Result Box Self HashMap");
        l.constants = words("true false None Some Ok Err self");
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("go");
        l.name = QT_TRANSLATE_NOOP("Language", "Go");
        l.patterns = {QStringLiteral("*.go")};
        l.mimeTypes = {QStringLiteral("text/x-go")};
        l.lineComments = slashes;
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"'");
        l.multilineQuotes = {QStringLiteral("`")};
        l.keywords = words("break case chan const continue default defer else fallthrough for func go goto if import "
                           "interface map package range return select struct switch type var");
        l.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string "
                        "uint uint8 uint16 uint32 uint64 uintptr any");
        l.constants = words("true false nil iota append cap close copy delete len make new panic print println recover");
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("java");
        l.name = QT_TRANSLATE_NOOP("Language", "Java");
        l.patterns = {QStringLiteral("*.java"), QStringLiteral("*.kt"), QStringLiteral("*.kts")};
        l.mimeTypes = {QStringLiteral("text/x-java")};
        l.lineComments = slashes;
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"'");
        l.multilineQuotes = {QStringLiteral("\"\"\"")};
        l.keywords = words("abstract assert break case catch class continue default do else enum extends final finally "
                           "for if implements import instanceof interface native new package private protected public "
                           "return static strictfp super switch synchronized this throw throws transient try volatile "
                           "while var record sealed permits yield fun val when object companion");
        l.types = words("boolean byte char double float int long short void String Object Integer Long Boolean List Map");
        l.constants = words("true false null");
        l.decorators = true;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("css");
        l.name = QT_TRANSLATE_NOOP("Language", "CSS");
        l.patterns = {QStringLiteral("*.css"), QStringLiteral("*.scss"), QStringLiteral("*.less"), QStringLiteral("*.qss")};
        l.mimeTypes = {QStringLiteral("text/css"), QStringLiteral("text/x-scss")};
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"'");
        l.keywords = words("@media @import @font-face @keyframes @supports @charset @layer !important");
        l.constants = words("none auto inherit initial unset transparent block inline flex grid absolute relative fixed "
                            "solid bold normal center left right top bottom");
        l.hexColors = true;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("sql");
        l.name = QT_TRANSLATE_NOOP("Language", "SQL");
        l.patterns = {QStringLiteral("*.sql")};
        l.mimeTypes = {QStringLiteral("application/sql"), QStringLiteral("text/x-sql")};
        l.lineComments = {QStringLiteral("--")};
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("'\"");
        l.caseInsensitive = true;
        l.keywords = words("select from where insert into values update set delete create table drop alter add column "
                           "index view primary key foreign references join inner left right outer full cross on as "
                           "and or not null is in like between order by group having limit offset union all distinct "
                           "case when then else end begin commit rollback transaction if exists default unique check "
                           "constraint returning with recursive asc desc trigger replace pragma");
        l.types = words("int integer bigint smallint real float double decimal numeric char varchar text blob boolean "
                        "date time timestamp serial");
        l.constants = words("true false current_timestamp current_date count sum avg min max coalesce");
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("cmake");
        l.name = QT_TRANSLATE_NOOP("Language", "CMake");
        l.patterns = {QStringLiteral("CMakeLists.txt"), QStringLiteral("*.cmake"), QStringLiteral("*.cmake.in")};
        l.mimeTypes = {QStringLiteral("text/x-cmake")};
        l.lineComments = hash;
        l.quotes = QStringLiteral("\"");
        l.keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction macro endmacro "
                           "return break continue");
        l.constants = words("ON OFF TRUE FALSE YES NO PRIVATE PUBLIC INTERFACE REQUIRED COMPONENTS NOT AND OR STREQUAL "
                            "EQUAL DEFINED");
        l.dollarVariables = true;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("makefile");
        l.name = QT_TRANSLATE_NOOP("Language", "Makefile");
        l.patterns = {QStringLiteral("Makefile"), QStringLiteral("makefile"), QStringLiteral("GNUmakefile"),
                      QStringLiteral("*.mk"), QStringLiteral("*.mak"), QStringLiteral("Makefile.*")};
        l.mimeTypes = {QStringLiteral("text/x-makefile")};
        l.lineComments = hash;
        l.quotes = QStringLiteral("\"'");
        l.keywords = words("ifeq ifneq ifdef ifndef else endif include define endef export unexport override .PHONY");
        l.dollarVariables = true;
        l.functions = false;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("markdown");
        l.name = QT_TRANSLATE_NOOP("Language", "Markdown");
        l.kind = Language::Markdown;
        l.patterns = {QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.mkd")};
        l.mimeTypes = {QStringLiteral("text/markdown"), QStringLiteral("text/x-markdown")};
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("html");
        l.name = QT_TRANSLATE_NOOP("Language", "HTML");
        l.kind = Language::Markup;
        l.patterns = {QStringLiteral("*.html"), QStringLiteral("*.htm"), QStringLiteral("*.xhtml")};
        l.mimeTypes = {QStringLiteral("text/html"), QStringLiteral("application/xhtml+xml")};
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("xml");
        l.name = QT_TRANSLATE_NOOP("Language", "XML");
        l.kind = Language::Markup;
        l.patterns = {QStringLiteral("*.xml"), QStringLiteral("*.svg"), QStringLiteral("*.ui"), QStringLiteral("*.qrc"),
                      QStringLiteral("*.xsl"), QStringLiteral("*.xslt"), QStringLiteral("*.plist"),
                      QStringLiteral("*.xbel"), QStringLiteral("*.kml"), QStringLiteral("*.gpx"), QStringLiteral("*.rss"),
                      QStringLiteral("*.atom"), QStringLiteral("*.policy")};
        l.mimeTypes = {QStringLiteral("application/xml"), QStringLiteral("text/xml"), QStringLiteral("image/svg+xml")};
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("json");
        l.name = QT_TRANSLATE_NOOP("Language", "JSON");
        l.kind = Language::Json;
        l.patterns = {QStringLiteral("*.json"), QStringLiteral("*.jsonc"), QStringLiteral("*.json5"),
                      QStringLiteral("*.geojson"), QStringLiteral("*.webmanifest"), QStringLiteral(".babelrc"),
                      QStringLiteral(".eslintrc")};
        l.mimeTypes = {QStringLiteral("application/json")};
        l.lineComments = slashes;
        l.blockComments = {cComment};
        l.quotes = QStringLiteral("\"");
        l.constants = words("true false null");
        l.functions = false;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("yaml");
        l.name = QT_TRANSLATE_NOOP("Language", "YAML");
        l.kind = Language::Yaml;
        l.patterns = {QStringLiteral("*.yaml"), QStringLiteral("*.yml"), QStringLiteral(".clang-format"),
                      QStringLiteral(".clang-tidy")};
        l.mimeTypes = {QStringLiteral("application/x-yaml"), QStringLiteral("application/yaml")};
        l.lineComments = hash;
        l.quotes = QStringLiteral("\"'");
        l.constants = words("true false null yes no on off True False Null ~");
        l.functions = false;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("toml");
        l.name = QT_TRANSLATE_NOOP("Language", "TOML");
        l.kind = Language::Ini;
        l.patterns = {QStringLiteral("*.toml"), QStringLiteral("Cargo.lock"), QStringLiteral("Pipfile")};
        l.mimeTypes = {QStringLiteral("application/toml")};
        l.lineComments = hash;
        l.quotes = QStringLiteral("\"'");
        l.multilineQuotes = {QStringLiteral("\"\"\""), QStringLiteral("'''")};
        l.constants = words("true false inf nan");
        l.functions = false;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("ini");
        l.name = QT_TRANSLATE_NOOP("Language", "INI");
        l.kind = Language::Ini;
        l.patterns = {QStringLiteral("*.ini"), QStringLiteral("*.conf"), QStringLiteral("*.cfg"), QStringLiteral("*.desktop"),
                      QStringLiteral("*.service"), QStringLiteral("*.socket"), QStringLiteral("*.timer"),
                      QStringLiteral("*.target"), QStringLiteral("*.mount"), QStringLiteral("*.path"),
                      QStringLiteral("*.network"), QStringLiteral("*.netdev"), QStringLiteral("*.link"),
                      QStringLiteral("*.properties"), QStringLiteral("*.editorconfig"), QStringLiteral(".editorconfig"),
                      QStringLiteral(".gitconfig"), QStringLiteral(".gitmodules"), QStringLiteral("*.theme"),
                      QStringLiteral("*.directory"), QStringLiteral("*.rc"), QStringLiteral("mimeapps.list"),
                      QStringLiteral("*.nmconnection"), QStringLiteral("*.inf")};
        l.mimeTypes = {QStringLiteral("text/x-ini"), QStringLiteral("application/x-desktop"),
                       QStringLiteral("text/x-systemd-unit")};
        l.lineComments = {QStringLiteral("#"), QStringLiteral(";")};
        l.constants = words("true false yes no on off");
        l.functions = false;
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("diff");
        l.name = QT_TRANSLATE_NOOP("Language", "Diff");
        l.kind = Language::Diff;
        l.patterns = {QStringLiteral("*.diff"), QStringLiteral("*.patch"), QStringLiteral("*.rej"),
                      QStringLiteral("*.debdiff")};
        l.mimeTypes = {QStringLiteral("text/x-patch"), QStringLiteral("text/x-diff")};
        list.append(l);
    }
    {
        Language l;
        l.id = QStringLiteral("log");
        l.name = QT_TRANSLATE_NOOP("Language", "Log");
        l.kind = Language::Log;
        l.patterns = {QStringLiteral("*.log"), QStringLiteral("*.log.[0-9]"), QStringLiteral("Xorg.*.log")};
        l.mimeTypes = {QStringLiteral("text/x-log")};
        list.append(l);
    }
    return list;
}

static const QList<Language> &allLanguages()
{
    static const QList<Language> list = makeLanguages();
    return list;
}

const QList<const Language *> &Syntax::languages()
{
    static const QList<const Language *> list = [] {
        QList<const Language *> result;
        for (const Language &language : allLanguages())
            result.append(&language);
        std::sort(result.begin(), result.end(), [](const Language *a, const Language *b) {
            return QString::localeAwareCompare(QString::fromLatin1(a->name), QString::fromLatin1(b->name)) < 0;
        });
        return result;
    }();
    return list;
}

const Language *Syntax::language(const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    for (const Language &language : allLanguages()) {
        if (language.id == id)
            return &language;
    }
    return nullptr;
}

QString Syntax::id(const Language *language)
{
    return language ? language->id : QString();
}

QString Syntax::name(const QString &id)
{
    const Language *l = language(id);
    return l ? QCoreApplication::translate("Language", l->name)
             : QCoreApplication::translate("Language", "Plain Text");
}

QString Syntax::detect(const QString &fileName, const QString &firstLine)
{
    const QString base = QFileInfo(fileName).fileName();
    if (!base.isEmpty()) {
        // Exact names first: "CMakeLists.txt" is CMake, not a text file.
        for (const Language &language : allLanguages()) {
            for (const QString &pattern : language.patterns) {
                if (!pattern.contains(QLatin1Char('*')) && pattern == base)
                    return language.id;
            }
        }
        for (const Language &language : allLanguages()) {
            for (const QString &pattern : language.patterns) {
                if (pattern.contains(QLatin1Char('*'))
                        && QRegularExpression::fromWildcard(pattern, Qt::CaseInsensitive).match(base).hasMatch())
                    return language.id;
            }
        }
    }

    if (firstLine.startsWith(QLatin1String("#!"))) {
        // "#!/usr/bin/env -S python3 -u" names python3.
        const QStringList parts = firstLine.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.startsWith(QLatin1Char('-')) || part.endsWith(QLatin1String("/env")))
                continue;
            QString interpreter = QFileInfo(part).fileName();
            interpreter.remove(QRegularExpression(QStringLiteral("[0-9.]+$")));
            for (const Language &language : allLanguages()) {
                for (const QString &name : language.interpreters) {
                    QString stripped = name;
                    stripped.remove(QRegularExpression(QStringLiteral("[0-9.]+$")));
                    if (stripped == interpreter)
                        return language.id;
                }
            }
            break;
        }
    }
    if (firstLine.startsWith(QLatin1String("<?xml")))
        return QStringLiteral("xml");
    if (firstLine.startsWith(QLatin1String("<!DOCTYPE html"), Qt::CaseInsensitive)
            || firstLine.startsWith(QLatin1String("<html"), Qt::CaseInsensitive))
        return QStringLiteral("html");
    if (firstLine.startsWith(QLatin1String("diff --git ")) || firstLine.startsWith(QLatin1String("--- ")))
        return QStringLiteral("diff");

    if (!base.isEmpty()) {
        const QMimeType mime = QMimeDatabase().mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
        for (const Language &language : allLanguages()) {
            for (const QString &type : language.mimeTypes) {
                if (mime.inherits(type))
                    return language.id;
            }
        }
    }
    return QString();
}

static bool isWordStart(QChar c)
{
    return c.isLetter() || c == QLatin1Char('_');
}

static bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

static void add(QList<SyntaxSpan> *spans, int start, int length, SyntaxStyle style)
{
    if (length <= 0 || style == SyntaxStyle::Normal)
        return;
    if (!spans->isEmpty()) {
        SyntaxSpan &last = spans->last();
        if (last.style == style && last.start + last.length == start) {
            last.length += length;
            return;
        }
    }
    spans->append({start, length, style});
}

// Scans a quoted string from `i` (just past the opening quote); returns the
// index past the closing one, or -1 when the line ends first.
static int scanString(const QString &text, int i, const QString &close, QList<SyntaxSpan> *spans, int start,
                      bool escapes = true)
{
    const int size = text.size();
    while (i < size) {
        if (escapes && text.at(i) == QLatin1Char('\\') && i + 1 < size) {
            add(spans, start, i - start, SyntaxStyle::String);
            add(spans, i, 2, SyntaxStyle::Escape);
            i += 2;
            start = i;
            continue;
        }
        if (QStringView(text).mid(i).startsWith(close)) {
            add(spans, start, i + int(close.size()) - start, SyntaxStyle::String);
            return i + int(close.size());
        }
        ++i;
    }
    add(spans, start, size - start, SyntaxStyle::String);
    return -1;
}

static int scanNumber(const QString &text, int i)
{
    const int size = text.size();
    const int begin = i;
    while (i < size) {
        const QChar c = text.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) {
            ++i;
        } else if ((c == QLatin1Char('+') || c == QLatin1Char('-')) && i > begin
                   && (text.at(i - 1) == QLatin1Char('e') || text.at(i - 1) == QLatin1Char('E'))
                   && !text.mid(begin, 2).startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
            ++i;
        } else {
            break;
        }
    }
    return i;
}

static constexpr int multilineBase = 16;

static Syntax::State highlightCode(const Language &l, const QString &text, Syntax::State state, QList<SyntaxSpan> *spans)
{
    const int size = text.size();
    int i = 0;

    // Carried over: inside a block comment or a multi-line string.
    if (state > 0 && state < multilineBase) {
        const QString &close = l.blockComments.at(state - 1).second;
        const qsizetype end = text.indexOf(close);
        if (end < 0) {
            add(spans, 0, size, SyntaxStyle::Comment);
            return state;
        }
        i = int(end + close.size());
        add(spans, 0, i, SyntaxStyle::Comment);
        state = 0;
    } else if (state >= multilineBase) {
        const QString &close = l.multilineQuotes.at(state - multilineBase);
        const int end = scanString(text, 0, close, spans, 0);
        if (end < 0)
            return state;
        i = end;
        state = 0;
    }

    int firstNonSpace = 0;
    while (firstNonSpace < size && text.at(firstNonSpace).isSpace())
        ++firstNonSpace;

    if (l.hashPreprocessor && firstNonSpace < size && text.at(firstNonSpace) == QLatin1Char('#') && i == 0) {
        int end = firstNonSpace + 1;
        while (end < size && text.at(end).isSpace())
            ++end;
        while (end < size && isWordChar(text.at(end)))
            ++end;
        add(spans, firstNonSpace, end - firstNonSpace, SyntaxStyle::Preprocessor);
        // #include <file> reads as a string.
        const qsizetype open = text.indexOf(QLatin1Char('<'), end);
        if (QStringView(text).mid(firstNonSpace).contains(QLatin1String("include")) && open >= 0) {
            const qsizetype close = text.indexOf(QLatin1Char('>'), open);
            if (close > open) {
                add(spans, int(open), int(close - open + 1), SyntaxStyle::String);
                end = int(close + 1);
            }
        }
        i = end;
    }

    // Makefile targets and CMake commands start at the beginning of a line.
    if (l.id == QLatin1String("makefile") && i == 0 && firstNonSpace == 0) {
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        if (colon > 0 && !text.left(colon).contains(QLatin1Char('=')) && !text.startsWith(QLatin1Char('\t'))
                && (colon + 1 >= size || text.at(colon + 1) != QLatin1Char('='))) {
            add(spans, 0, int(colon), SyntaxStyle::Function);
            i = int(colon);
        }
    }

    while (i < size) {
        const QChar c = text.at(i);
        const QStringView rest = QStringView(text).mid(i);

        bool matched = false;
        for (const QString &marker : l.lineComments) {
            if (rest.startsWith(marker)) {
                // "#" inside a word, e.g. "a#b" in shell, is no comment.
                if (marker == QLatin1String("#") && l.dollarVariables && i > 0 && !text.at(i - 1).isSpace())
                    continue;
                add(spans, i, size - i, SyntaxStyle::Comment);
                return 0;
            }
        }
        for (int b = 0; b < l.blockComments.size(); ++b) {
            if (rest.startsWith(l.blockComments.at(b).first)) {
                const qsizetype end = text.indexOf(l.blockComments.at(b).second, i + l.blockComments.at(b).first.size());
                if (end < 0) {
                    add(spans, i, size - i, SyntaxStyle::Comment);
                    return b + 1;
                }
                const int stop = int(end + l.blockComments.at(b).second.size());
                add(spans, i, stop - i, SyntaxStyle::Comment);
                i = stop;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;

        for (int q = 0; q < l.multilineQuotes.size(); ++q) {
            const QString &quote = l.multilineQuotes.at(q);
            if (rest.startsWith(quote)) {
                const int end = scanString(text, i + int(quote.size()), quote, spans, i);
                if (end < 0)
                    return multilineBase + q;
                i = end;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;

        if (l.quotes.contains(c)) {
            // A JSON string followed by a colon is a key.
            const int before = spans->size();
            const bool escapes = !(l.id == QLatin1String("sh") && c == QLatin1Char('\''));
            int end = scanString(text, i + 1, QString(c), spans, i, escapes);
            if (end < 0)
                end = size;
            if (l.kind == Language::Json || l.kind == Language::Yaml) {
                int after = end;
                while (after < size && text.at(after).isSpace())
                    ++after;
                if (after < size && text.at(after) == QLatin1Char(':')) {
                    spans->resize(before);
                    add(spans, i, end - i, SyntaxStyle::Key);
                }
            }
            i = end;
            continue;
        }

        if (l.dollarVariables && c == QLatin1Char('$') && i + 1 < size) {
            int end = i + 1;
            const QChar next = text.at(end);
            if (next == QLatin1Char('{') || next == QLatin1Char('(')) {
                const QChar close = next == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(')');
                const qsizetype stop = text.indexOf(close, end);
                end = stop < 0 ? size : int(stop + 1);
            } else if (isWordChar(next)) {
                while (end < size && isWordChar(text.at(end)))
                    ++end;
            } else {
                ++end;
            }
            add(spans, i, end - i, SyntaxStyle::Variable);
            i = end;
            continue;
        }

        if (l.decorators && c == QLatin1Char('@') && i + 1 < size && isWordStart(text.at(i + 1))) {
            int end = i + 1;
            while (end < size && (isWordChar(text.at(end)) || text.at(end) == QLatin1Char('.')))
                ++end;
            add(spans, i, end - i, SyntaxStyle::Preprocessor);
            i = end;
            continue;
        }

        if (l.hexColors && c == QLatin1Char('#') && i + 1 < size && text.at(i + 1).isLetterOrNumber()
                && (i == 0 || !isWordChar(text.at(i - 1)))) {
            int end = i + 1;
            while (end < size && (isWordChar(text.at(end)) || text.at(end) == QLatin1Char('-')))
                ++end;
            const bool color = end - i == 4 || end - i == 5 || end - i == 7 || end - i == 9;
            add(spans, i, end - i, color ? SyntaxStyle::Number : SyntaxStyle::Function);
            i = end;
            continue;
        }

        if (c.isDigit() && (i == 0 || !isWordChar(text.at(i - 1)))) {
            const int end = scanNumber(text, i);
            add(spans, i, end - i, SyntaxStyle::Number);
            i = end;
            continue;
        }

        if (isWordStart(c) || (c == QLatin1Char('@') && l.id == QLatin1String("css"))
                || (c == QLatin1Char('.') && l.id == QLatin1String("makefile"))
                || (c == QLatin1Char('!') && l.id == QLatin1String("css"))) {
            int end = i + 1;
            while (end < size && (isWordChar(text.at(end)) || (l.id == QLatin1String("css") && text.at(end) == QLatin1Char('-'))))
                ++end;
            const QString word = text.mid(i, end - i);
            const QString key = l.caseInsensitive ? word.toLower() : word;
            SyntaxStyle style = SyntaxStyle::Normal;
            if (l.keywords.contains(key))
                style = SyntaxStyle::Keyword;
            else if (l.types.contains(key))
                style = SyntaxStyle::Type;
            else if (l.constants.contains(key))
                style = SyntaxStyle::Constant;
            else if (l.functions && end < size && text.at(end) == QLatin1Char('('))
                style = SyntaxStyle::Function;
            else if (l.kind == Language::Yaml || l.id == QLatin1String("css")) {
                int after = end;
                while (after < size && (isWordChar(text.at(after)) || text.at(after) == QLatin1Char('-')
                                        || text.at(after) == QLatin1Char('.')))
                    ++after;
                if (after < size && text.at(after) == QLatin1Char(':')
                        && (after + 1 >= size || text.at(after + 1).isSpace())) {
                    style = SyntaxStyle::Key;
                    end = after;
                }
            }
            add(spans, i, end - i, style);
            i = end;
            continue;
        }

        ++i;
    }
    return 0;
}

static Syntax::State highlightMarkdown(const QString &text, Syntax::State state, QList<SyntaxSpan> *spans)
{
    const int size = text.size();
    const QString trimmed = text.trimmed();
    const bool fence = trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"));
    if (state == 1) {
        add(spans, 0, size, SyntaxStyle::Code);
        return fence ? 0 : 1;
    }
    if (fence) {
        add(spans, 0, size, SyntaxStyle::Code);
        return 1;
    }

    if (trimmed.startsWith(QLatin1Char('#'))) {
        add(spans, 0, size, SyntaxStyle::Heading);
        return 0;
    }
    if (trimmed.startsWith(QLatin1Char('>'))) {
        add(spans, 0, size, SyntaxStyle::Comment);
        return 0;
    }
    static const QRegularExpression rule(QStringLiteral("^\\s*([-*_])(\\s*\\1){2,}\\s*$"));
    if (rule.match(text).hasMatch() || (text.startsWith(QLatin1String("===")) && trimmed.count(QLatin1Char('=')) == trimmed.size())) {
        add(spans, 0, size, SyntaxStyle::Heading);
        return 0;
    }

    int i = 0;
    while (i < size && text.at(i).isSpace())
        ++i;
    static const QRegularExpression list(QStringLiteral("^([-*+]|\\d+[.)])\\s"));
    const QRegularExpressionMatch marker = list.match(text, i, QRegularExpression::NormalMatch,
                                                      QRegularExpression::AnchorAtOffsetMatchOption);
    if (marker.hasMatch()) {
        add(spans, i, int(marker.capturedLength(1)), SyntaxStyle::Keyword);
        i += int(marker.capturedLength());
    }

    while (i < size) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\') && i + 1 < size) {
            i += 2;
            continue;
        }
        if (c == QLatin1Char('`')) {
            const qsizetype end = text.indexOf(QLatin1Char('`'), i + 1);
            if (end > i) {
                add(spans, i, int(end - i + 1), SyntaxStyle::Code);
                i = int(end + 1);
                continue;
            }
        }
        if ((c == QLatin1Char('*') || c == QLatin1Char('_')) && i + 1 < size) {
            const bool strong = text.at(i + 1) == c;
            const QString marker = strong ? QString(2, c) : QString(c);
            const int from = i + int(marker.size());
            // "_" inside a word, as in snake_case, is not emphasis.
            const bool wordInside = c == QLatin1Char('_') && i > 0 && isWordChar(text.at(i - 1));
            if (!wordInside && from < size && !text.at(from).isSpace()) {
                const qsizetype end = text.indexOf(marker, from);
                if (end > from) {
                    const int stop = int(end + marker.size());
                    add(spans, i, stop - i, strong ? SyntaxStyle::Strong : SyntaxStyle::Emphasis);
                    i = stop;
                    continue;
                }
            }
        }
        if (c == QLatin1Char('[') || (c == QLatin1Char('!') && i + 1 < size && text.at(i + 1) == QLatin1Char('['))) {
            static const QRegularExpression link(QStringLiteral("!?\\[[^\\]]*\\](\\([^)]*\\)|\\[[^\\]]*\\])"));
            const QRegularExpressionMatch match = link.match(text, i, QRegularExpression::NormalMatch,
                                                             QRegularExpression::AnchorAtOffsetMatchOption);
            if (match.hasMatch()) {
                add(spans, i, int(match.capturedLength()), SyntaxStyle::Link);
                i += int(match.capturedLength());
                continue;
            }
        }
        if (c == QLatin1Char('<')) {
            static const QRegularExpression autolink(QStringLiteral("<(https?://|mailto:)[^>\\s]+>"));
            const QRegularExpressionMatch match = autolink.match(text, i, QRegularExpression::NormalMatch,
                                                                 QRegularExpression::AnchorAtOffsetMatchOption);
            if (match.hasMatch()) {
                add(spans, i, int(match.capturedLength()), SyntaxStyle::Link);
                i += int(match.capturedLength());
                continue;
            }
        }
        ++i;
    }
    return 0;
}

// States: 1 inside a comment, 2 inside a tag, 3 and 4 inside a "..." or
// '...' attribute value of a tag, 5 inside CDATA.
static Syntax::State highlightMarkup(const QString &text, Syntax::State state, QList<SyntaxSpan> *spans)
{
    const int size = text.size();
    int i = 0;
    while (i < size) {
        if (state == 1 || state == 5) {
            const QString close = state == 1 ? QStringLiteral("-->") : QStringLiteral("]]>");
            const qsizetype end = text.indexOf(close, i);
            const int stop = end < 0 ? size : int(end + close.size());
            add(spans, i, stop - i, state == 1 ? SyntaxStyle::Comment : SyntaxStyle::Code);
            if (end < 0)
                return state;
            i = stop;
            state = 0;
            continue;
        }
        if (state == 3 || state == 4) {
            const QChar quote = state == 3 ? QLatin1Char('"') : QLatin1Char('\'');
            const qsizetype end = text.indexOf(quote, i);
            const int stop = end < 0 ? size : int(end + 1);
            add(spans, i, stop - i, SyntaxStyle::String);
            if (end < 0)
                return state;
            i = stop;
            state = 2;
            continue;
        }
        if (state == 2) {
            const QChar c = text.at(i);
            if (c == QLatin1Char('>') || (c == QLatin1Char('/') && i + 1 < size && text.at(i + 1) == QLatin1Char('>'))
                    || (c == QLatin1Char('?') && i + 1 < size && text.at(i + 1) == QLatin1Char('>'))) {
                const int length = c == QLatin1Char('>') ? 1 : 2;
                add(spans, i, length, SyntaxStyle::Tag);
                i += length;
                state = 0;
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                state = c == QLatin1Char('"') ? 3 : 4;
                add(spans, i, 1, SyntaxStyle::String);
                ++i;
            } else if (isWordStart(c) || c == QLatin1Char(':')) {
                int end = i + 1;
                while (end < size && (isWordChar(text.at(end)) || text.at(end) == QLatin1Char('-')
                                      || text.at(end) == QLatin1Char(':') || text.at(end) == QLatin1Char('.')))
                    ++end;
                add(spans, i, end - i, SyntaxStyle::Attribute);
                i = end;
            } else {
                ++i;
            }
            continue;
        }

        const QStringView rest = QStringView(text).mid(i);
        if (rest.startsWith(QLatin1String("<!--"))) {
            state = 1;
            add(spans, i, 4, SyntaxStyle::Comment);
            i += 4;
        } else if (rest.startsWith(QLatin1String("<![CDATA["))) {
            state = 5;
            add(spans, i, 9, SyntaxStyle::Code);
            i += 9;
        } else if (text.at(i) == QLatin1Char('<') && i + 1 < size
                   && (isWordStart(text.at(i + 1)) || text.at(i + 1) == QLatin1Char('/')
                       || text.at(i + 1) == QLatin1Char('!') || text.at(i + 1) == QLatin1Char('?'))) {
            int end = i + 1;
            while (end < size && (isWordChar(text.at(end)) || text.at(end) == QLatin1Char('/')
                                  || text.at(end) == QLatin1Char('!') || text.at(end) == QLatin1Char('?')
                                  || text.at(end) == QLatin1Char('-') || text.at(end) == QLatin1Char(':')
                                  || text.at(end) == QLatin1Char('.')))
                ++end;
            add(spans, i, end - i, SyntaxStyle::Tag);
            i = end;
            state = 2;
        } else if (text.at(i) == QLatin1Char('&')) {
            const qsizetype end = text.indexOf(QLatin1Char(';'), i);
            if (end > i && end - i < 12) {
                add(spans, i, int(end - i + 1), SyntaxStyle::Escape);
                i = int(end + 1);
            } else {
                ++i;
            }
        } else {
            ++i;
        }
    }
    return state;
}

static Syntax::State highlightIni(const Language &l, const QString &text, Syntax::State state, QList<SyntaxSpan> *spans)
{
    const int size = text.size();
    if (state >= multilineBase)
        return highlightCode(l, text, state, spans);

    int i = 0;
    while (i < size && text.at(i).isSpace())
        ++i;
    if (i >= size)
        return 0;
    const QChar first = text.at(i);
    if (first == QLatin1Char('#') || first == QLatin1Char(';')) {
        add(spans, i, size - i, SyntaxStyle::Comment);
        return 0;
    }
    if (first == QLatin1Char('[')) {
        const qsizetype end = text.lastIndexOf(QLatin1Char(']'));
        add(spans, i, end > i ? int(end - i + 1) : size - i, SyntaxStyle::Section);
        return 0;
    }
    qsizetype equals = text.indexOf(QLatin1Char('='), i);
    if (equals < 0 && l.id == QLatin1String("ini"))
        equals = text.indexOf(QLatin1Char(':'), i);
    if (equals > i) {
        add(spans, i, int(equals - i), SyntaxStyle::Key);
        // The value: TOML values are typed, INI values are text.
        if (l.id == QLatin1String("toml")) {
            QList<SyntaxSpan> valueSpans;
            const Syntax::State end = highlightCode(l, text.mid(equals + 1), 0, &valueSpans);
            for (const SyntaxSpan &span : std::as_const(valueSpans))
                add(spans, span.start + int(equals) + 1, span.length, span.style);
            return end;
        }
        const QString value = text.mid(equals + 1).trimmed();
        if (l.constants.contains(value.toLower())) {
            add(spans, int(text.lastIndexOf(value)), int(value.size()), SyntaxStyle::Constant);
        } else if (!value.isEmpty() && (value.at(0).isDigit() || value.at(0) == QLatin1Char('-'))) {
            bool number = false;
            value.toDouble(&number);
            if (number)
                add(spans, int(text.lastIndexOf(value)), int(value.size()), SyntaxStyle::Number);
        }
    }
    return 0;
}

static Syntax::State highlightYaml(const Language &l, const QString &text, QList<SyntaxSpan> *spans)
{
    if (text.startsWith(QLatin1String("---")) || text.startsWith(QLatin1String("..."))) {
        add(spans, 0, int(text.size()), SyntaxStyle::Preprocessor);
        return 0;
    }
    int i = 0;
    while (i < text.size() && text.at(i).isSpace())
        ++i;
    if (i < text.size() && text.at(i) == QLatin1Char('-') && (i + 1 >= text.size() || text.at(i + 1).isSpace()))
        add(spans, i, 1, SyntaxStyle::Keyword);
    return highlightCode(l, text, 0, spans);
}

static void highlightDiff(const QString &text, QList<SyntaxSpan> *spans)
{
    const int size = text.size();
    if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---"))
            || text.startsWith(QLatin1String("diff ")) || text.startsWith(QLatin1String("index "))
            || text.startsWith(QLatin1String("Index: ")) || text.startsWith(QLatin1String("new file mode"))
            || text.startsWith(QLatin1String("deleted file mode")) || text.startsWith(QLatin1String("similarity index"))
            || text.startsWith(QLatin1String("rename ")))
        add(spans, 0, size, SyntaxStyle::DiffHeader);
    else if (text.startsWith(QLatin1String("@@")))
        add(spans, 0, size, SyntaxStyle::Function);
    else if (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('>')))
        add(spans, 0, size, SyntaxStyle::Added);
    else if (text.startsWith(QLatin1Char('-')) || text.startsWith(QLatin1Char('<')))
        add(spans, 0, size, SyntaxStyle::Removed);
}

static void highlightLog(const QString &text, QList<SyntaxSpan> *spans)
{
    static const QRegularExpression pattern(QStringLiteral(
            "(?<time>^\\S*\\d{2}:\\d{2}(:\\d{2})?\\S*|^\\w{3} [ \\d]\\d \\d{2}:\\d{2}:\\d{2}|^\\d{4}-\\d{2}-\\d{2}[T ][\\d:.,]+\\S*)"
            "|(?<error>\\b(ERROR|Error|error|FATAL|Fatal|CRITICAL|Critical|FAIL(ED)?|Failed|failed|EE)\\b)"
            "|(?<warning>\\b(WARN(ING)?|Warning|warning|WW)\\b)"
            "|(?<info>\\b(INFO|Info|NOTICE|DEBUG|Debug|TRACE|II)\\b)"));
    auto it = pattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        SyntaxStyle style = SyntaxStyle::Comment;
        if (match.capturedLength(QStringLiteral("error")) > 0)
            style = SyntaxStyle::Error;
        else if (match.capturedLength(QStringLiteral("warning")) > 0)
            style = SyntaxStyle::Warning;
        else if (match.capturedLength(QStringLiteral("info")) > 0)
            style = SyntaxStyle::Type;
        add(spans, int(match.capturedStart()), int(match.capturedLength()), style);
    }
}

Syntax::State Syntax::highlight(const Language *language, const QString &text, State state, QList<SyntaxSpan> *spans)
{
    if (!language)
        return 0;
    switch (language->kind) {
    case Language::Code:
    case Language::Json:
        return highlightCode(*language, text, state, spans);
    case Language::Markdown:
        return highlightMarkdown(text, state, spans);
    case Language::Markup:
        return highlightMarkup(text, state, spans);
    case Language::Ini:
        return highlightIni(*language, text, state, spans);
    case Language::Yaml:
        return highlightYaml(*language, text, spans);
    case Language::Diff:
        highlightDiff(text, spans);
        return 0;
    case Language::Log:
        highlightLog(text, spans);
        return 0;
    }
    return 0;
}
