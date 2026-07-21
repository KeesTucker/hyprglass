// JFA test shapes: a filled circle and a torus (annulus) on one layer
// surface, centered on screen. Both are drawn mostly-transparent so the
// glass refraction underneath stays visible; the torus adds a concave
// (hole) boundary the box SDF could never represent, and having both
// shapes in one surface also exercises the multi-lobe seam case.
//
// Run: qs -p test/jfa-shapes/shell.qml
import Quickshell
import Quickshell.Wayland
import QtQuick

ShellRoot {
    PanelWindow {
        color: "transparent"
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Top
        WlrLayershell.namespace: "hyprglass-jfa-test"
        implicitWidth: 900
        implicitHeight: 420
        // No anchors on either axis -> layer-shell centers the surface.

        Row {
            anchors.centerIn: parent
            spacing: 120

            Rectangle {
                width: 300
                height: 300
                radius: 150
                color: "#33ffffff"
            }

            Rectangle {
                width: 300
                height: 300
                radius: 150
                color: "transparent"
                border.width: 70
                border.color: "#33ffffff"
            }
        }
    }
}
