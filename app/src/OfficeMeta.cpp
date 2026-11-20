// Office documents: Word, Excel and PowerPoint (DOCX, XLSX, PPTX) and
// OpenDocument (ODT, ODS, ODP). Both are zip archives; only the small XML
// parts holding the document's properties are replaced, every other entry is
// copied byte for byte (see Zip.h).
//
// DOCX/XLSX/PPTX: docProps/core.xml (title, subject, authors, tags, comments,
//   category, status, language), docProps/app.xml (company, manager) and
//   docProps/custom.xml (copyright and any other field of your own; Word shows
//   these under File > Info > Properties > Advanced > Custom).
// ODT/ODS/ODP: meta.xml (the same, with user-defined fields for the rest).
//
// The XML is read without namespace processing, so it is written back with
// the prefixes it had; prefixes are looked up from the xmlns declarations.

#include "MetaIO.h"
#include "Zip.h"

#include <QDateTime>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>

#include <functional>

namespace meta {

namespace {

const QString kCp = "http://schemas.openxmlformats.org/package/2006/metadata/core-properties";
const QString kDc = "http://purl.org/dc/elements/1.1/";
const QString kDcterms = "http://purl.org/dc/terms/";
const QString kApp = "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties";
const QString kCustom = "http://schemas.openxmlformats.org/officeDocument/2006/custom-properties";
const QString kVt = "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes";
const QString kRels = "http://schemas.openxmlformats.org/package/2006/relationships";
const QString kTypes = "http://schemas.openxmlformats.org/package/2006/content-types";
const QString kOffice = "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
const QString kMeta = "urn:oasis:names:tc:opendocument:xmlns:meta:1.0";

struct Part {
    QString relType, contentType, defaultPath, rootName, rootNs, rootXml;
};

const Part kCorePart{"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties",
                     "application/vnd.openxmlformats-package.core-properties+xml", "docProps/core.xml", "cp:coreProperties", kCp,
                     R"(<cp:coreProperties xmlns:cp=")" + kCp + R"(" xmlns:dc=")" + kDc + R"(" xmlns:dcterms=")" + kDcterms
                         + R"(" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"/>)"};
const Part kAppPart{"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties",
                    "application/vnd.openxmlformats-officedocument.extended-properties+xml", "docProps/app.xml", "Properties", kApp,
                    R"(<Properties xmlns=")" + kApp + R"(" xmlns:vt=")" + kVt + R"("/>)"};
const Part kCustomPart{"http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties",
                       "application/vnd.openxmlformats-officedocument.custom-properties+xml", "docProps/custom.xml", "Properties",
                       kCustom, R"(<Properties xmlns=")" + kCustom + R"(" xmlns:vt=")" + kVt + R"("/>)"};

// Fields in core.xml.
struct CoreField {
    QString field, ns, local;
};
const QList<CoreField>& coreFields()
{
    static const QList<CoreField> f{
        {"title", kDc, "title"},       {"subject", kDc, "subject"},     {"artist", kDc, "creator"},
        {"keywords", kCp, "keywords"}, {"comment", kDc, "description"}, {"category", kCp, "category"},
        {"status", kCp, "contentStatus"}, {"language", kDc, "language"},
    };
    return f;
}
const QList<QPair<QString, QString>>& appFields()
{
    static const QList<QPair<QString, QString>> f{{"company", "Company"}, {"manager", "Manager"}};
    return f;
}
// A custom property's name for a field, and back.
QString customName(const QString& field) { return field == "copyright" ? QString("Copyright") : field; }
QString fieldOfCustom(const QString& name) { return name.compare("Copyright", Qt::CaseInsensitive) == 0 ? QString("copyright") : name; }

// --- XML without namespace processing ---------------------------------------------

std::optional<QString> prefixFor(QDomElement e, const QString& uri)
{
    for (; !e.isNull(); e = e.parentNode().toElement()) {
        const QDomNamedNodeMap attrs = e.attributes();
        for (int i = 0; i < attrs.count(); ++i) {
            const QDomAttr a = attrs.item(i).toAttr();
            if (a.value() != uri) continue;
            if (a.name() == "xmlns") return QString();
            if (a.name().startsWith("xmlns:")) return a.name().mid(6);
        }
    }
    return std::nullopt;
}

QString qname(const QString& prefix, const QString& local) { return prefix.isEmpty() ? local : prefix + ":" + local; }

// The prefix for @p uri, declared on @p root when it isn't yet.
QString declare(QDomElement root, const QString& uri, const QString& preferred)
{
    if (const auto p = prefixFor(root, uri)) return *p;
    QString prefix = preferred;
    for (int n = 1; root.hasAttribute("xmlns:" + prefix); ++n) prefix = preferred + QString::number(n);
    root.setAttribute("xmlns:" + prefix, uri);
    return prefix;
}

QString localName(const QString& q) { return q.mid(q.indexOf(':') + 1); }

bool parse(const QByteArray& xml, QDomDocument* doc) { return bool(doc->setContent(xml)); }

QByteArray serialize(const QDomDocument& doc)
{
    QByteArray b = doc.toByteArray(-1);
    if (!b.startsWith("<?xml")) b.prepend(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)" "\r\n");
    return b;
}

// The text of @p parent's child @p q (the first one).
QString childText(const QDomElement& parent, const QString& q) { return parent.firstChildElement(q).text().trimmed(); }

// Replaces @p parent's @p q children with one holding @p value (none when empty).
void setChild(QDomDocument& doc, QDomElement parent, const QString& q, const QString& value)
{
    for (QDomElement c = parent.firstChildElement(q); !c.isNull(); c = parent.firstChildElement(q)) parent.removeChild(c);
    if (value.isEmpty()) return;
    QDomElement e = doc.createElement(q);
    e.appendChild(doc.createTextNode(value));
    parent.appendChild(e);
}

QString readableDate(const QString& iso)
{
    const QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
    return t.isValid() ? t.toLocalTime().toString("yyyy-MM-dd HH:mm") : iso;
}

// --- The archive ---------------------------------------------------------------------

struct Doc {
    zip::Archive archive;
    bool odf = false;
    QString corePath, appPath, customPath;   // OOXML parts (empty: none yet)
};

std::optional<QByteArray> entry(QFile& f, const zip::Archive& a, const QString& name)
{
    const zip::Entry* e = a.find(name.toUtf8());
    if (!e) return std::nullopt;
    return zip::extract(f, *e);
}

// Where the package's relationships say each properties part is.
void findParts(QFile& f, Doc& d)
{
    QDomDocument rels;
    if (const auto b = entry(f, d.archive, "_rels/.rels"); b && parse(*b, &rels)) {
        const QDomElement root = rels.documentElement();
        for (QDomElement r = root.firstChildElement(); !r.isNull(); r = r.nextSiblingElement()) {
            if (localName(r.tagName()) != "Relationship") continue;
            QString target = r.attribute("Target");
            if (target.startsWith('/')) target = target.mid(1);
            const QString type = r.attribute("Type");
            if (type == kCorePart.relType) d.corePath = target;
            else if (type == kAppPart.relType) d.appPath = target;
            else if (type == kCustomPart.relType) d.customPath = target;
        }
    }
    auto exists = [&](QString& path) {
        if (!path.isEmpty() && !d.archive.find(path.toUtf8())) path.clear();
    };
    exists(d.corePath);
    exists(d.appPath);
    exists(d.customPath);
}

std::optional<Doc> open(QFile& f, QString* error)
{
    Doc d;
    const auto a = zip::read(f);
    if (!a) {
        *error = "This document's zip archive can't be read (damaged, encrypted or password-protected?).";
        return std::nullopt;
    }
    d.archive = *a;
    if (d.archive.find("[Content_Types].xml")) {
        findParts(f, d);
    } else if (d.archive.find("meta.xml") || d.archive.find("mimetype")) {
        d.odf = true;
    } else {
        *error = "This isn't a Word, Excel, PowerPoint or OpenDocument file.";
        return std::nullopt;
    }
    return d;
}

QString formatName(const QString& path)
{
    static const QHash<QString, QString> names{
        {"docx", "Word document"},          {"xlsx", "Excel workbook"},        {"pptx", "PowerPoint presentation"},
        {"odt", "OpenDocument text"},       {"ods", "OpenDocument spreadsheet"}, {"odp", "OpenDocument presentation"},
    };
    return names.value(QFileInfo(path).suffix().toLower(), "Office document");
}

void add(Pairs* out, const QString& k, const QString& v)
{
    if (!v.isEmpty()) out->append({k, v});
}

// --- OpenDocument meta.xml --------------------------------------------------------------

struct OdfNames {
    QString office, meta, dc;
};

OdfNames odfNames(const QDomElement& root)
{
    return {prefixFor(root, kOffice).value_or("office"), prefixFor(root, kMeta).value_or("meta"), prefixFor(root, kDc).value_or("dc")};
}

const QList<CoreField>& odfFields()
{
    static const QList<CoreField> f{
        {"title", kDc, "title"},       {"subject", kDc, "subject"},   {"comment", kDc, "description"},
        {"language", kDc, "language"}, {"artist", kMeta, "initial-creator"},
    };
    return f;
}

void readOdf(const QDomDocument& doc, ReadResult& r)
{
    const QDomElement root = doc.documentElement();
    const OdfNames n = odfNames(root);
    const QDomElement meta = root.firstChildElement(qname(n.office, "meta"));
    auto pfx = [&](const QString& ns) { return ns == kDc ? n.dc : n.meta; };
    for (const CoreField& f : odfFields()) add(&r.tags, f.field, childText(meta, qname(pfx(f.ns), f.local)));
    QStringList keywords;
    for (QDomElement k = meta.firstChildElement(qname(n.meta, "keyword")); !k.isNull(); k = k.nextSiblingElement(qname(n.meta, "keyword")))
        keywords.append(k.text().trimmed());
    add(&r.tags, "keywords", keywords.join("; "));
    for (QDomElement u = meta.firstChildElement(qname(n.meta, "user-defined")); !u.isNull();
         u = u.nextSiblingElement(qname(n.meta, "user-defined")))
        add(&r.tags, fieldOfCustom(u.attribute(qname(n.meta, "name"))), u.text().trimmed());
    add(&r.info, "Created", readableDate(childText(meta, qname(n.meta, "creation-date"))));
    add(&r.info, "Modified", readableDate(childText(meta, qname(n.dc, "date"))));
    add(&r.info, "Last saved by", childText(meta, qname(n.dc, "creator")));
    add(&r.info, "Application", childText(meta, qname(n.meta, "generator")));
    add(&r.info, "Pages", meta.firstChildElement(qname(n.meta, "document-statistic")).attribute(qname(n.meta, "page-count")));
}

void writeOdf(QDomDocument& doc, const QHash<QString, QString>& changes, QStringList* notStored, const std::function<bool(const QString&)>& custom)
{
    QDomElement root = doc.documentElement();
    const OdfNames n = odfNames(root);
    const QString officeP = declare(root, kOffice, "office"), metaP = declare(root, kMeta, "meta"), dcP = declare(root, kDc, "dc");
    Q_UNUSED(n);
    QDomElement meta = root.firstChildElement(qname(officeP, "meta"));
    if (meta.isNull()) {
        meta = doc.createElement(qname(officeP, "meta"));
        root.appendChild(meta);
    }
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QString& field = it.key();
        const QString& value = it.value();
        bool done = false;
        for (const CoreField& f : odfFields())
            if (f.field == field) {
                setChild(doc, meta, qname(f.ns == kDc ? dcP : metaP, f.local), value);
                done = true;
            }
        if (done) continue;
        if (field == "keywords") {
            const QString q = qname(metaP, "keyword");
            for (QDomElement k = meta.firstChildElement(q); !k.isNull(); k = meta.firstChildElement(q)) meta.removeChild(k);
            for (const QString& word : value.split(';', Qt::SkipEmptyParts)) {
                if (word.trimmed().isEmpty()) continue;
                QDomElement k = doc.createElement(q);
                k.appendChild(doc.createTextNode(word.trimmed()));
                meta.appendChild(k);
            }
            continue;
        }
        if (!custom(field)) {
            notStored->append(field);
            continue;
        }
        const QString q = qname(metaP, "user-defined"), nameAttr = qname(metaP, "name");
        for (QDomElement u = meta.firstChildElement(q); !u.isNull();) {
            QDomElement next = u.nextSiblingElement(q);
            if (u.attribute(nameAttr).compare(customName(field), Qt::CaseInsensitive) == 0) meta.removeChild(u);
            u = next;
        }
        if (value.isEmpty()) continue;
        QDomElement u = doc.createElement(q);
        u.setAttribute(nameAttr, customName(field));
        u.appendChild(doc.createTextNode(value));
        meta.appendChild(u);
    }
}

