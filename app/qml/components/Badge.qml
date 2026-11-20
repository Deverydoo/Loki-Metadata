import QtQuick
import LokiMetadata

// The file type chip in the list: "MP3", "MP4", "JPG"...
Rectangle {
    id: badge
    property string text
    property string kind   // audio, video, image, document or model

    readonly property color tint: kind === "audio" ? Theme.color.audio : kind === "video" ? Theme.color.video
                              : kind === "document" ? Theme.color.document : kind === "model" ? Theme.color.model
                              : Theme.color.image

    implicitWidth: Theme.size.badgeWidth
    implicitHeight: Theme.size.badgeHeight
    radius: Theme.size.radiusSmall
    color: kind === "audio" ? Theme.color.audioSoft : kind === "video" ? Theme.color.videoSoft
         : kind === "document" ? Theme.color.documentSoft : kind === "model" ? Theme.color.modelSoft
         : Theme.color.imageSoft

    Text {
        anchors.centerIn: parent
        text: badge.text
        color: badge.tint
        font.family: Theme.font.family
        font.pixelSize: Theme.font.small
        font.weight: Theme.font.strong
        font.letterSpacing: Theme.font.capsSpacing
    }
}
