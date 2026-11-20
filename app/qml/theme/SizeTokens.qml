import QtQuick

// Spacing, sizes and corner radii. Defaults suit any theme; a theme may
// override them (a compact theme, say).
QtObject {
    property int radiusSmall: 4
    property int radius: 6
    property int radiusLarge: 10

    property int gapSmall: 4
    property int gap: 8
    property int gapLarge: 16
    property int padding: 12
    property int paddingLarge: 20

    property int border: 1
    property int focusWidth: 2
    property int markerWidth: 3          // the bar beside a selected or edited row

    property int toolbarHeight: 56
    property int statusHeight: 30
    property int buttonHeight: 32
    property int fieldHeight: 34
    property int fileRowHeight: 48
    property int fieldLabelWidth: 132
    property int badgeWidth: 42
    property int badgeHeight: 20
    property int icon: 16
    property int iconSmall: 12
    property int previewSize: 96
    property int splitHandle: 6
    property int scrollBar: 6
    property int dot: 8                  // the "changed" dot

    property int listWidth: 380
    property int listMinWidth: 260
    property int detailsMinWidth: 420
    property int detailsMaxWidth: 860     // the fields stop growing past this
    property int dialogWidth: 440
    property int menuWidth: 220
}
