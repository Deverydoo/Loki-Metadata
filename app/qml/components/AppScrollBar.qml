import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

// A thin scroll bar that shows while scrolling or hovered.
ScrollBar {
    id: bar
    padding: Theme.size.border
    contentItem: Rectangle {
        implicitWidth: Theme.size.scrollBar
        implicitHeight: Theme.size.scrollBar
        radius: width / 2
        color: bar.pressed ? Theme.color.textFaint : Theme.color.borderStrong
        opacity: bar.size < 1 && (bar.active || bar.hovered) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }
    background: Item {}
}
