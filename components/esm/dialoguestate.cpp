#include "dialoguestate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

void ESM::DialogueState::load (ESMReader &esm)
{
    while (esm.isNextSub ("TOPI"))
    {
        // Save formats newer than 23 store dialogue topic IDs as typed RefIds.
        // ArenaMW still uses string IDs internally, so decode both the legacy
        // plain-string representation and the newer typed string representation.
        const std::string topic = esm.getCompatRefId();
        if (!topic.empty())
            mKnownTopics.push_back(topic);
    }

    while (esm.isNextSub ("FACT"))
    {
        const std::string faction = esm.getCompatRefId();

        while (esm.isNextSub("REA2"))
        {
            const std::string faction2 = esm.getCompatRefId();
            int reaction;
            esm.getHNT(reaction, "INTV");
            if (!faction.empty() && !faction2.empty())
                mChangedFactionReaction[faction][faction2] = reaction;
        }

        // no longer used
        while (esm.isNextSub ("REAC"))
        {
            esm.skipHSub();
            esm.getSubName();
            esm.skipHSub();
        }
    }
}

void ESM::DialogueState::save (ESMWriter &esm) const
{
    for (std::vector<std::string>::const_iterator iter (mKnownTopics.begin());
        iter!=mKnownTopics.end(); ++iter)
    {
        esm.writeHNString ("TOPI", *iter);
    }

    for (std::map<std::string, std::map<std::string, int> >::const_iterator iter = mChangedFactionReaction.begin();
         iter != mChangedFactionReaction.end(); ++iter)
    {
        esm.writeHNString ("FACT", iter->first);

        for (std::map<std::string, int>::const_iterator reactIter = iter->second.begin();
             reactIter != iter->second.end(); ++reactIter)
        {
            esm.writeHNString ("REA2", reactIter->first);
            esm.writeHNT ("INTV", reactIter->second);
        }
    }
}