// --- OOXML ---------------------------------------------------------------------------

void readCore(const QDomDocument& doc, ReadResult& r)
{
    const QDomElement root = doc.documentElement();
    for (const CoreField& f : coreFields())
        if (const auto p = prefixFor(root, f.ns)) add(&r.tags, f.field, childText(root, qname(*p, f.local)));
    if (const auto p = prefixFor(root, kDcterms)) {
        add(&r.info, "Created", readableDate(childText(root, qname(*p, "created"))));
        add(&r.info, "Modified", readableDate(childText(root, qname(*p, "modified"))));
    }
    if (const auto p = prefixFor(root, kCp)) add(&r.info, "Last saved by", childText(root, qname(*p, "lastModifiedBy")));
}

void readApp(const QDomDocument& doc, ReadResult& r)
{
    const QDomElement root = doc.documentElement();
    const QString p = prefixFor(root, kApp).value_or(QString());
    for (const auto& [field, local] : appFields()) add(&r.tags, field, childText(root, qname(p, local)));
    add(&r.info, "Application", childText(root, qname(p, "Application")));
    add(&r.info, "Pages", childText(root, qname(p, "Pages")));
    add(&r.info, "Words", childText(root, qname(p, "Words")));
    add(&r.info, "Slides", childText(root, qname(p, "Slides")));
}

