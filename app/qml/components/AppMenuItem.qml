import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

MenuItem {
    id: item
    implicitHeight: Theme.size.buttonHeight
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body
    contentItem: Text {
        text: item.text
        font: item.font
        leftPadding: Theme.size.gap
        color: item.enabled ? Theme.color.text : Theme.color.textFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.size.radiusSmall
        color: item.highlighted ? Theme.color.hover : "transparent"
    }
}
