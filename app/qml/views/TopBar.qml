import QtQuick
import QtQuick.Layouts
import LokiMetadata

// Adding files on the left; the waiting changes and Apply on the right.
Rectangle {
    id: bar
    implicitHeight: Theme.size.toolbarHeight
    color: Theme.color.pane

    signal addFilesRequested()
    signal addFolderRequested()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.size.padding
        anchors.rightMargin: Theme.size.padding
        spacing: Theme.size.gap

        Image {
            objectName: "headerLogo"
            Layout.rightMargin: Theme.size.padding
            Layout.preferredHeight: Theme.size.logoHeight
            Layout.preferredWidth: Theme.size.logoHeight * (implicitHeight > 0 ? implicitWidth / implicitHeight : 3)
            source: Theme.logo
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            Accessible.name: "Loki Metadata"
        }
        AppButton {
            text: "Add files"
            glyph: Icons.file
            tip: "Add files (Ctrl+O)"
            enabled: !editor.busy
            onClicked: bar.addFilesRequested()
        }
        AppButton {
            text: "Add folder"
            glyph: Icons.folder
            tip: "Add every supported file in a folder and its subfolders (Ctrl+Shift+O)"
            enabled: !editor.busy
            onClicked: bar.addFolderRequested()
        }

        Item { Layout.fillWidth: true }

        Row {
            visible: editor.modifiedCount > 0
            spacing: Theme.size.gap
            Layout.rightMargin: Theme.size.gap
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.size.dot
                height: width
                radius: width / 2
                color: Theme.color.edited
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: editor.modifiedCount === 1 ? "Changes in 1 file" : "Changes in " + editor.modifiedCount + " files"
                color: Theme.color.textMuted
                font.family: Theme.font.family
                font.pixelSize: Theme.font.body
            }
        }
        AppButton {
            objectName: "discardButton"
            variant: "subtle"
            text: "Discard"
            tip: "Forget every change that hasn't been applied"
            visible: editor.modifiedCount > 0
            enabled: !editor.busy
            onClicked: editor.revertAll()
        }
        AppButton {
            objectName: "applyButton"
            variant: "primary"
            glyph: Icons.check
            text: editor.busy ? "Saving..." : "Apply"
            tip: "Write the changes into the files (Ctrl+S)"
            enabled: editor.modifiedCount > 0 && !editor.busy
            onClicked: editor.apply()
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.size.border
        color: Theme.color.divider
    }
}
