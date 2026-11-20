import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

Menu {
    id: menu
    implicitWidth: Theme.size.menuWidth
    padding: Theme.size.gapSmall
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body
    delegate: AppMenuItem {}
    background: Rectangle {
        implicitWidth: Theme.size.menuWidth
        color: Theme.color.overlay
        border.width: Theme.size.border
        border.color: Theme.color.border
        radius: Theme.size.radius
    }
}
