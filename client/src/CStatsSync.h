#pragma once
class CStatsSync
{
public:
	// The remaining three PlayerStats wire slots are reserved, not stat ID zero.
	static constexpr inline int SYNCED_STATS_COUNT = 11;

	static std::array<eStats, SYNCED_STATS_COUNT> m_aeSyncedStats;
	static void ApplyNetworkPlayerContext(CNetworkPlayer* player);
	static void ApplyLocalContext();
	static void NotifyChanged();
	static int GetSyncIdByInternal(eStats stat);
};

