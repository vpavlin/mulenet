#pragma once
// FAKE logos SDK (test-only): just enough of the universal-module surface for
// mulenet_core_impl.cpp to build and run outside nix/Basecamp, with
// modules().loam_core backed by an in-process bus (fake_bus.h). The real header
// comes from logos-module-builder at nix build time. Pattern from whisperbox_core.
#include <functional>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

#define logos_events public

struct FakeLoamNode;
struct FakeLoamCore {
    FakeLoamNode* node = nullptr;
    using RecvFn = std::function<void(const std::string&, const std::string&, const std::string&, int64_t)>;
    using StatusFn = std::function<void(const std::string&)>;
    using Cb = std::function<void(std::string)>;
    void onReceived(RecvFn fn);
    void onStatusChanged(StatusFn fn);
    void setSenderIdAsync(const std::string& id, Cb cb);
    void startAsync(const std::string& cfg, Cb cb);
    void joinAsync(const std::string& topic, Cb cb);
    void sendSealedAsync(const std::string& topic, const std::string& b64, Cb cb);
};
struct FakeModules { FakeLoamCore loam_core; };

class LogosModuleContext {
public:
    virtual ~LogosModuleContext() = default;
    FakeModules& modules() { return m_fakeModules; }
    void fakeStart() { onContextReady(); }
    const std::string& instancePersistencePath() const { return m_fakePersist; }
    std::string m_fakePersist;
    FakeModules m_fakeModules;
protected:
    virtual void onContextReady() {}
};
