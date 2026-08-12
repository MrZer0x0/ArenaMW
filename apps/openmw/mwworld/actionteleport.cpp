#include "actionteleport.hpp"

#include <algorithm>
#include <list>

#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/class.hpp"

#include "player.hpp"

namespace MWWorld
{
    ActionTeleport::ActionTeleport (const std::string& cellName,
        const ESM::Position& position, bool teleportFollowers)
    : Action (true), mCellName (cellName), mPosition (position), mTeleportFollowers(teleportFollowers)
    {
    }

    void ActionTeleport::executeImp (const Ptr& actor)
    {
        if (mTeleportFollowers)
        {
            std::set<MWWorld::Ptr> followers;
            const bool includeHostilePursuers
                = Settings::Manager::getBool("combat pursuit through doors", "Game");
            getFollowers(actor, followers, includeHostilePursuers);

            for (std::set<MWWorld::Ptr>::iterator it = followers.begin(); it != followers.end(); ++it)
                teleport(*it, actor);
        }

        teleport(actor);
    }

    void ActionTeleport::teleport(const Ptr& actor, const Ptr& teleportTarget)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        actor.getClass().getCreatureStats(actor).land(actor == world->getPlayerPtr());
        if(actor == world->getPlayerPtr())
        {
            world->getPlayer().setTeleported(true);
            if (mCellName.empty())
                world->changeToExteriorCell (mPosition, true);
            else
                world->changeToInteriorCell (mCellName, mPosition, true);
        }
        else
        {
            MWMechanics::AiSequence& sequence
                = actor.getClass().getCreatureStats(actor).getAiSequence();
            const bool isCombatPursuer = !teleportTarget.isEmpty()
                && sequence.isInCombat(teleportTarget);

            // Keep AiCombat for an attacker following its target through this door.
            if (sequence.isInCombat(world->getPlayerPtr()) && !isCombatPursuer)
                sequence.stopCombat();
            else if (mCellName.empty())
            {
                int cellX;
                int cellY;
                world->positionToIndex(mPosition.pos[0],mPosition.pos[1],cellX,cellY);
                world->moveObject(actor,world->getExterior(cellX,cellY),
                    mPosition.pos[0],mPosition.pos[1],mPosition.pos[2]);
            }
            else
                world->moveObject(actor,world->getInterior(mCellName),mPosition.pos[0],mPosition.pos[1],mPosition.pos[2]);
        }
    }

    void ActionTeleport::getFollowers(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out, bool includeHostiles) {
        std::set<MWWorld::Ptr> followers;
        MWBase::MechanicsManager* mechanics
            = MWBase::Environment::get().getMechanicsManager();
        mechanics->getActorsFollowing(actor, followers);

        if (includeHostiles)
        {
            const std::list<MWWorld::Ptr> pursuers = mechanics->getActorsFighting(actor);
            followers.insert(pursuers.begin(), pursuers.end());
        }

        std::size_t hostilePursuerCount = 0;
        const int maxHostilePursuers = std::max(0,
            Settings::Manager::getInt("combat pursuit max actors", "Game"));
        const float guaranteedDistance = std::max(0.f,
            Settings::Manager::getFloat("combat pursuit guaranteed distance", "Game"));
        const float maximumDoorDistance = std::max(guaranteedDistance,
            Settings::Manager::getFloat("combat pursuit door max distance", "Game"));
        const float minimumChance = std::max(0.f, std::min(1.f,
            Settings::Manager::getFloat("combat pursuit minimum chance", "Game")));

        for(std::set<MWWorld::Ptr>::iterator it = followers.begin();it != followers.end();++it)
        {
            MWWorld::Ptr follower = *it;

            if (!follower.getRefData().getCount() || !follower.getRefData().isEnabled()
                || follower.getClass().getCreatureStats(follower).isDead())
                continue;

            std::string script = follower.getClass().getScript(follower);

            const bool isHostilePursuer
                = follower.getClass().getCreatureStats(follower).getAiSequence().isInCombat(actor);
            if (!includeHostiles && isHostilePursuer)
                continue;

            if (!script.empty() && follower.getRefData().getLocals().getIntVar(script, "stayoutside") == 1)
                continue;

            const float distance = (follower.getRefData().getPosition().asVec3()
                - actor.getRefData().getPosition().asVec3()).length();

            if (isHostilePursuer)
            {
                const bool humanoid
                    = follower.getClass().isNpc() || follower.getClass().isBipedal(follower);
                if (!humanoid || maxHostilePursuers == 0
                    || hostilePursuerCount >= static_cast<std::size_t>(maxHostilePursuers)
                    || distance > maximumDoorDistance)
                    continue;

                float pursuitChance = 1.f;
                if (distance > guaranteedDistance && maximumDoorDistance > guaranteedDistance)
                {
                    const float normalizedDistance = std::min(1.f,
                        (distance - guaranteedDistance) / (maximumDoorDistance - guaranteedDistance));
                    pursuitChance = 1.f - normalizedDistance * (1.f - minimumChance);
                }

                if (Misc::Rng::rollClosedProbability() > pursuitChance)
                    continue;

                ++hostilePursuerCount;
            }
            else if (distance > 800.f)
                continue;

            out.emplace(follower);
        }
    }
}
