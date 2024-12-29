
// /*
//  * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
//  */

#include "Chat.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "DBCStores.h" // Required for AreaTable access
#include <unordered_map>
#include <deque>
#include <sstream>

// Define a struct to store player positions
struct SavedPosition
{
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float orientation = 0.0f;
    std::string locationName = "Unknown"; // Add location name for identification

    // Default constructor
    SavedPosition() = default;

    // Parameterized constructor
    SavedPosition(uint32 mapId, float x, float y, float z, float orientation, const std::string& locationName)
        : mapId(mapId), x(x), y(y), z(z), orientation(orientation), locationName(locationName) {}
};

// Use a map to store the last 4 positions of each player
std::unordered_map<uint64, std::deque<SavedPosition>> playerPositionHistory;

class PlayerControls : public PlayerScript
{
public:
    PlayerControls() : PlayerScript("PlayerControls") { }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg) override
    {
        if (!player)
            return;

        uint64 playerGUID = player->GetGUID().GetRawValue(); // Convert ObjectGuid to uint64

        if (msg == "pc help")
        {
            std::string helpMessage =
                "PlayerControls Module Commands:\n"
                "- t <locationnamenospaces>: Teleport to a location in game_tele table.\n"
                "- tb: Teleport back to the most recent saved position.\n"
                "- tb list: List all saved positions with location names.\n"
                "- tb2, tb3, ..., tb8: Teleport to earlier saved positions (up to 8).\n"
                "- app <playername>: Teleport to another player's location.\n"
                "- s1-4: Changes player's speed. s1 is normal s4 is 4x speed.\n"
                "- pc help: Display this help message.\n";

            ChatHandler(player->GetSession()).SendSysMessage(helpMessage.c_str());
        }
        else if (msg == "tb list")
        {
            ListPlayerPositions(playerGUID, player);
        }
        else if (msg == "tb" || msg == "tb2" || msg == "tb3" || msg == "tb4" ||
                 msg == "tb5" || msg == "tb6" || msg == "tb7" || msg == "tb8")
        {
            int positionIndex = 0;
            if (msg == "tb") positionIndex = 1;
            else if (msg == "tb2") positionIndex = 2;
            else if (msg == "tb3") positionIndex = 3;
            else if (msg == "tb4") positionIndex = 4;
            else if (msg == "tb5") positionIndex = 5;
            else if (msg == "tb6") positionIndex = 6;
            else if (msg == "tb7") positionIndex = 7;
            else if (msg == "tb8") positionIndex = 8;
            TeleportToPreviousPosition(playerGUID, player, positionIndex);
        }
        else if (msg.substr(0, 2) == "t ")
        {
            std::string location = msg.substr(2); // Extract location name

            // Use a raw SQL query to get the teleport coordinates
            std::ostringstream query;
            query << "SELECT map, position_x, position_y, position_z, orientation FROM game_tele WHERE name = '" << location << "'";

            QueryResult result = WorldDatabase.Query(query.str().c_str());

            if (result)
            {
                Field* fields = result->Fetch();
                uint32 mapId = fields[0].Get<uint32_t>();
                float x = fields[1].Get<float>();
                float y = fields[2].Get<float>();
                float z = fields[3].Get<float>();
                float orientation = fields[4].Get<float>();

                // Save the current location only if teleportation is successful
                SavePlayerPosition(playerGUID, player);

                // Perform the teleportation
                player->TeleportTo(mapId, x, y, z, orientation);
                ChatHandler(player->GetSession()).SendSysMessage("Teleported successfully.");
            }
            else
            {
                ChatHandler(player->GetSession()).SendSysMessage("Invalid location. Teleportation failed.");
            }
        }
        else if (msg.substr(0, 4) == "app ")
        {
            std::string targetName = msg.substr(4); // Extract target player name
            Player* target = ObjectAccessor::FindPlayerByName(targetName);

            if (target && target->IsInWorld())
            {
                // Save the current location before teleporting
                SavePlayerPosition(playerGUID, player);

                // Teleport to the target player
                player->TeleportTo(
                    target->GetMapId(),
                    target->GetPositionX(),
                    target->GetPositionY(),
                    target->GetPositionZ(),
                    target->GetOrientation()
                );
                ChatHandler(player->GetSession()).SendSysMessage("Teleported to the target player.");
            }
            else
            {
                ChatHandler(player->GetSession()).SendSysMessage("Player not found or not online.");
            }
        }
        // Handle speed modification
        else if (msg == "s1")
        {
            player->SetSpeed(MOVE_RUN, 1.0f, true);
        }
        else if (msg == "s2")
        {
            player->SetSpeed(MOVE_RUN, 2.0f, true);
        }
        else if (msg == "s3")
        {
            player->SetSpeed(MOVE_RUN, 3.0f, true);
        }
        else if (msg == "s4")
        {
            player->SetSpeed(MOVE_RUN, 4.0f, true);
        }

    }

private:
    // Helper function to get the player's current location name
    std::string GetPlayerLocationName(Player* player)
    {
        if (!player)
            return "Unknown";

        uint32 areaId = player->GetAreaId();
        if (AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(areaId))
            return areaEntry->area_name[0]; // 0 is the default locale
        else
            return "Unknown";
    }

    // Save the current player position to the deque
    void SavePlayerPosition(uint64 playerGUID, Player* player)
    {
        std::string locationName = GetPlayerLocationName(player);

        SavedPosition currentPosition(
            player->GetMapId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation(),
            locationName
        );

        auto& history = playerPositionHistory[playerGUID];

        // Add the new position to the front of the deque
        history.push_front(currentPosition);

        // Ensure we only keep the last 4 positions
        if (history.size() > 8)
        {
            history.pop_back();
        }
    }

    // Teleport to a previous position in the deque
    void TeleportToPreviousPosition(uint64 playerGUID, Player* player, int index)
    {
        auto it = playerPositionHistory.find(playerGUID);
        if (it != playerPositionHistory.end() && index <= it->second.size())
        {
            // Save the current location before teleporting
            SavePlayerPosition(playerGUID, player);

            // Teleport to the requested previous position
            const SavedPosition& position = it->second[index - 1];
            player->TeleportTo(position.mapId, position.x, position.y, position.z, position.orientation);
            ChatHandler(player->GetSession()).SendSysMessage("Teleported to a previous position.");
        }
        else
        {
            ChatHandler(player->GetSession()).SendSysMessage("No position stored for the requested index.");
        }
    }


    // List all saved positions for the player
    void ListPlayerPositions(uint64 playerGUID, Player* player)
    {
        auto it = playerPositionHistory.find(playerGUID);
        if (it == playerPositionHistory.end() || it->second.empty())
        {
            ChatHandler(player->GetSession()).SendSysMessage("No saved positions available.");
            return;
        }

        std::string listMessage = "Saved Positions:\n";
        const auto& history = it->second;

        for (size_t i = 0; i < history.size(); ++i)
        {
            const SavedPosition& position = history[i];
            listMessage += "tb" + std::to_string(i + 1) + ": " + position.locationName + "\n";
        }

        ChatHandler(player->GetSession()).SendSysMessage(listMessage.c_str());
    }
};

void AddPlayerControlsScripts()
{
    new PlayerControls();
}
