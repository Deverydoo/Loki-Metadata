// FBX (Autodesk's 3D interchange format), binary and text.
//
// The document info lives in FBXHeaderExtension/SceneInfo: a MetaData list
// (Title, Subject, Author, Keywords, Revision, Comment) and a Properties70
// list. Copyright and fields of your own are user properties there ("U"
// flag), where the FBX SDK keeps a document's own data.
//
// Binary files: every node records the absolute offset where it ends, so
// when the header block changes size, every node after it is rewritten with
// its offset moved; the geometry, animation and the footer are copied byte
// for byte. Text files: only the lines inside SceneInfo change.
//
// Windows has no property handler for FBX; FBX tools and the SDK read these.

#include "MetaIO.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QtEndian>

#include <functional>

namespace meta {

namespace {

const QByteArray kMagic("Kaydara FBX Binary  \0", 21);

// The named fields that go in MetaData, and the node each is.
const QList<QPair<QString, QByteArray>>& metaNames()
{
    static const QList<QPair<QString, QByteArray>> names{{"title", "Title"},       {"subject", "Subject"},
                                                         {"artist", "Author"},     {"keywords", "Keywords"},
                                                         {"comment", "Comment"},   {"revision", "Revision"}};
    return names;
}

QByteArray metaNodeOf(const QString& field)
{
    for (const auto& [f, n] : metaNames())
        if (f == field) return n;
    return {};
}

// A user property's name for a field, and back.
QByteArray propNameOf(const QString& field) { return field == "copyright" ? QByteArray("Copyright") : field.toUtf8(); }
QString fieldOfProp(const QString& name) { return name.compare("Copyright", Qt::CaseInsensitive) == 0 ? QString("copyright") : name; }

// The info shown for a file, from its header values.
struct Facts {
    QString version, creator, creationTime;
    QHash<QString, QString> sceneProps;   // Original|ApplicationName...
    QMap<QString, int> objects;           // Geometry -> 6...
};

void addFacts(const Facts& f, bool text, Pairs* info)
{
    info->append({QString("Format"), QString("FBX %1 (%2)").arg(f.version, text ? "text" : "binary")});
    auto app = [&](const QString& which) {
        const QString name = f.sceneProps.value(which + "|ApplicationName").trimmed();
        const QString version = f.sceneProps.value(which + "|ApplicationVersion").trimmed();
        if (name.isEmpty()) return version;
        return version.isEmpty() || name.contains(version) || version.contains(name) ? (version.contains(name) ? version : name)
                                                                                      : name + " " + version;
    };
    const QString made = app("Original");
    const QString saved = app("LastSaved");
    if (!made.isEmpty()) info->append({QString("Made with"), made});
    else if (!f.creator.isEmpty()) info->append({QString("Made with"), f.creator});
    if (!saved.isEmpty() && saved != made) info->append({QString("Last saved with"), saved});
    const QString when = f.sceneProps.value("LastSaved|DateTime_GMT");
    if (!when.isEmpty() && !when.startsWith("01/01/1970")) info->append({QString("Saved"), when + " UTC"});
    // What's in it, said plainly.
    static const QList<std::tuple<QString, QString, QString>> kinds{
        {"Geometry", "mesh", "meshes"},       {"Model", "object", "objects"},     {"Material", "material", "materials"},
        {"Texture", "texture", "textures"},   {"AnimationStack", "animation", "animations"}};
    QStringList parts;
    for (const auto& [node, one, many] : kinds) {
        const int n = f.objects.value(node);
        if (n) parts.append(QString("%1 %2").arg(n).arg(n == 1 ? one : many));
    }
    if (f.objects.value("Deformer")) parts.append("rigged");
    if (!parts.isEmpty()) info->append({QString("Contents"), parts.join(", ")});
}

// --- Binary ----------------------------------------------------------------------

struct Prop {
    char type = 0;
    QByteArray raw;   // its bytes after the type code
};

struct Node {
    QByteArray name;
    QList<Prop> props;
    QList<Node> kids;
    bool nullEnd = false;   // its children end with a null record
};

quint32 le32(const QByteArray& b, qint64 at) { return qFromLittleEndian<quint32>(b.constData() + at); }
quint64 le64(const QByteArray& b, qint64 at) { return qFromLittleEndian<quint64>(b.constData() + at); }

void put32(QByteArray& b, quint32 v)
{
    char x[4];
    qToLittleEndian(v, x);
    b.append(x, 4);
}

void put64(QByteArray& b, quint64 v)
{
    char x[8];
    qToLittleEndian(v, x);
    b.append(x, 8);
}

Prop str(const QByteArray& s)
{
    Prop p{'S', {}};
    put32(p.raw, quint32(s.size()));
    p.raw += s;
    return p;
}

QByteArray strOf(const Prop& p) { return p.type == 'S' && p.raw.size() >= 4 ? p.raw.mid(4) : QByteArray(); }

// How many bytes a property's data takes, from @p at (after its type code).
qint64 propSize(const QByteArray& b, qint64 at, char t, qint64 end)
{
    switch (t) {
    case 'Y': return 2;
    case 'C': return 1;
    case 'I':
    case 'F': return 4;
    case 'D':
    case 'L': return 8;
    case 'S':
    case 'R': return at + 4 <= end ? 4 + qint64(le32(b, at)) : -1;
    case 'f':
    case 'd':
    case 'l':
    case 'i':
    case 'b':
    case 'c': return at + 12 <= end ? 12 + qint64(le32(b, at + 8)) : -1;
    }
    return -1;
}

class Binary {
public:
    Binary(bool wide) : wide_(wide), hs_(wide ? 25 : 13) {}

