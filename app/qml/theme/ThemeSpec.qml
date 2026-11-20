import QtQuick

// What a theme is: a name, whether it is dark (for the window frame), and
// its colours, sizes and fonts. Each file in themes/ is one of these.
QtObject {
    property string name
    property bool dark: true
    property ColorTokens color: ColorTokens {}
    property SizeTokens size: SizeTokens {}
    property FontTokens font: FontTokens {}
}
