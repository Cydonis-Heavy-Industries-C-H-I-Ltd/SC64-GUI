import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    visible: true
    width: 580
    height: 640
    title: "SC64 File Transfer Tool v0.0.1a. (c) 2026 Cydonis Heavy Industries (cydonis.co.uk/about)."

    FileDialog {
        id: romDialog
        title: "Select an N64 ROM."
        nameFilters: ["N64 ROMs (*.z64 *.n64 *.v64)", "All files (*)"]
        onAccepted: sc64.uploadRom(selectedFile)
    }

    FileDialog {
        id: copyDialog
        title: "Select a file to copy onto the SD card."
        nameFilters: ["All files (*)"]
        onAccepted: sc64.copyToCard(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Label {
            text: "SummerCart64 — File Transfer."
            font.pixelSize: 20
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ComboBox {
                id: portBox
                Layout.fillWidth: true
                model: sc64.devices
                enabled: !sc64.connected && !sc64.busy
            }
            Button {
                text: "Refresh"
                enabled: !sc64.busy
                onClicked: sc64.refresh()
            }
            Button {
                text: sc64.connected ? "Disconnect" : "Connect"
                enabled: (sc64.connected || portBox.count > 0) && !sc64.busy
                onClicked: sc64.connected ? sc64.disconnectPort()
                                          : sc64.connectPort(portBox.currentText)
            }
        }

        Button {
            text: "Upload ROM to cart…"
            enabled: sc64.connected && !sc64.busy
            onClicked: romDialog.open()
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#ddd" }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "SD card files"
                font.bold: true
                Layout.fillWidth: true
            }
            Button {
                text: "Mount / refresh"
                enabled: sc64.connected && !sc64.busy
                onClicked: sc64.refreshCard()
            }
            Button {
                text: "Copy file to card…"
                enabled: sc64.connected && !sc64.busy
                onClicked: copyDialog.open()
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#2a7"
            font.pixelSize: 12
            text: "Files are copied into the existing filesystem via FatFs — other "
                + "data on the card is preserved."
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: cardList
                anchors.fill: parent
                clip: true
                model: sc64.cardEntries
                delegate: RowLayout {
                    width: ListView.view ? ListView.view.width : 0
                    spacing: 8
                    Label {
                        text: modelData.isDir ? "📁" : "📄"
                    }
                    Label {
                        text: modelData.name
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: modelData.isDir ? "" : (modelData.size + " B")
                        color: "#888"
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: cardList.count === 0
                    color: "#aaa"
                    text: sc64.connected ? "Press “Mount / refresh” to list the card"
                                         : "Not connected"
                }
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: sc64.progress
        }

        Label {
            Layout.fillWidth: true
            text: sc64.status
            wrapMode: Text.WordWrap
            color: "#444"
        }
    }
}