    // The node at @p pos of @p b, whose first byte is at file offset @p base.
    bool parse(const QByteArray& b, qint64 base, qint64& pos, Node& n, int depth = 0) const
    {
        if (depth > 32 || pos + hs_ > b.size()) return false;
        const qint64 end = qint64(wide_ ? le64(b, pos) : le32(b, pos)) - base;
        const quint64 count = wide_ ? le64(b, pos + 8) : le32(b, pos + 4);
        const qint64 propLen = qint64(wide_ ? le64(b, pos + 16) : le32(b, pos + 8));
        const int nameLen = quint8(b[pos + hs_ - 1]);
        if (end <= pos || end > b.size()) return false;
        n.name = b.mid(pos + hs_, nameLen);
        qint64 p = pos + hs_ + nameLen;
        const qint64 propsEnd = p + propLen;
        if (propsEnd > end) return false;
        for (quint64 i = 0; i < count; ++i) {
            if (p >= propsEnd) return false;
            const char t = b[p];
            const qint64 size = propSize(b, p + 1, t, propsEnd);
            if (size < 0 || p + 1 + size > propsEnd) return false;
            n.props.append({t, b.mid(p + 1, size)});
            p += 1 + size;
        }
        if (p != propsEnd) return false;
        while (p < end) {
            if (p + hs_ > end) return false;
            if (isNull(b, p)) {
                n.nullEnd = true;
                p += hs_;
                break;
            }
            Node kid;
            if (!parse(b, base, p, kid, depth + 1)) return false;
            n.kids.append(kid);
        }
        if (p != end) return false;
        pos = end;
        return true;
    }

    QByteArray build(const Node& n, quint64 at) const
    {
        QByteArray props;
        for (const Prop& p : n.props) props += p.type + p.raw;
        QByteArray kids;
        const quint64 kidsAt = at + hs_ + n.name.size() + props.size();
        for (const Node& k : n.kids) kids += build(k, kidsAt + kids.size());
        if (n.nullEnd || !n.kids.isEmpty()) kids += QByteArray(hs_, '\0');
        const quint64 end = kidsAt + kids.size();
        QByteArray out;
        if (wide_) {
            put64(out, end);
            put64(out, quint64(n.props.size()));
            put64(out, quint64(props.size()));
        } else {
            put32(out, quint32(end));
            put32(out, quint32(n.props.size()));
            put32(out, quint32(props.size()));
        }
        out += char(n.name.size());
        return out + n.name + props + kids;
    }

