import QtQuick
import QtQuick.Layouts
import LokiMetadata

// A line of news about the file: kind "error" or "warning".
Rectangle {
    id: banner
    property string kind: "warning"
    property string text

    implicitHeight: row.implicitHeight + 2 * Theme.size.gap + Theme.size.gapSmall
    radius: Theme.size.radius
    color: kind === "error" ? Theme.color.dangerSoft : Theme.color.warningSoft

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.size.gap + Theme.size.gapSmall
        spacing: Theme.size.gap + Theme.size.gapSmall
        Glyph {
            Layout.alignment: Qt.AlignTop
            text: banner.kind === "error" ? Icons.error : Icons.warning
            color: banner.kind === "error" ? Theme.color.danger : Theme.color.warning
        }
        Text {
            Layout.fillWidth: true
            text: banner.text
            color: Theme.color.text
            font.family: Theme.font.family
            font.pixelSize: Theme.font.body
            wrapMode: Text.Wrap
        }
    }
}
