import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Logos.Theme
import Logos.Controls

// MuleNet - pure-QML view over mulenet_core (no C++ backend). Every call goes through
// callVia() -> logos.callModuleAsync; read state comes from snapshot() (polled) and the
// stateChanged event. Contract (mulenet_core_impl.h):
//   snapshot() resync()
//   setupHub(cardJson) retireHub() grantAddress(hub, addrJson) revokeGrant(ref)
//   scanArrival(scanJson) markShipped(jobId) dismissJob(jobId)
//   addTrustRoot(a) removeTrustRoot(a) vouch(hub) unvouch(hub)
//   createMailbox(exitHub, pickupJson) planParcel(parcelJson) forgetParcel(id)
//   labelQr(text) -> {n, cells}   exportLabel(text, name) -> {path}
Item {
    id: root
    anchors.fill: parent

    // Design-system tokens with fallbacks (an older host may lack a token).
    readonly property color cBg: Theme.palette.background || "#111110"
    readonly property color cCard: Theme.palette.backgroundElevated || "#1b1b19"
    readonly property color cInset: Theme.palette.backgroundInset || "#0c0c0b"
    readonly property color cLine: Theme.palette.borderHairline || "#2c2c29"
    readonly property color cText: Theme.palette.text || "#f2f1ec"
    readonly property color cText2: Theme.palette.textSecondary || "#c3c2b7"
    readonly property color cText3: Theme.palette.textTertiary || "#8f8e86"
    readonly property color cPrimary: Theme.palette.primary || "#e8663a"
    readonly property color cOk: Theme.palette.success || "#3fae5a"
    readonly property color cWarn: Theme.palette.warning || "#e0a03a"
    readonly property color cErr: Theme.palette.error || "#e5534b"
    readonly property int sp: Theme.spacing.medium || 12
    readonly property int spS: Theme.spacing.small || 8
    readonly property int rad: Theme.spacing.radiusSmall || 6

    property var st: ({})
    property string tab: "send"
    property string toastMsg: ""
    property bool toastErr: false
    property var lastPlan: null
    property var lastJob: null
    property var qrCache: ({})

    // ── calls ──────────────────────────────────────────────────────────────────
    function callVia(mod, method, args, cb) {
        var a = args || []
        var done = function (raw) { if (cb) { try { cb(raw === undefined || raw === null ? "" : raw) } catch (e) { console.warn(e) } } }
        if (typeof logos === "undefined" || logos === null) { Qt.callLater(function () { done("") }); return }
        if (typeof logos.callModuleAsync === "function") {
            try { logos.callModuleAsync(mod, method, a, done, 20000) } catch (e) { Qt.callLater(function () { done("") }) }
            return
        }
        Qt.callLater(function () { var r = ""; try { r = logos.callModule(mod, method, a) } catch (e) {} done(r) })
    }
    function core(method, args, cb) { root.callVia("mulenet_core", method, args, cb) }
    function parse(raw) {
        var v = raw
        for (var i = 0; i < 3 && typeof v === "string"; i++) { try { v = JSON.parse(v) } catch (e) { return null } }
        return (v && typeof v === "object") ? v : null
    }
    function toast(msg, err) { root.toastMsg = msg; root.toastErr = !!err; toastTimer.restart() }
    // One helper for every mutation: success toast or the core's error sentence.
    function act(method, args, okMsg, onOk) {
        root.core(method, args, function (raw) {
            var r = root.parse(raw)
            if (!r) { root.toast("Request failed - is mulenet_core loaded?", true); return }
            if (r.ok === false || r.error) { root.toast(r.error || "Failed", true); return }
            if (okMsg) root.toast(okMsg, false)
            if (onOk) onOk(r)
            root.refresh()
        })
    }
    property bool refreshBusy: false
    property bool refreshAgain: false
    function refresh() {
        if (root.refreshBusy) { root.refreshAgain = true; return }
        root.refreshBusy = true
        busyGuard.restart()
        root.core("snapshot", [], function (raw) {
            var s = root.parse(raw)
            if (s && s.ok) root.st = s
            root.refreshBusy = false
            if (root.refreshAgain) { root.refreshAgain = false; root.refresh() }
        })
    }
    Timer { id: busyGuard; interval: 45000; onTriggered: root.refreshBusy = false }
    Timer { interval: 2500; running: true; repeat: true; onTriggered: root.refresh() }
    Timer { id: toastTimer; interval: 4500; onTriggered: root.toastMsg = "" }
    Component.onCompleted: {
        if (typeof logos !== "undefined" && logos && logos.onModuleEvent) logos.onModuleEvent("mulenet_core", "stateChanged")
        root.refresh()
    }
    Connections {
        target: typeof logos !== "undefined" ? logos : null
        ignoreUnknownSignals: true
        function onModuleEventReceived(module, event, data) {
            if (module === "mulenet_core" && event === "stateChanged") { var s = root.parse(data); if (s && s.ok) root.st = s }
        }
    }
    TextEdit { id: clip; visible: false }
    function copy(text, what) { clip.text = text; clip.selectAll(); clip.copy(); root.toast((what || "Text") + " copied", false) }

    // helpers over the snapshot
    function hubs() { return (root.st.directory && root.st.directory.hubs) ? root.st.directory.hubs : [] }
    function hubNames(list) { var o = []; for (var i = 0; i < list.length; i++) o.push(list[i].name + " - " + list[i].city); return o }
    function cats() { return (root.st.catalog && root.st.catalog.categories) ? root.st.catalog.categories : [] }
    function carries(policy) {
        var acc = (policy && policy.accepts) || []
        if (acc.indexOf("*") >= 0) return "anything"
        var names = []
        var c = root.cats()
        for (var i = 0; i < acc.length; i++) for (var j = 0; j < c.length; j++) if (c[j].id === acc[i]) names.push(c[j].name)
        return names.join(", ") || "nothing yet"
    }
    function day(ms) { return ms ? new Date(ms).toISOString().slice(0, 10) : "" }

    // ── reusable pieces ────────────────────────────────────────────────────────
    component Label2: Text { textFormat: Text.PlainText; color: root.cText2; font.pixelSize: 12; wrapMode: Text.Wrap }
    component Heading: Text { textFormat: Text.PlainText; color: root.cText; font.pixelSize: 16; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
    component Small: Text { textFormat: Text.PlainText; color: root.cText3; font.pixelSize: 11; wrapMode: Text.Wrap }
    component Card: Rectangle {
        default property alias content: inner.data
        Layout.fillWidth: true
        implicitHeight: inner.implicitHeight + 2 * root.sp
        color: root.cCard
        radius: root.rad + 4
        border.color: root.cLine
        ColumnLayout { id: inner; anchors.fill: parent; anchors.margins: root.sp; spacing: root.spS }
    }
    component Field: TextField {
        Layout.fillWidth: true
        color: root.cText
        placeholderTextColor: root.cText3
        font.pixelSize: 13
        background: Rectangle { color: root.cInset; radius: root.rad; border.color: parent.activeFocus ? root.cPrimary : root.cLine }
    }
    component Pick: ComboBox {
        Layout.fillWidth: true
        font.pixelSize: 13
    }
    component Qr: Canvas {
        id: qrc
        property string text: ""
        property var m: null
        implicitWidth: 220; implicitHeight: 220
        onTextChanged: {
            if (!text) { m = null; requestPaint(); return }
            if (root.qrCache[text]) { m = root.qrCache[text]; requestPaint(); return }
            root.core("labelQr", [text], function (raw) {
                var r = root.parse(raw)
                if (r && r.ok) { var c = root.qrCache; c[qrc.text] = r; root.qrCache = c; qrc.m = r; qrc.requestPaint() }
            })
        }
        onPaint: {
            var ctx = getContext("2d")
            ctx.fillStyle = "#ffffff"; ctx.fillRect(0, 0, width, height)
            if (!m) return
            var q = Math.floor(Math.min(width, height) / (m.n + 8))
            var off = Math.floor((Math.min(width, height) - q * m.n) / 2)
            ctx.fillStyle = "#000000"
            for (var y = 0; y < m.n; y++) for (var x = 0; x < m.n; x++)
                if (m.cells.charAt(y * m.n + x) === "1") ctx.fillRect(off + x * q, off + y * q, q, q)
        }
    }

    // ── layout ─────────────────────────────────────────────────────────────────
    Rectangle { anchors.fill: parent; color: root.cBg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.sp + 4
        spacing: root.sp

        RowLayout {
            Layout.fillWidth: true
            spacing: root.sp
            ColumnLayout {
                spacing: 0
                Text { textFormat: Text.PlainText; text: "MuleNet"; color: root.cText; font.pixelSize: 22; font.weight: Font.Bold }
                Small { text: "a physical mixnet: parcels hop through friends' hubs so nobody can link sender and recipient" }
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [["send", "Send"], ["receive", "Receive"], ["hub", "My hub"], ["directory", "Directory"]]
                delegate: LogosButton {
                    text: (root.tab === modelData[0] ? "> " : "") + modelData[1]
                    onClicked: root.tab = modelData[0]
                }
            }
        }

        Rectangle {
            visible: root.toastMsg !== ""
            Layout.fillWidth: true
            implicitHeight: toastT.implicitHeight + 16
            radius: root.rad
            color: root.toastErr ? Qt.rgba(0.9, 0.33, 0.3, 0.15) : Qt.rgba(0.25, 0.68, 0.35, 0.15)
            border.color: root.toastErr ? root.cErr : root.cOk
            Text { id: toastT; textFormat: Text.PlainText; anchors.fill: parent; anchors.margins: 8; text: root.toastMsg; color: root.cText; wrapMode: Text.Wrap; font.pixelSize: 13 }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: root.sp

                // ════ SEND ════
                ColumnLayout {
                    visible: root.tab === "send"
                    Layout.fillWidth: true
                    spacing: root.sp

                    Card {
                        Heading { text: "Send a parcel" }
                        Label2 { Layout.fillWidth: true; text: "Pack the item in its own plain bag first (that's the core). MuleNet picks a random route, gives you one sealed sleeve per hop, and prints the first label. Each hub only learns the hub before and after it." }
                        GridLayout {
                            columns: 2; columnSpacing: root.sp; rowSpacing: root.spS
                            Layout.fillWidth: true
                            Small { text: "What is it" }
                            Field { id: pTitle; placeholderText: "e.g. 3D-printed figurine (only you see this)" }
                            Small { text: "Category" }
                            Pick { id: pCat; model: root.cats().map(function (c) { return c.name }) }
                            Small { text: "Box size" }
                            Pick { id: pBox; model: ["S - 25x18x8 cm", "M - 35x25x15 cm", "L - 50x35x20 cm"] }
                            Small { text: "Weight, packed (g)" }
                            Field { id: pGrams; placeholderText: "180"; inputMethodHints: Qt.ImhDigitsOnly }
                            Small { text: "Hops" }
                            Pick { id: pHops; model: ["2", "3", "4", "5"]; currentIndex: 1 }
                            Small { text: "Hold at each hub (days)" }
                            RowLayout {
                                Field { id: pHoldMin; text: "1"; Layout.preferredWidth: 60 }
                                Small { text: "to" }
                                Field { id: pHoldMax; text: "4"; Layout.preferredWidth: 60 }
                            }
                            Small { text: "Drop it off at" }
                            Pick { id: pEntry; model: root.hubNames(root.hubs()) }
                            Small { text: "Recipient's mailbox card" }
                            Field { id: pBox2; placeholderText: "MNBOX1.... (the recipient gives you this)" }
                        }
                        LogosButton {
                            text: "Plan route and make the label"
                            onClicked: {
                                var hs = root.hubs()
                                if (hs.length === 0) { root.toast("No hubs in your directory yet", true); return }
                                var c = root.cats()[pCat.currentIndex]
                                var p = { title: pTitle.text || "Parcel", category: c ? c.id : 1, boxClass: pBox.currentIndex + 1,
                                          coreGrams: parseInt(pGrams.text || "0"), hops: parseInt(pHops.currentText),
                                          holdMin: parseInt(pHoldMin.text || "1"), holdMax: parseInt(pHoldMax.text || "4"),
                                          entry: hs[pEntry.currentIndex].address, mailbox: pBox2.text.trim() }
                                root.act("planParcel", [JSON.stringify(p)], "Route planned - pack the sleeves and print the label", function (r) { root.lastPlan = r.parcel })
                            }
                        }
                    }

                    Card {
                        visible: root.lastPlan !== null
                        Heading { text: "Pack it like this" }
                        Label2 { Layout.fillWidth: true; text: "Seal the core inside sleeves, innermost first: the LAST sleeve in this list goes on first. Write each seal code on that sleeve's tamper sticker." }
                        Repeater {
                            model: root.lastPlan ? root.lastPlan.hops.slice().reverse() : []
                            delegate: Label2 { text: "Sleeve " + modelData.sleeve + ": seal " + modelData.sleeveCode + "   (removed by hop " + modelData.sleeve + ")"; font.family: "monospace" }
                        }
                        Label2 {
                            Layout.fillWidth: true
                            text: root.lastPlan ? ("Then put it in a plain " + root.lastPlan.boxClass + " box, stick this label on, and hand it to " + root.lastPlan.entryName +
                                                    ((root.lastPlan.dropOff && root.lastPlan.dropOff.text) ? (": " + root.lastPlan.dropOff.text) : "")) : ""
                        }
                        RowLayout {
                            spacing: root.sp
                            Qr { text: root.lastPlan ? root.lastPlan.label : "" }
                            ColumnLayout {
                                LogosButton { text: "Save label as SVG"; onClicked: root.act("exportLabel", [root.lastPlan.label, "parcel-" + root.lastPlan.id], "", function (r) { root.toast("Saved " + r.path, false) }) }
                                LogosButton { text: "Copy label text"; onClicked: root.copy(root.lastPlan.label, "Label") }
                                Small { Layout.preferredWidth: 280; text: "The label holds only the onion. It says nothing about you, the route or the recipient." }
                            }
                        }
                    }

                    Heading { text: "Your parcels"; visible: (root.st.parcels || []).length > 0 }
                    Repeater {
                        model: root.st.parcels || []
                        delegate: Card {
                            RowLayout {
                                Layout.fillWidth: true
                                Heading { text: modelData.title; Layout.fillWidth: true }
                                LogosButton { text: "Show label"; onClicked: root.lastPlan = modelData }
                                LogosButton { text: "Forget"; onClicked: root.act("forgetParcel", [modelData.id], "Forgotten") }
                            }
                            Small { text: modelData.category + ", box " + modelData.boxClass + ", via " + modelData.hops.map(function (h) { return h.city }).join(" > ") + "  (only you know this route)" }
                            Repeater {
                                model: modelData.steps
                                delegate: Label2 {
                                    property var rs: modelData.receipts || []
                                    color: rs.some(function (r) { return r.kind === "refused" }) ? root.cErr : (rs.length ? root.cText : root.cText3)
                                    text: (rs.length ? "* " : "o ") + modelData.step + ":  " +
                                          (rs.length ? rs.map(function (r) { return r.kind + " " + r.day + (r.reason ? " (" + r.reason + ")" : "") }).join(", ") : "waiting")
                                }
                            }
                        }
                    }
                }

                // ════ RECEIVE ════
                ColumnLayout {
                    visible: root.tab === "receive"
                    Layout.fillWidth: true
                    spacing: root.sp
                    Card {
                        Heading { text: "Create a mailbox" }
                        Label2 { Layout.fillWidth: true; text: "Pick the hub that hands parcels to you, and the pickup point it should ship to. Only that hub can read the pickup point - the sender never sees it. Give the sender the mailbox card." }
                        GridLayout {
                            columns: 2; columnSpacing: root.sp; rowSpacing: root.spS; Layout.fillWidth: true
                            Small { text: "Exit hub" }
                            Pick { id: mExit; model: root.hubNames(root.hubs()) }
                            Small { text: "Pickup locker" }
                            Field { id: mLocker; placeholderText: "e.g. a parcel locker ID" }
                            Small { text: "Name on the parcel" }
                            Field { id: mName; placeholderText: "a name the carrier accepts (can be a pseudonym where allowed)" }
                            Small { text: "Phone or email for the locker code" }
                            Field { id: mPhone; placeholderText: "the carrier sends the pickup code here" }
                        }
                        LogosButton {
                            text: "Create mailbox"
                            onClicked: {
                                var hs = root.hubs()
                                if (!hs.length) { root.toast("No hubs in your directory yet", true); return }
                                root.act("createMailbox", [hs[mExit.currentIndex].address, JSON.stringify({ locker: mLocker.text, name: mName.text, contact: mPhone.text })],
                                         "Mailbox created - copy the card for the sender")
                            }
                        }
                    }
                    Repeater {
                        model: root.st.mailboxes || []
                        delegate: Card {
                            Heading { text: "Mailbox at " + modelData.exitName }
                            Small { text: "Pickup: " + (modelData.pickup.locker || "") + (modelData.listed ? "" : "   (not synced to the directory yet)") }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { textFormat: Text.PlainText; text: modelData.card; color: root.cText2; font.family: "monospace"; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                LogosButton { text: "Copy card"; onClicked: root.copy(modelData.card, "Mailbox card") }
                            }
                            Label2 {
                                color: (modelData.notices || []).length ? root.cOk : root.cText3
                                text: (modelData.notices || []).length ? ("A parcel is on its way to your pickup point (" + modelData.notices[0].day + ")") : "No parcels yet"
                            }
                        }
                    }
                }

                // ════ HUB ════
                ColumnLayout {
                    visible: root.tab === "hub"
                    Layout.fillWidth: true
                    spacing: root.sp

                    Card {
                        Heading { text: root.st.hub ? ("Your hub: " + root.st.hub.card.name) : "Run a hub" }
                        Label2 { Layout.fillWidth: true; text: root.st.hub ? ("Listed in the directory as " + root.st.hub.card.city + ", " + root.st.hub.card.country + ". Your street or locker address is never published - you give it to other hubs one by one, sealed so only they can read it.")
                                                                         : "A hub receives parcels, removes one sleeve, re-boxes and ships on. Only your city is public. You never open the item." }
                        GridLayout {
                            columns: 2; columnSpacing: root.sp; rowSpacing: root.spS; Layout.fillWidth: true
                            Small { text: "Hub name" }
                            Field { id: hName; text: root.st.hub ? root.st.hub.card.name : "" }
                            Small { text: "City" }
                            Field { id: hCity; text: root.st.hub ? root.st.hub.card.city : "" }
                            Small { text: "Country (2 letters)" }
                            Field { id: hCountry; text: root.st.hub ? root.st.hub.card.country : ""; maximumLength: 2 }
                            Small { text: "Carries" }
                            Pick { id: hAccepts; model: ["anything (sealed, undeclared ok)", "declared goods only (no 'undeclared')"] }
                            Small { text: "Ships on (weekdays)" }
                            Field { id: hBatch; text: "Mon,Thu"; placeholderText: "e.g. Mon,Thu - batching hides timing" }
                            Small { text: "Max hold (days)" }
                            Field { id: hHold; text: "7" }
                            Small { text: "Where senders hand over" }
                            Field { id: hIntake; placeholderText: "public intake only, e.g. 'at the monthly Circle meetup'" }
                        }
                        RowLayout {
                            LogosButton {
                                text: root.st.hub ? "Update hub card" : "Start my hub"
                                onClicked: {
                                    var dn = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"], bd = []
                                    var parts = hBatch.text.split(",")
                                    for (var i = 0; i < parts.length; i++) { var k = dn.indexOf(parts[i].trim().slice(0, 3)); if (k >= 0) bd.push(k) }
                                    var acc = hAccepts.currentIndex === 0 ? ["*"] : root.cats().filter(function (c) { return c.id !== 99 }).map(function (c) { return c.id })
                                    var card = { name: hName.text, city: hCity.text, country: hCountry.text.toUpperCase(),
                                                 policy: { accepts: acc, boxClasses: [1, 2, 3], maxWeightClass: 4, shipsTo: ["*"], batchDays: bd, holdMaxDays: parseInt(hHold.text || "7") },
                                                 intake: { kind: hIntake.text ? "meetup" : "none", text: hIntake.text } }
                                    root.act("setupHub", [JSON.stringify(card)], root.st.hub ? "Hub card updated" : "Your hub is running")
                                }
                            }
                            LogosButton { visible: !!root.st.hub; text: "Retire hub"; onClicked: root.act("retireHub", [], "Hub retired") }
                        }
                    }

                    Card {
                        visible: !!root.st.hub
                        Heading { text: "A parcel arrived" }
                        Label2 { Layout.fillWidth: true; text: "Scan the label with your phone (or paste the text), type the code on the outer sleeve's seal, and weigh the box. MuleNet tells you what to do next." }
                        TextArea {
                            id: sLabel
                            Layout.fillWidth: true; Layout.preferredHeight: 70
                            placeholderText: "MN1.... label text"; wrapMode: TextEdit.WrapAnywhere
                            color: root.cText; font.family: "monospace"; font.pixelSize: 11
                            background: Rectangle { color: root.cInset; radius: root.rad; border.color: root.cLine }
                        }
                        GridLayout {
                            columns: 4; columnSpacing: root.sp; Layout.fillWidth: true
                            Small { text: "Sleeve seal code" }
                            Field { id: sCode; placeholderText: "XXXX-XXXX-XXXXX" }
                            Small { text: "Box weight (g)" }
                            Field { id: sGross; placeholderText: "gross, optional" }
                        }
                        LogosButton {
                            text: "Check parcel"
                            onClicked: root.act("scanArrival", [JSON.stringify({ label: sLabel.text.trim(), sleeveCode: sCode.text, grossGrams: parseInt(sGross.text || "0") })], "",
                                                function (r) { root.lastJob = r.job; root.toast(r.job.status === "refused" ? ("Refused: " + r.job.reason) : "Accepted - see what to do next", r.job.status === "refused"); sLabel.text = ""; sCode.text = ""; sGross.text = "" })
                        }
                    }

                    Repeater {
                        model: (root.st.jobs || []).slice().reverse()
                        delegate: Card {
                            property bool todo: modelData.status === "to ship"
                            Heading { text: todo ? ("Ship by " + modelData.shipDay + " to " + modelData.nextHubName) : (modelData.status === "refused" ? "Refused" : "Shipped") }
                            Label2 { visible: modelData.status === "refused"; color: root.cErr; text: modelData.reason || "" }
                            Label2 { visible: todo; Layout.fillWidth: true; text: "1. Remove the outer sleeve sealed " + modelData.removeSleeve + " and throw away the old box and label.\n2. Put the rest in a plain " + modelData.boxClass + " box, add " + modelData.paddingGrams + " g of padding.\n3. Print the new label below and stick it on." }
                            Label2 { visible: todo; text: "Ship to: " + JSON.stringify(modelData.address || {}) }
                            RowLayout {
                                visible: todo && !!modelData.nextLabel
                                Qr { text: todo && modelData.nextLabel ? modelData.nextLabel : "" }
                                ColumnLayout {
                                    LogosButton { text: "Save label as SVG"; onClicked: root.act("exportLabel", [modelData.nextLabel, "relay-" + modelData.id], "", function (r) { root.toast("Saved " + r.path, false) }) }
                                    LogosButton { text: "Copy label text"; onClicked: root.copy(modelData.nextLabel, "Label") }
                                }
                            }
                            RowLayout {
                                LogosButton { visible: todo; text: "I handed it to the carrier"; onClicked: root.act("markShipped", [modelData.id], "Marked shipped - receipt queued") }
                                LogosButton { visible: !todo; text: "Dismiss"; onClicked: root.act("dismissJob", [modelData.id], "") }
                                Small { visible: modelData.status === "shipped"; text: (modelData.alibi || []).length ? ("Next hub confirmed: " + modelData.alibi[0].kind + " " + modelData.alibi[0].day) : "Waiting for the next hub to confirm receipt" }
                            }
                        }
                    }

                    Card {
                        visible: !!root.st.hub
                        Heading { text: "Who may ship to you" }
                        Label2 { Layout.fillWidth: true; text: "Grant a hub your handoff address. It's sealed so that only that hub can read it; nobody else, senders included, ever sees it." }
                        GridLayout {
                            columns: 2; columnSpacing: root.sp; rowSpacing: root.spS; Layout.fillWidth: true
                            Small { text: "Hub" }
                            Pick { id: gHub; model: root.hubNames(root.hubs().filter(function (h) { return !h.mine })) }
                            Small { text: "Your locker / address" }
                            Field { id: gLocker; placeholderText: "parcel locker ID, or street address" }
                            Small { text: "Name + phone for the carrier" }
                            Field { id: gName }
                        }
                        LogosButton {
                            text: "Grant my address"
                            onClicked: {
                                var others = root.hubs().filter(function (h) { return !h.mine })
                                if (!others.length) { root.toast("No other hubs yet", true); return }
                                root.act("grantAddress", [others[gHub.currentIndex].address, JSON.stringify({ locker: gLocker.text, contact: gName.text })], "Address granted (sealed)")
                            }
                        }
                        Repeater {
                            model: root.st.hub ? root.st.hub.grants : []
                            delegate: RowLayout {
                                Label2 { text: "Granted to " + modelData.toName; Layout.fillWidth: true }
                                LogosButton { text: "Revoke"; onClicked: root.act("revokeGrant", [modelData.ref], "Grant revoked") }
                            }
                        }
                    }
                }

                // ════ DIRECTORY ════
                ColumnLayout {
                    visible: root.tab === "directory"
                    Layout.fillWidth: true
                    spacing: root.sp
                    Card {
                        Heading { text: "Trust" }
                        Label2 { Layout.fillWidth: true; text: "Routes only use hubs vouched for by a steward you trust. You are your own trust root; add your Circle steward's address to use the hubs they vouch for." }
                        RowLayout {
                            Layout.fillWidth: true
                            Small { text: "Your address:" }
                            Text { textFormat: Text.PlainText; text: root.st.me ? root.st.me.address : ""; color: root.cText2; font.family: "monospace"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
                            LogosButton { text: "Copy"; onClicked: root.copy(root.st.me.address, "Address") }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Field { id: tRoot; placeholderText: "0x... steward address" }
                            LogosButton { text: "Trust this steward"; onClicked: root.act("addTrustRoot", [tRoot.text.trim()], "Trust root added", function () { tRoot.text = "" }) }
                        }
                        Repeater {
                            model: root.st.me ? root.st.me.roots : []
                            delegate: RowLayout {
                                Text { textFormat: Text.PlainText; text: modelData + (modelData === root.st.me.address ? "  (you)" : ""); color: root.cText3; font.family: "monospace"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                LogosButton { visible: modelData !== root.st.me.address; text: "Remove"; onClicked: root.act("removeTrustRoot", [modelData], "Removed") }
                            }
                        }
                    }
                    Repeater {
                        model: root.hubs()
                        delegate: Card {
                            RowLayout {
                                Layout.fillWidth: true
                                Heading { text: modelData.name + " - " + modelData.city + ", " + modelData.country + (modelData.mine ? "  (you)" : ""); Layout.fillWidth: true }
                                LogosButton {
                                    visible: root.st.me && root.st.me.isSteward
                                    text: modelData.vouchedByMe ? "Withdraw vouch" : "Vouch"
                                    onClicked: root.act(modelData.vouchedByMe ? "unvouch" : "vouch", [modelData.address], modelData.vouchedByMe ? "Vouch withdrawn" : "Vouched")
                                }
                            }
                            Small { text: "Carries " + root.carries(modelData.policy) + "   |   " + modelData.vouchedBy + " vouch(es) from your stewards" +
                                          (modelData.intake && modelData.intake.text ? ("   |   intake: " + modelData.intake.text) : "") }
                            Small { visible: !!root.st.hub && !modelData.mine; text: (modelData.canShipTo ? "you can ship to them" : "they haven't granted you their address") + " / " + (modelData.iGranted ? "they can ship to you" : "you haven't granted them yours") }
                        }
                    }
                    Label2 { visible: root.hubs().length === 0; text: "No hubs yet. Start one under My hub, or wait for the directory to sync." }
                }
            }
        }

        // footer: status + diagnostics
        RowLayout {
            Layout.fillWidth: true
            Small {
                Layout.fillWidth: true
                color: root.st.status === "Connected" ? root.cOk : root.cWarn
                text: (root.st.status || "Connecting to mulenet_core...") + "   |   core " + (root.st.version || "?") +
                      (root.st.counters ? ("   |   rx " + root.st.counters.rx + " / tx " + root.st.counters.tx + ", " + (root.st.directory ? root.st.directory.events : 0) + " directory events, " + root.st.counters.receipts + " receipts") : "") +
                      (root.st.storage && !root.st.storage.ok ? ("   |   " + root.st.storage.note) : "")
            }
            LogosButton { text: "Sync now"; onClicked: root.act("resync", [], "Asked peers for anything missing") }
            LogosButton { text: "Copy diagnostics"; onClicked: root.copy(JSON.stringify({ version: root.st.version, status: root.st.status, counters: root.st.counters, directory: root.st.directory ? { hubs: root.st.directory.hubs.length, grants: root.st.directory.grants, events: root.st.directory.events } : null, storage: root.st.storage }), "Diagnostics") }
        }
    }
}
