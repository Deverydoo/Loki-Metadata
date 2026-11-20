import QtQuick
import LokiMetadata

// A small square button with just an icon (and a tooltip saying what it does).
AppButton {
    variant: "subtle"
    implicitHeight: Theme.size.fieldHeight - Theme.size.gapSmall
    implicitWidth: implicitHeight
}
