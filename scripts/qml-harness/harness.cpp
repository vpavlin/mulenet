// Offscreen render harness for the MuleNet view, backed by the REAL mulenet_core.
// The view's `logos` object dispatches callModuleAsync("mulenet_core", ...) to a real
// MulenetCoreImpl (fake loam_core bus underneath), while three more core instances act
// as friends' hubs. The scenario builds a small network, then screenshots every tab.
// Catches runtime QML errors qmllint can't, and shows the view on real state.
//   scripts/qml-harness/render.sh
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QQuickItem>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QJSValue>
#include <QJSEngine>
#include <filesystem>
#include <functional>
#include <map>
#include <cstdio>
#include "mulenet_core_impl.h"
#include "logos_sdk.h"

void MulenetCoreImpl::stateChanged(const std::string&) {}
using json = nlohmann::json;
static int g_errors = 0;

static void handler(QtMsgType t, const QMessageLogContext&, const QString& m) {
    if ((t == QtWarningMsg || t == QtCriticalMsg) && (m.contains("is not a type") || m.contains("non-existent property") ||
        m.contains("TypeError") || m.contains("ReferenceError") || m.contains("error") || m.contains("Error"))) g_errors++;
    fprintf(stderr, "[%s] %s\n", t == QtWarningMsg ? "W" : t == QtCriticalMsg ? "E" : "I", m.toUtf8().constData());
}

class RealLogos : public QObject {
    Q_OBJECT
public:
    MulenetCoreImpl* core = nullptr;
    std::string dispatch(const QString& method, const QVariantList& a) {
        auto s = [&](int i) { return i < a.size() ? a[i].toString().toStdString() : std::string(); };
        static const std::map<QString, std::function<std::string(MulenetCoreImpl*, const QVariantList&)>> table = {};
        const std::string m = method.toStdString();
        if (m == "snapshot") return core->snapshot();
        if (m == "resync") return core->resync();
        if (m == "setupHub") return core->setupHub(s(0));
        if (m == "retireHub") return core->retireHub();
        if (m == "grantAddress") return core->grantAddress(s(0), s(1));
        if (m == "revokeGrant") return core->revokeGrant(s(0));
        if (m == "scanArrival") return core->scanArrival(s(0));
        if (m == "markShipped") return core->markShipped(s(0));
        if (m == "dismissJob") return core->dismissJob(s(0));
        if (m == "addTrustRoot") return core->addTrustRoot(s(0));
        if (m == "removeTrustRoot") return core->removeTrustRoot(s(0));
        if (m == "vouch") return core->vouch(s(0));
        if (m == "unvouch") return core->unvouch(s(0));
        if (m == "createMailbox") return core->createMailbox(s(0), s(1));
        if (m == "planParcel") return core->planParcel(s(0));
        if (m == "forgetParcel") return core->forgetParcel(s(0));
        if (m == "labelQr") return core->labelQr(s(0));
        if (m == "exportLabel") return core->exportLabel(s(0), s(1));
        return "{\"error\":\"Invalid response\"}";   // what the host says for an unknown method
    }
    Q_INVOKABLE void callModuleAsync(const QString& mod, const QString& method, const QVariantList& args, const QJSValue& cb, int) {
        if (method != "snapshot" && method != "labelQr") fprintf(stderr, "CALL %s argc=%d\n", method.toUtf8().constData(), (int)args.size());
        QString r = mod == "mulenet_core" ? QString::fromStdString(dispatch(method, args)) : QString("{\"error\":\"unknown module\"}");
        QJSValue c = cb;
        QTimer::singleShot(0, this, [c, r]() mutable { if (c.isCallable()) c.call({QJSValue(r)}); });
    }
    Q_INVOKABLE void onModuleEvent(const QString&, const QString&) {}
signals:
    void moduleEventReceived(const QString& mod, const QString& ev, const QString& data);
};
#include "harness.moc"

