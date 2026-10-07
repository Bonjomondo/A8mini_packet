import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 760
    minimumWidth: 800
    minimumHeight: 560
    title: "A8 mini · Video Probe"
    color: "#10151f"

    header: ToolBar {
        background: Rectangle { color: "#1a2230" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20; anchors.rightMargin: 20
            Label { text: "A8 mini  /  视频 Probe"; color: "#f1f5f9"; font.pixelSize: 20; font.bold: true }
            Item { Layout.fillWidth: true }
            Rectangle { width: 9; height: 9; radius: 5; color: a8Video.state === "PLAYING" ? "#4ade80" : "#fbbf24" }
            Label { text: a8Video.state; color: "#f1f5f9"; font.bold: true }
        }
    }
    RowLayout {
        anchors.fill: parent; anchors.margins: 18; spacing: 18
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.preferredWidth: 800; Layout.minimumWidth: 400
            color: "black"; border.color: "#293548"; radius: 4
            Loader {
                anchors.fill: parent; anchors.margins: 1
                // Do not import the GL module in software mode or when missing.
                active: a8Video.available
                source: active ? "qrc:/VideoSurface.qml" : ""
            }
            Rectangle { anchors.centerIn: parent; width: 52; height: 2; color: "#ff5252" }
            Rectangle { anchors.centerIn: parent; width: 2; height: 52; color: "#ff5252" }
            Rectangle {
                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 14
                width: badge.implicitWidth + 24; height: 32; color: "#bb10151f"; radius: 4
                Label { id: badge; anchors.centerIn: parent; text: "GStreamer · QML  |  " + a8Video.state; color: "white" }
            }
            Label {
                anchors.centerIn: parent; anchors.verticalCenterOffset: 56
                width: parent.width - 40; horizontalAlignment: Text.AlignHCenter
                text: a8Video.state === "PLAYING" ? "" : a8Video.lastError || "等待视频…"
                color: "#cbd5e1"; wrapMode: Text.WordWrap
            }
        }
        ColumnLayout {
            Layout.preferredWidth: 280; Layout.minimumWidth: 280; Layout.maximumWidth: 300
            Layout.fillWidth: false; Layout.fillHeight: true; spacing: 12
            Label { text: "连接视频"; font.pixelSize: 18; font.bold: true; color: "#f1f5f9" }
            Label { text: "RTSP 地址"; color: "#94a3b8" }
            TextField { id: url; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 280; text: initialUrl; selectByMouse: true }
            Label { text: "用户名"; color: "#94a3b8" }
            TextField { id: username; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 280; text: initialUsername; selectByMouse: true }
            Label { text: "密码"; color: "#94a3b8" }
            TextField { id: password; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 280; text: initialPassword; echoMode: TextInput.Password; selectByMouse: true }
            RowLayout {
                Label { text: "缓冲 (ms)"; color: "#94a3b8" }
                SpinBox { id: latency; from: 80; to: 2000; stepSize: 50; value: initialLatency; editable: true }
            }
            RowLayout {
                Button { Layout.minimumWidth: 60; Layout.preferredWidth: 80; text: "连接"; enabled: a8Video.available; onClicked: a8Video.play(url.text, username.text, password.text, latency.value) }
                Button { Layout.minimumWidth: 60; Layout.preferredWidth: 80; text: "断开"; onClicked: a8Video.stop() }
                Button { Layout.minimumWidth: 60; Layout.preferredWidth: 80; text: "彩条"; enabled: a8Video.available; onClicked: a8Video.playTest() }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#293548" }
            Label { text: "视频信息"; color: "#f1f5f9"; font.bold: true }
            Label { text: "分辨率  " + a8Video.width + " × " + a8Video.height; color: "#cbd5e1" }
            Label { text: "帧率      " + a8Video.fps.toFixed(1) + " fps"; color: "#cbd5e1" }
            Label { text: "已渲染  " + a8Video.renderedFrames; color: "#cbd5e1" }
            Label { text: "丢帧      " + a8Video.droppedFrames; color: "#cbd5e1" }
            Label { text: "重连      " + a8Video.reconnectCount; color: "#cbd5e1" }
            Label { text: "解码器  " + (a8Video.decoderName || "—"); color: "#cbd5e1"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: a8Video.lastError; color: "#fbbf24"
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#64748b"
                text: "H.265 · RTSP/TCP · 默认 300 ms\n视频停滞自动重连，主动断开停止重连。"
            }
        }
    }
}
