// Loki Metadata: edit the tags of audio, video and image files, one at a
// time or many at once. Files named on the command line are added at start.

#include "MetaEditor.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(LokiMetadataPlugin)

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Chaos Rift Entertainment");
    app.setApplicationName("Loki Metadata");
    app.setApplicationVersion(LOKI_METADATA_VERSION);   // from CMakeLists.txt
    QQuickStyle::setStyle("Basic");   // the controls are styled by the theme, not by Windows

    MetaEditor editor;
    QSettings settings;   // the user's choices, kept between runs
    editor.setIncludeSubfolders(settings.value("includeSubfolders", false).toBool());
    QObject::connect(&editor, &MetaEditor::includeSubfoldersChanged, &editor,
                     [&] { settings.setValue("includeSubfolders", editor.includeSubfolders()); });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.loadFromModule("LokiMetadata", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    const QStringList files = app.arguments().mid(1);
    if (!files.isEmpty()) editor.addPaths(files);
    return app.exec();
}