void readCustom(const QDomDocument& doc, ReadResult& r)
{
    const QDomElement root = doc.documentElement();
    for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
        if (localName(e.tagName()) == "property") add(&r.tags, fieldOfCustom(e.attribute("name")), e.text().trimmed());
}

QDomDocument partOrNew(QFile& f, const zip::Archive& a, const QString& path, const Part& part)
{
    QDomDocument doc;
    if (!path.isEmpty())
        if (const auto b = entry(f, a, path); b && parse(*b, &doc)) return doc;
    parse(QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)") + part.rootXml.toUtf8(), &doc);
    return doc;
}

void setCustom(QDomDocument& doc, const QString& name, const QString& value)
{
    QDomElement root = doc.documentElement();
    const QString p = prefixFor(root, kCustom).value_or(QString());
    const QString vt = declare(root, kVt, "vt");
    int maxPid = 1;
    for (QDomElement e = root.firstChildElement(); !e.isNull();) {
        QDomElement next = e.nextSiblingElement();
        if (localName(e.tagName()) == "property") {
            if (e.attribute("name").compare(name, Qt::CaseInsensitive) == 0) root.removeChild(e);
            else maxPid = std::max(maxPid, e.attribute("pid").toInt());
        }
        e = next;
    }
    if (value.isEmpty()) return;
    QDomElement prop = doc.createElement(qname(p, "property"));
    prop.setAttribute("fmtid", "{D5CDD505-2E9C-101B-9397-08002B2CF9AE}");
    prop.setAttribute("pid", QString::number(maxPid + 1));
    prop.setAttribute("name", name);
    QDomElement text = doc.createElement(qname(vt, "lpwstr"));
    text.appendChild(doc.createTextNode(value));
    prop.appendChild(text);
    root.appendChild(prop);
}

