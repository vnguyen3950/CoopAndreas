#include <cstdint>
#include <iostream>
#include <limits>
#include "shared/network/session_sync.h"

using namespace SessionSync;
static unsigned checks = 0, failures = 0;
static void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition) { ++failures; std::cout << "FAIL: " << description << '\n'; }
}
static bool same(const Snapshot& a, const Snapshot& b)
{
    return a.epoch == b.epoch && a.revision == b.revision && a.incarnation == b.incarnation
        && a.acknowledged == b.acknowledged && a.recipient == b.recipient && a.host == b.host
        && a.ready == b.ready && a.money == b.money && a.wanted == b.wanted
        && a.maximumWanted == b.maximumWanted && a.policeIgnore == b.policeIgnore
        && a.everyoneIgnore == b.everyoneIgnore && a.toggles == b.toggles;
}
static Server room(int32_t money = 100)
{
    Server s;
    expect(s.Join(0, 0) != 0, "Host joins first room.");
    expect(s.Join(1, 0) != 0, "Guest joins same room.");
    auto seed = s.For(0); seed.money = money;
    expect(s.Seed(0, seed), "Initial host seeds canonical room once.");
    return s;
}
static Operation op(const Server& s, int sender, uint32_t sequence, Kind kind = Kind::Money)
{
    Operation o; o.epoch = s.state.epoch; o.incarnation = s.peers[sender].incarnation;
    o.sequence = sequence; o.kind = kind; return o;
}
static void transaction_tests()
{
    for (bool reverse : {false, true}) {
        auto s = room(); auto earn = op(s, 0, 1), spend = op(s, 1, 1);
        earn.delta = 30; spend.delta = -20;
        expect(s.Apply(reverse ? 1 : 0, reverse ? spend : earn).status == Status::Accepted,
            "First simultaneous wallet delta is accepted.");
        expect(s.Apply(reverse ? 0 : 1, reverse ? earn : spend).status == Status::Accepted,
            "Second simultaneous wallet delta is accepted.");
        expect(s.state.money == 110, "Both concurrent earn and spend survive either order.");
        auto before = s.state;
        expect(s.Apply(0, earn).status == Status::Duplicate && same(before, s.state),
            "Duplicate wallet sequence has no canonical effect.");
    }
    auto s = room(); auto a = op(s, 0, 1), b = op(s, 1, 1); a.delta = -80; b.delta = -70;
    expect(s.Apply(0, a).status == Status::Accepted && s.Apply(1, b).status == Status::Accepted
        && s.state.money == -50, "Concurrent native spends preserve signed canonical debt.");
    auto gap = op(s, 0, 3); gap.delta = 50; auto before = s.state;
    expect(s.Apply(0, gap).status == Status::Gap && same(before, s.state)
        && s.peers[0].acknowledged == 1, "Sequence gap does not consume a delta or receipt.");
    auto next = op(s, 0, 2); next.delta = 10;
    expect(s.Apply(0, next).status == Status::Accepted && s.Apply(0, gap).status == Status::Accepted
        && s.state.money == 10, "Missing sequence then retry retains both amounts.");
    before = s.state;
    expect(s.Apply(0, next).status == Status::Duplicate && same(before, s.state),
        "Stale request cannot roll back canonical state.");
    auto forged = op(s, 0, 4); forged.delta = 4;
    expect(s.Apply(1, forged).status == Status::Invalid && same(before, s.state),
        "Authenticated sender cannot use another incarnation.");
    forged = op(s, 0, 4); forged.epoch++;
    expect(s.Apply(0, forged).status == Status::Invalid && same(before, s.state),
        "Wrong epoch cannot mutate the room.");
    Server unready; unready.Join(0, 0); auto premature = op(unready, 0, 1); premature.delta = 10;
    expect(unready.Apply(0, premature).status == Status::NotReady && unready.peers[0].acknowledged == 0,
        "Unseeded room preserves retry sequence.");
    auto limit = room(MONEY_LIMIT); auto over = op(limit, 0, 1); over.delta = 1;
    expect(limit.Apply(0, over).status == Status::Rejected && limit.state.money == MONEY_LIMIT
        && limit.peers[0].acknowledged == 1, "Safety-bound rejection acknowledges without silent clamp.");
    over = op(limit, 0, 2); over.delta = -DELTA_LIMIT;
    expect(limit.Apply(0, over).status == Status::Accepted && limit.state.money == -MONEY_LIMIT,
        "Maximum signed delta uses widened arithmetic.");
    over = op(limit, 0, 3); over.delta = -1;
    expect(limit.Apply(0, over).status == Status::Rejected && limit.state.money == -MONEY_LIMIT,
        "Debt beyond outer safety bound is explicitly rejected.");
    auto invalid = op(limit, 0, 4); invalid.delta = std::numeric_limits<int32_t>::min();
    expect(!invalid.Valid() && limit.Apply(0, invalid).status == Status::Invalid
        && limit.peers[0].acknowledged == 3, "INT32 minimum cannot consume a valid sequence.");
    invalid.delta = std::numeric_limits<int32_t>::max(); expect(!invalid.Valid(), "INT32 maximum delta is invalid.");
}
static void client_tests()
{
    auto s = room(); Client c;
    expect(c.Accept(s.For(0), 0), "Client accepts its recipient snapshot.");
    Operation earn; earn.delta = 30; Operation spend; spend.delta = -20;
    expect(c.Queue(earn) && c.Queue(spend) && c.ProjectedMoney() == 110,
        "Pending plain Money deltas enter optimistic balance.");
    auto guest = op(s, 1, 1); guest.delta = 7; s.Apply(1, guest);
    s.Apply(0, earn); auto first = s.For(0);
    expect(c.Accept(first, 0) && c.pending.size() == 1 && c.pending.front().sequence == spend.sequence
        && c.ProjectedMoney() == 117, "Receipt removes only acknowledged local delta and retains remote earn.");
    expect(!c.Accept(first, 0) && c.pending.size() == 1, "Repeated snapshot cannot subtract pending twice.");
    s.Apply(0, spend); auto newest = s.For(0);
    expect(c.Accept(newest, 0) && c.pending.empty() && c.ProjectedMoney() == 117,
        "Final receipt leaves exactly canonical balance.");
    expect(!c.Accept(first, 0) && c.state.money == 117, "Reordered older snapshot cannot roll back balance.");
    auto corrupt = newest; ++corrupt.revision; ++corrupt.acknowledged;
    expect(!c.Accept(corrupt, 0), "Receipt cannot acknowledge a local sequence never queued.");
    corrupt = newest; ++corrupt.revision; --corrupt.acknowledged;
    expect(!c.Accept(corrupt, 0), "Acknowledgement regression is rejected even at later revision.");
    expect(!c.Accept(s.For(1), 0), "Other recipient's snapshot cannot clear this client's pending state.");
    Client pendingCash; pendingCash.Accept(s.For(1), 1);
    Operation cheat; cheat.kind = Kind::CheatAction; cheat.cheat = 3;
    expect(pendingCash.Queue(cheat) && pendingCash.ProjectedMoney() == 117,
        "Pending cash cheat intentionally waits for canonical cash rather than projecting fixed reward.");
    expect(s.Apply(1, cheat).status == Status::Accepted && pendingCash.Accept(s.For(1), 1)
        && pendingCash.ProjectedMoney() == 250117, "Accepted cash cheat contributes canonical fixed reward once.");
    Client capacity; capacity.Accept(s.For(0), 0);
    for (size_t i = 0; i < MAX_PENDING; ++i) { Operation p; p.delta = 1;
        expect(capacity.Queue(p), "Each documented pending slot is available."); }
    auto sequence = capacity.nextSequence; Operation full;
    expect(!capacity.Queue(full) && capacity.nextSequence == sequence && capacity.pending.size() == MAX_PENDING,
        "Full pending queue fails without consuming sequence.");
    Client exhausted; exhausted.Accept(s.For(0), 0); exhausted.nextSequence = MAX_COUNTER;
    Operation last; expect(!exhausted.Queue(last) && exhausted.pending.empty(), "Sequence exhaustion fails closed.");
    Client offline; Operation p; expect(!offline.Queue(p), "Uninitialized client cannot queue room operations.");
}
static void lifetime_tests()
{
    auto s = room(); Client guest; guest.Accept(s.For(1), 1);
    Operation queued; queued.delta = -25; guest.Queue(queued);
    expect(s.Apply(1, queued).status == Status::Accepted, "Pre-migration pending request is accepted.");
    auto epoch = s.state.epoch; auto incarnation = s.peers[1].incarnation;
    s.Leave(0); s.HostChanged(1);
    expect(s.state.ready && s.state.money == 75 && s.state.epoch == epoch
        && s.peers[1].incarnation == incarnation, "Migration preserves ledger, epoch and surviving identity.");
    expect(s.Apply(1, queued).status == Status::Duplicate && s.state.money == 75,
        "Pending retry through new host counts once.");
    expect(guest.Accept(s.For(1), 1) && guest.pending.empty() && guest.state.host == 1,
        "Migrated acknowledgement clears the right pending request.");
    expect(s.Join(0, 1) != 0, "Vacant slot can reconnect during a room.");
    auto joinSeed = s.For(0); joinSeed.money = 90000;
    expect(!s.Seed(0, joinSeed) && s.state.money == 75, "Joining guest's native wallet does not contribute.");
    Client slot; slot.Accept(s.For(0), 0); Operation old; old.delta = 10; slot.Queue(old);
    auto oldSnapshot = s.For(0); auto oldIncarnation = old.incarnation;
    s.Leave(0); s.Join(0, 1);
    expect(s.peers[0].incarnation > oldIncarnation && s.Apply(0, old).status == Status::Invalid,
        "Reused player slot rejects the old connection's request.");
    expect(slot.Accept(s.For(0), 0) && slot.pending.empty() && slot.nextSequence == 0,
        "New incarnation clears old pending namespace.");
    Operation fresh; fresh.delta = 20; slot.Queue(fresh); auto revision = s.state.revision;
    oldSnapshot.revision = revision + 1; oldSnapshot.acknowledged = fresh.sequence;
    expect(!slot.Accept(oldSnapshot, 0) && slot.pending.size() == 1,
        "Delayed old-incarnation receipt cannot clear new same-numbered request.");
    expect(s.Apply(0, fresh).status == Status::Accepted && s.state.money == 95,
        "New slot occupant can use sequence one without aliasing old receipt.");
    auto oldEpochOp = op(s, 0, 2); s.Leave(0); s.Leave(1);
    expect(s.Empty() && !s.state.ready && s.state.epoch == 0, "Only empty room clears live canonical state.");
    s.Join(0, 0); auto seed = s.For(0); seed.money = 8; s.Seed(0, seed);
    expect(s.state.epoch > epoch && s.state.money == 8 && s.Apply(0, oldEpochOp).status == Status::Invalid,
        "New room epoch rejects old packets and may seed its new host.");
    expect(slot.Accept(s.For(0), 0) && slot.pending.empty(), "New epoch discards old pending operations.");
}
static void wanted_tests()
{
    auto s = room(); auto raise = op(s, 1, 1, Kind::WantedRaise); raise.level = 4;
    expect(s.Apply(1, raise).status == Status::Accepted && s.state.wanted == 4,
        "Guest natural wanted raise affects canonical wanted.");
    auto lower = op(s, 1, 2, Kind::WantedLower); lower.reason = Reason::HostDecay; lower.level = 0;
    expect(s.Apply(1, lower).status == Status::Rejected && s.state.wanted == 4,
        "Guest decay cannot erase shared wanted.");
    lower = op(s, 0, 1, Kind::WantedLower); lower.reason = Reason::HostDecay; lower.level = 2;
    expect(s.Apply(0, lower).status == Status::Accepted && s.state.wanted == 2,
        "Host decay can reduce canonical wanted.");
    lower = op(s, 1, 3, Kind::WantedLower); lower.reason = Reason::Bribe; lower.level = 0;
    expect(s.Apply(1, lower).status == Status::Accepted && s.state.wanted == 1,
        "Guest bribe removes exactly one star rather than requested absolute level.");
    lower = op(s, 1, 4, Kind::WantedLower); lower.reason = Reason::Respray;
    expect(s.Apply(1, lower).status == Status::Accepted && s.state.wanted == 0,
        "Guest respray explicitly clears wanted under documented policy.");
    raise = op(s, 1, 5, Kind::WantedRaise); raise.level = 6; s.Apply(1, raise);
    auto rules = op(s, 1, 6, Kind::WantedRules); rules.reason = Reason::HostScript; rules.level = 1;
    expect(s.Apply(1, rules).status == Status::Rejected && s.state.maximumWanted == 6,
        "Guest cannot alter mission wanted rules.");
    rules = op(s, 0, 2, Kind::WantedRules); rules.reason = Reason::HostScript; rules.level = 3;
    rules.policeIgnore = true; rules.everyoneIgnore = true;
    expect(s.Apply(0, rules).status == Status::Accepted && s.state.wanted == 3
        && s.state.policeIgnore && s.state.everyoneIgnore, "Host rules clamp wanted and update ignore policies.");
    auto never = op(s, 1, 7, Kind::CheatToggle); never.cheat = 65; never.active = true;
    s.Apply(1, never); raise = op(s, 1, 8, Kind::WantedRaise); raise.level = 6;
    expect(s.Apply(1, raise).status == Status::Accepted && s.state.wanted == 0,
        "Never-wanted toggle blocks natural raise.");
    never = op(s, 1, 9, Kind::CheatToggle); never.cheat = 65; never.active = false; s.Apply(1, never);
    raise = op(s, 1, 10, Kind::WantedRaise); raise.level = 6;
    expect(s.Apply(1, raise).status == Status::Accepted && s.state.wanted == 3,
        "Disabling never-wanted permits raise bounded by maximumWanted.");
    auto clear = op(s, 0, 3, Kind::WantedLower); clear.reason = Reason::HostScript; clear.level = 0;
    expect(s.Apply(0, clear).status == Status::Accepted && s.state.wanted == 0,
        "Host mission clear is authorized.");
    clear = op(s, 1, 11, Kind::WantedLower); clear.reason = Reason::Natural;
    expect(!clear.Valid(), "Unclassified natural decrease is invalid rather than accidental clear.");
    raise.level = 7; expect(!raise.Valid(), "Out-of-range wanted level is invalid.");
}
static void cheat_tests()
{
    const int actions[] = {0,1,2,3,4,5,63,64,66};
    const int functions[] = {65,69};
    const int flags[] = {11,16,27,28,30,31,34,35,48,49,52,53,54,60,61,62,67,68,72,74,76,77,78};
    for (int id = -1; id <= CHEATS; ++id) {
        CheatMode expected = CheatMode::Unsupported;
        for (int a : actions) if (a == id) expected = CheatMode::Action;
        for (int f : functions) if (f == id) expected = CheatMode::FunctionToggle;
        for (int f : flags) if (f == id) expected = CheatMode::FlagToggle;
        expect(Mode(id) == expected, "Documented bounded cheat ID classification matches production Mode.");
    }
    auto s = room(); auto cash = op(s, 1, 1, Kind::CheatAction); cash.cheat = 3;
    auto receipt = s.Apply(1, cash);
    expect(receipt.status == Status::Accepted && receipt.action && receipt.cheat == 3 && s.state.money == 250100,
        "Accepted cash action supplies one native event and one canonical cash effect.");
    auto duplicate = s.Apply(1, cash);
    expect(duplicate.status == Status::Duplicate && !duplicate.action && s.state.money == 250100,
        "Duplicate action does not emit native reward event again.");
    cash = op(s, 1, 2, Kind::CheatAction); cash.cheat = 3;
    expect(s.Apply(1, cash).action && s.state.money == 500100,
        "Distinct rapid cash actions are not lost to time debounce.");
    auto toggle = op(s, 1, 3, Kind::CheatToggle); toggle.cheat = 69; toggle.active = true;
    auto on = s.Apply(1, toggle);
    expect(on.status == Status::Accepted && !on.action && s.state.toggles[69],
        "Toggle is canonical target state rather than one-shot event.");
    toggle.sequence = 4; s.Apply(1, toggle);
    expect(s.state.toggles[69], "Repeated active=true remains true.");
    toggle.sequence = 5; toggle.active = false; s.Apply(1, toggle);
    expect(!s.state.toggles[69], "Explicit toggle off cannot re-enable.");
    toggle.sequence = 6; toggle.active = true; s.Apply(1, toggle);
    s.Join(2, 0); auto join = s.For(2);
    expect(join.toggles[69] && join.acknowledged == 0 && join.money == 500100,
        "Join snapshot contains persistent toggle and canonical wallet without historical action event.");
    auto unsupported = op(s, 1, 7, Kind::CheatAction); unsupported.cheat = 6;
    expect(s.Apply(1, unsupported).status == Status::Invalid && s.peers[1].acknowledged == 6,
        "Unsupported cheat cannot consume sequence.");
    unsupported.cheat = 69;
    expect(!unsupported.Valid(), "Toggle ID cannot impersonate action.");
    unsupported.kind = Kind::CheatToggle; unsupported.cheat = 3;
    expect(!unsupported.Valid(), "Action ID cannot impersonate toggle.");
    auto upper = room(MONEY_LIMIT); auto rejected = op(upper, 0, 1, Kind::CheatAction); rejected.cheat = 3;
    auto r = upper.Apply(0, rejected);
    expect(r.status == Status::Rejected && !r.action && upper.state.money == MONEY_LIMIT,
        "Rejected cash action emits no native reward event.");
    int32_t nativeMoney = 100; int nativeWanted = 4, nativeHealth = 20, nativeArmor = 0;
    ApplyWithoutFeedback(nativeMoney, nativeWanted, [&] {
        nativeMoney += 250000; nativeWanted = 0; nativeHealth = 100; nativeArmor = 100;
    });
    expect(nativeMoney == 100 && nativeWanted == 4 && nativeHealth == 100 && nativeArmor == 100,
        "Production feedback seam restores cash and wanted while retaining health and armor effects.");
    expect(SkipRewardReplay(0x0109, true) && !SkipRewardReplay(0x0109, false)
        && !SkipRewardReplay(0x010A, true), "Reward replay seam suppresses only authenticated ADD_SCORE.");
}
static void atomicity_and_host_tests()
{
    Server invalidHost;
    auto incarnation = invalidHost.Join(0, 7);
    expect(incarnation == 0 && invalidHost.Empty() && invalidHost.state.epoch == 0,
        "Initial Join rejects a host ID that is not the joining peer or an existing peer.");
    auto s = room(); auto before = s.state;
    s.HostChanged(7);
    expect(same(s.state, before), "HostChanged rejects a host ID with no joined peer.");
    s = room(); before = s.state; auto oldIncarnation = s.peers[1].incarnation;
    auto joined = s.Join(2, 7);
    expect(joined == 0 && !s.peers[2].incarnation && same(s.state, before)
        && s.peers[1].incarnation == oldIncarnation, "Join rejects a mismatched or unjoined host argument.");
    auto exhausted = room(); exhausted.state.revision = MAX_COUNTER; before = exhausted.state;
    auto peerBefore = exhausted.peers[0];
    expect(exhausted.Join(2, 0) == 0 && !exhausted.peers[2].incarnation && same(before, exhausted.state),
        "Revision-exhausted Join is atomic and cannot create an unobservable peer.");
    exhausted.Leave(0);
    expect(exhausted.peers[0].incarnation == peerBefore.incarnation && same(before, exhausted.state),
        "Revision-exhausted Leave cannot silently mutate peer or host state.");
    exhausted = room(); exhausted.state.revision = MAX_COUNTER; before = exhausted.state;
    exhausted.HostChanged(1);
    expect(same(before, exhausted.state), "Revision-exhausted HostChanged cannot change canonical host.");
    Server seed; seed.Join(0, 0); seed.state.revision = MAX_COUNTER;
    auto proposed = seed.For(0); proposed.money = 10000; before = seed.state;
    expect(!seed.Seed(0, proposed) && same(before, seed.state),
        "Revision-exhausted Seed fails without changing ready, money or rules.");
    auto apply = room(); apply.state.revision = MAX_COUNTER; auto p = op(apply, 0, 1); p.delta = 10;
    before = apply.state;
    expect(apply.Apply(0, p).status == Status::Invalid && same(before, apply.state)
        && apply.peers[0].acknowledged == 0, "Revision-exhausted Apply already fails atomically.");
}
static void prediction_and_validation_tests()
{
    expect(NativeBalance(-50) == 0 && DisplayBalance(-50) == 0,
        "Signed ledger debt is represented as zero native spendable cash.");
    expect(NativeBalance(std::numeric_limits<int64_t>::max()) == MONEY_LIMIT
        && NativeBalance(std::numeric_limits<int64_t>::min()) == 0,
        "Native money projection clamps safely without narrowing overflow.");
    expect(DisplayBalance(std::numeric_limits<int64_t>::max()) == MONEY_LIMIT
        && DisplayBalance(std::numeric_limits<int64_t>::min()) == 0,
        "Money display projection remains bounded and nonnegative.");
    auto s = room(); Client host; host.Accept(s.For(0), 0);
    Operation wanted; wanted.kind = Kind::WantedRaise; wanted.level = 4; host.Queue(wanted);
    expect(host.Predicted().wanted == 4 && host.state.wanted == 0,
        "Predicted wanted includes local pending raise without mutating canonical state.");
    Operation decay; decay.kind = Kind::WantedLower; decay.reason = Reason::HostDecay; decay.level = 2;
    host.Queue(decay); expect(host.Predicted().wanted == 2, "Host pending decay reduces optimistic wanted.");
    Operation rules; rules.kind = Kind::WantedRules; rules.reason = Reason::HostScript; rules.level = 1;
    rules.policeIgnore = true; host.Queue(rules);
    expect(host.Predicted().wanted == 1 && host.Predicted().maximumWanted == 1
        && host.Predicted().policeIgnore, "Pending host rules clamp predicted wanted and retain ignore flags.");
    Operation cash; cash.kind = Kind::CheatAction; cash.cheat = 3; host.Queue(cash);
    expect(host.ProjectedMoney() == 100 && host.Predicted().money == 100,
        "Both projections wait for the fixed cash cheat effect to be acknowledged.");
    Client guest; guest.Accept(s.For(1), 1); guest.Queue(wanted); guest.Queue(decay); guest.Queue(rules);
    expect(guest.Predicted().wanted == 4 && guest.Predicted().maximumWanted == 6
        && !guest.Predicted().policeIgnore, "Predicted guest decay and host-only rules cannot overwrite authority.");
    Operation never; never.kind = Kind::CheatToggle; never.cheat = 65; never.active = true;
    guest.Queue(never); expect(guest.Predicted().wanted == 0, "Predicted never-wanted clears pending stars.");
    auto authoritativeNever = op(s, 0, 1, Kind::CheatToggle); authoritativeNever.cheat = 65;
    authoritativeNever.active = true; s.Apply(0, authoritativeNever);
    auto assignment = op(s, 0, 2, Kind::WantedLower); assignment.reason = Reason::HostScript; assignment.level = 3;
    s.Apply(0, assignment);
    expect(s.For(0).Valid() && s.state.wanted == 0,
        "Host script assignment cannot violate never-wanted snapshot invariant.");
    Client predictedNever; auto initial = s.For(0); initial.wanted = 0; predictedNever.Accept(initial, 0);
    Operation assign; assign.kind = Kind::WantedLower; assign.reason = Reason::HostScript; assign.level = 3;
    predictedNever.Queue(assign);
    expect(predictedNever.Predicted().Valid() && predictedNever.Predicted().wanted == 0,
        "Predicted host script assignment preserves never-wanted invariant.");
    auto valid = op(s, 0, 3); expect(valid.Valid(), "Ordinary operation is valid.");
    for (int field = 0; field < 3; ++field) {
        auto bad = valid;
        if (field == 0) bad.epoch = 0;
        if (field == 1) bad.incarnation = 0;
        if (field == 2) bad.sequence = 0;
        expect(!bad.Valid(), "Zero identity or sequence is rejected.");
        bad = valid;
        if (field == 0) bad.epoch = MAX_COUNTER + 1;
        if (field == 1) bad.incarnation = MAX_COUNTER + 1;
        if (field == 2) bad.sequence = MAX_COUNTER + 1;
        expect(!bad.Valid(), "Out-of-wire-range identity or sequence is rejected.");
    }
    auto bad = valid; bad.kind = static_cast<Kind>(255); expect(!bad.Valid(), "Unknown operation kind is rejected.");
    bad = valid; bad.reason = static_cast<Reason>(255); expect(!bad.Valid(), "Unknown operation reason is rejected.");
    auto snap = s.For(0); snap.wanted = 0;
    expect(snap.Valid(), "Canonical populated recipient snapshot is valid.");
    auto malformed = snap; malformed.recipient = MAX_PEERS; expect(!malformed.Valid(), "Out-of-range recipient is rejected.");
    malformed = snap; malformed.host = MAX_PEERS; expect(!malformed.Valid(), "Out-of-range host is rejected.");
    malformed = snap; malformed.toggles[3] = true; expect(!malformed.Valid(), "Action ID cannot appear as persistent toggle.");
    malformed = snap; malformed.toggles[6] = true; expect(!malformed.Valid(), "Unsupported ID cannot appear as persistent toggle.");
    malformed = snap; malformed.maximumWanted = 7; expect(!malformed.Valid(), "Snapshot maximum wanted is bounded.");
    malformed = snap; malformed.money = MONEY_LIMIT + 1; expect(!malformed.Valid(), "Snapshot money rejects out-of-contract values.");
}
static void observation_helper_tests()
{
    MoneyObservation money;
    auto inactive = money.Observe(false,0,350);
    expect(!inactive.rebase && inactive.delta == 0 && !money.armed,
        "Money observation in menu neither arms nor contributes default cash.");
    auto initialized = money.Observe(true,1,100);
    expect(initialized.rebase && initialized.delta == 0 && money.armed,
        "First ready observation establishes a baseline without an earn delta.");
    auto earn = money.Observe(true,1,130);
    expect(!earn.rebase && earn.delta == 30, "Money helper observes real earning from the native baseline.");
    money.Written(150); auto echo = money.Observe(true,1,150);
    expect(!echo.rebase && echo.delta == 0, "Canonical write is rebased and cannot echo as income.");
    auto spend = money.Observe(true,1,-50);
    expect(spend.delta == -200, "Money helper preserves signed native spend and debt.");
    auto recreate = money.Observe(true,2,350);
    expect(recreate.rebase && recreate.delta == 0, "Changed native lifecycle suppresses default starting cash.");
    money.Observe(false,2,0); auto resume = money.Observe(true,2,0);
    expect(resume.rebase && resume.delta == 0, "Readiness interruption rebases even when native ped identity is reused.");
    money.Written(std::numeric_limits<int32_t>::min());
    auto maximum = money.Observe(true,2,std::numeric_limits<int32_t>::max());
    expect(maximum.delta == 4294967295LL, "Native difference is widened before INT32-extreme subtraction.");
    auto minimum = money.Observe(true,2,std::numeric_limits<int32_t>::min());
    expect(minimum.delta == -4294967295LL, "Widened subtraction also retains the negative extreme.");
    int wanted = 3, calls = 0;
    auto read = [&] { return wanted; };
    auto setter = [&](int desired) { ++calls; wanted = desired; };
    expect(ApplyWantedLevel(3,read,setter) == 3 && calls == 0,
        "Already-correct wanted does not invoke destructive native setter again.");
    expect(ApplyWantedLevel(0,read,setter) == 0 && calls == 1,
        "Changed wanted invokes setter and returns actual resulting state.");
    wanted = 3;
    auto clamp = [&](int desired) { ++calls; wanted = std::min(desired,2); };
    expect(ApplyWantedLevel(6,read,clamp) == 2 && wanted == 2,
        "Wanted helper returns native clamped value rather than requested value.");
    auto ignored = [&](int) { ++calls; };
    expect(ApplyWantedLevel(6,read,ignored) == 2,
        "Wanted helper returns actual state after native early-return.");
}
int main()
{
    transaction_tests(); client_tests(); lifetime_tests(); wanted_tests(); cheat_tests(); atomicity_and_host_tests();
    prediction_and_validation_tests();
    observation_helper_tests();
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
