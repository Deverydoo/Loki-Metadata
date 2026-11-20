import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

// A one-line text box. `edited` marks it as holding a change not yet applied.
TextField {
    id: control
    property bool edited: false

    implicitHeight: Theme.size.fieldHeight
    leftPadding: Theme.size.gap + Theme.size.gapSmall
    rightPadding: leftPadding
    hoverEnabled: true
    color: Theme.color.text
    placeholderTextColor: Theme.color.placeholder
    selectionColor: Theme.color.accent
    selectedTextColor: Theme.color.textOnAccent
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: Theme.size.radius
        color: Theme.color.raised
        border.width: control.activeFocus ? Theme.size.focusWidth : Theme.size.border
        border.color: control.activeFocus ? Theme.color.focus
                    : control.edited ? Theme.color.edited
                    : control.hovered ? Theme.color.borderStrong : Theme.color.border
    }
}
