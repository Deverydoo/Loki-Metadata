#include "Xmp.h"

#include <QDateTime>
#include <QDomDocument>
#include <QDomElement>

#include <functional>

namespace meta::xmp {

namespace {

const QString kRdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
const QString kDc = "http://purl.org/dc/elements/1.1/";
const QString kXmp = "http://ns.adobe.com/xap/1.0/";

enum class Shape { Simple, Alt, Seq, Bag };

struct Property {
    QString uri, prefix, name, field;
    Shape shape;
};

const QList<Property>& properties()
{
    static const QList<Property> props{
        {kDc, "dc", "title", "title", Shape::Alt},
        {kDc, "dc", "creator", "artist", Shape::Seq},
        {kDc, "dc", "description", "description", Shape::Alt},
        {kDc, "dc", "subject", "keywords", Shape::Bag},
        {kDc, "dc", "rights", "copyright", Shape::Alt},
        {kXmp, "xmp", "CreatorTool", "encoder", Shape::Simple},
        {kXmp, "xmp", "Rating", "rating", Shape::Simple},
    };
    return props;
}

// The packet is parsed without namespace processing, so names are kept as
// written ("dc:title") and xmlns declarations are plain attributes; prefixes
// are looked up from those declarations, since a file may use any prefix.

QString localName(const QString& qualified)
{
    const qsizetype colon = qualified.indexOf(':');
    return colon < 0 ? qualified : qualified.mid(colon + 1);
}

QString prefixFor(QDomElement e, const QString& uri)
{
    for (; !e.isNull(); e = e.parentNode().toElement()) {
        const QDomNamedNodeMap attrs = e.attributes();
        for (int i = 0; i < attrs.count(); ++i) {
            const QDomAttr a = attrs.item(i).toAttr();
            if (a.name().startsWith("xmlns:") && a.value() == uri) return a.name().mid(6);
        }
    }
    return {};
}

bool prefixTaken(QDomElement e, const QString& prefix)
{
    for (; !e.isNull(); e = e.parentNode().toElement())
        if (e.hasAttribute("xmlns:" + prefix)) return true;
    return false;
}

// The rdf:Description elements, where the properties live.
QList<QDomElement> descriptions(const QDomDocument& doc)
{
    QList<QDomElement> out;
    std::function<void(const QDomElement&)> walk = [&](const QDomElement& e) {
        for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
            if (localName(c.tagName()) == "Description" && localName(e.tagName()) == "RDF") out.append(c);
            else walk(c);
        }
    };
    walk(doc.documentElement());
    return out;
}

QString readProperty(const QList<QDomElement>& descs, const QString& uri, const QString& name)
{
    for (const QDomElement& d : descs) {
        const QString prefix = prefixFor(d, uri);
        if (prefix.isEmpty()) continue;
        const QString qname = prefix + ":" + name;
        if (d.hasAttribute(qname)) return d.attribute(qname).trimmed();
        const QDomElement p = d.firstChildElement(qname);
        if (p.isNull()) continue;
        const QDomElement container = p.firstChildElement();
        const QString kind = localName(container.tagName());
        if (container.isNull() || (kind != "Alt" && kind != "Seq" && kind != "Bag")) return p.text().trimmed();
        QStringList items;
        QString fallback;
        for (QDomElement li = container.firstChildElement(); !li.isNull(); li = li.nextSiblingElement()) {
            const QString text = li.text().trimmed();
            if (kind == "Alt") {   // one text per language: take the default
                if (li.attribute("xml:lang") == "x-default") return text;
                if (fallback.isEmpty()) fallback = text;
            } else if (!text.isEmpty()) {
                items.append(text);
            }
        }
        return kind == "Alt" ? fallback : items.join("; ");
    }
    return {};
}

void writeProperty(QDomDocument& doc, const QList<QDomElement>& descs, const Property& p, const QString& value)
{
    // Out with the old, wherever it is...
    for (QDomElement d : descs) {
        const QString prefix = prefixFor(d, p.uri);
        if (prefix.isEmpty()) continue;
        const QString qname = prefix + ":" + p.name;
        d.removeAttribute(qname);
        for (QDomElement c = d.firstChildElement(qname); !c.isNull(); c = d.firstChildElement(qname)) d.removeChild(c);
    }
    if (value.trimmed().isEmpty()) return;

    // ...and in with the new, in the first Description.
    QDomElement d = descs.first();
    QString prefix = prefixFor(d, p.uri);
    if (prefix.isEmpty()) {
        prefix = p.prefix;
        for (int n = 1; prefixTaken(d, prefix); ++n) prefix = p.prefix + QString::number(n);
        d.setAttribute("xmlns:" + prefix, p.uri);
    }
    QString rdf = prefixFor(d, kRdf);
    if (rdf.isEmpty()) {
        rdf = "rdf";
        d.setAttribute("xmlns:rdf", kRdf);
    }
    QDomElement e = doc.createElement(prefix + ":" + p.name);
    if (p.shape == Shape::Simple) {
        e.appendChild(doc.createTextNode(value.trimmed()));
    } else {
        QDomElement container = doc.createElement(rdf + (p.shape == Shape::Alt ? ":Alt" : p.shape == Shape::Seq ? ":Seq" : ":Bag"));
        const QStringList items = p.shape == Shape::Alt ? QStringList{value.trimmed()} : value.split(';', Qt::SkipEmptyParts);
        for (const QString& item : items) {
            if (item.trimmed().isEmpty()) continue;
            QDomElement li = doc.createElement(rdf + ":li");
            if (p.shape == Shape::Alt) li.setAttribute("xml:lang", "x-default");
            li.appendChild(doc.createTextNode(item.trimmed()));
            container.appendChild(li);
        }
        e.appendChild(container);
    }
    d.appendChild(e);
}

QString readableDate(const QString& iso)
{
    const QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
    if (!t.isValid()) return iso;
    return t.toString(iso.contains('T') ? "yyyy-MM-dd HH:mm" : "yyyy-MM-dd");
}

bool parse(const QByteArray& packet, QDomDocument* doc)
{
    return bool(doc->setContent(packet));   // no namespace processing: see above
}

} // namespace

