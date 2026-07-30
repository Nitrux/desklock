import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import org.mauikit.controls as Maui

Item {
    id: applicationRoot
    width: 0
    height: 0
    visible: false

    property var lockWindows: []
    property bool unlocking: false
    property bool authenticationVisible: false
    property bool interfaceReady: lockWindows.length > 0
        && lockWindows.length === ScreenRegistry.screens.length
    property bool lockSurfacesReady: {
        if (!interfaceReady) {
            return false
        }
        for (var i = 0; i < lockWindows.length; ++i) {
            if (!lockWindows[i].attached) {
                return false
            }
        }
        return true
    }

    function addScreen(screen) {
        for (var i = 0; i < lockWindows.length; ++i) {
            if (lockWindows[i].targetScreen === screen) {
                return
            }
        }
        var window = lockWindowComponent.createObject(applicationRoot, {
            "targetScreen": screen,
            "unlocking": unlocking
        })
        if (window) {
            var windows = lockWindows.slice()
            windows.push(window)
            lockWindows = windows
            if (SessionLock.supported) {
                window.attachToSessionLock()
            }
        }
    }

    function removeScreen(screen) {
        var windows = []
        for (var i = 0; i < lockWindows.length; ++i) {
            if (lockWindows[i].targetScreen === screen) {
                lockWindows[i].destroy()
            } else {
                windows.push(lockWindows[i])
            }
        }
        lockWindows = windows
    }

    function beginUnlock() {
        if (unlocking) {
            return
        }
        unlocking = true
        for (var i = 0; i < lockWindows.length; ++i) {
            lockWindows[i].unlocking = true
        }
    }

    // Instantiate every MauiKit control before requesting the protocol lock.
    function prepareLockWindows() {
        for (var i = 0; i < ScreenRegistry.screens.length; ++i) {
            addScreen(ScreenRegistry.screens[i])
        }
    }

    Component.onCompleted: prepareLockWindows()

    Connections {
        target: ScreenRegistry

        function onScreenAdded(screen) {
            applicationRoot.addScreen(screen)
        }

        function onScreenRemoved(screen) {
            applicationRoot.removeScreen(screen)
        }
    }

    Connections {
        target: SessionLock

        function onSupportedChanged() {
            for (var i = 0; i < applicationRoot.lockWindows.length; ++i) {
                applicationRoot.lockWindows[i].attachToSessionLock()
            }
        }

        function onLockDenied() {
            Qt.quit()
        }

        function onUnlockAuthorized() {
            applicationRoot.beginUnlock()
        }
    }

    Component {
        id: lockWindowComponent

        Window {
            id: lockWindow
            required property var targetScreen
            property bool unlocking: false
            property bool attached: false
            property string clockText: ""
            property string dateText: ""
            property real contentOpacity: 0

            screen: targetScreen
            visible: false
            flags: Qt.FramelessWindowHint
            color: "black"
            Maui.Theme.colorSet: Maui.Theme.Window

            function updateClock() {
                var now = new Date()
                var timeFmt = (typeof TimeFormat !== "undefined" && TimeFormat) ? TimeFormat : "hh:mm"
                clockText = Qt.formatDateTime(now, timeFmt)

                var dateFmt = (typeof DateFormat !== "undefined" && DateFormat) ? DateFormat : Qt.DefaultLocaleLongDate
                var formattedDate = Qt.formatDateTime(now, dateFmt) || ""

                dateText = LowercaseDate ? formattedDate.toLowerCase() : formattedDate
            }

            function attachToSessionLock() {
                if (attached || !SessionLock.supported) {
                    return
                }
                if (!SessionLock.attachWindow(lockWindow, targetScreen)) {
                    console.error("Could not attach lock surface for", targetScreen.name)
                    return
                }
                attached = true
            }

            function showAuthentication() {
                if (Authentication.processing) {
                    return
                }
                Authentication.reset()
                passwordField.text = ""
                applicationRoot.authenticationVisible = true
                Qt.callLater(function() {
                    passwordField.forceActiveFocus()
                })
            }

            function cancelAuthentication() {
                if (Authentication.processing) {
                    return
                }
                passwordField.text = ""
                Authentication.reset()
                applicationRoot.authenticationVisible = false
                Qt.callLater(function() {
                    avatarImage.forceActiveFocus()
                })
            }

            Component.onCompleted: {
                contentOpacity = 0
                updateClock()
            }

            onUnlockingChanged: {
                if (unlocking) {
                    contentOpacity = 0
                }
            }

            onVisibleChanged: {
                if (visible && !unlocking) {
                    contentOpacity = 1
                    Qt.callLater(function() {
                        if (applicationRoot.authenticationVisible) {
                            passwordField.forceActiveFocus()
                        } else {
                            avatarImage.forceActiveFocus()
                        }
                    })
                }
            }

            Component.onDestruction: SessionLock.detachWindow(lockWindow)

            Timer {
                interval: 1000
                running: true
                repeat: true
                onTriggered: lockWindow.updateClock()
            }

            Item {
                id: lockContent
                anchors.fill: parent
                opacity: lockWindow.contentOpacity

                Behavior on opacity {
                    NumberAnimation {
                        duration: lockWindow.unlocking
                            ? FadeOutDuration : FadeInDuration
                        easing.type: Easing.InOutQuad
                    }
                }

                Maui.Chip {
                    anchors.fill: parent
                    Maui.Theme.colorSet: Maui.Theme.Window
                    color: Maui.Theme.backgroundColor
                    padding: 0
                    spacing: 0
                    hoverEnabled: false
                    enabled: false

                    contentItem: Item {
                        Maui.IconItem {
                            id: wallpaper
                            anchors.fill: parent
                            imageSource: BackgroundImage !== ""
                                ? "file://" + BackgroundImage : ""
                            iconSource: ""
                            imageSizeHint: -1
                            fillMode: Image.PreserveAspectCrop
                            layer.enabled: image.status === Image.Ready
                            layer.effect: MultiEffect {
                                autoPaddingEnabled: false
                                blurEnabled: BackgroundBlurRadius > 0
                                blur: Math.min(1.0, BackgroundBlurRadius / 64.0)
                                blurMax: Math.max(32, BackgroundBlurRadius)
                            }
                        }

                        Maui.Chip {
                            anchors.fill: parent
                            anchors.margins: -Maui.Style.radiusV
                            visible: wallpaper.image.status !== Image.Ready
                            Maui.Theme.colorSet: Maui.Theme.Window
                            spacing: 0
                            color: Maui.Theme.backgroundColor
                            hoverEnabled: false
                        }

                        Maui.Chip {
                            anchors.fill: parent
                            anchors.margins: -Maui.Style.radiusV
                            Maui.Theme.colorSet: Maui.Theme.Window
                            spacing: 0
                            color: Maui.Theme.backgroundColor
                            opacity: BackgroundOverlayOpacity
                            hoverEnabled: false
                        }
                    }
                }

                ColumnLayout {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: Math.max(48, parent.height * 0.08)
                width: Math.min(900, parent.width - Maui.Style.space.big * 2)
                spacing: 12

                Maui.IconLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: font.pixelSize * 1.05
                    Layout.alignment: Qt.AlignHCenter
                    display: ToolButton.TextOnly
                    spacing: 0
                    text: lockWindow.clockText
                    color: Maui.Theme.textColor
                    alignment: Qt.AlignHCenter
                    font.pixelSize: 155
                    font.weight: Font.Bold
                }

                Maui.IconLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: font.pixelSize * 1.3
                    Layout.alignment: Qt.AlignHCenter
                    display: ToolButton.TextOnly
                    spacing: 0
                    text: lockWindow.dateText
                    color: Maui.Theme.textColor
                    alignment: Qt.AlignHCenter
                    font.pixelSize: 25
                    font.weight: Font.Light
                }

                Item {
                    Layout.preferredHeight: 16
                }

                Maui.Chip {
                    Layout.alignment: Qt.AlignHCenter
                    visible: ShowBattery && Battery.available
                    enabled: false
                    hoverEnabled: false
                    text: Battery.info
                    color: Qt.rgba(0, 0, 0, 0.3)
                    label.font.weight: Font.Medium
                }
            }

            ColumnLayout {
                id: avatarView
                anchors.centerIn: parent
                visible: !applicationRoot.authenticationVisible
                spacing: Maui.Style.space.big

                Maui.IconItem {
                    id: avatarImage
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 150
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Unlock as %1").arg(CurrentUser.realName)
                    imageSource: CurrentUser.avatarUrl
                    iconSource: "user-identity"
                    iconSizeHint: 72
                    imageSizeHint: -1
                    imageWidth: 150
                    imageHeight: 150
                    fillMode: Image.PreserveAspectCrop
                    maskRadius: Math.min(width, height) / 2

                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter
                                || event.key === Qt.Key_Space) {
                            lockWindow.showAuthentication()
                            event.accepted = true
                        }
                    }

                    Connections {
                        target: avatarImage.image

                        function onStatusChanged() {
                            if (avatarImage.image.status === Image.Error
                                    && avatarImage.image.source.toString()
                                        !== "qrc:/icons/user-avatar.svg") {
                                avatarImage.imageSource =
                                    "qrc:/icons/user-avatar.svg"
                            }
                        }
                    }

                    Maui.ShadowedRectangle {
                        anchors.fill: parent
                        anchors.margins: -5
                        z: 2
                        color: "transparent"
                        border.width: 3
                        border.color: Maui.Theme.highlightColor
                        corners.topLeftRadius: width / 2
                        corners.topRightRadius: width / 2
                        corners.bottomLeftRadius: width / 2
                        corners.bottomRightRadius: width / 2
                        opacity: avatarMouse.containsMouse
                            || avatarImage.activeFocus ? 1 : 0

                        Behavior on opacity {
                            NumberAnimation {
                                duration: 150
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }

                    MouseArea {
                        id: avatarMouse
                        z: 3
                        anchors.fill: parent
                        enabled: !Authentication.processing
                            && !lockWindow.unlocking
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPressed: avatarImage.forceActiveFocus()
                        onClicked: lockWindow.showAuthentication()
                    }
                }

                Maui.IconLabel {
                    Layout.alignment: Qt.AlignHCenter
                    display: ToolButton.TextOnly
                    spacing: 0
                    text: CurrentUser.realName
                    color: Maui.Theme.textColor
                    font.weight: Font.Medium
                }
            }

            ColumnLayout {
                id: authenticationView
                anchors.centerIn: parent
                width: Math.min(400,
                    parent.width - Maui.Style.space.big * 2)
                visible: applicationRoot.authenticationVisible
                spacing: Maui.Style.space.medium

                Maui.SectionHeader {
                    Layout.fillWidth: true
                    text1: qsTr("Password")
                    text2: qsTr("Enter your password to continue")
                }

                Maui.PasswordField {
                    id: passwordField
                    Layout.fillWidth: true
                    Layout.preferredHeight: Maui.Style.rowHeight
                    enabled: !lockWindow.unlocking
                    readOnly: Authentication.processing
                    echoMode: TextInput.Password
                    passwordMaskDelay: 0
                    actions: []
                    icon.source: ""
                    inputMethodHints: Qt.ImhHiddenText
                        | Qt.ImhSensitiveData
                        | Qt.ImhNoPredictiveText
                        | Qt.ImhNoAutoUppercase
                    placeholderText: Authentication.processing
                        || Authentication.attempts > 0
                        ? "" : qsTr("Enter password")
                    passwordCharacter: "●"
                    selectByMouse: false
                    KeyNavigation.tab: cancelButton
                    KeyNavigation.backtab: cancelButton
                    Maui.Controls.status: Authentication.error !== ""
                        ? Maui.Controls.Negative : 0

                    Keys.onEscapePressed: function(event) {
                        lockWindow.cancelAuthentication()
                        event.accepted = true
                    }

                    onAccepted: {
                        if (text.length > 0 && !Authentication.processing) {
                            var submittedPassword = text
                            text = ""
                            Authentication.authenticate(submittedPassword)
                            submittedPassword = ""
                        }
                    }
                }

                Maui.Chip {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    visible: Authentication.processing
                    enabled: false
                    hoverEnabled: false
                    Maui.Theme.colorSet: Maui.Theme.Window
                    color: Maui.Theme.highlightColor
                    spacing: 0

                    SequentialAnimation on opacity {
                        running: Authentication.processing
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: 0.35
                            to: 1
                            duration: 450
                        }
                        NumberAnimation {
                            from: 1
                            to: 0.35
                            duration: 450
                        }
                    }
                }

                Maui.IconLabel {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: parent.width
                    display: ToolButton.TextOnly
                    spacing: 0
                    visible: Authentication.error !== ""
                    text: Authentication.attempts > 0
                        ? qsTr("%1 (%2)").arg(Authentication.error)
                            .arg(Authentication.attempts)
                        : Authentication.error
                    color: Maui.Theme.negativeTextColor
                    alignment: Text.AlignHCenter
                    label.wrapMode: Text.Wrap
                }

                Button {
                    id: cancelButton
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: Maui.Style.rowHeight
                    enabled: !Authentication.processing
                    activeFocusOnTab: true
                    text: qsTr("Cancel")
                    KeyNavigation.tab: passwordField
                    KeyNavigation.backtab: passwordField
                    Keys.onEscapePressed: function(event) {
                        lockWindow.cancelAuthentication()
                        event.accepted = true
                    }
                    onClicked: lockWindow.cancelAuthentication()
                }
            }

            Maui.Chip {
                id: mediaCard
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: resourceRow.visible
                    ? resourceRow.top : parent.bottom
                anchors.bottomMargin: resourceRow.visible
                    ? Maui.Style.space.big : 28
                width: Math.min(440,
                    parent.width - Maui.Style.space.big * 2)
                height: 72
                visible: ShowMediaControls && Mpris.available
                color: Qt.rgba(0, 0, 0, 0.55)
                padding: 8
                spacing: 0
                hoverEnabled: false

                background: Maui.ShadowedRectangle {
                    color: mediaCard.color
                    corners.topLeftRadius: Maui.Style.radiusV
                    corners.topRightRadius: Maui.Style.radiusV
                    corners.bottomLeftRadius: Maui.Style.radiusV
                    corners.bottomRightRadius: Maui.Style.radiusV
                }

                contentItem: RowLayout {
                    spacing: 8

                    Maui.IconItem {
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                        imageSource: Mpris.artUrl
                        iconSource: "media-album-cover"
                        iconSizeHint: 32
                        imageSizeHint: -1
                        imageWidth: 56
                        imageHeight: 56
                        fillMode: Image.PreserveAspectCrop
                        maskRadius: Maui.Style.radiusV
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 0

                        Maui.IconLabel {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            display: ToolButton.TextOnly
                            spacing: 0
                            text: Mpris.title !== ""
                                ? Mpris.title : qsTr("Unknown track")
                            color: Maui.Theme.textColor
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            label.elide: Text.ElideRight
                            alignment: Qt.AlignLeft
                        }

                        Maui.IconLabel {
                            Layout.fillWidth: true
                            display: ToolButton.TextOnly
                            spacing: 0
                            text: (Mpris.artist !== ""
                                ? Mpris.artist : Mpris.identity)
                                + " — " + Mpris.timeText
                            color: Maui.Theme.textColor
                            opacity: 0.8
                            font.pixelSize: 12
                            label.elide: Text.ElideRight
                        }
                    }

                    ToolSeparator {
                        Layout.preferredHeight:
                            mediaCard.availableHeight / 2
                        Layout.alignment: Qt.AlignVCenter
                        bottomPadding: 10
                        topPadding: 10
                    }

                    Item {
                        Layout.preferredWidth: Math.max(
                            mediaActions.implicitWidth, 120)
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                        Maui.ToolActions {
                            id: mediaActions
                            anchors.centerIn: parent
                            checkable: false
                            autoExclusive: false
                            expanded: true
                            display: ToolButton.IconOnly

                            Action {
                                text: qsTr("Previous")
                                enabled: Mpris.canGoPrevious
                                icon.name: "media-skip-backward"
                                icon.color: Maui.Theme.textColor
                                onTriggered: Mpris.previous()
                            }

                            Action {
                                text: Mpris.playing
                                    ? qsTr("Pause") : qsTr("Play")
                                enabled: Mpris.canControl
                                icon.name: Mpris.playing
                                    ? "media-playback-pause"
                                    : "media-playback-start"
                                icon.color: Maui.Theme.textColor
                                onTriggered: Mpris.playPause()
                            }

                            Action {
                                text: qsTr("Next")
                                enabled: Mpris.canGoNext
                                icon.name: "media-skip-forward"
                                icon.color: Maui.Theme.textColor
                                onTriggered: Mpris.next()
                            }
                        }
                    }
                }
            }

            RowLayout {
                id: resourceRow
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 28
                spacing: Maui.Style.space.small
                visible: ShowSystemMonitor

                Repeater {
                    model: [
                        qsTr("💻 CPU %1%").arg(SystemMonitor.cpuUsage.toFixed(0)),
                        qsTr("🧠 RAM %1%").arg(SystemMonitor.memoryUsage.toFixed(0)),
                        SystemMonitor.online
                            ? qsTr("🌐 %1  ↓ %2/s  ↑ %3/s")
                                .arg(SystemMonitor.interfaceName)
                                .arg(SystemMonitor.receiveRateText)
                                .arg(SystemMonitor.transmitRateText)
                            : qsTr("🌐 Offline")
                    ]

                    delegate: Maui.Chip {
                        required property string modelData
                        enabled: false
                        hoverEnabled: false
                        text: modelData
                        color: Qt.rgba(0, 0, 0, 0.3)
                        label.font.weight: Font.Medium
                    }
                }
            }
            }
        }
    }
}