// A new part needs a content type and a relationship.
void registerPart(QFile& f, const zip::Archive& a, const Part& part, QHash<QByteArray, QByteArray>& replace)
{
    QDomDocument types;
    const QByteArray typesName = "[Content_Types].xml";
    if (const auto b = replace.contains(typesName) ? std::optional(replace.value(typesName)) : entry(f, a, typesName); b && parse(*b, &types)) {
        QDomElement root = types.documentElement();
        const QString p = prefixFor(root, kTypes).value_or(QString());
        QDomElement o = types.createElement(qname(p, "Override"));
        o.setAttribute("PartName", "/" + part.defaultPath);
        o.setAttribute("ContentType", part.contentType);
        root.appendChild(o);
        replace.insert(typesName, serialize(types));
    }
    QDomDocument rels;
    const QByteArray relsName = "_rels/.rels";
    if (const auto b = replace.contains(relsName) ? std::optional(replace.value(relsName)) : entry(f, a, relsName); b && parse(*b, &rels)) {
        QDomElement root = rels.documentElement();
        const QString p = prefixFor(root, kRels).value_or(QString());
        QStringList ids;
        for (QDomElement r = root.firstChildElement(); !r.isNull(); r = r.nextSiblingElement()) ids.append(r.attribute("Id"));
        QString id = "rIdLoki1";
        for (int n = 2; ids.contains(id); ++n) id = "rIdLoki" + QString::number(n);
        QDomElement r = rels.createElement(qname(p, "Relationship"));
        r.setAttribute("Id", id);
        r.setAttribute("Type", part.relType);
        r.setAttribute("Target", part.defaultPath);
        root.appendChild(r);
        replace.insert(relsName, serialize(rels));
    }
}

