import QtQuick 2.0
import QOneRender

Window {
    id: root
    width: 1920
    height: 1080
    visible: true
    title: "Render Vulkan into Qml Application"

    Rectangle {
        id: bg
        anchors.fill: parent
        color: "black"       
    }

    Viewport {
        id: renderer
        width: height * (root.width / root.height)
        height: root.height

        SequentialAnimation on t {
            NumberAnimation { to: 1; duration: 2500; easing.type: Easing.InQuad }
            NumberAnimation { to: 0; duration: 2500; easing.type: Easing.OutQuad }
            loops: Animation.Infinite
            running: true
        }
    }

//    SequentialAnimation {
//        id:anim
//        property int xRes: 1920
//        property double beltVelocity: 300 // mm/s
//        property double integrationTime: 0.004 // s
//        property double sensorX: 1.2

//        property double beltDuration: (xRes / (beltVelocity / sensorX)) * 1000

//        NumberAnimation { target: renderer; property: "x"; to: (renderer.width * 2); duration: anim.beltDuration }
//        running: true
//        loops: Animation.Infinite
//    }

    Rectangle {
        id: labelFrame
        anchors.margins: -10
        radius: 5
        color: "white"
        border.color: "black"
        opacity: 0.5
        anchors.fill: label
    }

    Text {
        id: label
        anchors.bottom: bg.bottom
        anchors.left: bg.left
        anchors.right: bg.right
        anchors.margins: 20
        wrapMode: Text.WordWrap
        text: "The squircle, using rendering code borrowed from the vulkanunderqml example, is rendered into a texture directly with Vulkan. The VkImage is then imported and used in a custom Qt Quick item."
    }
}
