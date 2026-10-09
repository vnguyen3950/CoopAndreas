#pragma once
#include "../CCustomCommand.h"

class CCommandNetworkPlayerButton : public CCustomCommand
{
public:
    explicit CCommandNetworkPlayerButton(bool justPressed) : m_justPressed(justPressed) {}
    void Process(CRunningScript* script) override;

private:
    bool m_justPressed;
};
