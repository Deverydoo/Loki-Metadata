import QtQuick
import QtQuick.Controls.Basic
import LokiMetadata

ApplicationWindow {
    id: window
    width: 1240
    height: 780
    minimumWidth: Theme.size.listMinWidth + Theme.size.detailsMinWidth + Theme.size.splitHandle
    minimumHeight: 480
    visible: true
    // "Loki Metadata 0.1.0", and how many files have changes waiting.
    title: "Loki Metadata " + Qt.application.version + (editor.modifiedCount > 0 ? "  •  " + editor.modifiedCount + " unsaved" : "")
    color: Theme.color.window
    font.family: Theme.font.family
    font.pixelSize: Theme.font.body

    // A dark theme gets a dark title bar.
    Binding {
        target: Application.styleHints
        property: "colorScheme"
        value: Theme.current.dark ? Qt.ColorScheme.Dark : Qt.ColorScheme.Light
    }

    MainView { anchors.fill: parent }

    // Closing with changes not applied: ask.
    property bool closeAnyway: false
    property bool closeAfterApply: false

    onClosing: (close) => {
        if (editor.busy) {
            close.accepted = false
            return
        }
        if (editor.modifiedCount > 0 && !closeAnyway) {
            close.accepted = false
            closeDialog.open()
        }
    }

    AppDialog {
        id: closeDialog
        title: "Apply your changes first?"
        message: (editor.modifiedCount === 1 ? "1 file has changes" : editor.modifiedCount + " files have changes")
                 + " that haven't been written yet."
        buttons: [
            AppButton {
                text: "Close without saving"
                variant: "subtle"
                onClicked: {
                    window.closeAnyway = true
                    window.close()
                }
            },
            AppButton { text: "Cancel"; onClicked: closeDialog.close() },
            AppButton {
                text: "Apply and close"
                variant: "primary"
                onClicked: {
                    closeDialog.close()
                    window.closeAfterApply = true
                    editor.apply()
                }
            }
        ]
    }

    Connections {
        target: editor
        function onBusyChanged() {
            if (editor.busy || !window.closeAfterApply) return
            window.closeAfterApply = false
            if (editor.modifiedCount === 0) window.close()   // anything that failed keeps the window open
        }
    }
}
