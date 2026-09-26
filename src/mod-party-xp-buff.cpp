/*
 * mod-party-xp-buff
 *
 * "Its Party Time!"
 *
 * Server-side party XP bonus for AzerothCore WotLK.
 *
 * Features:
 *   - No client patch required.
 *   - No visible aura.
 *   - Controlled by the party leader:
 *       .partytime on
 *       .partytime off
 *       .partytime status
 *   - Bonus is stored per group.
 *   - Bonus is disabled by default for new groups.
 *   - Bonus automatically stops applying when the group is below
 *     the configured minimum size.
 *   - Bonus automatically clears when the group is disbanded.
 *   - Optional support for bot XP.
 *   - Optional system announcements.
 *
 * Example:
 *
 *   PartyXPBuff.Enable = true
 *   PartyXPBuff.BonusPercent = 100
 *   PartyXPBuff.MinGroupSize = 3
 *   PartyXPBuff.ApplyToBots = true
 *   PartyXPBuff.Announce = true
 *
 * With BonusPercent = 100:
 *
 *   Normal XP = 100
 *   Party Time XP = 200
 *
 * NOTE:
 * This module applies the bonus to XP awarded through
 * PlayerScript::OnPlayerGiveXP when the player belongs to an
 * active qualifying group.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Group.h"
#include "Config.h"
#include "Chat.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_set>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

static constexpr int32 DEFAULT_BONUS_PERCENT = 100;
static constexpr int32 MAX_BONUS_PERCENT     = 1000;

static constexpr uint32 DEFAULT_MIN_GROUP_SIZE = 2;

// -----------------------------------------------------------------------------
// Runtime configuration
// -----------------------------------------------------------------------------

static bool   s_Enable       = true;
static int32  s_BonusPercent = DEFAULT_BONUS_PERCENT;
static uint32 s_MinGroupSize = DEFAULT_MIN_GROUP_SIZE;
static bool   s_ApplyToBots  = true;
static bool   s_Announce     = true;

// -----------------------------------------------------------------------------
// Active groups
//
// ObjectGuid is used rather than the GUID low/counter value so that we retain
// the complete group identity.
//
// Access is protected because MapUpdate can potentially run with multiple
// threads.
// -----------------------------------------------------------------------------

static std::mutex s_GroupMutex;
static std::unordered_set<ObjectGuid> s_ActiveGroups;

// -----------------------------------------------------------------------------
// Group state helpers
// -----------------------------------------------------------------------------

static bool IsGroupActive(Group* group)
{
    if (!group)
        return false;

    std::lock_guard<std::mutex> lock(s_GroupMutex);

    return s_ActiveGroups.find(group->GetGUID()) != s_ActiveGroups.end();
}

static void SetGroupActive(Group* group, bool active)
{
    if (!group)
        return;

    std::lock_guard<std::mutex> lock(s_GroupMutex);

    ObjectGuid guid = group->GetGUID();

    if (active)
        s_ActiveGroups.insert(guid);
    else
        s_ActiveGroups.erase(guid);
}

static void ClearAllActiveGroups()
{
    std::lock_guard<std::mutex> lock(s_GroupMutex);
    s_ActiveGroups.clear();
}

// -----------------------------------------------------------------------------
// Group qualification
// -----------------------------------------------------------------------------

static bool IsGroupLargeEnough(Group* group)
{
    return group && group->GetMembersCount() >= s_MinGroupSize;
}

// -----------------------------------------------------------------------------
// Player eligibility
// -----------------------------------------------------------------------------

static bool IsEligible(Player* player)
{
    if (!player || !s_Enable)
        return false;

    // If bot support is disabled, bots do not receive the bonus.
    if (!s_ApplyToBots)
    {
        if (player->GetSession() && player->GetSession()->IsBot())
            return false;
    }

    Group* group = player->GetGroup();

    if (!group)
        return false;

    if (!IsGroupLargeEnough(group))
        return false;

    return IsGroupActive(group);
}

// -----------------------------------------------------------------------------
// Announcements
// -----------------------------------------------------------------------------

static void Announce(Group* group, char const* message)
{
    if (!s_Announce || !group || !message)
        return;

    for (GroupReference* itr = group->GetFirstMember();
         itr;
    itr = itr->next())
         {
             Player* member = itr->GetSource();

             if (!member)
                 continue;

             if (!member->GetSession())
                 continue;

             ChatHandler(member->GetSession()).PSendSysMessage("{}", message);
         }
}

// -----------------------------------------------------------------------------
// World script
// -----------------------------------------------------------------------------

class PartyXPBuffWorld : public WorldScript
{
public:
    PartyXPBuffWorld()
    : WorldScript("PartyXPBuffWorld")
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        s_Enable = sConfigMgr->GetOption<bool>(
            "PartyXPBuff.Enable",
            true);

        s_BonusPercent = sConfigMgr->GetOption<int32>(
            "PartyXPBuff.BonusPercent",
            DEFAULT_BONUS_PERCENT);

        s_MinGroupSize = sConfigMgr->GetOption<uint32>(
            "PartyXPBuff.MinGroupSize",
            DEFAULT_MIN_GROUP_SIZE);

        s_ApplyToBots = sConfigMgr->GetOption<bool>(
            "PartyXPBuff.ApplyToBots",
            true);

        s_Announce = sConfigMgr->GetOption<bool>(
            "PartyXPBuff.Announce",
            true);

        // Prevent negative XP bonuses and unreasonable configuration values.
        s_BonusPercent = std::clamp(
            s_BonusPercent,
            0,
            MAX_BONUS_PERCENT);

        // A group size below 2 is not meaningful for a party XP bonus.
        s_MinGroupSize = std::max<uint32>(
            s_MinGroupSize,
            2);

        // Configuration reloads should not leave stale group state behind.
        if (!s_Enable)
            ClearAllActiveGroups();
    }
};

// -----------------------------------------------------------------------------
// Player script
// -----------------------------------------------------------------------------

class PartyXPBuffPlayer : public PlayerScript
{
public:
    PartyXPBuffPlayer()
    : PlayerScript("PartyXPBuffPlayer")
    {
    }

    void OnPlayerGiveXP(
        Player* player,
        uint32& amount,
        Unit* /*victim*/,
        uint8 /*xpSource*/) override
        {
            if (!IsEligible(player))
                return;

            if (amount == 0 || s_BonusPercent == 0)
                return;

            /*
             * Calculate using 64-bit arithmetic so a large configured bonus
             * cannot overflow uint32 during the multiplication.
             *
             * Example:
             *
             *   amount = 100
             *   bonus  = 100%
             *
             *   100 * 200 / 100 = 200 XP
             */
            uint64 boostedAmount =
            static_cast<uint64>(amount) *
            static_cast<uint64>(100 + s_BonusPercent);

            boostedAmount /= 100;

            // Protect against overflow when converting back to uint32.
            if (boostedAmount >
                static_cast<uint64>(std::numeric_limits<uint32>::max()))
            {
                amount = std::numeric_limits<uint32>::max();
            }
            else
            {
                amount = static_cast<uint32>(boostedAmount);
            }
        }
};

