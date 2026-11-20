import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import LokiMetadata

// The whole page: toolbar, files on the left, metadata on the right.
Item {
    id: view

    function addFiles() { fileDialog.open() }
    function addFolder() { folderDialog.open() }
    function requestRemove() {
        if (editor.selectedCount === 0 || editor.busy) return
        if (editor.selectionHasChanges()) removeDialog.open()
        else editor.removeSelected()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            onAddFilesRequested: view.addFiles()
            onAddFolderRequested: view.addFolder()
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Item {
                id: handle
                implicitWidth: Theme.size.splitHandle
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.size.border
                    height: parent.height
                    color: handle.SplitHandle.hovered || handle.SplitHandle.pressed ? Theme.color.accent : Theme.color.divider
                }
            }

            FileListPane {
                SplitView.preferredWidth: Theme.size.listWidth
                SplitView.minimumWidth: Theme.size.listMinWidth
                onAddFilesRequested: view.addFiles()
                onAddFolderRequested: view.addFolder()
                onRemoveRequested: view.requestRemove()
            }
            DetailsPane {
                SplitView.fillWidth: true
                SplitView.minimumWidth: Theme.size.detailsMinWidth
                onCustomFieldRequested: {
                    customKey.text = ""
                    customDialog.open()
                }
            }
        }

        StatusBar { Layout.fillWidth: true }
    }

    // --- Drag and drop ------------------------------------------------------------
    DropArea {
        id: drop
        anchors.fill: parent
        enabled: !editor.busy
        onEntered: (drag) => drag.accepted = drag.hasUrls
        onDropped: (drag) => {
            if (drag.hasUrls) editor.addUrls(drag.urls)
        }
    }
    DropOverlay {
        anchors.fill: parent
        visible: drop.containsDrag
    }

    // --- Dialogs ------------------------------------------------------------------
    FileDialog {
        id: fileDialog
        title: "Add files"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "Everything it can edit (*.mp3 *.wav *.flac *.ogg *.oga *.opus *.aif *.aiff *.aifc *.m4a *.mp4 *.m4v *.mov *.mkv *.webm *.avi *.png *.jpg *.jpeg *.webp *.docx *.xlsx *.pptx *.odt *.ods *.odp *.pdf)",
            "Audio (*.mp3 *.wav *.flac *.ogg *.oga *.opus *.aif *.aiff *.aifc *.m4a)",
            "Video (*.mp4 *.m4v *.mov *.mkv *.webm *.avi)",
            "Images (*.png *.jpg *.jpeg *.webp)",
            "Documents (*.docx *.xlsx *.pptx *.odt *.ods *.odp *.pdf)"
        ]
        currentFolder: editor.lastFolderUrl   // where files were last added from
        onAccepted: editor.addUrls(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        title: "Add a folder"
        currentFolder: editor.lastFolderParentUrl   // beside the folder last added...
        selectedFolder: editor.lastFolderUrl       // ...with it picked
        onAccepted: editor.addUrls([selectedFolder])
    }

    AppDialog {
        id: removeDialog
        title: "Remove files with changes?"
        message: "Some of the selected files have changes you haven't applied. Removing them from the list forgets those changes. The files themselves stay as they are."
        buttons: [
            AppButton { text: "Cancel"; onClicked: removeDialog.close() },
            AppButton {
                text: "Remove"
                variant: "danger"
                onClicked: {
                    removeDialog.close()
                    editor.removeSelected()
                }
            }
        ]
    }

    AppDialog {
        id: customDialog
        title: "Add a custom field"
        message: "The usual fields are already listed. This is for one of your own, named as the file should store it (for example \"studio\" or \"location\"). MP3, PNG and MKV files take these; if none of the selected files can, you'll be told and nothing is added."
        onOpened: customKey.forceActiveFocus()
        AppTextField {
            id: customKey
            Layout.fillWidth: true
            placeholderText: "Field name"
            onAccepted: addCustom.clicked()
        }
        buttons: [
            AppButton { text: "Cancel"; onClicked: customDialog.close() },
            AppButton {
                id: addCustom
                text: "Add"
                variant: "primary"
                enabled: customKey.text.trim() !== ""
                onClicked: {
                    editor.addField(customKey.text)
                    customDialog.close()
                }
            }
        ]
    }

    // --- Keys -----------------------------------------------------------------------
    Shortcut { sequence: "Ctrl+O"; enabled: !editor.busy; onActivated: view.addFiles() }
    Shortcut { sequence: "Ctrl+Shift+O"; enabled: !editor.busy; onActivated: view.addFolder() }
    Shortcut { sequence: "Ctrl+S"; enabled: editor.modifiedCount > 0 && !editor.busy; onActivated: editor.apply() }
}
