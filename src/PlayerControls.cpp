#include "Chat.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "DBCStores.h"
#include "SpellAuraDefines.h"
#include "SpellAuraEffects.h"
#include <unordered_map>
#include <deque>
#include <sstream>

// =======================================================
//  1) Global storage for each player's chosen multiplier
// =======================================================
static std::unordered_map<uint64, float> g_playerSpeedMultipliers;

// =======================================================
//  2) Check for movement-inhibiting auras
// =======================================================
//
// Returns true if the player has any aura that slows, roots,
// stuns, or pacifies them. We skip forcing custom speed in that case.
bool HasMovementHinderingDebuff(Player *player)
{
    if (!player)
        return false;

    // Slows (e.g., Frostbolt)
    if (!player->GetAuraEffectsByType(SPELL_AURA_MOD_DECREASE_SPEED).empty())
        return true;

    // "All speed reduced" auras
    if (!player->GetAuraEffectsByType(SPELL_AURA_MOD_SPEED_SLOW_ALL).empty())
        return true;

    // Roots (e.g., Entangling Roots, Frost Nova)
    if (!player->GetAuraEffectsByType(SPELL_AURA_MOD_ROOT).empty())
        return true;

    // Stuns (e.g., Hammer of Justice)
    if (!player->GetAuraEffectsByType(SPELL_AURA_MOD_STUN).empty())
        return true;

    // Pacify (some CC effects)
    if (!player->GetAuraEffectsByType(SPELL_AURA_MOD_PACIFY).empty())
        return true;

    // Add more checks if you want to respect other debuff types
    return false;
}

// =======================================================
//  3) Struct & Map for Teleport-Back Positions (unchanged)
// =======================================================
struct SavedPosition
{
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float orientation = 0.0f;
    std::string locationName = "Unknown";

    SavedPosition() = default;
    SavedPosition(uint32 mapId, float x, float y, float z, float orientation, const std::string &locationName)
        : mapId(mapId), x(x), y(y), z(z), orientation(orientation), locationName(locationName)
    {
    }
};

static std::unordered_map<uint64, std::deque<SavedPosition>> playerPositionHistory;

// =======================================================
//  4) Main Script Class
// =======================================================
class PlayerControls : public PlayerScript
{
public:
    PlayerControls() : PlayerScript("PlayerControls") {}

    void OnChat(Player *player, uint32 type, uint32 lang, std::string &msg) override
    {
        if (!player)
            return;

        uint64 playerGUID = player->GetGUID().GetRawValue();

        // ----- Sample commands -----
        if (msg == "pc help")
        {
            std::string helpMessage =
                "PlayerControls Module Commands:\n"
                "- t <location>: Teleport to a named location from game_tele.\n"
                "- tb / tb1..tb8: Teleport back to saved positions.\n"
                "- tb list: Show saved positions.\n"
                "- app <playername>: Teleport to another player.\n"
                "- s1..s5: Set speed multipliers (1.0, 2.5, 3.5, 4.5, 5.5).\n"
                "- pc help: Show this help.\n";
            ChatHandler(player->GetSession()).SendSysMessage(helpMessage.c_str());
        }
        else if (msg == "tb list")
        {
            ListPlayerPositions(playerGUID, player);
        }
        else if (msg == "tb" || msg == "tb1" || msg == "tb2" ||
                 msg == "tb3" || msg == "tb4" || msg == "tb5" ||
                 msg == "tb6" || msg == "tb7" || msg == "tb8")
        {
            int positionIndex = 1;
            if (msg == "tb1")
                positionIndex = 1;
            else if (msg == "tb2")
                positionIndex = 2;
            else if (msg == "tb3")
                positionIndex = 3;
            else if (msg == "tb4")
                positionIndex = 4;
            else if (msg == "tb5")
                positionIndex = 5;
            else if (msg == "tb6")
                positionIndex = 6;
            else if (msg == "tb7")
                positionIndex = 7;
            else if (msg == "tb8")
                positionIndex = 8;

            TeleportToPreviousPosition(playerGUID, player, positionIndex);
        }
        else if (msg.substr(0, 2) == "t ")
        {
            std::string location = msg.substr(2); // e.g. "t Stormwind"
            TeleportNamedLocation(playerGUID, player, location);
        }
        else if (msg.substr(0, 4) == "app ")
        {
            std::string targetName = msg.substr(4);
            TeleportToPlayer(playerGUID, player, targetName);
        }
        // ----- Speed Commands -----
        else if (msg == "s1")
        {
            g_playerSpeedMultipliers[playerGUID] = 1.0f; // 100%
            player->SetSpeed(MOVE_RUN, 1.0f, true);
            player->SetSpeed(MOVE_SWIM, 1.0f, true);
        }
        else if (msg == "s2")
        {
            g_playerSpeedMultipliers[playerGUID] = 2.5f; // 250%
            player->SetSpeed(MOVE_RUN, 2.5f, true);
            player->SetSpeed(MOVE_SWIM, 2.5f, true);
        }
        else if (msg == "s3")
        {
            g_playerSpeedMultipliers[playerGUID] = 3.5f; // 350%
            player->SetSpeed(MOVE_RUN, 3.5f, true);
            player->SetSpeed(MOVE_SWIM, 3.5f, true);
        }
        else if (msg == "s4")
        {
            g_playerSpeedMultipliers[playerGUID] = 4.5f; // 450%
            player->SetSpeed(MOVE_RUN, 4.5f, true);
            player->SetSpeed(MOVE_SWIM, 4.5f, true);
        }
        else if (msg == "s5")
        {
            g_playerSpeedMultipliers[playerGUID] = 5.5f; // 550%
            player->SetSpeed(MOVE_RUN, 5.5f, true);
            player->SetSpeed(MOVE_SWIM, 5.5f, true);
        }
    }

