#pragma once
#include "object_sync.h"
#include "npc_sync.h"
#include "eNetworkEntityType.h"

enum eNetworkEntityType : uint8_t;

class CNetworkEntitySerializer
{
public:
	eNetworkEntityType entityType = (eNetworkEntityType)1;
	int entityId = 0;
	uint32_t entityGeneration = 0;
	bool Valid() const
	{
		if (entityType < NETWORK_ENTITY_TYPE_PLAYER || entityType > NETWORK_ENTITY_TYPE_NOTINPOOLS) return false;
		if (entityType == NETWORK_ENTITY_TYPE_PED)
			return entityId >= 0 && entityId < Config::MAX_SERVER_PEDS && entityGeneration > 0 && entityGeneration <= NPCSync::MaxCounter;
		if (entityGeneration != 0) return false;
		if (entityType == NETWORK_ENTITY_TYPE_PLAYER) return entityId >= 0 && entityId < Config::MAX_SERVER_PLAYERS;
		if (entityType == NETWORK_ENTITY_TYPE_VEHICLE) return entityId >= 0 && entityId < Config::MAX_SERVER_VEHICLES;
		if (entityType == NETWORK_ENTITY_TYPE_OBJECT) return entityId > 0 && entityId <= int(ObjectSync::MAX_ID);
		return true;
	}

#ifdef COOP_CLIENT
	CEntity* GetEntity();
	void SetEntity(CEntity* entity);
#endif

	template <typename Stream>
	bool Serialize(Stream& stream)
	{
		if (entityType != NETWORK_ENTITY_TYPE_PED) entityGeneration = 0;
		if (!Stream::IsReading && !Valid()) return false;
		int wireType = static_cast<int>(entityType);
		serialize_int(stream, wireType, NETWORK_ENTITY_TYPE_PLAYER, NETWORK_ENTITY_TYPE_NOTINPOOLS);
		if (Stream::IsReading) entityType = static_cast<eNetworkEntityType>(wireType);

		if (entityType != NETWORK_ENTITY_TYPE_PED) entityGeneration = 0;
		int maxValue = 0;
		switch (entityType)
		{
		case NETWORK_ENTITY_TYPE_PLAYER:
			maxValue = Config::MAX_SERVER_PLAYERS;
			break;
		case NETWORK_ENTITY_TYPE_VEHICLE:
			maxValue = Config::MAX_SERVER_VEHICLES;
			break;
		case NETWORK_ENTITY_TYPE_PED:
			maxValue = Config::MAX_SERVER_PEDS;
			break;
		case NETWORK_ENTITY_TYPE_OBJECT:
			serialize_int(stream, entityId, 1, int(ObjectSync::MAX_ID));
			return Valid();
		}
		
		if (maxValue == 0)
		{
			return Valid();
		}

		serialize_int(stream, entityId, 0, maxValue - 1);
		if (entityType == NETWORK_ENTITY_TYPE_PED)
		{
			serialize_int(stream, entityGeneration, 1, int(NPCSync::MaxCounter));
		}
		else entityGeneration = 0;
		return Valid();
	}
};

