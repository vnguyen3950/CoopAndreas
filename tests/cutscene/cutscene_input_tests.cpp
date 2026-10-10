#include "native_doubles.h"
#include "client/src/CCutsceneVotes.h"
#include "extracted_client.inc"
using namespace CutsceneVotes;
static_assert(sizeof(CKeyboardState) == 0x270, "Actual SDK keyboard ABI");
static_assert(offsetof(CKeyboardState, standardKeys) == 0x18, "Actual SDK key-array ABI");
static_assert(sizeof(CPad::NewKeyState.standardKeys[0]) == 2, "Native keyboard cells are WORDs");
static_assert(offsetof(CKeyboardState, standardKeys) + 32 * sizeof(short) == 0x58, "Native Space cell offset");
static Token generation = 100;
static Snapshot snapshot;
static void Space(bool down) { CPad::NewKeyState.standardKeys[' '] = down ? 128 : 0; }
static size_t VoteCount() {
    size_t count = 0;
    for (const auto& item : GetPacketFactory().sent)
        if (item.second->GetType() == ePacketType::CUTSCENE_VOTE) ++count;
    return count;
}
static void Prepare(bool held = false, bool playing = true) {
    CNetwork::m_bAuthenticated = true; CLocalPlayer::m_bIsHost = false;
    Double::focused = true; Double::foreground = 1; Double::pressed = false; Double::nativeQueries = 0;
    CChat::m_bInputActive = false;
    CPad::NewKeyState = {}; CPad::OldKeyState = {};
    CPad::NewMouseControllerState = {}; CPad::GetPad(0)->NewState = {};
    Space(held);
    CCutsceneMgr::dataFileLoaded = true; CCutsceneMgr::finished = false;
    CCutsceneMgr::ms_running = playing; CCutsceneMgr::ms_cutsceneLoadStatus = playing ? 2 : 1;
    std::memcpy(CCutsceneMgr::ms_cutsceneName, "INTRO1A", 8);
    CTheScripts::OnAMissionFlag = 1; CTheScripts::ScriptSpace[1] = 1; CTheScripts::FailCurrentMission = 0;
    CCutsceneVotes::Reset(); CCutsceneVotes::ObserveOpcode(0x02E7, false);
    snapshot = {}; snapshot.host = 0; snapshot.generation = snapshot.serial = ++generation;
    snapshot.identity = 11; snapshot.name = Name("INTRO1A"); snapshot.total = 2;
    snapshot.phase = Phase::Active; snapshot.eligible = true;
    Packets::Cutscene::Begin begin; begin.state = snapshot; CCutsceneVotes::ReceiveBegin(begin);
    GetPacketFactory().sent.clear();
    if (!held && playing) CCutsceneVotes::NativeSkipQuery(); // Actual focused Space release arms this scene.
}
int main() {
    bool otherKeysSafe = true;
    unsigned otherKeyCases = 0;
    for (int key = 0; key < 256; ++key) {
        if (key == ' ') continue;
        Prepare(); CPad::NewKeyState.standardKeys[key] = 128;
        CPad::GetPad(0)->NewState.ButtonCross = 128;
        Double::pressed = true; // Recorded original native query would accept controller Cross.
        CCutsceneVotes::NativeSkipQuery(); ++otherKeyCases;
        otherKeysSafe = otherKeysSafe && VoteCount() == 0;
    }
    expect(otherKeysSafe && otherKeyCases == 255, "All 255 other native standard keys cannot vote, including simultaneous broad controller input");
    short CKeyboardState::* specialKeys[] = {
        &CKeyboardState::enter, &CKeyboardState::extenter, &CKeyboardState::esc,
        &CKeyboardState::tab, &CKeyboardState::back, &CKeyboardState::up,
        &CKeyboardState::down, &CKeyboardState::left, &CKeyboardState::right,
        &CKeyboardState::lshift, &CKeyboardState::lctrl, &CKeyboardState::lmenu
    };
    for (const auto key : specialKeys) {
        Prepare(); CPad::NewKeyState.*key = 128; Double::pressed = true;
        CCutsceneVotes::NativeSkipQuery();
        expect(VoteCount() == 0, "Native special-key input cannot cast a Space vote");
    }
    for (int key = 0; key < 12; ++key) {
        Prepare(); CPad::NewKeyState.FKeys[key] = 128; Double::pressed = true;
        CCutsceneVotes::NativeSkipQuery(); expect(VoteCount() == 0, "Native function keys cannot cast a Space vote");
    }
    Prepare(); CPad::NewMouseControllerState.lmb = CPad::NewMouseControllerState.rmb = 1;
    CPad::NewMouseControllerState.wheelUp = 1; Double::pressed = true;
    CCutsceneVotes::NativeSkipQuery(); expect(VoteCount() == 0, "Mouse buttons and wheel cannot vote");
    Prepare(); CPad::GetPad(0)->NewState.ButtonCross = CPad::GetPad(0)->NewState.ButtonCircle = 128;
    CPad::GetPad(0)->NewState.ButtonSquare = CPad::GetPad(0)->NewState.Start = 128;
    Double::pressed = true; CCutsceneVotes::NativeSkipQuery(); expect(VoteCount() == 0, "Controller buttons cannot vote");
    expect(Double::nativeQueries == 0, "Managed scenes never read the broad native query");

    Prepare(true); Double::pressed = true;
    CCutsceneVotes::NativeSkipQuery(); CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 0, "Space held before START and BEGIN must not vote");
    Space(false); CCutsceneVotes::NativeSkipQuery(); Space(true);
    expect(!CCutsceneVotes::NativeSkipQuery() && VoteCount() == 1, "Focused release then Space press sends one vote, without unilateral skip");
    bool exactIdentity = false;
    if (VoteCount() == 1) {
        const auto& vote = static_cast<const Packets::Cutscene::Vote&>(*GetPacketFactory().sent.back().second);
        exactIdentity = vote.generation == snapshot.generation && vote.identity == snapshot.identity;
    }
    expect(exactIdentity, "Space vote keeps exact current generation and identity");
    CCutsceneVotes::NativeSkipQuery(); Space(false); CCutsceneVotes::NativeSkipQuery(); Space(true); CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 1, "Held Space and repeated release/repress do not duplicate this scene's vote");
    expect(Double::nativeQueries == 0, "Space voting itself does not call the broad native query");
    Packets::Cutscene::Update counted; counted.state = snapshot;
    counted.state.votes = 1; counted.state.voted = true; CCutsceneVotes::ReceiveState(counted);
    CCutsceneVotes::Draw();
    expect(Double::overlay.find("your vote is counted") != std::string::npos, "Overlay retains actual counted-vote guidance");
    Prepare(); CCutsceneVotes::Draw();
    expect(Double::overlay.find("Space") != std::string::npos && Double::overlay.find("Enter") == std::string::npos &&
        Double::overlay.find("click") == std::string::npos && Double::overlay.find("controller") == std::string::npos,
        "Eligible overlay advertises only Space");

    Prepare(true, false); Double::pressed = true; CCutsceneVotes::NativeSkipQuery();
    CCutsceneMgr::ms_cutsceneLoadStatus = 2; CCutsceneMgr::ms_running = true; CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 0, "Space held through deferred native START cannot vote");
    Space(false); CCutsceneVotes::NativeSkipQuery(); Space(true); CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 1, "Deferred playback permits one fresh focused Space edge");

    Prepare(); Double::foreground = 0x100u; Space(false); CCutsceneVotes::NativeSkipQuery();
    Space(true); Double::pressed = true; CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 1, "Native nonzero DWORD foreground remains focused even when its low byte is zero");
    Prepare(); Double::foreground = 0; Space(true); Double::pressed = true; CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 0, "Native zero DWORD foreground inhibits Space input");
    for (const bool chat : {false, true}) {
        Prepare();
        if (chat) CChat::m_bInputActive = true; else Double::focused = false;
        Space(true); Double::pressed = true; CCutsceneVotes::NativeSkipQuery();
        expect(VoteCount() == 0, "Foreground loss or active chat inhibits a Space vote");
        CChat::m_bInputActive = false; Double::focused = true; CCutsceneVotes::NativeSkipQuery();
        expect(VoteCount() == 0, "Focus/chat return with held Space needs a new release");
        Space(false); CCutsceneVotes::NativeSkipQuery(); Space(true); CCutsceneVotes::NativeSkipQuery();
        expect(VoteCount() == 1, "Actual focused release/repress after inhibition casts one vote");

        Prepare();
        if (chat) CChat::m_bInputActive = true; else Double::focused = false;
        CCutsceneVotes::Process(); // No native query while chat/foreground is inhibited.
        Space(true); Double::pressed = true; CChat::m_bInputActive = false; Double::focused = true;
        CCutsceneVotes::NativeSkipQuery();
        expect(VoteCount() == 0, "Actual Process disarms focus/chat loss between native input queries");
    }
    Prepare(); Space(true); CCutsceneVotes::NativeSkipQuery();
    CCutsceneVotes::ObserveOpcode(0x02E7, false);
    snapshot.generation = snapshot.serial = ++generation;
    Packets::Cutscene::Begin replacement; replacement.state = snapshot; CCutsceneVotes::ReceiveBegin(replacement);
    GetPacketFactory().sent.clear(); Double::pressed = true; CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 0, "Same-name replacement scene rearms and rejects preexisting held Space");
    Space(false); CCutsceneVotes::NativeSkipQuery(); Space(true); CCutsceneVotes::NativeSkipQuery();
    expect(VoteCount() == 1, "Replacement scene accepts only its fresh Space edge");
    Packets::Cutscene::Commit commit; commit.state = snapshot; commit.state.phase = Phase::Committed; commit.state.votes = 2;
    CCutsceneVotes::ReceiveCommit(commit); Space(false);
    expect(CCutsceneVotes::NativeSkipQuery() && VoteCount() == 1, "Matching unanimous commit retains original native skip path without another input vote");

    Prepare(); CCutsceneVotes::Reset(); Double::pressed = true; Double::nativeQueries = 0;
    expect(CCutsceneVotes::NativeSkipQuery() && VoteCount() == 0 && Double::nativeQueries == 1, "Unmanaged native scenes preserve original broad input");
    Prepare(); CNetwork::m_bAuthenticated = false; Double::pressed = true; Double::nativeQueries = 0;
    expect(CCutsceneVotes::NativeSkipQuery() && VoteCount() == 0 && Double::nativeQueries == 1, "Offline behavior still delegates to original broad query");
    Prepare(); std::memcpy(CCutsceneMgr::ms_cutsceneName, "finale", 7); CCutsceneVotes::ObserveOpcode(0x02E7, false); Double::pressed = true; Double::nativeQueries = 0;
    expect(CCutsceneVotes::NativeSkipQuery() && VoteCount() == 0 && Double::nativeQueries == 1, "Excluded native finale preserves original broad input");
    std::cout << checks << " Space input assertions, " << failures << " failures; " << otherKeyCases << " other-key cases\n";
    return failures ? 1 : 0;
}