bool isCoreField(const QString& field)
{
    for (const CoreField& f : coreFields())
        if (f.field == field) return true;
    return false;
}

bool isAppField(const QString& field)
{
    for (const auto& [f, local] : appFields())
        if (f == field) return true;
    return false;
}

} // namespace

ReadResult readOffice(const QString& path)
{
    ReadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = "Can't open the file: " + f.errorString();
        return r;
    }
    const auto d = open(f, &r.error);
    if (!d) return r;
    r.info.append({QString("Format"), formatName(path)});
    QDomDocument doc;
    if (d->odf) {
        if (const auto b = entry(f, d->archive, "meta.xml"); b && parse(*b, &doc)) readOdf(doc, r);
    } else {
        if (const auto b = d->corePath.isEmpty() ? std::nullopt : entry(f, d->archive, d->corePath); b && parse(*b, &doc)) readCore(doc, r);
        if (const auto b = d->appPath.isEmpty() ? std::nullopt : entry(f, d->archive, d->appPath); b && parse(*b, &doc)) readApp(doc, r);
        if (const auto b = d->customPath.isEmpty() ? std::nullopt : entry(f, d->archive, d->customPath); b && parse(*b, &doc)) readCustom(doc, r);
    }
    r.ok = true;
    return r;
}

QString writeOffice(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return "Can't open the file: " + in.errorString();
    QString error;
    const auto d = open(in, &error);
    if (!d) return error;
    // Named fields with no place in a document; anything else becomes a custom property.
    auto custom = [](const QString& field) { return field == "copyright" || fieldOrder(field) >= knownFields().size(); };

    QHash<QByteArray, QByteArray> replace;
    QList<QPair<QByteArray, QByteArray>> added;
    if (d->odf) {
        QDomDocument doc;
        const auto b = entry(in, d->archive, "meta.xml");
        if (!b || !parse(*b, &doc)) return "This document has no properties part (meta.xml) to edit.";
        writeOdf(doc, changes, notStored, custom);
        replace.insert("meta.xml", serialize(doc));
    } else {
        QHash<QString, QString> core, app, cust;
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            if (isCoreField(it.key())) core.insert(it.key(), it.value());
            else if (isAppField(it.key())) app.insert(it.key(), it.value());
            else if (custom(it.key())) cust.insert(it.key(), it.value());
            else notStored->append(it.key());
        }
        auto store = [&](const QString& existing, const Part& part, const std::function<void(QDomDocument&)>& edit) {
            QDomDocument doc = partOrNew(in, d->archive, existing, part);
            edit(doc);
            if (!existing.isEmpty()) {
                replace.insert(existing.toUtf8(), serialize(doc));
            } else {
                added.append({part.defaultPath.toUtf8(), serialize(doc)});
                registerPart(in, d->archive, part, replace);
            }
        };
        if (!core.isEmpty())
            store(d->corePath, kCorePart, [&](QDomDocument& doc) {
                QDomElement root = doc.documentElement();
                for (auto it = core.begin(); it != core.end(); ++it)
                    for (const CoreField& f : coreFields())
                        if (f.field == it.key())
                            setChild(doc, root, qname(declare(root, f.ns, f.ns == kDc ? "dc" : "cp"), f.local), it.value());
            });
        if (!app.isEmpty())
            store(d->appPath, kAppPart, [&](QDomDocument& doc) {
                QDomElement root = doc.documentElement();
                const QString p = prefixFor(root, kApp).value_or(QString());
                for (auto it = app.begin(); it != app.end(); ++it)
                    for (const auto& [field, local] : appFields())
                        if (field == it.key()) setChild(doc, root, qname(p, local), it.value());
            });
        if (!cust.isEmpty())
            store(d->customPath, kCustomPart, [&](QDomDocument& doc) {
                for (auto it = cust.begin(); it != cust.end(); ++it) setCustom(doc, customName(it.key()), it.value());
            });
    }
    if (replace.isEmpty() && added.isEmpty()) return {};

    const QString temp = tempPathFor(path);
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "Can't write in this folder: " + out.errorString();
    error = zip::rewrite(in, d->archive, replace, added, out);
    out.close();
    in.close();
    if (!error.isEmpty()) {
        QFile::remove(temp);
        return error;
    }
    if (!replaceFile(path, temp, &error)) return error;
    return {};
}

} // namespace meta
