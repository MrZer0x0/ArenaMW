#include "actiontake.hpp"



#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwgui/inventorywindow.hpp"

#include "class.hpp"
#include "containerstore.hpp"
#include "interactionanimation.hpp"

namespace MWWorld
{
    ActionTake::ActionTake (const MWWorld::Ptr& object) : Action (true, object) {}

    void ActionTake::executeImp (const Ptr& actor)
    {
        // When in GUI mode, we should use drag and drop.
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            MWGui::GuiMode mode = MWBase::Environment::get().getWindowManager()->getMode();
            if (mode == MWGui::GM_Inventory || mode == MWGui::GM_Container)
            {
                MWBase::Environment::get().getWindowManager()->getInventoryWindow()->pickUpObject(getTarget());
                return;
            }
        }

        if (InteractionAnimation::queueTake(getTarget(), actor))
            return;

        InteractionAnimation::takeImmediately(getTarget(), actor);
    }
}
