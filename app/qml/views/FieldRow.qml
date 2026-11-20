import QtQuick
import QtQuick.Layouts
import LokiMetadata

// One field: its name, its value to edit, undo and remove.
Item {
    id: row
    required property string key
    required property string label
    required property string value
    required property bool mixed
    required property bool edited
    required property bool multiline
    required property int appliesTo
    required property int total

    // Fewer files than selected can hold this field (an album on a photo...).
    readonly property bool partial: total > 1 && appliesTo < total

    implicitHeight: Math.max(Theme.size.fieldHeight, input.item ? input.item.implicitHeight : 0)

    HoverHandler { id: hover }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.size.gap

        // The name, with a dot when it holds a change.
        Item {
            Layout.preferredWidth: Theme.size.fieldLabelWidth
            Layout.fillHeight: true
            Rectangle {
                visible: row.edited
                width: Theme.size.dot - 2
                height: width
                radius: width / 2
                color: Theme.color.edited
                y: (Theme.size.fieldHeight - height) / 2
            }
            ColumnLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.size.padding
                anchors.right: parent.right
                height: Theme.size.fieldHeight
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: row.label
                    elide: Text.ElideRight
                    color: row.edited ? Theme.color.edited : Theme.color.textMuted
                    font.family: Theme.font.family
                    font.pixelSize: Theme.font.body
                }
                Text {
                    id: scope
                    visible: row.partial
                    Layout.fillWidth: true
                    text: row.appliesTo + " of " + row.total + " files"
                    color: Theme.color.textFaint
                    font.family: Theme.font.family
                    font.pixelSize: Theme.font.small
                    HoverHandler { id: scopeHover }
                    AppToolTip {
                        visible: scopeHover.hovered
                        text: "Only " + row.appliesTo + " of the " + row.total + " selected files can hold " + row.label
                              + ". The others are left as they are."
                    }
                }
            }
        }

        Loader {
            id: input
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            sourceComponent: row.key === "rating" ? stars : row.multiline ? area : line
        }

        IconButton {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.size.gapSmall / 2
            glyph: Icons.undo
            tip: "Undo the change to " + row.label
            opacity: row.edited ? 1 : 0
            enabled: row.edited
            onClicked: editor.revertField(row.key)
        }
        IconButton {
            objectName: "removeField"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.size.gapSmall / 2
            glyph: Icons.close
            tip: editor.selectedCount > 1 ? "Clear " + row.label + " in the selected files" : "Clear " + row.label
            readonly property bool clearable: row.value !== "" || row.mixed
            opacity: clearable && (hover.hovered || activeFocus) ? 1 : 0
            enabled: clearable
            onClicked: editor.removeField(row.key)
        }
    }

    // Where values differ: a grey "Different values" until you click in.
    readonly property string differentHint: "Different values"

    Component {
        id: line
        AppTextField {
            objectName: "field_" + row.key
            text: row.value
            edited: row.edited
            placeholderText: row.mixed && !activeFocus ? row.differentHint : ""
            onTextEdited: editor.setField(row.key, text)
        }
    }

    Component {
        id: area
        AppTextArea {
            objectName: "field_" + row.key
            text: row.value
            edited: row.edited
            placeholderText: row.mixed && !activeFocus ? row.differentHint : ""
            onTextChanged: if (activeFocus && text !== row.value) editor.setField(row.key, text)
        }
    }

    // Rating: five stars. Click one to set it; click the lit one again to clear.
    Component {
        id: stars
        Item {
            id: starBox
            objectName: "field_" + row.key
            implicitHeight: Theme.size.fieldHeight
            readonly property int current: row.mixed ? 0 : (parseInt(row.value) || 0)
            property int hover: 0

            Row {
                id: starRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.size.gapSmall
                Repeater {
                    model: 5
                    delegate: Glyph {
                        required property int index
                        readonly property bool lit: index < (starBox.hover > 0 ? starBox.hover : starBox.current)
                        text: lit ? Icons.starFilled : Icons.star
                        color: lit ? Theme.color.rating : Theme.color.textFaint
                        font.pixelSize: Theme.size.icon + Theme.size.gapSmall
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.size.gapSmall / 2
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: starBox.hover = index + 1
                            onExited: starBox.hover = 0
                            onClicked: editor.setField(row.key, index + 1 === starBox.current ? "" : String(index + 1))
                        }
                    }
                }
            }
            Text {
                anchors.left: starRow.right
                anchors.leftMargin: Theme.size.padding
                anchors.verticalCenter: parent.verticalCenter
                visible: row.mixed && starBox.hover === 0
                text: row.differentHint
                color: Theme.color.placeholder
                font.family: Theme.font.family
                font.pixelSize: Theme.font.body
            }
        }
    }
}
