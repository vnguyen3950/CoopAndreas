#include "stdafx.h"
#include "CPlayerVitalsSync.h"

CVector2D GetPlayerMarkerPosition(const CVector& position)
{
	CVector2D vec = CVector2D(position.x, position.y) - CRadar::vec2DRadarOrigin;
	CVector2D playerDirection = 
	{ 
		vec.x / CRadar::m_radarRange, 
		vec.y / CRadar::m_radarRange 
	};

	CVector2D rotatedPos = {
		CRadar::cachedSin * playerDirection.y + CRadar::cachedCos * playerDirection.x,
		CRadar::cachedCos * playerDirection.y - CRadar::cachedSin * playerDirection.x
	};
	CRadar::LimitRadarPoint(rotatedPos);

	CVector2D ret{};
	
	CRadar::TransformRadarPointToScreenSpace(ret, rotatedPos);

	return ret;
};

float CalculateMarkerAngle(CNetworkPlayer* player)
{
	float baseAngle = player->m_pPed->m_nPhysicalFlags.bOnSolidSurface ? player->m_pPed->GetHeading() : player->m_onFootSnapshotInterpolated.currentRotation.m_angle;

	if (player->m_pPed->m_pVehicle && player->m_pPed->m_nPedFlags.bInVehicle)
	{
		baseAngle = player->m_pPed->m_pVehicle->GetHeading();
	}

	if (!FrontEndMenuManager.m_bDrawRadarOrMap)
	{
		return baseAngle - CRadar::m_fRadarOrientation - (float)M_PI;
	}
	else
	{
		return baseAngle - CRadar::m_fRadarOrientation + (float)M_PI;
	}
}

void CNetworkPlayerMapPin::Process()
{
	for (auto player : CNetworkPlayerManager::m_pPlayers)
	{
		if (!CNetwork::m_bAuthenticated || !CPlayerVitalsSync::HasBoundPed(player))
			continue;

		const auto& position = player->m_pPed->GetPosition();
		if (!std::isfinite(position.x) || !std::isfinite(position.y)) continue;
		CVector2D pos = GetPlayerMarkerPosition(position);
		float angle = CalculateMarkerAngle(player);
		// The native sprite rotates directly in screen pixels; use one scale
		// for both axes so the marker keeps its proportions at any aspect ratio.
		const float markerSize = 5.0f * RsGlobal.maximumHeight / 360.0f;

		CRadar::DrawRotatingRadarSprite(
			&CRadar::RadarBlipSprites[RADAR_SPRITE_CENTRE],
			pos.x,
			pos.y,
			angle,
			markerSize,
			markerSize,
			player->m_pPed->IsHidden() ? CRGBA{ 50, 50, 50, 255 } : CRGBA{ 255, 255, 255, 255 }
		);
	}

}
