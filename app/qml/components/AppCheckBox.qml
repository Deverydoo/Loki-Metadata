import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

// A check box with its label, styled by the theme.
CheckBox {
    id: control
    property string tip: ""

    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    spacing: Theme.size.gap
    padding: 0
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body

    indicator: Rectangle {
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: Theme.size.checkSize
        implicitHeight: Theme.size.checkSize
        radius: Theme.size.radiusSmall
        color: control.checked ? (control.hovered ? Theme.color.accentHover : Theme.color.accent) : Theme.color.raised
        border.width: control.checked ? 0 : Theme.size.border
        border.color: control.hovered ? Theme.color.borderStrong : Theme.color.border
        Glyph {
            anchors.centerIn: parent
            visible: control.checked
            text: Icons.check
            color: Theme.color.textOnAccent
            font.pixelSize: Theme.size.iconSmall - 2
        }
        Rectangle {   // keyboard focus
            anchors.fill: parent
            anchors.margins: -Theme.size.focusWidth - 1
            radius: parent.radius + Theme.size.focusWidth
            color: "transparent"
            border.width: Theme.size.focusWidth
            border.color: Theme.color.focus
            visible: control.visualFocus
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.color.text : Theme.color.textFaint
        verticalAlignment: Text.AlignVCenter
    }

    AppToolTip {
        visible: control.hovered && control.tip !== ""
        text: control.tip
    }
}
