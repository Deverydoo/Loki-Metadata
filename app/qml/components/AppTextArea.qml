import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

// A text box for longer text (comments, descriptions, lyrics). Grows with
// what is typed.
TextArea {
    id: control
    property bool edited: false

    topPadding: Theme.size.gap
    bottomPadding: Theme.size.gap
    leftPadding: Theme.size.gap + Theme.size.gapSmall
    rightPadding: leftPadding
    hoverEnabled: true
    wrapMode: TextEdit.Wrap
    color: Theme.color.text
    placeholderTextColor: Theme.color.placeholder
    selectionColor: Theme.color.accent
    selectedTextColor: Theme.color.textOnAccent
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body

    background: Rectangle {
        implicitHeight: Theme.size.fieldHeight
        radius: Theme.size.radius
        color: Theme.color.raised
        border.width: control.activeFocus ? Theme.size.focusWidth : Theme.size.border
        border.color: control.activeFocus ? Theme.color.focus
                    : control.edited ? Theme.color.edited
                    : control.hovered ? Theme.color.borderStrong : Theme.color.border
    }
}
