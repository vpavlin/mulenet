#pragma once
// MulenetCoreImpl - MuleNet (a physical mixnet for parcels) as a Logos CORE module
// (universal authoring). Everything lives here: the hub directory (a signed event log,
// RBSR-synced over loam_core), route planning and label building for senders, the
// arrival flow for hubs, recipient mailboxes, and custody receipts. The `mulenet`
// ui_qml view only calls these actions and renders snapshot(). The same core runs
// headless under logoscore as an always-on hub peer.
//
// Module rules (logos-basecamp-module skill): public methods return std::string JSON
// ({"ok":true,...} or {"ok":false,"error":"<sentence>"}), never throw across IPC;
// at most 4 args, structured input as ONE JSON string; read state via the snapshot()
// action plus the stateChanged event; no trailing comments on declaration lines.
#include <string>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include "logos_module_context.h"
#include "mulenet_engine.hpp"

class QTimer;

class MulenetCoreImpl : public LogosModuleContext {
public:
    ~MulenetCoreImpl() override;

    std::string snapshot();
    std::string resync();

    // running a hub
    std::string setupHub(std::string cardJson);
    std::string retireHub();
    std::string grantAddress(std::string hubAddress, std::string addressJson);
    std::string revokeGrant(std::string ref);
    std::string scanArrival(std::string scanJson);
    std::string markShipped(std::string jobId);
    std::string dismissJob(std::string jobId);

    // trust: whose vouches count, and vouching as a steward
    std::string addTrustRoot(std::string address);
    std::string removeTrustRoot(std::string address);
    std::string vouch(std::string hubAddress);
    std::string unvouch(std::string hubAddress);

    // receiving
    std::string createMailbox(std::string exitHub, std::string pickupJson);

    // sending
    std::string planParcel(std::string parcelJson);
    std::string forgetParcel(std::string parcelId);

    // labels
    std::string labelQr(std::string text);
    std::string exportLabel(std::string text, std::string name);

protected:
    void onContextReady() override;

logos_events:
    void stateChanged(const std::string& snapshotJson);

private:
    // persistence
    void setupDataDir();
    void loadAll();
    void saveFile(const std::string& name, const mulenet::json& j);
    mulenet::json loadFile(const std::string& name, const mulenet::json& dflt);
    void saveRegistry();
    void saveHub();
    void saveParcels();
    void saveJobs();
    void saveMailboxes();
    void saveSettings();
    void saveReceipts();

    // directory
    void refold();
    void author(const std::string& type, const mulenet::json& payload);
    bool ingestRegistryEvent(const mulenet::Event& e);

    // receipts
    void queueReceipt(const mulenet::OutReceipt& r);
    void flushOutbox();

    // transport (loam_core)
    void startTransport();
    void sendFrame(const std::string& topic, const mulenet::json& frame);
    void onFrame(const std::string& topic, const std::string& payloadB64);
    void catchupRound();
    void tick();

    // state
    void publishState();
    mulenet::json buildSnapshot();
    std::string hubName(const std::string& address) const;
    long long nowMs() const;
    std::string fail(const std::string& why);

    std::recursive_mutex m_mtx;
    std::string m_dataDir;
    bool m_storageOk = true;
    std::string m_storageNote;

    mulenet::Identity m_id;
    std::vector<mulenet::Event> m_log;
    mulenet::Directory m_dir;
    std::set<std::string> m_roots;

    bool m_isHub = false;
    mulenet::HubKeys m_hub;
    mulenet::json m_hubCard = mulenet::json::object();
    mulenet::json m_labelKeys = mulenet::json::array();

    mulenet::json m_parcels = mulenet::json::array();
    mulenet::json m_jobs = mulenet::json::array();
    mulenet::json m_mailboxes = mulenet::json::array();
    mulenet::json m_outbox = mulenet::json::array();
    std::vector<mulenet::Event> m_receiptLog;

    std::string m_status = "Starting...";
    bool m_ready = false;
    bool m_started = false;
    QTimer* m_timer = nullptr;
    long long m_lastCatchupMs = 0;
    long m_rx = 0, m_tx = 0, m_rxEvents = 0, m_rxReceipts = 0, m_rxBad = 0;
};
