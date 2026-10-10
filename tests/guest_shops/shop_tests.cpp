#include "shop_doubles.h"

static bool abiPreserved=true;
static bool ScriptCall(CPlayerPed* player, CRunningScript* script)
{
    bool result;
    CRunningScript* afterScript;uintptr_t beforeStack,afterStack;unsigned afterEbx,afterEdi;
    // Reproduce the verified native SCM call site's ECX/ESI binding.
    __asm {
        push ebx
        push edi
        push esi
        mov ebx, 11223344h
        mov edi, 55667788h
        mov esi, script
        mov ecx, player
        mov beforeStack,esp
        call CPlayerPed__CanPlayerStartMission_ScriptHook
        test al,al
        setnz result
        mov afterStack,esp
        mov afterScript,esi
        mov afterEbx,ebx
        mov afterEdi,edi
        pop esi
        pop edi
        pop ebx
    }
    abiPreserved &= beforeStack==afterStack && afterScript==script && afterEbx==0x11223344 && afterEdi==0x55667788;
    return result;
}
static void Name(CRunningScript& script, const char* name)
{
    std::memset(script.m_szName, 0, 8);
    std::memcpy(script.m_szName, name, std::min(size_t(8), std::strlen(name)));
}
static void Frame(CRunningScript* script, uint16_t opcode)
{
    lastProcessedScript = script; activeOpcodeScope = true; lastOpCodeProcessed = opcode;
    scriptParamCount = textParamCount = 0;
    BuildAndSendOpcode(); activeOpcodeScope = false;
}
int main()
{
    CWorld::Players[1].m_pPed = &otherPed;
    SessionSync::Server bootstrap;bootstrap.Join(0,0);bootstrap.Join(1,0);
    auto bootstrapSeed=bootstrap.For(0);bootstrapSeed.money=1000;bootstrap.Seed(0,bootstrapSeed);
    g_client.Accept(bootstrap.For(1),1);CaptureMoney();
    auto* script = &CTheScripts::storage[0];
    CTheScripts::pActiveScripts = script;
    m_serializedSequences[0][0]={0x05,0x06,0,0};
    COpCodeSync::scriptParamsBuffer[0].value=77;COpCodeSync::scriptParamsBuffer[1].value=0;
    for (const char* name : {"BARB", "CLOTH", "TATTO", "AMUNAT", "JFUD"}) {
        Name(*script, name); CLocalPlayer::m_bIsHost = false;
        expect(ScriptCall(&localPed, script), "Authenticated guest may enter exact active local retail controller");
        for(bool host:{false,true}) {
            CLocalPlayer::m_bIsHost=host;
            g_client.state.ready=false;expect(!ScriptCall(&localPed,script),"Retail entry waits for canonical wallet receipt for both roles");g_client.state.ready=true;
            g_money.armed=false;expect(!ScriptCall(&localPed,script),"Retail entry rejects an uninitialized cash lifecycle for both roles");g_money.armed=true;
            expect(ScriptCall(&localPed,script),"Established wallet permits native eligible retail for both roles");
        }
        CLocalPlayer::m_bIsHost=false;
        localPed.nativeEligible = false;
        expect(!ScriptCall(&localPed, script), "Native death/control/task eligibility false remains false");
        localPed.nativeEligible = true;
        expect(!ScriptCall(&otherPed, script), "Guest service permission rejects another actor");
        CWorld::PlayerInFocus = 1;
        expect(!ScriptCall(&localPed, script), "Guest service permission rejects remote focus");
        CWorld::PlayerInFocus = 0; CPools::recycled = true;
        expect(!ScriptCall(&localPed, script), "Guest service permission rejects recycled native pool reference");
        CPools::recycled = false;
        CTheScripts::ScriptSpace[1] = 1;
        expect(!ScriptCall(&localPed, script), "Host shared mission blocks new guest service entry");
        CTheScripts::ScriptSpace[1] = 0;
        for(int invalidFlag:{0,-1,200000}) {CTheScripts::OnAMissionFlag=invalidFlag;expect(!ScriptCall(&localPed,script),"Unknown/out-of-range shared mission flag cannot grant guest service");}
        CTheScripts::OnAMissionFlag=1;
        script->m_bIsExternal=false;expect(!ScriptCall(&localPed,script),"Non-streamed thread cannot impersonate retail");script->m_bIsExternal=true;
        script->m_bIsMission = true;
        expect(!ScriptCall(&localPed, script), "Mission thread cannot inherit retail-name permission");
        script->m_bIsMission = false; script->m_bIsActive = false;
        expect(!ScriptCall(&localPed, script), "Retired script name cannot grant retail permission");
        script->m_bIsActive = true; CTheScripts::pActiveScripts = nullptr;
        expect(!ScriptCall(&localPed, script), "Stale array script outside active chain cannot grant permission");
        CTheScripts::pActiveScripts = script;
        CRunningScript synthetic; Name(synthetic, name);
        expect(!ScriptCall(&localPed, &synthetic), "Synthetic network/Command script cannot grant retail permission");

        for (bool host : {false, true}) {
            CLocalPlayer::m_bIsHost = host;
            COpCodeSync::ms_iFreeSyncedScript = 1; std::memcpy(COpCodeSync::ms_aszSyncedScripts[0], script->m_szName, 8);
            Stats::opcodeSends = Stats::taskCapture = 0;
            GetPacketFactory().sequenceSends=0;
            Frame(script, COMMAND_SET_PLAYER_CONTROL); Frame(script, 0x015F); Frame(script, COMMAND_TASK_PLAY_ANIM);
            Frame(script, COMMAND_OPEN_SEQUENCE_TASK); Frame(script, COMMAND_PERFORM_SEQUENCE_TASK);
            expect(Stats::opcodeSends == 0 && Stats::taskCapture == 0 && GetPacketFactory().sequenceSends==0,
                "Native retail camera/control/tasks remain local before sequence capture for both roles");
            activeOpcodeScope = true; lastProcessedScript = script;
            CAudioEngine engine; Stats::audioSends = 0; int plays = Stats::nativeAudioPlays;
            CAudioEngine__PreloadMissionAudio_Hook(&engine, nullptr, 3, 4400);
            CAudioEngine__PlayLoadedMissionAudio_Hook(&engine, nullptr, 3);
            expect(Stats::nativeAudioPlays == plays + 1 && Stats::audioSends == 0,
                "Retail voice executes native local audio without mission-audio broadcast");
            activeOpcodeScope = false;
        }
    }
    CLocalPlayer::m_bIsHost = false;
    for (const char* name : {"INTRO1", "INT", "R3", "GYM", "BLOODR", "BARBFAKE", "PCHAIR"}) {
        Name(*script, name); expect(!ScriptCall(&localPed, script), "Story/contact/side activity names remain guest-denied");
    }
    CNetwork::m_bAuthenticated = false;
    expect(!ScriptCall(&localPed,script),"Connected pending-auth client cannot start contacts through offline fallback");
    CNetwork::m_bConnected=false;
    expect(ScriptCall(&localPed, script), "Disconnected SCM path retains native offline eligibility");
    CNetwork::m_bAuthenticated = true; CNetwork::m_bConnected=true; CLocalPlayer::m_bIsHost = true;
    g_client.state.ready=false;
    expect(ScriptCall(&localPed, script), "Host story predicate retains native result even before wallet ready");
    g_client.state.ready=true;
    localPed.nativeEligible = false;
    expect(!ScriptCall(&localPed, script), "Host native false remains false"); localPed.nativeEligible = true;
    CLocalPlayer::m_bIsHost = false;
    expect(!CPlayerPed__CanPlayerStartMission_Hook(&localPed, nullptr), "Special-pickup host gate is unchanged");
    CLocalPlayer::m_bIsHost = true; Name(*script, "INTRO1");
    COpCodeSync::ms_iFreeSyncedScript = 1; std::memcpy(COpCodeSync::ms_aszSyncedScripts[0], script->m_szName, 8);
    Stats::opcodeSends = 0; Frame(script, COMMAND_SET_PLAYER_CONTROL);
    expect(Stats::opcodeSends == 1, "Host registered story control still broadcasts");
    GetPacketFactory().sequenceSends=0;Frame(script,COMMAND_PERFORM_SEQUENCE_TASK);
    expect(GetPacketFactory().sequenceSends==1,"Non-retail story retains actual direct PerformSequence publisher");
    Name(*script, "BARB"); activeOpcodeScope = false; lastProcessedScript = script;
    expect(COpCodeSync::GetActiveScript() == nullptr, "Ended VM scope hides stale last retail script");
    activeOpcodeScope = true; COpCodeSync::bProcessingNetworkOpcode = true;
    expect(COpCodeSync::GetActiveScript() == nullptr, "Network replay never inherits local retail scope");
    COpCodeSync::bProcessingNetworkOpcode = false; activeOpcodeScope = false;

    expect(abiPreserved,"Actual script wrapper preserves ESI/EBX/EDI/stack and AL test result");
    // Real native Buy body plus real client wallet observation and server ledger.
    for (const auto section : {PRICE_SECTION_HAIRCUTS, PRICE_SECTION_CLOTHES, PRICE_SECTION_TATTOOS, PRICE_SECTION_WEAPONS, PRICE_SECTION_FOOD}) {
        CLocalPlayer::m_bIsHost = false; CWorld::PlayerInFocus = 0;
        SessionSync::Server server; expect(server.Join(0,0) && server.Join(1,0), "Wallet room and guest initialized");
        auto seed = server.For(0); seed.money = 1000; expect(server.Seed(0,seed), "Wallet seeds from host only");
        g_client = {}; g_money = {}; g_unsentMoney = 0; GetPacketFactory().money.clear();
        expect(g_client.Accept(server.For(1),1), "Guest receives canonical wallet");
        CWorld::Players[0].m_nMoney = 50; CaptureMoney();
        expect(CWorld::Players[0].m_nMoney == 1000 && GetPacketFactory().money.empty(), "Guest starting cash rebaselines without contribution");
        localPed.clothes = {}; otherPed.clothes = {}; localPed.ammo = otherPed.ammo = 0;
        CWorld::Players[1].m_nMoney = 555; CShopping::ms_priceSectionLoaded = section;
        CShopping::ms_prices[0].price = 100; CShopping::Buy(22,0);
        expect(CWorld::Players[0].m_nMoney == 900 && CWorld::Players[1].m_nMoney == 555,
            "Unchanged native Buy charges only focused collector exactly once");
        if (section == PRICE_SECTION_WEAPONS) expect(localPed.ammo == 30 && otherPed.ammo == 0, "Native weapon equipment remains personal");
        if (section == PRICE_SECTION_HAIRCUTS || section == PRICE_SECTION_CLOTHES || section == PRICE_SECTION_TATTOOS)
            expect(localPed.clothes.writes == 1 && otherPed.clothes.writes == 0, "Native cosmetic changes remain personal");
        CaptureMoney(); CaptureMoney();
        expect(GetPacketFactory().money.size() == 1 && GetPacketFactory().money[0].delta == -100,
            "Actual money observation submits one native purchase delta across repeated polls");
        auto op = GetPacketFactory().money[0]; server.Apply(1,op); server.Apply(1,op);
        expect(server.state.money == 900, "Canonical server deduplicates repeated purchase transaction");
        expect(g_client.Accept(server.For(1),1), "Purchase receipt acknowledges original pending operation");
        WriteMoney(); CaptureMoney();
        expect(g_client.pending.empty() && CWorld::Players[0].m_nMoney == 900 && GetPacketFactory().money.size() == 1,
            "Receipt and canonical rewrite create no second charge");
    }
    Name(*script,"TATTO");CWorld::PlayerInFocus=0;
    CWorld::Players[0].m_nMoney-=400;Stats::opcodeSends=Stats::taskCapture=0;
    auto beforeTransactions=GetPacketFactory().money.size();
    Frame(script,0x0109);CaptureMoney();
    expect(GetPacketFactory().money.size()==beforeTransactions+1 && GetPacketFactory().money.back().delta==-400,
        "Local service ADD_SCORE retains actual ConsumeOpcode wallet observation exactly once");
    expect(Stats::opcodeSends==0 && Stats::taskCapture==0,"Local charge never broadcasts legacy score or service task capture");
    std::cout << checks << " shop source/native/wallet assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
