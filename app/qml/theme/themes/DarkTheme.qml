import QtQuick

// The dark theme. Neutral greys with a slight cool cast, one blue accent,
// amber for changes that haven't been applied yet.
// (Colours with transparency are written #AARRGGBB.)
ThemeSpec {
    name: "Dark"
    dark: true
    logo: Qt.resolvedUrl("../../images/logo.png")

    color: ColorTokens {
        window: "#111215"
        pane: "#16171b"
        raised: "#1d1f24"
        overlay: "#23252b"
        hover: "#22242a"
        pressed: "#2a2d34"
        selection: "#1d2740"
        selectionHover: "#222d4a"
        divider: "#23252b"
        border: "#2c2f37"
        borderStrong: "#3b3f49"
        focus: "#6e8cff"
        scrim: "#8c000000"
        dropTint: "#265b7cfa"

        text: "#e8e9ed"
        textMuted: "#a2a5af"
        textFaint: "#6d717c"
        placeholder: "#5e626d"
        textOnAccent: "#ffffff"

        accent: "#5b7cfa"
        accentHover: "#6e8cff"
        accentPressed: "#4b6aea"
        accentSoft: "#295b7cfa"

        edited: "#e6b35c"
        editedSoft: "#24e6b35c"
        danger: "#f0717a"
        dangerSoft: "#24f0717a"
        success: "#62cf91"
        warning: "#e6b35c"
        warningSoft: "#24e6b35c"
        rating: "#f2c14e"

        audio: "#b596f2"
        audioSoft: "#26b596f2"
        video: "#56c2e8"
        videoSoft: "#2656c2e8"
        image: "#80d183"
        imageSoft: "#2680d183"
        document: "#ef9f6e"
        documentSoft: "#26ef9f6e"
        model: "#d9c27a"
        modelSoft: "#26d9c27a"
        credentials: "#ee8fcf"
        credentialsSoft: "#26ee8fcf"
    }
}
