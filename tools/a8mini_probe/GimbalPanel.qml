import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ScrollView {
    id: panel
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: panel.availableWidth
        spacing: 10
        Label { text: "云台与相机"; font.pixelSize: 18; font.bold: true; color: "#f1f5f9" }
        Label { text: "控制网关 / 相机地址"; color: "#94a3b8" }
        TextField { id: host; Layout.fillWidth: true; text: initialControlHost; selectByMouse: true; enabled: !a8Gimbal.connected }
        RowLayout {
            Label { text: "端口"; color: "#94a3b8" }
            SpinBox { id: port; from: 1; to: 65535; value: initialControlPort; editable: true; enabled: !a8Gimbal.connected }
        }
        RowLayout {
            Button { text: "连接控制"; enabled: !a8Gimbal.connected; onClicked: a8Gimbal.connectTo(host.text, port.value) }
            Button { text: "断开"; enabled: a8Gimbal.connected; onClicked: a8Gimbal.disconnectControl() }
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: a8Gimbal.status; color: a8Gimbal.connected ? "#4ade80" : "#94a3b8" }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: a8Gimbal.lastError; visible: text.length > 0; color: "#fbbf24" }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#293548" }
        Label { text: "按住转动 · 松手停止"; color: "#f1f5f9"; font.bold: true }
        RowLayout {
            Label { text: "速度"; color: "#94a3b8" }
            Slider { id: speed; Layout.fillWidth: true; from: 5; to: 100; stepSize: 5; value: 20 }
            Label { text: Math.round(speed.value); color: "#cbd5e1" }
        }
        GridLayout {
            columns: 3; Layout.alignment: Qt.AlignHCenter
            enabled: a8Gimbal.connected && !a8Gimbal.busy
            Item { Layout.preferredWidth: 70 }
            Button { text: "↑"; Layout.preferredWidth: 70; onPressed: a8Gimbal.setMotion(0, speed.value); onReleased: a8Gimbal.setMotion(0, 0); onCanceled: a8Gimbal.setMotion(0, 0) }
            Item { Layout.preferredWidth: 70 }
            Button { text: "←"; Layout.preferredWidth: 70; onPressed: a8Gimbal.setMotion(-speed.value, 0); onReleased: a8Gimbal.setMotion(0, 0); onCanceled: a8Gimbal.setMotion(0, 0) }
            Button { text: "回中"; Layout.preferredWidth: 70; onClicked: a8Gimbal.center() }
            Button { text: "→"; Layout.preferredWidth: 70; onPressed: a8Gimbal.setMotion(speed.value, 0); onReleased: a8Gimbal.setMotion(0, 0); onCanceled: a8Gimbal.setMotion(0, 0) }
            Item { Layout.preferredWidth: 70 }
            Button { text: "↓"; Layout.preferredWidth: 70; onPressed: a8Gimbal.setMotion(0, -speed.value); onReleased: a8Gimbal.setMotion(0, 0); onCanceled: a8Gimbal.setMotion(0, 0) }
            Item { Layout.preferredWidth: 70 }
        }
        Button { text: "停止转动 / 变焦"; Layout.fillWidth: true; enabled: a8Gimbal.connected; onClicked: a8Gimbal.stopAll() }
        RowLayout {
            enabled: a8Gimbal.connected && !a8Gimbal.busy
            Button { text: "朝下"; onClicked: a8Gimbal.pointDown() }
            ComboBox { id: mode; Layout.fillWidth: true; model: ["锁定", "跟随", "FPV"] }
            Button { text: "切换"; onClicked: a8Gimbal.setMode(mode.currentIndex) }
        }
        Label { text: "指定角度 (°)"; color: "#f1f5f9"; font.bold: true }
        RowLayout {
            Label { text: "偏航"; color: "#94a3b8" }
            TextField { id: yaw; Layout.fillWidth: true; text: "0.0"; selectByMouse: true; validator: DoubleValidator { bottom: -135; top: 135; decimals: 1; locale: "C" } }
            Label { text: "俯仰"; color: "#94a3b8" }
            TextField { id: pitch; Layout.fillWidth: true; text: "0.0"; selectByMouse: true; validator: DoubleValidator { bottom: -90; top: 25; decimals: 1; locale: "C" } }
        }
        Button { text: "转到指定角度"; Layout.fillWidth: true; enabled: a8Gimbal.connected && !a8Gimbal.busy && yaw.acceptableInput && pitch.acceptableInput; onClicked: a8Gimbal.setAngles(Number(yaw.text), Number(pitch.text)) }
        Label { text: "变焦"; color: "#f1f5f9"; font.bold: true }
        RowLayout {
            enabled: a8Gimbal.connected && !a8Gimbal.busy
            Button { text: "按住缩小 −"; Layout.fillWidth: true; onPressed: a8Gimbal.setZoom(-1); onReleased: a8Gimbal.setZoom(0); onCanceled: a8Gimbal.setZoom(0) }
            Button { text: "按住放大 +"; Layout.fillWidth: true; onPressed: a8Gimbal.setZoom(1); onReleased: a8Gimbal.setZoom(0); onCanceled: a8Gimbal.setZoom(0) }
        }
        RowLayout {
            TextField { id: zoom; Layout.fillWidth: true; text: "1.0"; selectByMouse: true; validator: DoubleValidator { bottom: 1; top: 30; decimals: 1; locale: "C" } }
            Button { text: "指定倍数"; enabled: a8Gimbal.connected && !a8Gimbal.busy && zoom.acceptableInput; onClicked: a8Gimbal.setAbsoluteZoom(Number(zoom.text)) }
        }
        Label { text: "实际变焦范围由相机决定"; color: "#94a3b8" }
        RowLayout {
            Button { text: "拍照"; enabled: a8Gimbal.connected && !a8Gimbal.busy; onClicked: a8Gimbal.takePhoto() }
            Button { text: a8Gimbal.recording === 1 ? "停止录像" : "开始录像"; enabled: a8Gimbal.connected && !a8Gimbal.busy && (a8Gimbal.recording === 0 || a8Gimbal.recording === 1); onClicked: a8Gimbal.toggleRecording() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#293548" }
        Label { text: "相机状态"; color: "#f1f5f9"; font.bold: true }
        Label { color: "#cbd5e1"; text: a8Gimbal.attitudeValid ? "偏航 " + a8Gimbal.yaw.toFixed(1) + "°  俯仰 " + a8Gimbal.pitch.toFixed(1) + "°\n横滚 " + a8Gimbal.roll.toFixed(1) + "°" : "姿态：等待数据" }
        Label { text: "变焦  " + (a8Gimbal.zoom > 0 ? a8Gimbal.zoom.toFixed(1) + "×" : "—"); color: "#cbd5e1" }
        Label { text: "模式  " + (["锁定", "跟随", "FPV"][a8Gimbal.mode] || "未知"); color: "#cbd5e1" }
        Label { text: "录像  " + (["未录像", "录像中", "未插 TF 卡", "TF 卡写入异常"][a8Gimbal.recording] || "未知"); color: "#cbd5e1" }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "连续转动 / 变焦 UDP · 20 Hz\n停止 / 回中 / 角度 / 模式 / 拍照 / 录像 TCP\n切换页签或窗口失焦会停止连续控制。"; color: "#64748b" }
    }
}