    // Copies the node list at @p pos (up to @p limit, or its null record)
    // from @p in to @p out, every end offset moved by @p delta. Returns where
    // it stopped, or -1 when the layout doesn't add up.
    qint64 copyMoved(QFile& in, QFile& out, qint64 pos, qint64 limit, qint64 delta, int depth = 0) const
    {
        if (depth > 64) return -1;
        while (pos + hs_ <= limit) {
            in.seek(pos);
            QByteArray h = in.read(hs_);
            if (h.size() < hs_) return -1;
            const qint64 end = qint64(wide_ ? le64(h, 0) : le32(h, 0));
            if (end == 0) {   // a null record: the list's end
                out.write(h);
                return pos + hs_;
            }
            const qint64 propLen = qint64(wide_ ? le64(h, 16) : le32(h, 8));
            const int nameLen = quint8(h[hs_ - 1]);
            const qint64 kidsAt = pos + hs_ + nameLen + propLen;
            if (end <= pos || end > limit || kidsAt > end) return -1;
            const qint64 moved = end + delta;
            if (!wide_ && moved > 0xFFFFFFFFLL) return -1;
            if (wide_) qToLittleEndian<quint64>(quint64(moved), h.data());
            else qToLittleEndian<quint32>(quint32(moved), h.data());
            out.write(h);
            if (!copy(in, out, pos + hs_, nameLen + propLen)) return -1;
            if (kidsAt < end) {
                const qint64 reached = copyMoved(in, out, kidsAt, end, delta, depth + 1);
                if (reached < 0 || (reached < end && !copy(in, out, reached, end - reached))) return -1;
            }
            pos = end;
        }
        return pos;
    }

    int headerSize() const { return hs_; }

    // Top-level nodes: name -> [where, end), read header by header.
    QList<std::tuple<QByteArray, qint64, qint64>> topLevel(QFile& f) const
    {
        QList<std::tuple<QByteArray, qint64, qint64>> out;
        for (qint64 pos = 27; pos + hs_ <= f.size();) {
            f.seek(pos);
            const QByteArray h = f.read(hs_);
            const qint64 end = qint64(wide_ ? le64(h, 0) : le32(h, 0));
            if (end == 0) break;
            if (end <= pos || end > f.size()) return {};
            out.append({f.read(quint8(h[hs_ - 1])), pos, end});
            pos = end;
        }
        return out;
    }

    // The names of a node's children, without reading their contents.
    QMap<QString, int> childNames(QFile& f, qint64 pos, qint64 end) const
    {
        QMap<QString, int> out;
        f.seek(pos);
        const QByteArray h = f.read(hs_);
        const qint64 propLen = qint64(wide_ ? le64(h, 16) : le32(h, 8));
        qint64 p = pos + hs_ + quint8(h[hs_ - 1]) + propLen;
        while (p + hs_ <= end) {
            f.seek(p);
            const QByteArray k = f.read(hs_);
            const qint64 e = qint64(wide_ ? le64(k, 0) : le32(k, 0));
            if (e == 0 || e <= p || e > end) break;
            ++out[QString::fromLatin1(f.read(quint8(k[hs_ - 1])))];
            p = e;
        }
        return out;
    }

private:
    bool isNull(const QByteArray& b, qint64 at) const
    {
        for (int i = 0; i < hs_; ++i)
            if (b[at + i] != 0) return false;
        return true;
    }

    static bool copy(QFile& in, QFile& out, qint64 at, qint64 length)
    {
        if (!in.seek(at)) return false;
        while (length > 0) {
            const QByteArray buf = in.read(std::min<qint64>(length, 4 << 20));
            if (buf.isEmpty() || out.write(buf) != buf.size()) return false;
            length -= buf.size();
        }
        return true;
    }