    // -----------------------------------------------------
    //   Continuously enforce chosen speed in OnUpdate
    //   unless the player is truly slowed/stunned/etc.
    // -----------------------------------------------------
    void OnUpdate(Player *player, uint32 /*diff*/) override
    {
        if (!player)
            return;

        uint64 guid = player->GetGUID().GetRawValue();
        auto it = g_playerSpeedMultipliers.find(guid);
        if (it == g_playerSpeedMultipliers.end())
            return; // no custom speed set for this player

        // If the player is slowed, rooted, stunned, etc., skip forcing speed
        if (HasMovementHinderingDebuff(player))
            return;

        // If the player is mounted, use "baseMountSpeed" (2.0f = +100%).
        // Otherwise, normal base = 1.0f.
        float baseMountSpeed = player->IsMounted() ? 2.0f : 1.0f;

        float multiplier = it->second; // e.g. 2.5
        float desiredSpeed = baseMountSpeed * multiplier;

        // Apply to both run & swim. This way, if you go into water,
        // you keep the same speed multiplier.
        float currentRunSpeed = player->GetSpeed(MOVE_RUN);
        float currentSwimSpeed = player->GetSpeed(MOVE_SWIM);

        // Only re-apply if it changed
        if (currentRunSpeed != desiredSpeed)
            player->SetSpeed(MOVE_RUN, desiredSpeed, true);

        if (currentSwimSpeed != desiredSpeed)
            player->SetSpeed(MOVE_SWIM, desiredSpeed, true);
    }

private:
    // ------------------------------------------------------
    //   Teleport Commands
    // ------------------------------------------------------
    void TeleportNamedLocation(uint64 playerGUID, Player *player, const std::string &locationName)
    {
        std::ostringstream query;
        query << "SELECT map, position_x, position_y, position_z, orientation "
              << "FROM game_tele WHERE name = '" << locationName << "'";

        QueryResult result = WorldDatabase.Query(query.str().c_str());
        if (!result)
        {
            ChatHandler(player->GetSession()).SendSysMessage("Invalid location. Teleportation failed.");
            return;
        }

        Field *fields = result->Fetch();
        uint32 mapId = fields[0].Get<uint32>();
        float x = fields[1].Get<float>();
        float y = fields[2].Get<float>();
        float z = fields[3].Get<float>();
        float orientation = fields[4].Get<float>();

        // Save current location first
        SavePlayerPosition(playerGUID, player);

        // Teleport
        player->TeleportTo(mapId, x, y, z, orientation);
        ChatHandler(player->GetSession()).SendSysMessage("Teleported successfully.");
    }

    void TeleportToPlayer(uint64 playerGUID, Player *player, const std::string &targetName)
    {
        Player *target = ObjectAccessor::FindPlayerByName(targetName);
        if (!target || !target->IsInWorld())
        {
            ChatHandler(player->GetSession()).SendSysMessage("Player not found or not online.");
            return;
        }

        // Save current location first
        SavePlayerPosition(playerGUID, player);

        // Teleport to target
        player->TeleportTo(
            target->GetMapId(),
            target->GetPositionX(),
            target->GetPositionY(),
            target->GetPositionZ(),
            target->GetOrientation());
        ChatHandler(player->GetSession()).SendSysMessage("Teleported to the target player.");
    }

    // ------------------------------------------------------
    //   Teleport-Back logic
    // ------------------------------------------------------
    void SavePlayerPosition(uint64 playerGUID, Player *player)
    {
        SavedPosition currentPos(
            player->GetMapId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation(),
            GetPlayerLocationName(player));

        auto &history = playerPositionHistory[playerGUID];
        history.push_front(currentPos);
        if (history.size() > 8)
            history.pop_back();
    }

    void TeleportToPreviousPosition(uint64 playerGUID, Player *player, int index)
    {
        auto it = playerPositionHistory.find(playerGUID);
        if (it == playerPositionHistory.end() || it->second.size() < static_cast<size_t>(index))
        {
            ChatHandler(player->GetSession()).SendSysMessage("No position stored for that index.");
            return;
        }

        // Save current location before we teleport
        SavedPosition currentPos(
            player->GetMapId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation(),
            GetPlayerLocationName(player));

        const SavedPosition &prevPos = it->second[index - 1];
        player->TeleportTo(prevPos.mapId, prevPos.x, prevPos.y, prevPos.z, prevPos.orientation);

        // After teleport, push the old position to the front
        auto &history = it->second;
        history.push_front(currentPos);
        if (history.size() > 8)
            history.pop_back();

        ChatHandler(player->GetSession()).SendSysMessage("Teleported to a previous position.");
    }

    void ListPlayerPositions(uint64 playerGUID, Player *player)
    {
        auto it = playerPositionHistory.find(playerGUID);
        if (it == playerPositionHistory.end() || it->second.empty())
        {
            ChatHandler(player->GetSession()).SendSysMessage("No saved positions available.");
            return;
        }

        std::string msg = "Saved Positions:\n";
        const auto &history = it->second;
        for (size_t i = 0; i < history.size(); ++i)
        {
            msg += "tb" + std::to_string(i + 1) + ": " + history[i].locationName + "\n";
        }

        ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
    }

    std::string GetPlayerLocationName(Player *player)
    {
        if (!player)
            return "Unknown";

        uint32 areaId = player->GetAreaId();
        if (AreaTableEntry const *areaEntry = sAreaTableStore.LookupEntry(areaId))
            return areaEntry->area_name[0]; // default locale
        else
            return "Unknown";
    }
};

void AddPlayerControlsScripts()
{
    new PlayerControls();
}
