pragma Singleton
import QtQuick

// The theme in use. Components read Theme.color.*, Theme.size.* and
// Theme.font.* and nothing else, so switching `name` restyles everything.
//
// Adding a theme: copy themes/DarkTheme.qml, change the values, list it in
// `available` below (and in CMakeLists.txt).
QtObject {
    readonly property list<ThemeSpec> available: [
        DarkTheme {}
    ]

    property string name: "Dark"

    readonly property ThemeSpec current: {
        for (let i = 0; i < available.length; ++i)
            if (available[i].name === name) return available[i]
        return available[0]
    }

    readonly property ColorTokens color: current.color
    readonly property SizeTokens size: current.size
    readonly property FontTokens font: current.font
}