    bool wide_;
    int hs_;
};

Node* kid(Node& n, const QByteArray& name)
{
    for (Node& k : n.kids)
        if (k.name == name) return &k;
    return nullptr;
}

const Node* kid(const Node& n, const QByteArray& name)
{
    for (const Node& k : n.kids)
        if (k.name == name) return &k;
    return nullptr;
}

struct BinaryFile {
    quint32 version = 0;
    bool wide = false;
    Node header;              // FBXHeaderExtension
    qint64 headerEnd = 0;     // where it ended in the file
};

std::optional<BinaryFile> openBinary(QFile& f, QString* error)
{
    BinaryFile bf;
    const QByteArray head = f.read(27);
    if (head.size() < 27 || !head.startsWith(kMagic)) {
        *error = "This isn't a binary FBX file.";
        return std::nullopt;
    }
    bf.version = le32(head, 23);
    bf.wide = bf.version >= 7500;
    const Binary bin(bf.wide);
    const QByteArray h = f.read(bin.headerSize());
    if (h.size() < bin.headerSize()) {
        *error = "The file is cut short.";
        return std::nullopt;
    }
    bf.headerEnd = qint64(bf.wide ? le64(h, 0) : le32(h, 0));
    if (bf.headerEnd <= 27 || bf.headerEnd > f.size() || bf.headerEnd - 27 > (16 << 20)) {
        *error = "This FBX file is damaged.";
        return std::nullopt;
    }
    f.seek(27);
    const QByteArray bytes = f.read(bf.headerEnd - 27);
    qint64 pos = 0;
    if (!bin.parse(bytes, 27, pos, bf.header) || bf.header.name != "FBXHeaderExtension") {
        *error = "This FBX file's header can't be read.";
        return std::nullopt;
    }
    return bf;
}

void binaryFields(const Node& header, Pairs* tags, Facts* facts)
{
    if (const Node* c = kid(header, "Creator"); c && !c->props.isEmpty()) facts->creator = QString::fromUtf8(strOf(c->props[0]));
    const Node* scene = kid(header, "SceneInfo");
    if (!scene) return;
    if (const Node* md = kid(*scene, "MetaData"))
        for (const auto& [field, name] : metaNames())
            if (const Node* n = kid(*md, name); n && !n->props.isEmpty())
                if (const QString v = QString::fromUtf8(strOf(n->props[0])); !v.isEmpty()) tags->append({field, v});
    if (const Node* p70 = kid(*scene, "Properties70"))
        for (const Node& p : p70->kids) {
            if (p.name != "P" || p.props.size() < 5) continue;
            const QString name = QString::fromUtf8(strOf(p.props[0]));
            const QString value = QString::fromUtf8(strOf(p.props[4]));
            facts->sceneProps.insert(name, value);
            if (strOf(p.props[3]).contains('U') && !value.isEmpty()) tags->append({fieldOfProp(name), value});
        }
}

// The changes, made to SceneInfo. False when there's no SceneInfo to change.
bool applyBinary(Node& header, const QHash<QString, QString>& changes)
{
    Node* scene = kid(header, "SceneInfo");
    if (!scene) return false;
    Node* md = kid(*scene, "MetaData");
    Node* p70 = kid(*scene, "Properties70");
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QByteArray value = it.value().toUtf8();
        if (const QByteArray node = metaNodeOf(it.key()); !node.isEmpty()) {
            if (!md) {
                Node fresh{"MetaData", {}, {}, true};
                Node version{"Version", {}, {}, false};
                Prop v{'I', {}};
                put32(v.raw, 100);
                version.props.append(v);
                fresh.kids.append(version);
                for (const auto& [f, n] : metaNames()) fresh.kids.append(Node{n, {str({})}, {}, false});
                scene->kids.insert(std::min<qsizetype>(2, scene->kids.size()), fresh);
                md = kid(*scene, "MetaData");
                p70 = kid(*scene, "Properties70");
            }
            if (Node* n = kid(*md, node)) n->props = {str(value)};
            else md->kids.append(Node{node, {str(value)}, {}, false});
            continue;
        }
        if (!p70) {
            scene->kids.append(Node{"Properties70", {}, {}, true});
            p70 = &scene->kids.last();
            md = kid(*scene, "MetaData");
        }
        const QByteArray name = propNameOf(it.key());
        qsizetype at = -1;
        for (qsizetype i = 0; i < p70->kids.size(); ++i)
            if (p70->kids[i].name == "P" && !p70->kids[i].props.isEmpty()
                && QString::fromUtf8(strOf(p70->kids[i].props[0])).compare(QString::fromUtf8(name), Qt::CaseInsensitive) == 0)
                at = i;
        if (value.isEmpty()) {
            if (at >= 0) p70->kids.removeAt(at);
            continue;
        }
        Node p{"P", {str(at >= 0 ? strOf(p70->kids[at].props[0]) : name), str("KString"), str(""), str("U"), str(value)}, {}, false};
        if (at >= 0) p70->kids[at] = p;
        else p70->kids.append(p);
    }
    return true;
}

// --- Text ----------------------------------------------------------------------

// The block "name: ... { ... }" starting at or after @p from, before @p limit:
// where its "{" and its matching "}" are.
std::optional<QPair<qsizetype, qsizetype>> block(const QByteArray& t, const QByteArray& name, qsizetype from, qsizetype limit)
{
    for (qsizetype at = t.indexOf(name + ':', from); at >= 0 && at < limit; at = t.indexOf(name + ':', at + 1)) {
        if (at > 0 && t[at - 1] != '\t' && t[at - 1] != ' ' && t[at - 1] != '\n') continue;   // part of another name
        const qsizetype open = t.indexOf('{', at);
        const qsizetype eol = t.indexOf('\n', at);
        if (open < 0 || (eol >= 0 && open > eol)) continue;
        int depth = 0;
        bool quoted = false;
        for (qsizetype i = open; i < limit; ++i) {
            const char c = t[i];
            if (c == '"') quoted = !quoted;
            else if (quoted) continue;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) return QPair<qsizetype, qsizetype>{open, i};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// The quoted strings on a line, &quot; undone.
QList<QByteArray> quotedOn(const QByteArray& line)
{
    QList<QByteArray> out;
    for (qsizetype i = line.indexOf('"'); i >= 0;) {
        const qsizetype j = line.indexOf('"', i + 1);
        if (j < 0) break;
        out.append(line.mid(i + 1, j - i - 1).replace("&quot;", "\""));
        i = line.indexOf('"', j + 1);
    }
    return out;
}

QByteArray quote(const QString& v)
{
    QString s = v;
    s.replace('"', "&quot;").replace("\r\n", " ").replace('\n', ' ');
    return '"' + s.toUtf8() + '"';
}

// The lines of t between from and to, each with where it starts.
QList<QPair<qsizetype, QByteArray>> linesIn(const QByteArray& t, qsizetype from, qsizetype to)
{
    QList<QPair<qsizetype, QByteArray>> out;
    for (qsizetype at = t.indexOf('\n', from) + 1; at > 0 && at < to;) {
        qsizetype end = t.indexOf('\n', at);
        if (end < 0 || end > to) end = to;
        out.append({at, t.mid(at, end - at)});
        at = end + 1;
    }
    return out;
}

bool isText(const QByteArray& head) { return !head.startsWith(kMagic) && head.trimmed().startsWith(';'); }

void textFields(const QByteArray& t, Pairs* tags, Facts* facts)
{
    if (t.startsWith("; FBX ")) facts->version = QString::fromLatin1(t.mid(6, t.indexOf(' ', 6) - 6));
    const qsizetype n = t.size();
    const auto scene = block(t, "SceneInfo", 0, n);
    if (scene) {
        if (const auto md = block(t, "MetaData", scene->first, scene->second))
            for (const auto& [at, line] : linesIn(t, md->first, md->second)) {
                const QByteArray s = line.trimmed();
                for (const auto& [field, name] : metaNames())
                    if (s.startsWith(name + ':'))
                        if (const auto q = quotedOn(s); !q.isEmpty() && !q[0].isEmpty()) tags->append({field, QString::fromUtf8(q[0])});
            }
        if (const auto p70 = block(t, "Properties70", scene->first, scene->second))
            for (const auto& [at, line] : linesIn(t, p70->first, p70->second)) {
                if (!line.trimmed().startsWith("P:")) continue;
                const auto q = quotedOn(line);
                if (q.size() < 5) continue;
                const QString name = QString::fromUtf8(q[0]), value = QString::fromUtf8(q[4]);
                facts->sceneProps.insert(name, value);
                if (q[3].contains('U') && !value.isEmpty()) tags->append({fieldOfProp(name), value});
            }
    }
    const qsizetype creator = t.indexOf("\nCreator:");
    if (creator >= 0) facts->creator = QString::fromUtf8(quotedOn(t.mid(creator, t.indexOf('\n', creator + 1) - creator)).value(0));
    if (const auto objects = block(t, "Objects", 0, n)) {
        int depth = 0;
        for (const auto& [at, line] : linesIn(t, objects->first, objects->second)) {
            const QByteArray s = line.trimmed();
            if (depth == 0 && s.contains(':')) ++facts->objects[QString::fromLatin1(s.left(s.indexOf(':')))];
            depth += int(line.count('{')) - int(line.count('}'));
        }
    }
}

// One change made to the text; false when there's no SceneInfo to make it in.
bool applyText(QByteArray& t, const QString& field, const QString& value, const QByteArray& eol)
{
    const auto scene = block(t, "SceneInfo", 0, t.size());
    if (!scene) return false;
    auto indentOf = [](const QByteArray& line) { return line.left(line.size() - line.trimmed().size() - (line.endsWith('\r') ? 1 : 0)); };
    auto replaceLine = [&](qsizetype at, const QByteArray& line, const QByteArray& with) {
        const qsizetype len = line.size() - (line.endsWith('\r') ? 1 : 0);
        t.replace(at, len, with);
    };
    // A line in a block, put before its closing brace, indented like its others.
    auto insertInto = [&](QPair<qsizetype, qsizetype> b, const QByteArray& content) {
        const auto lines = linesIn(t, b.first, b.second);
        QByteArray indent = "\t\t\t";
        for (const auto& [at, line] : lines)
            if (!line.trimmed().isEmpty()) indent = indentOf(line);
        const qsizetype lineStart = t.lastIndexOf('\n', b.second) + 1;
        t.insert(lineStart, indent + content + eol);
    };

    if (const QByteArray node = metaNodeOf(field); !node.isEmpty()) {
        const auto md = block(t, "MetaData", scene->first, scene->second);
        if (!md) return false;
        for (const auto& [at, line] : linesIn(t, md->first, md->second))
            if (line.trimmed().startsWith(node + ':')) {
                replaceLine(at, line, indentOf(line) + node + ": " + quote(value));
                return true;
            }
        insertInto(*md, node + ": " + quote(value));
        return true;
    }
    const auto p70 = block(t, "Properties70", scene->first, scene->second);
    if (!p70) return false;
    const QByteArray name = propNameOf(field);
    for (const auto& [at, line] : linesIn(t, p70->first, p70->second)) {
        if (!line.trimmed().startsWith("P:")) continue;
        const auto q = quotedOn(line);
        if (q.isEmpty() || QString::fromUtf8(q[0]).compare(QString::fromUtf8(name), Qt::CaseInsensitive) != 0) continue;
        if (value.isEmpty()) {
            const qsizetype end = t.indexOf('\n', at);
            t.remove(at, (end < 0 ? t.size() : end + 1) - at);
        } else {
            replaceLine(at, line, indentOf(line) + "P: " + quote(QString::fromUtf8(q[0])) + ", \"KString\", \"\", \"U\", " + quote(value));
        }
        return true;
    }
    if (!value.isEmpty()) insertInto(*p70, "P: " + quote(QString::fromUtf8(name)) + ", \"KString\", \"\", \"U\", " + quote(value));
    return true;
}

} // namespace

ReadResult readFbx(const QString& path)
{
    ReadResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = "Can't open the file: " + f.errorString();
        return r;
    }
    Facts facts;
    const QByteArray head = f.peek(64);
    if (isText(head)) {
        if (f.size() > (512ll << 20)) {
            r.error = "This text FBX is too big to read here.";
            return r;
        }
        const QByteArray t = f.readAll();
        textFields(t, &r.tags, &facts);
        addFacts(facts, true, &r.info);
        r.ok = true;
        return r;
    }
    const auto bf = openBinary(f, &r.error);
    if (!bf) return r;
    facts.version = QString("%1.%2").arg(bf->version / 1000).arg((bf->version % 1000) / 100);
    binaryFields(bf->header, &r.tags, &facts);
    const Binary bin(bf->wide);
    for (const auto& [name, at, end] : bin.topLevel(f)) {
        if (name == "Objects") facts.objects = bin.childNames(f, at, end);
        if (name == "Creator" && facts.creator.isEmpty() && end - at < 4096) {
            f.seek(at);
            const QByteArray bytes = f.read(end - at);
            Node n;
            qint64 pos = 0;
            if (bin.parse(bytes, at, pos, n) && !n.props.isEmpty()) facts.creator = QString::fromUtf8(strOf(n.props[0]));
        }
    }
    addFacts(facts, false, &r.info);
    r.ok = true;
    return r;
}

QString writeFbx(const QString& path, const QHash<QString, QString>& changes, QStringList* notStored)
{
    Q_UNUSED(notStored);   // every field it's asked to keep has a place (others are refused before)
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return "Can't open the file: " + in.errorString();
    const QString temp = tempPathFor(path);
    const QString noInfo = "This FBX has no document info block (SceneInfo) to write to.";

    if (isText(in.peek(64))) {
        if (in.size() > (512ll << 20)) return "This text FBX is too big to edit here.";
        QByteArray t = in.readAll();
        in.close();
        const QByteArray eol = t.contains("\r\n") ? "\r\n" : "\n";
        for (auto it = changes.begin(); it != changes.end(); ++it)
            if (!applyText(t, it.key(), it.value(), eol)) return noInfo;
        QFile out(temp);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "Can't write in this folder: " + out.errorString();
        if (out.write(t) != t.size()) {
            out.close();
            QFile::remove(temp);
            return "Writing failed (is the disk full?).";
        }
        out.close();
        QString error;
        return replaceFile(path, temp, &error) ? QString() : error;
    }

    QString error;
    auto bf = openBinary(in, &error);
    if (!bf) return error;
    if (!applyBinary(bf->header, changes)) return noInfo;
    const Binary bin(bf->wide);
    const QByteArray header = bin.build(bf->header, 27);
    const qint64 delta = qint64(header.size()) - (bf->headerEnd - 27);

    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "Can't write in this folder: " + out.errorString();
    auto abandon = [&](const QString& why) {
        out.close();
        QFile::remove(temp);
        return why;
    };
    in.seek(0);
    out.write(in.read(27));
    out.write(header);
    const qint64 reached = bin.copyMoved(in, out, bf->headerEnd, in.size(), delta);
    if (reached < 0) return abandon("This FBX file's layout isn't one it can rewrite safely, so it was left alone.");
    // The footer, byte for byte.
    in.seek(reached);
    while (!in.atEnd()) {
        const QByteArray buf = in.read(4 << 20);
        if (out.write(buf) != buf.size()) return abandon("Writing failed (is the disk full?).");
    }
    if (out.error() != QFile::NoError) return abandon("Writing failed: " + out.errorString());
    out.close();
    in.close();
    return replaceFile(path, temp, &error) ? QString() : error;
}

} // namespace meta
