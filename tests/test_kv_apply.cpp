#include "../app/statemachine/KVStateMachine.h"
#include "../app/protocol/Protocol.h"

#include <iostream>
#include <cassert>
#include <string>

using namespace app;

namespace {

void decodeResult(const std::vector<uint8_t>& result,
                  bool& found, std::string& value) {
    protocol::decodeApplyResult(
        std::string(result.begin(), result.end()), found, value);
}

} // namespace

int main() {
    KVStateMachine fsm;

    // --- SET on new key ---
    auto setCmd = protocol::encodeSet("x", "5");
    auto result = fsm.apply(
        std::vector<uint8_t>(setCmd.begin(), setCmd.end()));
    bool found; std::string value;
    decodeResult(result, found, value);
    assert(!found);  // new key, no previous value
    std::cout << "SET x=5 → found=" << found << " oldVal=\"" << value << "\"\n";

    // --- GET x → should return 5 ---
    auto getCmd = protocol::encodeGet("x");
    result = fsm.apply(
        std::vector<uint8_t>(getCmd.begin(), getCmd.end()));
    decodeResult(result, found, value);
    assert(found);
    assert(value == "5");
    std::cout << "GET x → found=" << found << " val=\"" << value << "\"\n";

    // --- SET overwrites, returns old value ---
    setCmd = protocol::encodeSet("x", "10");
    result = fsm.apply(
        std::vector<uint8_t>(setCmd.begin(), setCmd.end()));
    decodeResult(result, found, value);
    assert(found);
    assert(value == "5");
    std::cout << "SET x=10 → found=" << found << " oldVal=\"" << value << "\"\n";

    // --- DEL returns old value ---
    auto delCmd = protocol::encodeDel("x");
    result = fsm.apply(
        std::vector<uint8_t>(delCmd.begin(), delCmd.end()));
    decodeResult(result, found, value);
    assert(found);
    assert(value == "10");
    std::cout << "DEL x → found=" << found << " oldVal=\"" << value << "\"\n";

    // --- GET after DEL → not found ---
    getCmd = protocol::encodeGet("x");
    result = fsm.apply(
        std::vector<uint8_t>(getCmd.begin(), getCmd.end()));
    decodeResult(result, found, value);
    assert(!found);
    std::cout << "GET x after DEL → found=" << found << "\n";

    // --- Direct get() helper ---
    std::string directVal;
    assert(!fsm.get("x", directVal));
    assert(fsm.size() == 0);

    // --- Snapshot / restore ---
    {
        auto cmd = protocol::encodeSet("a", "1");
        fsm.apply(std::vector<uint8_t>(cmd.begin(), cmd.end()));
    }
    {
        auto cmd = protocol::encodeSet("b", "2");
        fsm.apply(std::vector<uint8_t>(cmd.begin(), cmd.end()));
    }
    auto snap = fsm.snapshot();
    std::cout << "Snapshot size: " << snap.size() << " bytes\n";

    KVStateMachine restored;
    restored.restore(snap);
    assert(restored.size() == 2);
    std::string v;
    assert(restored.get("a", v) && v == "1");
    assert(restored.get("b", v) && v == "2");
    std::cout << "Restore: a=1, b=2 — both present\n";

    std::cout << "test_kv_apply: PASS\n";
    return 0;
}