// -----------------------------------------------------------------------------
// Group script
// -----------------------------------------------------------------------------

class PartyXPBuffGroup : public GroupScript
{
public:
    PartyXPBuffGroup()
    : GroupScript("PartyXPBuffGroup")
    {
    }

    void OnRemoveMember(
        Group* group,
        ObjectGuid /*guid*/,
        RemoveMethod /*method*/,
        ObjectGuid /*kicker*/,
        char const* /*reason*/) override
        {
            if (!group)
                return;

            /*
             * We deliberately do NOT disable the group's Party Time state here.
             *
             * Example:
             *
             *   4 players -> Party Time ON
             *   4 -> 2 players
             *   2 -> 3 players
             *
             * The group remains Party Time ON, but XP is only modified while
             * the group satisfies MinGroupSize.
             *
             * This means the party leader doesn't have to repeatedly use
             * .partytime on/off as people join and leave.
             */

            if (IsGroupActive(group) &&
                group->GetMembersCount() == s_MinGroupSize - 1)
            {
                Announce(
                    group,
                    "|cffff0000Its Party Time is paused|r - "
                    "need more party members.");
            }
        }

        void OnDisband(Group* group) override
        {
            SetGroupActive(group, false);
        }
};

// -----------------------------------------------------------------------------
// Chat command
// -----------------------------------------------------------------------------

using namespace Acore::ChatCommands;

