import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import LokiMetadata

// The left side: the files. Click to pick one; Ctrl and Shift pick several.
Rectangle {
    id: pane
    color: Theme.color.pane

    signal addFilesRequested()
    signal addFolderRequested()
    signal removeRequested()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: how many, and what to do with the selection.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.size.fieldHeight + Theme.size.padding
            Layout.leftMargin: Theme.size.padding
            Layout.rightMargin: Theme.size.gap
            spacing: Theme.size.gapSmall
            visible: editor.count > 0

            SectionHeading {
                text: editor.count === 1 ? "1 file" : editor.count + " files"
            }
            Text {
                visible: editor.selectedCount > 1
                text: "·  " + editor.selectedCount + " selected"
                color: Theme.color.textFaint
                font.family: Theme.font.family
                font.pixelSize: Theme.font.small
            }
            Item { Layout.fillWidth: true }
            IconButton {
                glyph: Icons.selectAll
                tip: "Select all (Ctrl+A)"
                onClicked: editor.selectAll()
            }
            IconButton {
                objectName: "removeButton"
                glyph: Icons.remove
                tip: "Remove the selected files from the list (Delete). The files themselves stay as they are."
                enabled: editor.selectedCount > 0 && !editor.busy
                onClicked: pane.removeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.size.border
            color: Theme.color.divider
            visible: editor.count > 0
        }

        ListView {
            id: list
            objectName: "fileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: editor.count > 0
            clip: true
            focus: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            model: editor.files
            ScrollBar.vertical: AppScrollBar {}

            delegate: FileRow {
                width: ListView.view.width
                onClicked: (index, modifiers) => {
                    list.forceActiveFocus()
                    editor.click(index, modifiers)
                }
            }

            Keys.onUpPressed: (e) => editor.step(-1, e.modifiers & Qt.ShiftModifier)
            Keys.onDownPressed: (e) => editor.step(1, e.modifiers & Qt.ShiftModifier)
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                    editor.selectAll()
                    e.accepted = true
                } else if (e.key === Qt.Key_Delete) {
                    pane.removeRequested()
                    e.accepted = true
                } else if (e.key === Qt.Key_Home) {
                    editor.click(0, e.modifiers & Qt.ShiftModifier)
                    e.accepted = true
                } else if (e.key === Qt.Key_End) {
                    editor.click(editor.count - 1, e.modifiers & Qt.ShiftModifier)
                    e.accepted = true
                }
            }

            Connections {
                target: editor
                function onSelectionChanged() {
                    if (editor.currentRow >= 0) list.positionViewAtIndex(editor.currentRow, ListView.Contain)
                }
            }
        }

        // Nothing yet: say what to do.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: editor.count === 0

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.size.gapLarge
                radius: Theme.size.radiusLarge
                color: "transparent"
                border.width: Theme.size.border
                border.color: Theme.color.borderStrong

                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.size.paddingLarge
                    spacing: Theme.size.gap

                    Glyph {
                        Layout.alignment: Qt.AlignHCenter
                        text: Icons.download
                        color: Theme.color.textFaint
                        font.pixelSize: Theme.font.heading + Theme.size.gap
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.size.gap
                        text: "Drop files or folders here"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.color.text
                        font.family: Theme.font.family
                        font.pixelSize: Theme.font.title
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Music, video, pictures, Office documents and PDFs"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.color.textFaint
                        font.family: Theme.font.family
                        font.pixelSize: Theme.font.small
                        wrapMode: Text.Wrap
                    }
                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: Theme.size.padding
                        spacing: Theme.size.gap
                        AppButton { text: "Add files"; glyph: Icons.file; onClicked: pane.addFilesRequested() }
                        AppButton { text: "Add folder"; glyph: Icons.folder; onClicked: pane.addFolderRequested() }
                    }
                }
            }
        }
    }
}
