import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

// A button. variant: "normal", "primary" (the one main action), "subtle"
// (no frame until hovered) or "danger".
Button {
    id: control
    property string variant: "normal"
    property string glyph: ""
    property string tip: ""

    implicitHeight: Theme.size.buttonHeight
    implicitWidth: Math.max(implicitHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: text !== "" ? Theme.size.padding : 0
    rightPadding: leftPadding
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body
    font.weight: variant === "primary" ? Theme.font.strong : Theme.font.normal

    readonly property color foreground: !enabled ? Theme.color.textFaint
                                      : variant === "primary" ? Theme.color.textOnAccent
                                      : variant === "danger" ? Theme.color.danger
                                      : Theme.color.text

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.size.gap
            Glyph {
                visible: control.glyph !== ""
                text: control.glyph
                color: control.foreground
                font.pixelSize: control.text !== "" ? Theme.size.iconSmall : Theme.size.icon
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: control.text !== ""
                text: control.text
                font: control.font
                color: control.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: Theme.size.radius
        color: {
            if (control.variant === "primary")
                return !control.enabled ? Theme.color.raised
                     : control.down ? Theme.color.accentPressed
                     : control.hovered ? Theme.color.accentHover : Theme.color.accent
            if (control.variant === "subtle")
                return control.down ? Theme.color.pressed : control.hovered && control.enabled ? Theme.color.hover : "transparent"
            if (control.variant === "danger")
                return control.down ? Theme.color.pressed : control.hovered ? Theme.color.dangerSoft : Theme.color.raised
            return control.down ? Theme.color.pressed : control.hovered && control.enabled ? Theme.color.hover : Theme.color.raised
        }
        border.width: control.variant === "normal" || control.variant === "danger" ? Theme.size.border : 0
        border.color: control.hovered && control.enabled ? Theme.color.borderStrong : Theme.color.border

        Rectangle {   // keyboard focus
            anchors.fill: parent
            anchors.margins: -Theme.size.focusWidth
            radius: parent.radius + Theme.size.focusWidth
            color: "transparent"
            border.width: Theme.size.focusWidth
            border.color: Theme.color.focus
            visible: control.visualFocus
        }
    }

    AppToolTip {
        visible: control.hovered && control.tip !== ""
        text: control.tip
    }
}
