import QtQuick
import QtQuick.Layouts
import LokiMetadata

// Content Credentials: the signed record of where a file came from (which AI
// tool or camera made it), embedded by the program that made it. Shown, and
// removable on Apply; never edited, since it is signed by its maker.
ColumnLayout {
    id: section
    property var file          // editor.current, with one file selected
    property bool single

    readonly property var cr: single && file ? (file.credentials ?? ({})) : ({})
    readonly property bool dropping: single ? (file?.dropCredentials ?? false)
                                            : editor.credentialsCount > 0 && editor.credentialsDropping === editor.credentialsCount
    readonly property color valueColor: dropping ? Theme.color.textFaint : Theme.color.textMuted

    visible: single ? (cr.present ?? false) : editor.credentialsCount > 0
    spacing: Theme.size.gap

    Rectangle {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.size.padding
        implicitHeight: Theme.size.border
        color: Theme.color.divider
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.size.gap
        SectionHeading {
            Layout.fillWidth: true
            text: "Content Credentials"
        }
        AppButton {
            objectName: "credentialsButton"
            variant: "subtle"
            implicitHeight: Theme.size.badgeHeight + Theme.size.gapSmall
            leftPadding: Theme.size.gap
            rightPadding: Theme.size.gap
            font.pixelSize: Theme.font.small
            glyph: section.dropping ? Icons.undo : Icons.remove
            text: section.dropping ? "Keep them"
                : section.single || editor.credentialsCount === 1 ? "Remove"
                : "Remove from all " + editor.credentialsCount
            enabled: !editor.busy
            onClicked: section.dropping ? editor.keepCredentials() : editor.removeCredentials()
        }
    }

    Text {
        Layout.fillWidth: true
        text: "A signed record of where this file came from, added by the program that made it. "
            + "You can read it and remove it here, but not change it. (Its signature isn't checked here.)"
        wrapMode: Text.Wrap
        color: Theme.color.textFaint
        font.family: Theme.font.family
        font.pixelSize: Theme.font.small
    }

    // To be removed on Apply.
    Rectangle {
        visible: section.dropping
        Layout.fillWidth: true
        Layout.topMargin: Theme.size.gapSmall
        implicitHeight: pendingText.implicitHeight + 2 * (Theme.size.gap + Theme.size.gapSmall)
        radius: Theme.size.radius
        color: Theme.color.editedSoft
        Text {
            id: pendingText
            anchors.fill: parent
            anchors.margins: Theme.size.gap + Theme.size.gapSmall
            text: (section.single || editor.credentialsCount === 1 ? "They'll be removed" : "They'll be removed from "
                   + editor.credentialsCount + " files") + " when you press Apply. Nothing else in the file changes."
            wrapMode: Text.Wrap
            color: Theme.color.text
            font.family: Theme.font.family
            font.pixelSize: Theme.font.body
        }
    }

    // Several files: how many have them (the list marks which).
    Text {
        visible: !section.single
        Layout.fillWidth: true
        Layout.topMargin: Theme.size.gapSmall
        text: editor.credentialsCount + " of the " + editor.selectedCount + " selected files "
            + (editor.credentialsCount === 1 ? "has them" : "have them")
            + ". Files marked AI or CR in the list carry them; select one to read its record."
        wrapMode: Text.Wrap
        color: Theme.color.textMuted
        font.family: Theme.font.family
        font.pixelSize: Theme.font.body
    }

    // One file: what the record says.
    RowLayout {
        visible: section.single && (section.cr.source ?? "") !== ""
        Layout.topMargin: Theme.size.gapSmall
        spacing: Theme.size.gap
        CredentialsChip { ai: section.cr.ai ?? false }
        Text {
            text: section.cr.source ?? ""
            color: section.dropping ? Theme.color.textFaint : Theme.color.text
            font.family: Theme.font.family
            font.pixelSize: Theme.font.body
            font.weight: Theme.font.strong
            font.strikeout: section.dropping
        }
    }

    GridLayout {
        visible: section.single
        Layout.fillWidth: true
        Layout.topMargin: Theme.size.gapSmall
        columns: 2
        columnSpacing: Theme.size.gap
        rowSpacing: Theme.size.gap

        Repeater {
            model: {
                const c = section.cr
                const rows = []
                const add = (label, text) => { if (text) rows.push({ label: label, text: text }) }
                add("Made with", c.tool)
                add("Maker", c.provider)
                add("Author", c.author)
                if (c.recordedBy && c.recordedBy !== c.tool) add("Recorded by", c.recordedBy)
                add("Signed by", c.signer)
                add("When", c.when)
                add("Steps", (c.actions ?? []).join("\n"))
                if ((c.history ?? 0) > 0)
                    add("Earlier records", c.history === 1 ? "1, from an earlier step" : c.history + ", from earlier steps")
                add("Since signing", c.match === "unchanged" ? "Unchanged: the file is exactly as it was signed"
                                   : c.match === "changed" ? "Changed (editing its metadata counts too); the record still says what it said"
                                   : "")
                return rows
            }
            delegate: RowLayout {
                required property var modelData
                Layout.columnSpan: 2
                Layout.fillWidth: true
                spacing: Theme.size.gap
                Text {
                    Layout.preferredWidth: Theme.size.fieldLabelWidth - Theme.size.padding
                    Layout.alignment: Qt.AlignTop
                    text: modelData.label
                    elide: Text.ElideRight
                    color: Theme.color.textFaint
                    font.family: Theme.font.family
                    font.pixelSize: Theme.font.body
                }
                TextEdit {
                    Layout.fillWidth: true
                    text: modelData.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: section.valueColor
                    selectionColor: Theme.color.accent
                    selectedTextColor: Theme.color.textOnAccent
                    font.family: Theme.font.family
                    font.pixelSize: Theme.font.body
                    font.strikeout: section.dropping
                }
            }
        }
    }

    Repeater {
        model: section.single ? (section.cr.notes ?? []) : []
        delegate: Text {
            required property string modelData
            Layout.fillWidth: true
            Layout.topMargin: Theme.size.gapSmall
            text: modelData
            wrapMode: Text.Wrap
            color: Theme.color.textFaint
            font.family: Theme.font.family
            font.pixelSize: Theme.font.small
        }
    }
}
