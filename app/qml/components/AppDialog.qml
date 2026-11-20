import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import LokiMetadata

// A question in the window. Set title and message, and put the buttons in
// `buttons` (right-aligned, main action last).
Dialog {
    id: dialog
    property string message
    default property alias content: body.data
    property alias buttons: buttonRow.data

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(Theme.size.dialogWidth, (parent ? parent.width : Theme.size.dialogWidth) - 2 * Theme.size.paddingLarge)
    padding: Theme.size.paddingLarge
    topPadding: Theme.size.gap
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body

    Overlay.modal: Rectangle { color: Theme.color.scrim }

    background: Rectangle {
        color: Theme.color.overlay
        radius: Theme.size.radiusLarge
        border.width: Theme.size.border
        border.color: Theme.color.border
    }

    header: Text {
        text: dialog.title
        color: Theme.color.text
        font.family: Theme.font.family
        font.pixelSize: Theme.font.title
        font.weight: Theme.font.strong
        padding: Theme.size.paddingLarge
        bottomPadding: 0
        elide: Text.ElideRight
    }

    contentItem: ColumnLayout {
        id: body
        spacing: Theme.size.padding
        Text {
            visible: dialog.message !== ""
            Layout.fillWidth: true
            text: dialog.message
            color: Theme.color.textMuted
            font: dialog.font
            wrapMode: Text.Wrap
            lineHeight: 1.15
        }
    }

    footer: Item {
        implicitHeight: buttonRow.implicitHeight + Theme.size.paddingLarge
        Row {
            id: buttonRow
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: Theme.size.paddingLarge
            spacing: Theme.size.gap
        }
    }
}
