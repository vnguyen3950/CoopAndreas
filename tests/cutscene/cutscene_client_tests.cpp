#include "native_doubles.h"
#include "client/src/CCutsceneVotes.h"
#include "extracted_client.inc"
#include "extracted_deferred.inc"
#include "extracted_receive.inc"
using namespace CutsceneVotes;
static Snapshot Announcement(Token generation) {
    Snapshot s; s.generation = generation; s.serial = generation; s.identity = 11;
    s.host = 0; s.name = Name("INTRO1A"); s.total = 2; s.phase = Phase::Active; s.eligible = true; return s;
}
int main() {
    CCutsceneVotes::Reset();
    CCutsceneVotes::ObserveOpcode(0x02E4, false);
    CCutsceneVotes::ObserveOpcode(0x02E7, false);
    COpCodeSync::ms_bLoadingCutscene = true;
    Packets::Cutscene::Begin begin; begin.state = Announcement(1);
    CCutsceneVotes::ReceiveBegin(begin);
    expect(Votes().State().generation == 1, "BEGIN retained while guest native load is pending");
    Packets::Cutscene::Commit commit; commit.state = begin.state;
    commit.state.phase = Phase::Committed; commit.state.votes = commit.state.total;
    CCutsceneVotes::ReceiveCommit(commit);
    expect(!CCutsceneVotes::NativeSkipQuery(), "Commit cannot skip before playback starts");
    CCutsceneMgr::ms_cutsceneLoadStatus = 2;
    ProcessActualDeferredStart();
    expect(CCutsceneMgr::ms_running && !COpCodeSync::ms_bLoadingCutscene, "Actual Main deferred START branch executes");
    expect(Votes().State().generation == 1, "Deferred START does not replace announcement ticket");
    expect(CCutsceneVotes::NativeSkipQuery(), "Matching deferred commit releases original skip input path");
    CCutsceneVotes::ObserveOpcode(0x02EA, false);
    CCutsceneVotes::ObserveOpcode(0x02E4, false);
    CCutsceneVotes::ObserveOpcode(0x02E7, false);
    CCutsceneVotes::ReceiveCommit(commit);
    expect(!CCutsceneVotes::NativeSkipQuery(), "Identical-name replacement rejects old commit");
    begin.state = Announcement(2); CCutsceneVotes::ReceiveBegin(begin);
    COpCodeSync::ms_bLoadingCutscene = true;
    Packets::Cutscene::Update cancelled; cancelled.state = begin.state;
    cancelled.state.phase = Phase::Idle; cancelled.state.total = cancelled.state.votes = 0;
    cancelled.state.eligible = cancelled.state.voted = false;
    CCutsceneVotes::ReceiveState(cancelled);
    expect(!COpCodeSync::ms_bLoadingCutscene, "Server cancellation invalidates deferred native START");
    CCutsceneVotes::ObserveOpcode(0x02E7, false); begin.state = Announcement(3); CCutsceneVotes::ReceiveBegin(begin);
    GetPacketFactory().sent.clear();
    Double::pressed = false; CCutsceneVotes::NativeSkipQuery();
    Double::focused = false; CCutsceneVotes::NativeSkipQuery();
    Double::pressed = true; Double::focused = true;
    expect(!CCutsceneVotes::NativeSkipQuery() && GetPacketFactory().sent.empty(), "Focus return with held input casts no vote");
    Double::pressed = false; CCutsceneVotes::NativeSkipQuery();
    Double::pressed = true; CCutsceneVotes::NativeSkipQuery();
    expect(GetPacketFactory().sent.size() == 1 && GetPacketFactory().sent.back().second->GetType() == ePacketType::CUTSCENE_VOTE,
        "Focused native release and press emits one real vote packet");
    CCutsceneVotes::NativeSkipQuery();
    expect(GetPacketFactory().sent.size() == 1, "Repeated native query does not duplicate vote");
    CCutsceneVotes::Cancel();
    expect(!CCutsceneVotes::NativeSkipQuery(), "Migration cancellation does not restore unilateral skipping of managed scene");
    commit.state = begin.state; commit.state.phase = Phase::Committed; commit.state.votes = 2;
    CCutsceneVotes::ReceiveCommit(commit);
    expect(!CCutsceneVotes::NativeSkipQuery(), "Cancelled binding rejects delayed commit");
    GetPacketBuffer().m_packets.push_back(new Packets::Cutscene::Begin(begin));
    GetPacketBuffer().m_packets.push_back(new Packets::Cutscene::Commit(commit));
    CCutsceneVotes::HostChanged(1);
    expect(GetPacketBuffer().m_packets.empty(), "Migration discards queued old-host vote packets");
    CCutsceneVotes::ObserveOpcode(0x02E7, false); begin.state = Announcement(4);
    CCutsceneVotes::ReceiveBegin(begin);
    expect(Votes().State().generation == 0, "Late old-host BEGIN cannot rebind after migration");
    CCutsceneVotes::HostChanged(0);
    CCutsceneVotes::ObserveOpcode(0x02E7, false); begin.state = Announcement(4); CCutsceneVotes::ReceiveBegin(begin);
    CTheScripts::FailCurrentMission = 1; COpCodeSync::ms_bLoadingCutscene = true;
    expect(!CCutsceneVotes::NativeSkipQuery(), "Native query cancels failure before the after-process callback");
    CCutsceneVotes::Process();
    expect(!Votes().Started() && !COpCodeSync::ms_bLoadingCutscene, "Failure cancels votes and deferred native START");
    CTheScripts::FailCurrentMission = 0;
    CCutsceneVotes::ObserveOpcode(0x02E7, false); begin.state = Announcement(5); CCutsceneVotes::ReceiveBegin(begin);
    CCutsceneVotes::Process(); CTheScripts::ScriptSpace[1] = 0; CCutsceneVotes::Process();
    expect(!Votes().Started(), "Mission teardown invalidates scene ticket");
    CCutsceneVotes::ObserveOpcode(0x02E7, false); begin.state = Announcement(6); CCutsceneVotes::ReceiveBegin(begin);
    CNetwork::m_bAuthenticated = false; COpCodeSync::ms_bLoadingCutscene = true; CCutsceneVotes::Process();
    expect(!Votes().Started() && !COpCodeSync::ms_bLoadingCutscene, "Disconnect clears scene and pending start");
    CNetwork::m_bAuthenticated = true; CCutsceneVotes::Reset();
    std::memcpy(CCutsceneMgr::ms_cutsceneName, "finale", 7); CCutsceneVotes::ObserveOpcode(0x02E7, false);
    Double::pressed = true;
    expect(CCutsceneVotes::NativeSkipQuery(), "Excluded native finale keeps original query behavior");
    // Real Receive inserts with timestamp upper_bound. A prior legacy setup
    // opcode is ahead of server time; lifecycle and announcements must follow it.
    CCutsceneVotes::Reset();
    auto* setup = new Packets::Scripts::OpCodeSync; setup->serverTime = 50000;
    uint16_t opcode = 0x015F; std::memcpy(setup->buffer, &opcode, 2);
    GetPacketBuffer().Receive(setup);
    auto* start = new Packets::Scripts::OpCodeSync; start->serverTime = 1000;
    opcode = 0x02E7; std::memcpy(start->buffer, &opcode, 2);
    GetPacketBuffer().Receive(start);
    auto* announcement = new Packets::Cutscene::Begin; announcement->serverTime = 1000;
    auto* finish = new Packets::Cutscene::Commit; finish->serverTime = 1010;
    GetPacketBuffer().Receive(announcement); GetPacketBuffer().Receive(finish);
    const auto& queue = GetPacketBuffer().m_packets;
    expect(queue.size() == 4 && queue[0] == setup && queue[1] == start && queue[2] == announcement && queue[3] == finish,
        "Actual buffer keeps setup, START, BEGIN, COMMIT reliable order despite host clock being ahead");
    expect(setup->serverTime == 50000 && start->serverTime == 50000 && announcement->serverTime == 50000,
        "Only cutscene-related packet timestamps are aligned to queued SCRIPT predecessor");
    auto* unrelated = new Packets::Scripts::OpCodeSync; unrelated->serverTime = 900;
    opcode = 0x1234; std::memcpy(unrelated->buffer, &opcode, 2); GetPacketBuffer().Receive(unrelated);
    expect(GetPacketBuffer().m_packets.front() == unrelated && unrelated->serverTime == 900,
        "Unrelated legacy opcode preserves existing timestamp insertion");
    for (auto* packet : GetPacketBuffer().m_packets) delete packet;
    GetPacketBuffer().m_packets.clear();
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
