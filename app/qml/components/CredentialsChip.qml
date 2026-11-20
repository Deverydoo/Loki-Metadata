import QtQuick
import LokiMetadata

// Marks a file with Content Credentials: "AI" when they say AI made it,
// "CR" otherwise.
Rectangle {
    id: chip
    property bool ai: false

    implicitWidth: label.implicitWidth + 2 * Theme.size.gap
    implicitHeight: Theme.size.badgeHeight - Theme.size.gapSmall
    radius: Theme.size.radiusSmall
    color: Theme.color.credentialsSoft

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.ai ? "AI" : "CR"
        color: Theme.color.credentials
        font.family: Theme.font.family
        font.pixelSize: Theme.font.small
        font.weight: Theme.font.strong
        font.letterSpacing: Theme.font.capsSpacing
    }
}
