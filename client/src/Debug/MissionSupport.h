#pragma once

// Source adaptation status only; each entry still requires multiplayer playtesting.
constexpr bool IsMissionPreparedForCoop(int missionId)
{
    switch (missionId)
    {
    case 11: // Big Smoke
    case 12: // Ryder
    case 13: // Tagging Up Turf
    case 14: // Cleaning The Hood
    case 15: // Drive-Thru
    case 16: // Nines and AK's
    case 17: // Drive-By
    case 18: // Sweet's Girl
    case 21: // Doberman
    case 23: // Gray Imports
    case 28: // Running Dog
    case 32: // Madd Dogg's Rhymes
    case 34: // House Party
    case 36: // High Stakes, Low Rider (prelude hands off to specialized CPRACE)
    case 39: // Badlands
    case 41: // Local Liquor Store
    case 42: // Small Town Bank
    case 43: // Tanker Commander
    case 44: // Against All Odds
    case 46: // Body Harvest
    case 49: // Wear Flowers In Your Hair
    case 58: // Photo Opportunity
    case 62: // Toreno's Last Flight
    case 65: // T-Bone Mendez
    case 78: // Verdant Meadows
    case 87: // Fish In A Barrel
        return true;
    default:
        return false;
    }
}
