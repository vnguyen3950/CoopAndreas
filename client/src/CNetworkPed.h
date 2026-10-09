#pragma once
#include "network/npc_sync.h"
class CNetworkPed
{
private:
	CNetworkPed() {}
public:
	int m_nPedId = -1;
	CPed* m_pPed = nullptr;
	int m_nPedPoolRef = -1;
	bool m_bSyncing = false;
	unsigned char m_nTempId = 255;
	ePedType m_nPedType = PED_TYPE_CIVMALE;
	unsigned char m_nCreatedBy = RANDOM_CHAR;
	CVector m_vecVelocity{0.0f, 0.0f, 0.0f};
	float m_fAimingRotation = 0.0f;
	float m_fCurrentRotation = 0.0f;
	float m_fLookDirection = 0.0f;
	eMoveState m_nMoveState = eMoveState::PEDMOVE_NONE;
	float m_fMoveBlendRatio = 0.0f;
	CAutoPilot m_autoPilot;
	float m_fGasPedal = 0.0f;
	float m_fBreakPedal = 0.0f;
	float m_fSteerAngle = 0.0f;
	float m_fHealth = 100.0f;
	int m_nBlipHandle = -1;
	bool m_bClaimOnRelease = false;
	uint32_t m_generation = 0, m_ownerEpoch = 0, m_stateSequence = 0, m_requestToken = 0;
	int m_ownerId = -1;
	bool m_bPinned = false, m_bAllowReplay = false;
	inline static uint32_t m_lastRequestToken = 0;
	static bool WasRequested(uint32_t token) { return token > 0 && token <= m_lastRequestToken; }
	NPCSync::Stamp GetStamp() const { return {m_generation, m_ownerEpoch, m_stateSequence}; }
	bool NextState(NPCSync::Stamp& stamp);
	bool AcceptState(const NPCSync::Stamp& stamp);
	bool CanAcceptState(const NPCSync::Stamp& stamp) const;

	static CNetworkPed* CreateHosted(CPed* pPed);
	bool HasValidPed() const;
	void DetachPed();
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void RemoveFromVehicle(CVehicle* vehicle);
	void ClaimOnRelease();
	void CancelClaim();

	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);

	CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[]);
	~CNetworkPed();
};

