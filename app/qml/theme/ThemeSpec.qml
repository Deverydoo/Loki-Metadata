import QtQuick

// What a theme is: a name, whether it is dark (for the window frame), its
// colours, sizes and fonts, and the logo drawn for it. Each file in themes/
// is one of these.
QtObject {
    property string name
    property bool dark: true
    property url logo                 // the header's logo, made for this theme's background
    property ColorTokens color: ColorTokens {}
    property SizeTokens size: SizeTokens {}
    property FontTokens font: FontTokens {}
}
