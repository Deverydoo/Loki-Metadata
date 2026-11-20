import QtQuick
import QtQuick.Layouts
import LokiMetadata

// One file in the list.
Rectangle {
    id: row
    required property int index
    required property string name
    required property string folder
    required property string badge
    required property string kind
    required property string state
    required property string error
    required property string warning
    required property bool selected
    required property bool modified

    signal clicked(int index, int modifiers)

    implicitHeight: Theme.size.fileRowHeight
    color: selected ? (mouse.containsMouse ? Theme.color.selectionHover : Theme.color.selection)
                    : mouse.containsMouse ? Theme.color.hover : "transparent"

    Rectangle {   // selection mark
        width: Theme.size.markerWidth
        height: parent.height
        color: Theme.color.accent
        visible: row.selected
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.size.padding
        anchors.rightMargin: Theme.size.padding
        spacing: Theme.size.padding

        Badge {
            text: row.badge
            kind: row.kind
            opacity: row.state === "loading" ? 0.5 : 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: row.name
                elide: Text.ElideMiddle
                color: row.state === "failed" ? Theme.color.danger : Theme.color.text
                font.family: Theme.font.family
                font.pixelSize: Theme.font.body
            }
            Text {
                Layout.fillWidth: true
                text: row.state === "loading" ? "Reading..."
                    : row.state === "saving" ? "Saving..."
                    : row.error !== "" ? row.error
                    : row.folder
                elide: row.error !== "" ? Text.ElideRight : Text.ElideMiddle
                color: row.error !== "" ? Theme.color.danger : Theme.color.textFaint
                font.family: Theme.font.family
                font.pixelSize: Theme.font.small
            }
        }

        // What needs attention: an error, a warning, or changes waiting.
        Item {
            implicitWidth: Theme.size.icon
            implicitHeight: Theme.size.icon
            Glyph {
                anchors.centerIn: parent
                visible: row.error !== "" || (row.warning !== "" && !row.modified)
                text: row.error !== "" ? Icons.error : Icons.warning
                color: row.error !== "" ? Theme.color.danger : Theme.color.warning
                font.pixelSize: Theme.size.iconSmall + 2
            }
            Rectangle {
                anchors.centerIn: parent
                visible: row.modified && row.error === ""
                width: Theme.size.dot
                height: width
                radius: width / 2
                color: Theme.color.edited
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: (m) => row.clicked(row.index, m.modifiers)
    }

    AppToolTip {
        visible: mouse.containsMouse && (row.error !== "" || row.warning !== "" || row.modified)
        text: row.error !== "" ? row.error
            : row.modified ? "Has changes waiting for Apply."
            : row.warning
    }
}
