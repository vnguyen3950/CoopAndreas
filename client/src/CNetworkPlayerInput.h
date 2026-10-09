#pragma once

// Matches CRunningScript::GetPadState's Button IDs without swapping global pad state.
template <typename ControllerState>
short GetNetworkScriptButtonState(const ControllerState& state, int button)
{
    switch (button)
    {
    case 0: return state.LeftStickX;
    case 1: return state.LeftStickY;
    case 2: return state.RightStickX;
    case 3: return state.RightStickY;
    case 4: return state.LeftShoulder1;
    case 5: return state.LeftShoulder2;
    case 6: return state.RightShoulder1;
    case 7: return state.RightShoulder2;
    case 8: return state.DPadUp;
    case 9: return state.DPadDown;
    case 10: return state.DPadLeft;
    case 11: return state.DPadRight;
    case 12: return state.Start;
    case 13: return state.Select;
    case 14: return state.ButtonSquare;
    case 15: return state.ButtonTriangle;
    case 16: return state.ButtonCross;
    case 17: return state.ButtonCircle;
    case 18: return state.ShockButtonL;
    case 19: return state.ShockButtonR;
    default: return 0;
    }
}