bool holds(const QString& field)
{
    for (const Property& p : properties())
        if (p.field == field) return true;
    return false;
}

void read(const QByteArray& packet, Pairs* tags, Pairs* info)
{
    QDomDocument doc;
    if (!parse(packet, &doc)) {
        info->append({QString("XMP"), QString("Present, but not readable")});
        return;
    }
    const QList<QDomElement> descs = descriptions(doc);
    for (const Property& p : properties()) {
        const QString v = readProperty(descs, p.uri, p.name);
        if (v.isEmpty()) continue;
        bool have = false;
        for (const auto& [k, old] : *tags) have = have || k == p.field;
        if (!have) tags->append({p.field, v});
    }
    const QString created = readProperty(descs, kXmp, "CreateDate");
    const QString modified = readProperty(descs, kXmp, "ModifyDate");
    if (!created.isEmpty()) info->append({QString("Created"), readableDate(created)});
    if (!modified.isEmpty()) info->append({QString("Modified"), readableDate(modified)});
}

QByteArray update(const QByteArray& packet, const QHash<QString, QString>& changes)
{
    bool touches = false;
    for (auto it = changes.begin(); it != changes.end(); ++it) touches = touches || holds(it.key());
    if (!touches) return packet;

    QDomDocument doc;
    if (!parse(packet, &doc)) return {};
    const QList<QDomElement> descs = descriptions(doc);
    if (descs.isEmpty()) return {};
    for (const Property& p : properties())
        if (changes.contains(p.field)) writeProperty(doc, descs, p, changes.value(p.field));
    return doc.toByteArray(1);
}

} // namespace meta::xmp
