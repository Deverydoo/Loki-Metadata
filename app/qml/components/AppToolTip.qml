import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

ToolTip {
    id: tip
    delay: 500
    padding: Theme.size.gap
    font.family: Theme.font.family
    font.pixelSize: Theme.font.small
    contentItem: Text {
        text: tip.text
        font: tip.font
        color: Theme.color.text
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        color: Theme.color.overlay
        border.width: Theme.size.border
        border.color: Theme.color.border
        radius: Theme.size.radiusSmall
    }
}
