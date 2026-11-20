import QtQuick

// Typefaces and type sizes.
QtObject {
    property string family: "Segoe UI"
    property string iconFamily: "Segoe MDL2 Assets"   // the icon glyphs in Icons.qml come from this font
    property string monoFamily: "Consolas"

    property int small: 11
    property int body: 13
    property int title: 15
    property int heading: 20

    property int normal: Font.Normal
    property int strong: Font.DemiBold
    property real capsSpacing: 0.6        // letter spacing of the small section headings
}
