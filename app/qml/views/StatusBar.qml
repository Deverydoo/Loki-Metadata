import QtQuick
import QtQuick.Layouts
import LokiMetadata

// The last thing that happened, and progress while files are read or saved.
Rectangle {
    id: bar
    implicitHeight: Theme.size.statusHeight
    color: Theme.color.pane

    readonly property color tone: editor.messageKind === "error" ? Theme.color.danger
                                : editor.messageKind === "warning" ? Theme.color.warning
                                : editor.messageKind === "success" ? Theme.color.success
                                : Theme.color.textMuted

    Rectangle {
        width: parent.width
        height: Theme.size.border
        color: Theme.color.divider
    }

    // Progress along the top edge.
    Rectangle {
        visible: editor.busy
        width: parent.width * editor.progress
        height: Theme.size.focusWidth
        color: Theme.color.accent
        Behavior on width { NumberAnimation { duration: 120 } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.size.padding
        anchors.rightMargin: Theme.size.padding
        spacing: Theme.size.gap

        Glyph {
            visible: editor.message !== "" && editor.messageKind !== "info"
            text: editor.messageKind === "error" ? Icons.error
                : editor.messageKind === "warning" ? Icons.warning : Icons.check
            color: bar.tone
            font.pixelSize: Theme.size.iconSmall
        }
        Text {
            objectName: "statusMessage"
            Layout.fillWidth: true
            text: editor.message
            elide: Text.ElideRight
            color: bar.tone
            font.family: Theme.font.family
            font.pixelSize: Theme.font.small
        }
        Text {
            visible: editor.loadingCount > 0 || editor.scanning
            text: editor.loadingCount > 0 ? "Reading " + editor.loadingCount + "..." : "Looking for files..."
            color: Theme.color.textFaint
            font.family: Theme.font.family
            font.pixelSize: Theme.font.small
        }
    }
}
