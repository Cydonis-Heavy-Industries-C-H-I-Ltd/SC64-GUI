import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    visible: true
    width: 640
    height: 660
    title: "SC64 File Transfer"

    FileDialog {
        id: romDialog
        title: "Select an N64 ROM"
        nameFilters: ["N64 ROMs (*.z64 *.n64 *.v64)", "All files (*)"]
        onAccepted: sc64.uploadRom(selectedFile)
    }

    FileDialog {
        id: copyDialog
        title: "Select a file to copy onto the SD card"
        nameFilters: ["All files (*)"]
        onAccepted: sc64.copyToCard(selectedFile)
    }

    Dialog {
        id: mkdirDialog
        title: "New folder"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: sc64.makeDir(mkdirField.text)
        ColumnLayout {
            Label { text: "Folder name:" }
            TextField {
                id: mkdirField
                Layout.preferredWidth: 260
                onAccepted: mkdirDialog.accept()
            }
        }
    }

    Dialog {
        id: renameDialog
        title: "Rename"
        modal: true
        anchors.centerIn: parent
        property string targetName: ""
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: sc64.renameEntry(targetName, renameField.text)
        ColumnLayout {
            Label { text: "Rename “" + renameDialog.targetName + "” to:" }
            TextField {
                id: renameField
                Layout.preferredWidth: 260
                onAccepted: renameDialog.accept()
            }
        }
    }

    Dialog {
        id: deleteDialog
        title: "Delete"
        modal: true
        anchors.centerIn: parent
        property string targetName: ""
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: sc64.deleteEntry(targetName)
        Label {
            text: "Delete “" + deleteDialog.targetName + "”?\n"
                + "Folders are removed with all their contents. This cannot be undone."
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: aboutDialog
        title: "About"
        modal: true
        anchors.centerIn: parent
        width: Math.min(win.width - 48, 440)
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Label {
                text: "SummerCart64 — File Transfer"
                font.pixelSize: 17
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                lineHeight: 1.15
                textFormat: Text.StyledText
                onLinkActivated: (link) => Qt.openUrlExternally(link)
                text: "© 2026, Amanda Hariette-Scott &amp; Cydonis Heavy Industries, "
                    + "and contributors. "
                    + "(<a href=\"https://www.cydonis.co.uk/about\">www.cydonis.co.uk/about</a>)"
                    + "<br><br>"
                    + "Special thanks go to the entire Nintendo 64 homebrew community, "
                    + "this tool is made for you, with love, on planet Earth! ^_^v"
                HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: parent.hoveredLink != "" }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "SummerCart64 — File Transfer"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }
            Button { text: "About"; onClicked: aboutDialog.open() }
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

        Label { text: "SD card files"; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                text: "⬆ Up"
                enabled: sc64.connected && !sc64.atRoot && !sc64.busy
                onClicked: sc64.navigateUp()
            }
            Label {
                text: sc64.currentPath
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                font.family: "monospace"
                color: "#333"
            }
            Button {
                text: "New folder"
                enabled: sc64.connected && !sc64.busy
                onClicked: { mkdirField.text = ""; mkdirDialog.open() }
            }
            Button {
                text: "Copy file…"
                enabled: sc64.connected && !sc64.busy
                onClicked: copyDialog.open()
            }
            Button {
                text: "Refresh"
                enabled: sc64.connected && !sc64.busy
                onClicked: sc64.refreshCard()
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: cardList
                anchors.fill: parent
                clip: true
                model: sc64.cardEntries
                delegate: ItemDelegate {
                    width: ListView.view ? ListView.view.width : 0
                    enabled: !sc64.busy
                    onClicked: if (modelData.isDir) sc64.openDir(modelData.name)
                    contentItem: RowLayout {
                        spacing: 8
                        Label { text: modelData.isDir ? "📁" : "📄" }
                        Label {
                            text: modelData.name
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            text: modelData.isDir ? "" : (modelData.size + " B")
                            color: "#888"
                        }
                        ToolButton {
                            text: "✎"
                            ToolTip.text: "Rename"
                            ToolTip.visible: hovered
                            onClicked: { renameDialog.targetName = modelData.name;
                                         renameField.text = modelData.name; renameDialog.open() }
                        }
                        ToolButton {
                            text: "🗑"
                            ToolTip.text: "Delete"
                            ToolTip.visible: hovered
                            onClicked: { deleteDialog.targetName = modelData.name; deleteDialog.open() }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: cardList.count === 0
                    color: "#aaa"
                    text: sc64.connected ? "Empty — or press “Refresh” to list the card"
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