struct Peer { FakeLoamNode node; MulenetCoreImpl core; json snap() { return json::parse(core.snapshot()); } };
static Peer* spawn(const std::string& root, const std::string& name) {
    std::string d = root + "/" + name;
    std::filesystem::remove_all(d); std::filesystem::create_directories(d);
    setenv("MULENET_CORE_DATA", d.c_str(), 1);
    Peer* p = new Peer();
    p->core.modules().loam_core.node = &p->node;
    FakeLoamBus::get().nodes.push_back(&p->node);
    p->core.fakeStart();
    return p;
}
static void pump(int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    setenv("MULENET_NO_RECEIPT_DELAY", "1", 1);
    setenv("MULENET_TICK_MS", "200", 1);
    QGuiApplication app(argc, argv);
    qInstallMessageHandler(handler);
    if (argc < 3) { fprintf(stderr, "usage: harness <Main.qml> <outdir>\n"); return 2; }
    std::string out = argv[2], root = out + "/data";

    // The network: "me" (the view's core: steward + Prague hub + sender), two friends' hubs, a recipient.
    Peer* me = spawn(root, "me"); Peer* ber = spawn(root, "berlin"); Peer* mad = spawn(root, "madrid"); Peer* bob = spawn(root, "bob");
    pump(300);
    std::string steward = me->snap()["me"]["address"];
    for (Peer* p : {ber, mad, bob}) p->core.addTrustRoot(steward);
    auto card = [](const char* n, const char* c, const char* cc, json acc) {
        return json{{"name", n}, {"city", c}, {"country", cc}, {"policy", {{"accepts", acc}, {"maxWeightClass", 4}, {"batchDays", json::array({1, 4})}, {"holdMaxDays", 3}}},
                    {"intake", {{"kind", "meetup"}, {"text", std::string("at the ") + c + " Logos Circle meetup"}}}}.dump();
    };
    me->core.setupHub(card("Prague mule", "Prague", "CZ", json::array({1, 2, 3, 4})));
    ber->core.setupHub(card("Berlin mule", "Berlin", "DE", json::array({"*"})));
    mad->core.setupHub(card("Madrid mule", "Madrid", "ES", json::array({1, 2, 6})));
    for (int i = 0; i < 20 && me->snap()["directory"]["hubs"].size() < 3; i++) pump(200);
    json dir = me->snap()["directory"]["hubs"];   // keep the snapshot alive while iterating
    for (const auto& h : dir) me->core.vouch(h["address"]);
    std::map<std::string, std::string> addr = {{"me", me->snap()["me"]["address"]}, {"berlin", ber->snap()["me"]["address"]}, {"madrid", mad->snap()["me"]["address"]}};
    pump(600);
    std::vector<std::pair<std::string, Peer*>> hubs = {{"me", me}, {"berlin", ber}, {"madrid", mad}};
    for (auto& [tn, to] : hubs) for (auto& [fn, from] : hubs) if (to != from)
        to->core.grantAddress(addr[fn], json{{"locker", tn + "-locker-17"}, {"contact", "+00 000"}}.dump());
    json mb = json::parse(bob->core.createMailbox(addr["madrid"], json{{"locker", "MAD-CITYLOCKER-7"}, {"name", "Bob"}}.dump()));
    pump(800);
    // A parcel I send, and one that arrives at my hub from Berlin's direction.
    json pl = json::parse(me->core.planParcel(json{{"title", "3D-printed mule figurine"}, {"category", 1}, {"boxClass", 1}, {"coreGrams", 180},
                                                  {"entry", addr["me"]}, {"mailbox", mb["card"]}, {"hops", 3}, {"holdMin", 1}, {"holdMax", 2}}.dump()));
    if (!pl.value("ok", false)) { fprintf(stderr, "plan failed: %s\n", pl.dump().c_str()); return 1; }
    json parcel = pl["parcel"];
    json j1 = json::parse(me->core.scanArrival(json{{"label", parcel["label"]}, {"sleeveCode", parcel["hops"][0]["sleeveCode"]}, {"grossGrams", 430}}.dump()));
    me->core.markShipped(j1["job"]["id"]);
    json j2 = json::parse(ber->core.scanArrival(json{{"label", j1["job"]["nextLabel"]}, {"sleeveCode", parcel["hops"][1]["sleeveCode"]}, {"grossGrams", 430}}.dump()));
    pump(800);
    // A second arrival at my hub, still to ship (Berlin -> me -> Madrid route for Bob).
    json pl2 = json::parse(ber->core.planParcel(json{{"title", "books"}, {"category", 2}, {"boxClass", 2}, {"coreGrams", 600},
                                                   {"entry", addr["berlin"]}, {"mailbox", mb["card"]}, {"hops", 3}}.dump()));
    if (pl2.value("ok", false)) {
        json a = json::parse(ber->core.scanArrival(json{{"label", pl2["parcel"]["label"]}, {"sleeveCode", pl2["parcel"]["hops"][0]["sleeveCode"]}}.dump()));
        if (a["job"].contains("nextLabel")) me->core.scanArrival(json{{"label", a["job"]["nextLabel"]}, {"sleeveCode", pl2["parcel"]["hops"][1]["sleeveCode"]}, {"grossGrams", 880}}.dump());
    }
    me->core.createMailbox(addr["berlin"], json{{"locker", "BER-PACKSTATION-112"}, {"name", "me"}}.dump());
    pump(800);

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1280, 1500);
    RealLogos logos; logos.core = &me->core;
    view.engine()->rootContext()->setContextProperty("logos", &logos);
    view.setSource(QUrl::fromLocalFile(argv[1]));
    if (view.status() == QQuickView::Error) { for (auto& e : view.errors()) fprintf(stderr, "LOAD ERROR %s\n", e.toString().toUtf8().constData()); return 1; }
    view.show();
    pump(1500);
    QQuickItem* r = view.rootObject();
    // Show the planned parcel's packing card on the Send tab.
    QJSValue plan = view.engine()->toScriptValue(QVariant::fromValue(QString::fromStdString(parcel.dump())));
    r->setProperty("lastPlan", view.engine()->evaluate("(" + QString::fromStdString(parcel.dump()) + ")").toVariant());
    for (const char* tab : {"send", "receive", "hub", "directory"}) {
        r->setProperty("tab", tab);
        pump(900);
        view.grabWindow().save(QString::fromStdString(out + "/mulenet-" + tab + ".png"));
        fprintf(stderr, "SHOT %s\n", tab);
    }
    fprintf(stderr, "QML_ERRORS=%d\n", g_errors);
    return g_errors ? 1 : 0;
}
