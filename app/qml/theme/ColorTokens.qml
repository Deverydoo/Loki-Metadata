import QtQuick

// Every colour the app uses. A theme fills these in; nothing outside the
// theme folder names a colour, so a new theme is one new file.
QtObject {
    // Surfaces, back to front
    property color window          // behind everything
    property color pane            // the two main panes
    property color raised          // inputs, cards
    property color overlay         // menus, dialogs, tooltips
    property color hover
    property color pressed
    property color selection       // selected rows
    property color selectionHover
    property color divider         // hairlines between areas
    property color border          // around inputs and buttons
    property color borderStrong    // hovered borders
    property color focus           // keyboard focus ring
    property color scrim           // behind a dialog
    property color dropTint        // over the window while files are dragged in

    // Text
    property color text
    property color textMuted
    property color textFaint
    property color placeholder
    property color textOnAccent

    // The accent: the main button, selection marks, links
    property color accent
    property color accentHover
    property color accentPressed
    property color accentSoft

    // Meaning
    property color edited          // a change waiting for Apply
    property color editedSoft
    property color danger
    property color dangerSoft
    property color success
    property color warning
    property color warningSoft
    property color rating          // lit stars

    // File kinds (the badges in the list)
    property color audio
    property color audioSoft
    property color video
    property color videoSoft
    property color image
    property color imageSoft
    property color document
    property color documentSoft
}
