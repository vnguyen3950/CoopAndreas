#include "stdafx.h"
#include "CCutsceneVotes.h"
#include "COpCodeSync.h"
#include "CPacketBuffer.h"
#include "UI/CDXFont.h"
#include <CPad.h>
#include <limits>

namespace {
CutsceneVotes::Client& Votes() { static CutsceneVotes::Client votes; return votes; }
CutsceneVotes::Token hostSerial = 0;
bool sawPlaying = false, wasOnMission = false, managed = false;
CutsceneVotes::SceneName managedName{};
int expectedHost = -1;
CutsceneVotes::Token knownHostTicket = 0;
struct PendingHostScene {
    CutsceneVotes::Snapshot begin{}, latest{};
    CutsceneVotes::Token ticket = 0;
    bool committed = false;
};
auto& PendingScenes() { static std::array<PendingHostScene, CutsceneVotes::MaxPeers> scenes{}; return scenes; }
void ClearPending() { PendingScenes() = {}; }
const CutsceneVotes::Snapshot* PacketState(Packet& packet) {
    switch (packet.GetType()) {
    case ePacketType::CUTSCENE_VOTE_BEGIN: return &static_cast<Packets::Cutscene::Begin&>(packet).state;
    case ePacketType::CUTSCENE_VOTE_STATE: return &static_cast<Packets::Cutscene::Update&>(packet).state;
    case ePacketType::CUTSCENE_VOTE_COMMIT: return &static_cast<Packets::Cutscene::Commit&>(packet).state;
    default: return nullptr;
    }
}
void DiscardQueuedVotes(int preserveHost = -1) {
    auto& packets = GetPacketBuffer().m_packets;
    for (auto it = packets.begin(); it != packets.end();) {
        const auto type = (*it)->GetType();
        if (type == ePacketType::CUTSCENE_VOTE_BEGIN || type == ePacketType::CUTSCENE_VOTE ||
            type == ePacketType::CUTSCENE_VOTE_STATE || type == ePacketType::CUTSCENE_VOTE_COMMIT) {
            const auto* state = PacketState(**it);
            if (preserveHost >= 0 && state && state->Valid() && state->host == preserveHost) { ++it; continue; }
            delete *it; it = packets.erase(it);
        } else ++it;
    }
}
bool PendingUpdate(const CutsceneVotes::Snapshot& state, bool commit) {
    if (state.host < 0 || state.host >= CutsceneVotes::MaxPeers) return false;
    auto& pending = PendingScenes()[state.host];
    if (!pending.ticket || pending.ticket != Votes().SceneTicket()) return false;
    auto candidate = Votes();
    if (!candidate.Begin(pending.begin)) return false;
    if (pending.committed) candidate.Commit(pending.latest); else candidate.Update(pending.latest);
    if (state.phase == CutsceneVotes::Phase::Idle && candidate.Update(state)) { pending = {}; return true; }
    if (commit ? candidate.Commit(state) : candidate.Update(state)) {
        pending.latest = state; pending.committed = commit; return true;
    }
    return false;
}
bool VoteInputFocused() {
    // Native IsForeground (0x746070) tests the full DWORD at this address.
    return *reinterpret_cast<const uint32_t*>(0xC920EC) != 0 && !CChat::m_bInputActive;
}
bool Covered() {
    return Votes().Started() && CCutsceneMgr::dataFileLoaded &&
        Votes().CurrentName() == CutsceneVotes::Name(CCutsceneMgr::ms_cutsceneName);
}
bool Playing() {
    return Covered() && CCutsceneMgr::ms_cutsceneLoadStatus == 2 && CCutsceneMgr::ms_running &&
        !CCutsceneMgr::HasCutsceneFinished();
}
void CancelRequest() {
    if (!CLocalPlayer::m_bIsHost || !CNetwork::m_bAuthenticated || !Votes().Started()) return;
    Packets::Cutscene::Begin packet; packet.active = false; packet.state.serial = hostSerial;
    GetPacketFactory().Send(packet);
}
}
void CCutsceneVotes::Init() {
    // Verified E8 calls to the bool __cdecl native query (the SDK declaration incorrectly says void).
    patch::RedirectCall(0x5B1947, NativeSkipQuery); // native CCutsceneMgr::Update_overlay
    patch::RedirectCall(0x469F0E, NativeSkipQuery); // CRunningScript::Process scene-skip label
    patch::RedirectCall(0x475459, NativeSkipQuery); // script skip-button predicate
}
void CCutsceneVotes::Cancel(bool notifyHost) {
    if (notifyHost) CancelRequest();
    Votes().Invalidate(); sawPlaying = false; ClearPending();
}
void CCutsceneVotes::Reset() {
    Votes().Reset(); sawPlaying = wasOnMission = managed = false;
    expectedHost = -1; knownHostTicket = 0; ClearPending(); DiscardQueuedVotes();
    COpCodeSync::ms_bLoadingCutscene = false;
}
void CCutsceneVotes::HostChanged(int hostId) {
    const auto pending = hostId >= 0 && hostId < CutsceneVotes::MaxPeers ? PendingScenes()[hostId] : PendingHostScene{};
    const bool alreadyBound = expectedHost == hostId && Votes().State().host == hostId &&
        Votes().State().phase != CutsceneVotes::Phase::Idle;
    bool queuedFreshBegin = false, queuedLifecycle = false;
    for (auto* packet : GetPacketBuffer().m_packets) {
        if (packet->GetType() == ePacketType::OPCODE_SYNC) {
            const auto& op = static_cast<Packets::Scripts::OpCodeSync&>(*packet);
            uint16_t command = 0;
            if (op.size >= 4) std::memcpy(&command, op.buffer, sizeof command);
            if (command == 0x02E4 || command == 0x02E7 || command == 0x02EA) queuedLifecycle = true;
        }
        if (packet->GetType() == ePacketType::CUTSCENE_VOTE_BEGIN && !queuedLifecycle) {
            const auto& begin = static_cast<Packets::Cutscene::Begin&>(*packet);
            auto candidate = Votes();
            if (begin.state.host == hostId && Votes().SceneTicket() != knownHostTicket && candidate.Begin(begin.state))
                queuedFreshBegin = true;
        }
    }
    expectedHost = hostId; DiscardQueuedVotes(hostId); ClearPending();
    if (alreadyBound) return; // Initial/duplicate SYSTEM notification must not cancel its own scene.
    if (pending.ticket && pending.ticket == Votes().SceneTicket() && Votes().Begin(pending.begin)) {
        knownHostTicket = Votes().SceneTicket();
        if (pending.committed) Votes().Commit(pending.latest); else Votes().Update(pending.latest);
        return; // SCRIPT START/BEGIN was processed before SYSTEM; retain its deferred start too.
    }
    if (queuedFreshBegin) { Votes().Unbind(); return; } // START processed, new-host BEGIN still buffered.
    Votes().Invalidate(); sawPlaying = false; COpCodeSync::ms_bLoadingCutscene = false;
}
void CCutsceneVotes::Queue(Packet& packet) {
    const auto type = packet.GetType();
    bool related = type == ePacketType::CUTSCENE_VOTE_BEGIN || type == ePacketType::CUTSCENE_VOTE_STATE ||
        type == ePacketType::CUTSCENE_VOTE_COMMIT;
    if (type == ePacketType::OPCODE_SYNC) {
        const auto& opcode = static_cast<const Packets::Scripts::OpCodeSync&>(packet);
        if (opcode.size >= 4) {
            uint16_t command = 0; std::memcpy(&command, opcode.buffer, sizeof command);
            related = command == 0x02E4 || command == 0x02E7 || command == 0x02EA || command == 0x0701;
        }
    }
    if (!related) return;
    // Server-normalized lifecycle/announcements cannot overtake earlier setup
    // opcodes carrying the host's ahead-of-server legacy timestamp. Equal times
    // preserve reliable receive order through CPacketBuffer::upper_bound.
    const auto& packets = GetPacketBuffer().m_packets;
    for (auto it = packets.rbegin(); it != packets.rend(); ++it) {
        if ((*it)->GetChannel() == ePacketChannel::SCRIPT) {
            packet.serverTime = (std::max)(packet.serverTime, (*it)->serverTime); break;
        }
    }
}
void CCutsceneVotes::ObserveOpcode(uint16_t opcode, bool fromHostScript) {
    if (opcode == 0x02E4 || opcode == 0x02EA || opcode == 0x0701) {
        Cancel(fromHostScript);
        if (opcode != 0x0701) managed = false;
        return;
    }
    if (opcode != 0x02E7) return;
    ClearPending(); Votes().Start(CutsceneVotes::Name(CCutsceneMgr::ms_cutsceneName)); sawPlaying = false;
    managedName = Votes().CurrentName(); managed = Votes().Started();
    if (!fromHostScript || !CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost || !Covered()) return;
    if (hostSerial == std::numeric_limits<CutsceneVotes::Token>::max()) { Cancel(); return; }
    Packets::Cutscene::Begin packet;
    packet.state.serial = ++hostSerial; packet.state.name = Votes().CurrentName();
    // Sent after the actual START opcode on the same reliable SCRIPT channel.
    GetPacketFactory().Send(packet);
}
void CCutsceneVotes::Process() {
    if (!CNetwork::m_bAuthenticated) { Reset(); return; }
    // Disarm even if the native skip query is not reached during this frame.
    if (!VoteInputFocused()) Votes().WantsVote(false, false);
    const bool onMission = CTheScripts::OnAMissionFlag && CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag];
    if ((wasOnMission && !onMission) || CTheScripts::FailCurrentMission) {
        COpCodeSync::ms_bLoadingCutscene = false;
        Cancel(true);
    }
    wasOnMission = onMission;
    if (!Votes().Started()) return;
    if (Playing()) sawPlaying = true;
    if (Votes().CurrentName() != CutsceneVotes::Name(CCutsceneMgr::ms_cutsceneName) ||
        CCutsceneMgr::ms_cutsceneLoadStatus == 0 ||
        (sawPlaying && (!CCutsceneMgr::ms_running || CCutsceneMgr::HasCutsceneFinished()))) Cancel(true);
}
bool CCutsceneVotes::NativeSkipQuery() {
    if (!CNetwork::m_bAuthenticated || !managed ||
        managedName != CutsceneVotes::Name(CCutsceneMgr::ms_cutsceneName) || !CCutsceneMgr::dataFileLoaded)
        return plugin::CallAndReturn<bool, 0x4D5D10>();
    if (CTheScripts::FailCurrentMission || (wasOnMission && CTheScripts::OnAMissionFlag &&
        !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])) {
        Cancel(true); COpCodeSync::ms_bLoadingCutscene = false; return false;
    }
    const bool focused = VoteInputFocused();
    // Feed actual held Space state: the scene-bound focused-release latch
    // supplies the edge. A broad/just-pressed result would confuse other
    // inputs or a held key with a release.
    const bool input = focused && CPad::NewKeyState.standardKeys[' '] != 0;
    const bool playing = Playing();
    if (playing && Votes().WantsVote(input, focused)) {
        Packets::Cutscene::Vote packet;
        packet.generation = Votes().State().generation; packet.identity = Votes().State().identity;
        GetPacketFactory().Send(packet); Votes().MarkVoteSent();
    }
    // Do not call Finish/Delete directly: the original caller handles native skip and SCM cleanup.
    return Votes().Skip(playing, CCutsceneMgr::HasCutsceneFinished());
}
void CCutsceneVotes::ReceiveBegin(const Packets::Cutscene::Begin& packet) {
    if (!CNetwork::m_bAuthenticated || !packet.active) return;
    if (expectedHost >= 0 && packet.state.host != expectedHost) {
        if (packet.state.host < 0 || packet.state.host >= CutsceneVotes::MaxPeers) return;
        if (Votes().SceneTicket() == knownHostTicket) return; // No replacement START has been observed.
        auto candidate = Votes();
        auto& pending = PendingScenes()[packet.state.host];
        if (packet.state.generation <= pending.begin.generation || !candidate.Begin(packet.state)) return;
        pending = {packet.state, packet.state, Votes().SceneTicket(), false};
        return; // Authenticated server announcement awaits SYSTEM authority confirmation.
    }
    if (CLocalPlayer::m_bIsHost && packet.state.serial != hostSerial) return;
    if (Votes().Begin(packet.state)) { expectedHost = packet.state.host; knownHostTicket = Votes().SceneTicket(); }
}
void CCutsceneVotes::ReceiveState(const Packets::Cutscene::Update& packet) {
    if (CNetwork::m_bAuthenticated && packet.state.host != expectedHost) { PendingUpdate(packet.state, false); return; }
    if (CNetwork::m_bAuthenticated && packet.state.host == expectedHost && Votes().Update(packet.state) &&
        packet.state.phase == CutsceneVotes::Phase::Idle) COpCodeSync::ms_bLoadingCutscene = false;
}
void CCutsceneVotes::ReceiveCommit(const Packets::Cutscene::Commit& packet) {
    if (CNetwork::m_bAuthenticated && packet.state.host != expectedHost) { PendingUpdate(packet.state, true); return; }
    if (CNetwork::m_bAuthenticated && packet.state.host == expectedHost && Covered()) Votes().Commit(packet.state);
}
void CCutsceneVotes::Draw() {
    if (!CNetwork::m_bAuthenticated || !Playing()) return;
    const auto& state = Votes().State();
    std::string text = "Cutscene skip vote: waiting for host";
    if (state.phase != CutsceneVotes::Phase::Idle) {
        text = "Skip votes: " + std::to_string(state.votes) + "/" + std::to_string(state.total);
        if (!state.eligible) text += " (not in this scene's voting roster)";
        else if (state.voted) text += " - your vote is counted";
        else text += " - press Space to vote";
    }
    CDXFont::Draw(20, RsGlobal.maximumHeight - 2 * CDXFont::m_fFontSize, text, D3DCOLOR_ARGB(255, 255, 255, 255));
}
