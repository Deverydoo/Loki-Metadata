import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import LokiMetadata

// The right side: the selected file's metadata, or the fields the selected
// files share.
Rectangle {
    id: pane
    color: Theme.color.window

    signal customFieldRequested()

    readonly property var file: editor.current
    readonly property bool single: editor.selectedCount === 1
    readonly property bool multi: editor.selectedCount > 1
    readonly property bool ready: multi || (single && file.state === "ready") || (single && file.state === "saving")

    // Nothing selected.
    Text {
        anchors.centerIn: parent
        visible: editor.selectedCount === 0
        text: editor.count === 0 ? "Add some files to see their metadata here." : "Select a file to see its metadata."
        color: Theme.color.textFaint
        font.family: Theme.font.family
        font.pixelSize: Theme.font.body
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        visible: editor.selectedCount > 0
        contentWidth: availableWidth
        ScrollBar.vertical: AppScrollBar { parent: scroll; x: scroll.width - width; height: scroll.height }
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: Math.min(scroll.availableWidth, Theme.size.detailsMaxWidth)
            spacing: 0

            // --- Header ---------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.size.paddingLarge
                Layout.bottomMargin: Theme.size.padding
                spacing: Theme.size.padding + Theme.size.gapSmall

                // A picture of the image, when one image is selected.
                Rectangle {
                    visible: pane.single && pane.file.kind === "image" && preview.status !== Image.Error   // formats Qt can draw
                    Layout.preferredWidth: Theme.size.previewSize
                    Layout.preferredHeight: Theme.size.previewSize
                    radius: Theme.size.radius
                    color: Theme.color.raised
                    border.width: Theme.size.border
                    border.color: Theme.color.border
                    clip: true
                    Image {
                        id: preview
                        anchors.fill: parent
                        anchors.margins: Theme.size.border
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        sourceSize.width: Theme.size.previewSize * 2
                        sourceSize.height: Theme.size.previewSize * 2
                        source: pane.single && pane.file.kind === "image" ? "file:///" + pane.file.path : ""
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.size.gapSmall

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.size.gap + Theme.size.gapSmall
                        Badge {
                            visible: pane.single
                            text: pane.file.badge ?? ""
                            kind: pane.file.kind ?? ""
                        }
                        Text {
                            Layout.fillWidth: true
                            text: pane.single ? (pane.file.name ?? "") : editor.selectedCount + " files selected"
                            elide: Text.ElideMiddle
                            color: Theme.color.text
                            font.family: Theme.font.family
                            font.pixelSize: Theme.font.heading
                            font.weight: Theme.font.strong
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.size.gap
                        Text {
                            Layout.fillWidth: true
                            text: pane.single ? (pane.file.folder ?? "") : editor.selectionSummary
                            elide: Text.ElideMiddle
                            color: Theme.color.textMuted
                            font.family: Theme.font.family
                            font.pixelSize: Theme.font.small
                        }
                        AppButton {
                            id: showButton
                            visible: pane.single
                            variant: "subtle"
                            implicitHeight: Theme.size.badgeHeight + Theme.size.gapSmall
                            leftPadding: Theme.size.gap
                            rightPadding: Theme.size.gap
                            font.pixelSize: Theme.font.small
                            text: "Show in folder"
                            onClicked: editor.showInFolder(pane.file.path)
                        }
                    }
                    Text {
                        visible: pane.multi
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.size.gapSmall
                        text: "Every field these files can hold. Where they differ, the box says Different values; type there to make them all the same. Apply writes each field only to the files that can hold it."
                        wrapMode: Text.Wrap
                        color: Theme.color.textFaint
                        font.family: Theme.font.family
                        font.pixelSize: Theme.font.small
                    }
                }
            }

            // --- News about the file -------------------------------------
            Banner {
                visible: pane.single && (pane.file.error ?? "") !== ""
                Layout.fillWidth: true
                Layout.leftMargin: Theme.size.paddingLarge
                Layout.rightMargin: Theme.size.paddingLarge
                Layout.bottomMargin: Theme.size.padding
                kind: "error"
                text: pane.file.state === "failed" ? "Can't read this file. " + (pane.file.error ?? "")
                                                    : "The last save didn't work: " + (pane.file.error ?? "")
            }
            Banner {
                visible: pane.single && (pane.file.warning ?? "") !== ""
                Layout.fillWidth: true
                Layout.leftMargin: Theme.size.paddingLarge
                Layout.rightMargin: Theme.size.paddingLarge
                Layout.bottomMargin: Theme.size.padding
                kind: "warning"
                text: pane.file.warning ?? ""
            }
            Text {
                visible: pane.single && pane.file.state === "loading"
                Layout.leftMargin: Theme.size.paddingLarge
                text: "Reading the file..."
                color: Theme.color.textFaint
                font.family: Theme.font.family
                font.pixelSize: Theme.font.body
            }

            // --- The fields -----------------------------------------------
            ColumnLayout {
                visible: pane.ready
                Layout.fillWidth: true
                Layout.leftMargin: Theme.size.paddingLarge - Theme.size.padding
                Layout.rightMargin: Theme.size.paddingLarge - Theme.size.gap
                spacing: Theme.size.gap

                SectionHeading {
                    Layout.leftMargin: Theme.size.padding
                    Layout.topMargin: Theme.size.gap
                    Layout.bottomMargin: Theme.size.gapSmall
                    text: "Metadata"
                }

                Repeater {
                    id: fieldRepeater
                    model: editor.fields
                    delegate: FieldRow { Layout.fillWidth: true }
                }

                // Beyond the usual fields: one of your own, where the format allows.
                AppButton {
                    objectName: "customFieldButton"
                    Layout.leftMargin: Theme.size.padding - Theme.size.gap
                    Layout.topMargin: Theme.size.gapSmall
                    variant: "subtle"
                    glyph: Icons.add
                    text: "Add a custom field"
                    font.pixelSize: Theme.font.small
                    enabled: !editor.busy
                    onClicked: pane.customFieldRequested()
                }
            }

            // --- File info (one file) -------------------------------------
            ColumnLayout {
                visible: pane.single && pane.file.state !== "loading" && (pane.file.info ?? []).length > 0
                Layout.fillWidth: true
                Layout.margins: Theme.size.paddingLarge
                Layout.topMargin: Theme.size.paddingLarge + Theme.size.gap
                spacing: Theme.size.gap

                Rectangle {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.size.padding
                    implicitHeight: Theme.size.border
                    color: Theme.color.divider
                }
                SectionHeading {
                    Layout.bottomMargin: Theme.size.gapSmall
                    text: "File info"
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.size.gap
                    rowSpacing: Theme.size.gap
                    Repeater {
                        model: pane.single ? pane.file.info : []
                        delegate: Text {
                            required property var modelData
                            required property int index
                            Layout.row: index
                            Layout.column: 0
                            Layout.preferredWidth: Theme.size.fieldLabelWidth - Theme.size.padding
                            Layout.alignment: Qt.AlignTop
                            text: modelData.label
                            elide: Text.ElideRight
                            color: Theme.color.textFaint
                            font.family: Theme.font.family
                            font.pixelSize: Theme.font.body
                        }
                    }
                    Repeater {
                        model: pane.single ? pane.file.info : []
                        delegate: TextEdit {
                            required property var modelData
                            required property int index
                            Layout.row: index
                            Layout.column: 1
                            Layout.fillWidth: true
                            text: modelData.text
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.color.textMuted
                            selectionColor: Theme.color.accent
                            selectedTextColor: Theme.color.textOnAccent
                            font.family: Theme.font.family
                            font.pixelSize: Theme.font.body
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.size.paddingLarge }
        }
    }
}
