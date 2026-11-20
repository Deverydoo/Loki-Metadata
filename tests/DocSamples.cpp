// Small Office documents for the tests, built by hand. (Kept out of
// test_metadata.cpp: these XML literals trip up moc's parser there.)

#include "DocSamples.h"
#include "Zip.h"

using meta::zip::create;

namespace {
const QByteArray decl = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
}

// A Word document with core properties only (no app.xml or custom.xml yet).
QByteArray sampleDocx()
{
    return create({
            {"[Content_Types].xml", decl + R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/></Types>)"},
            {"_rels/.rels", decl + R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/></Relationships>)"},
            {"word/document.xml", decl + R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>Hello</w:t></w:r></w:p></w:body></w:document>)"},
            {"docProps/core.xml", decl + R"(<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><dc:title>Sample</dc:title><dc:creator>Someone</dc:creator><dcterms:created xsi:type="dcterms:W3CDTF">2026-11-01T10:00:00Z</dcterms:created></cp:coreProperties>)"},
        });
}

// An OpenDocument text: "mimetype" first and stored, as the format asks.
QByteArray sampleOdt()
{
    return create({
            {"mimetype", "application/vnd.oasis.opendocument.text"},
            {"content.xml", decl + R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.3"><office:body><office:text><text:p>Hello</text:p></office:text></office:body></office:document-content>)"},
            {"meta.xml", decl + R"(<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:meta="urn:oasis:names:tc:opendocument:xmlns:meta:1.0" xmlns:dc="http://purl.org/dc/elements/1.1/" office:version="1.3"><office:meta><dc:title>Sample</dc:title><meta:initial-creator>Someone</meta:initial-creator><meta:generator>Test</meta:generator></office:meta></office:document-meta>)"},
            {"META-INF/manifest.xml", decl + R"(<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.3"><manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/><manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/><manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/></manifest:manifest>)"},
        });
}