class PartyXPBuffCommand : public CommandScript
{
public:
    PartyXPBuffCommand()
    : CommandScript("PartyXPBuffCommand")
    {
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable partyTimeTable =
        {
            {
                "on",
                HandlePartyTimeOn,
                rbac::RBAC_PERM_COMMAND_HELP,
                Console::No
            },
            {
                "off",
                HandlePartyTimeOff,
                rbac::RBAC_PERM_COMMAND_HELP,
                Console::No
            },
            {
                "status",
                HandlePartyTimeStatus,
                rbac::RBAC_PERM_COMMAND_HELP,
                Console::No
            },
        };

        static ChatCommandTable commandTable =
        {
            {
                "partytime",
                partyTimeTable
            },
        };

        return commandTable;
    }

private:

    // -------------------------------------------------------------------------
    // Get the player issuing the command.
    // -------------------------------------------------------------------------

    static Player* GetCommandPlayer(ChatHandler* handler)
    {
        if (!handler)
            return nullptr;

        WorldSession* session = handler->GetSession();

        if (!session)
            return nullptr;

        return session->GetPlayer();
    }

    // -------------------------------------------------------------------------
    // Require the caller to be the party leader.
    // -------------------------------------------------------------------------

    static bool RequireLeader(
        ChatHandler* handler,
        Group*& group)
    {
        group = nullptr;

        Player* player = GetCommandPlayer(handler);

        if (!player)
            return false;

        group = player->GetGroup();

        if (!group)
        {
            handler->SendSysMessage(
                "You're not in a party.");

            return false;
        }

        if (group->GetLeaderGUID() != player->GetGUID())
        {
            handler->SendSysMessage(
                "Only the party leader can do that.");

            return false;
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // .partytime on
    // -------------------------------------------------------------------------

    static bool HandlePartyTimeOn(ChatHandler* handler)
    {
        Group* group = nullptr;

        if (!RequireLeader(handler, group))
            return true;

        if (!s_Enable)
        {
            handler->SendSysMessage(
                "Party Time is currently disabled by the server.");

            return true;
        }

        if (!IsGroupLargeEnough(group))
        {
            handler->PSendSysMessage(
                "Need at least {} party members first.",
                s_MinGroupSize);

            return true;
        }

        if (IsGroupActive(group))
        {
            handler->SendSysMessage(
                "Party Time is already ON.");

            return true;
        }

        SetGroupActive(group, true);

        Announce(
            group,
            "|cff00ff00Its Party Time!|r "
            "Experience bonus is now ON for this party.");

        return true;
    }

    // -------------------------------------------------------------------------
    // .partytime off
    // -------------------------------------------------------------------------

    static bool HandlePartyTimeOff(ChatHandler* handler)
    {
        Group* group = nullptr;

        if (!RequireLeader(handler, group))
            return true;

        if (!IsGroupActive(group))
        {
            handler->SendSysMessage(
                "Party Time is already OFF.");

            return true;
        }

        SetGroupActive(group, false);

        Announce(
            group,
            "|cffff0000Party Time over.|r "
            "Experience bonus removed.");

        return true;
    }

    // -------------------------------------------------------------------------
    // .partytime status
    // -------------------------------------------------------------------------

    static bool HandlePartyTimeStatus(ChatHandler* handler)
    {
        Player* player = GetCommandPlayer(handler);

        if (!player)
            return true;

        Group* group = player->GetGroup();

        if (!group)
        {
            handler->SendSysMessage(
                "You're not in a party.");

            return true;
        }

        bool active = IsGroupActive(group);
        bool largeEnough = IsGroupLargeEnough(group);

        handler->PSendSysMessage(
            "Party Time is {} (+{}% XP, needs {}+ members).",
                                 active ? "ON" : "OFF",
                                 s_BonusPercent,
                                 s_MinGroupSize);

        if (active && !largeEnough)
        {
            handler->PSendSysMessage(
                "Party Time is currently PAUSED because "
                "the group has {} member(s).",
                                     group->GetMembersCount());
        }

        return true;
    }
};

// -----------------------------------------------------------------------------
// Script registration
// -----------------------------------------------------------------------------

void Addmod_party_xp_buffScripts()
{
    new PartyXPBuffWorld();
    new PartyXPBuffPlayer();
    new PartyXPBuffGroup();
    new PartyXPBuffCommand();
}
