#include "native_doubles.h"
#include "client/src/CCutsceneVotes.h"
#include "extracted_client.inc"
#include "extracted_receive.inc"
using namespace CutsceneVotes;
static Snapshot Scene(int host, Token generation) {
    Snapshot s; s.host = int8_t(host); s.generation = generation; s.serial = generation;
    s.identity = 11; s.name = Name("INTRO1A"); s.total = 2; s.phase = Phase::Active; s.eligible = true; return s;
}
static void OldScene() {
    CNetwork::m_bAuthenticated = true; CLocalPlayer::m_bIsHost = false;
    CCutsceneMgr::ms_cutsceneLoadStatus = 2; CCutsceneMgr::ms_running = true;
    CCutsceneMgr::finished = false;
    CCutsceneVotes::Reset(); CCutsceneVotes::HostChanged(0); CCutsceneVotes::ObserveOpcode(0x02E7, false);
    Packets::Cutscene::Begin old; old.state = Scene(0, 1); CCutsceneVotes::ReceiveBegin(old);
}
static void FreshStart() { CCutsceneVotes::ObserveOpcode(0x02E4, false); CCutsceneVotes::ObserveOpcode(0x02E7, false); }
int main() {
    // Independent reviewer vote-migration-001's two failing cases, using the same actual service functions.
    OldScene(); FreshStart();
    Packets::Cutscene::Begin next; next.state = Scene(1, 2); CCutsceneVotes::ReceiveBegin(next);
    expect(Votes().State().generation == 0, "Unconfirmed host announcement cannot yet authorize a vote");
    CCutsceneVotes::HostChanged(1);
    expect(Votes().Started() && Votes().State().generation == 2, "Processed new-host START/BEGIN survives later SYSTEM assignment");
    CCutsceneVotes::Reset(); CCutsceneVotes::HostChanged(0);
    GetPacketBuffer().m_packets.push_back(new Packets::Cutscene::Begin(next));
    CCutsceneVotes::HostChanged(1);
    expect(!GetPacketBuffer().m_packets.empty(), "Migration preserves new host's queued announcement");
    CCutsceneVotes::Reset();

    // SYSTEM first, then the complete SCRIPT lifecycle.
    OldScene(); CCutsceneVotes::HostChanged(1); FreshStart(); CCutsceneVotes::ReceiveBegin(next);
    expect(Votes().State().generation == 2, "SYSTEM-first order binds new host's later START/BEGIN");

    // START already processed, BEGIN still in the timestamp buffer when SYSTEM arrives.
    OldScene(); FreshStart(); COpCodeSync::ms_bLoadingCutscene = true;
    auto* queuedBegin = new Packets::Cutscene::Begin(next); GetPacketBuffer().Receive(queuedBegin);
    CCutsceneVotes::HostChanged(1);
    expect(Votes().Started() && COpCodeSync::ms_bLoadingCutscene, "New deferred START retained while its BEGIN remains queued");
    GetPacketBuffer().m_packets.pop_front(); CCutsceneVotes::ReceiveBegin(*queuedBegin); delete queuedBegin;
    expect(Votes().State().generation == 2, "Queued BEGIN binds retained new scene ticket");

    // Both START and BEGIN are queued: cancel the old binding, process the new lifecycle normally.
    OldScene();
    auto* start = new Packets::Scripts::OpCodeSync;
    uint16_t opcode = 0x02E7; std::memcpy(start->buffer, &opcode, sizeof opcode);
    GetPacketBuffer().Receive(start); GetPacketBuffer().Receive(new Packets::Cutscene::Begin(next));
    CCutsceneVotes::HostChanged(1);
    expect(!Votes().Started(), "Queued new START does not preserve the prior host's active scene ticket");
    GetPacketBuffer().m_packets.pop_front(); CCutsceneVotes::ObserveOpcode(0x02E7, false); delete start;
    queuedBegin = static_cast<Packets::Cutscene::Begin*>(GetPacketBuffer().m_packets.front());
    GetPacketBuffer().m_packets.pop_front(); CCutsceneVotes::ReceiveBegin(*queuedBegin); delete queuedBegin;
    expect(Votes().State().generation == 2, "Queued START followed by preserved BEGIN restores voting");

    // Retain new-owner votes/commit received before SYSTEM, but never replay the old owner's commit.
    OldScene(); FreshStart(); CCutsceneVotes::ReceiveBegin(next);
    Packets::Cutscene::Commit committed; committed.state = next.state; committed.state.phase = Phase::Committed;
    committed.state.votes = committed.state.total;
    CCutsceneVotes::ReceiveCommit(committed);
    expect(!CCutsceneVotes::NativeSkipQuery(), "Pre-SYSTEM commit cannot skip without confirmed host authority");
    CCutsceneVotes::HostChanged(1);
    expect(CCutsceneVotes::NativeSkipQuery(), "Confirmed matching new-host commit releases native path");
    committed.state = Scene(0, 1); committed.state.phase = Phase::Committed; committed.state.votes = 2;
    CCutsceneVotes::ReceiveCommit(committed);
    expect(Votes().State().host == 1 && Votes().State().generation == 2, "Delayed old-host commit cannot overwrite new binding");
    CCutsceneVotes::HostChanged(1);
    expect(Votes().State().generation == 2, "Duplicate/initial SYSTEM notification preserves its already-bound scene");

    OldScene(); FreshStart(); CCutsceneVotes::ReceiveBegin(next);
    CCutsceneVotes::ObserveOpcode(0x02EA, false); CCutsceneVotes::HostChanged(1);
    expect(!Votes().Started(), "CLEAR invalidates a staged foreign-host announcement before migration");
    OldScene(); FreshStart(); CCutsceneVotes::ReceiveBegin(next);
    CCutsceneVotes::ObserveOpcode(0x02E7, false); CCutsceneVotes::HostChanged(1);
    expect(!Votes().Started(), "Replacement START invalidates staged announcement even with identical name");
    OldScene(); CCutsceneVotes::ReceiveBegin(next); CCutsceneVotes::HostChanged(1);
    expect(!Votes().Started(), "Announcement without a replacement START cannot rebind old native scene");

    CCutsceneVotes::Reset();
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
