#include <iostream>
#include "network/cutscene_votes.h"
using namespace CutsceneVotes;
static unsigned checks = 0, failures = 0;
static void expect(bool ok, const char* message) {
    ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
int main() {
    Room room;
    room.Join(0); room.Join(1); room.GameplayReady(0); room.GameplayReady(1); room.HostChanged(0); room.Prepare(0); room.Prepare(1); room.Start(0); room.Start(1);
    SceneName name = Name("INTRO1A");
    expect(!room.Begin(1, 1, name), "Guest cannot announce a scene");
    expect(room.Begin(0, 1, name), "Host begins with captured roster");
    const auto first = room.For(1);
    expect(first.eligible && first.total == 2, "Connected eligible peers captured");
    expect(!room.Begin(0, 1, name), "Duplicate begin does not erase votes");
    expect(!room.Vote(2, first.generation, first.identity), "Unknown sender rejected");
    expect(!room.Vote(1, first.generation, first.identity + 1), "Forged incarnation rejected");
    expect(room.Vote(1, first.generation, first.identity), "Authenticated guest vote counted");
    expect(!room.Vote(1, first.generation, first.identity), "Duplicate vote ignored");
    room.Join(2); room.GameplayReady(2);
    expect(room.For(2).total == 2 && !room.For(2).eligible, "Late join does not change threshold");
    expect(!room.Vote(2, first.generation, room.For(2).identity), "Late join cannot vote");
    expect(room.Vote(0, first.generation, room.For(0).identity), "Host casts explicit vote");
    expect(room.Current().phase == Phase::Committed, "All captured votes commit");
    expect(room.Begin(0, 2, name), "Same scene name has fresh generation");
    expect(!room.Vote(1, first.generation, first.identity), "Stale generation vote rejected");
    room.Leave(2); room.Leave(1); room.Join(1);
    expect(!room.For(1).eligible, "Reused ID is not captured connection");
    expect(!room.Vote(1, room.Current().generation, first.identity), "Previous connection token rejected");
    expect(room.Vote(0, room.Current().generation, room.For(0).identity), "Remaining host votes");
    expect(room.Current().phase == Phase::Committed, "Departed voters do not deadlock");
    expect(room.Begin(0, 3, name), "Next generation starts");
    const auto beforeCancel = room.Current();
    expect(!room.Cancel(1, 3), "Guest cannot cancel room vote");
    expect(room.Cancel(0, 3), "Host clear cancels");
    expect(!room.Vote(0, beforeCancel.generation, room.For(0).identity), "Cancelled vote rejected");
    expect(room.Begin(0, 4, name), "Retry creates generation");
    room.HostChanged(1);
    expect(room.Current().phase == Phase::Idle, "Migration cancels pending votes");
    expect(!room.Begin(0, 5, name), "Former host rejected");
    room.GameplayReady(1); room.Prepare(1); room.Start(1);
    expect(room.Begin(1, 1, name), "New host has independent serial");
    room.Leave(1);
    expect(room.Current().phase == Phase::Idle, "Host departure cancels");

    Client client;
    Snapshot scene = first; scene.phase = Phase::Active;
    expect(!client.Begin(scene), "Begin without a synchronized START rejected");
    client.Start(name);
    expect(client.Begin(scene), "Begin binds current local scene ticket");
    expect(!client.Skip(true, false), "Native input alone cannot skip");
    client.WantsVote(false, true);
    expect(client.WantsVote(true, true), "Fresh focused native input casts vote");
    client.MarkVoteSent();
    expect(!client.WantsVote(true, true), "Held/repeated input cannot resend");
    scene.phase = Phase::Committed; scene.votes = scene.total;
    expect(client.Commit(scene), "Matching commit accepted");
    expect(client.Skip(true, false), "Commit releases native skip path");
    expect(!client.Skip(false, false), "Commit waits for local loaded and playing scene");
    expect(!client.Skip(true, true), "Finished scene cannot skip again");
    client.Invalidate(); client.Start(name);
    expect(!client.Commit(scene), "Delayed commit cannot affect replacement with same name");
    expect(!client.Begin(scene), "Already seen stale generation cannot bind later scene");
    scene.generation++; scene.phase = Phase::Active; scene.votes = 0;
    expect(client.Begin(scene), "Fresh generation binds replacement");
    client.Invalidate();
    expect(!client.Commit(scene), "CLEAR/end/failure invalidation rejects commit");
    client.Start(name); scene.generation++; scene.eligible = false;
    expect(client.Begin(scene), "Late observer receives status");
    expect(!client.WantsVote(true, true), "Ineligible observer cannot vote");
    expect(!ValidName(Name("finale")), "Unskippable finale excluded");
    expect(!ValidName(Name("")), "Empty scene excluded");

    Client focused;
    scene = first; focused.Start(name); expect(focused.Begin(scene), "Focus test begins");
    focused.WantsVote(false, true);
    expect(!focused.WantsVote(false, false), "Focus loss disarms a previously released input");
    expect(!focused.WantsVote(true, true), "Returning focus with held input cannot vote");
    focused.WantsVote(false, true);
    expect(focused.WantsVote(true, true), "Actual focused release then press can vote");

    Client display;
    scene = first; scene.total = 4; scene.votes = 2;
    display.Start(name); expect(display.Begin(scene), "Departure display begins");
    scene.total = 3; scene.votes = 1;
    expect(display.Update(scene), "Already-voted departure lowers total and votes");
    expect(display.State().votes == 1 && display.State().total == 3, "Display reflects remaining electorate");
    auto invalid = scene; invalid.votes = 0;
    expect(!display.Update(invalid), "Votes cannot disappear without corresponding departures");
    scene.total = 2; scene.votes = 1;
    expect(display.Update(scene), "Nonvoted departure lowers only threshold");
    scene.votes = 2; scene.phase = Phase::Committed;
    expect(display.Commit(scene) && display.Skip(true, false), "Remaining electorate unanimously commits");

    Room menuRoom;
    menuRoom.Join(0); menuRoom.Join(1); menuRoom.HostChanged(0);
    expect(!menuRoom.Begin(0, 1, name), "Authentication alone is insufficient for host gameplay readiness");
    menuRoom.GameplayReady(0); menuRoom.Prepare(0); menuRoom.Start(0);
    expect(menuRoom.Begin(0, 1, name), "Gameplay-ready host can announce scene");
    expect(menuRoom.Current().total == 1 && !menuRoom.For(1).eligible, "Menu-only peer excluded from captured electorate");
    menuRoom.GameplayReady(1);
    expect(menuRoom.Current().total == 1 && !menuRoom.For(1).eligible, "Becoming ready after BEGIN cannot increase threshold");
    menuRoom.Leave(1);
    expect(menuRoom.Current().total == 1, "Menu-only or late-ready departure does not affect threshold");
    expect(menuRoom.Vote(0, menuRoom.Current().generation, menuRoom.For(0).identity) &&
        menuRoom.Current().phase == Phase::Committed, "Menu-only peer cannot deadlock host unanimity");
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
