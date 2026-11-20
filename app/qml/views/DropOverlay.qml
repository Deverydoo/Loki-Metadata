import QtQuick
import QtQuick.Layouts
import LokiMetadata

// Shown over the window while files are dragged onto it.
Rectangle {
    color: Theme.color.dropTint

    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.size.padding
        radius: Theme.size.radiusLarge
        color: "transparent"
        border.width: Theme.size.focusWidth
        border.color: Theme.color.accent
    }

    Rectangle {
        anchors.centerIn: parent
        width: label.implicitWidth + 2 * Theme.size.paddingLarge
        height: label.implicitHeight + 2 * Theme.size.padding
        radius: Theme.size.radiusLarge
        color: Theme.color.overlay
        border.width: Theme.size.border
        border.color: Theme.color.border
        RowLayout {
            id: label
            anchors.centerIn: parent
            spacing: Theme.size.gap
            Glyph { text: Icons.add; color: Theme.color.accent }
            Text {
                text: "Drop to add them to the list"
                color: Theme.color.text
                font.family: Theme.font.family
                font.pixelSize: Theme.font.title
            }
        }
    }
}
