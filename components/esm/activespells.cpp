#include "activespells.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "cellref.hpp"
#include "refidcompat.hpp"

#include <limits>

namespace ESM
{

    void ActiveSpells::save(ESMWriter &esm) const
    {
        for (TContainer::const_iterator it = mSpells.begin(); it != mSpells.end(); ++it)
        {
            esm.writeHNString ("ID__", it->first);

            const ActiveSpellParams& params = it->second;

            esm.writeHNT ("CAST", params.mCasterActorId);
            esm.writeHNString ("DISP", params.mDisplayName);

            for (std::vector<ActiveEffect>::const_iterator effectIt = params.mEffects.begin(); effectIt != params.mEffects.end(); ++effectIt)
            {
                esm.writeHNT ("MGEF", effectIt->mEffectId);
                if (effectIt->mArg != -1)
                    esm.writeHNT ("ARG_", effectIt->mArg);
                esm.writeHNT ("MAGN", effectIt->mMagnitude);
                esm.writeHNT ("DURA", effectIt->mDuration);
                esm.writeHNT ("EIND", effectIt->mEffectIndex);
                esm.writeHNT ("LEFT", effectIt->mTimeLeft);
            }
        }
    }

    void ActiveSpells::load(ESMReader &esm)
    {
        const int format = esm.getFormat();

        const auto loadActorId = [&esm, format](const char* name) -> int
        {
            if (format <= 34)
            {
                int actorId = -1;
                esm.getHNT(actorId, name);
                return actorId;
            }

            RefNum ref;
            ref.unset();
            ref.load(esm, true, name);
            return ref.mIndex == std::numeric_limits<unsigned int>::max()
                ? -1 : static_cast<int>(ref.mIndex);
        };

        const auto loadBlocks = [&](const char* tag, bool keep)
        {
            while (esm.isNextSub(tag))
            {
                std::string sourceSpellId = esm.getCompatRefId();
                std::string activeSpellId;
                if (format > 31)
                    activeSpellId = esm.getHNCompatRefId("SPID");

                ActiveSpellParams params;
                params.mCasterActorId = loadActorId("CAST");
                params.mDisplayName = esm.getHNString("DISP");

                if (format > 16 && format <= 31)
                {
                    int oldType = 0;
                    esm.getHNT(oldType, "TYPE");
                }
                else if (format > 31)
                {
                    int flags = 0;
                    esm.getHNT(flags, "FLAG");
                    if (esm.isNextSub("ITEM"))
                        esm.skipHSub();
                }

                if (esm.isNextSub("WORS"))
                {
                    esm.skipHSub();
                    if (esm.isNextSub("TIME"))
                        esm.skipHSub();
                }
                else if (esm.isNextSub("TIME"))
                    esm.skipHSub(); // obsolete spell-casting timestamp

                while (esm.isNextSub("MGEF"))
                {
                    ActiveEffect effect;
                    effect.mEffectId = -1;

                    if (format <= 35)
                        esm.getHT(effect.mEffectId);
                    else
                    {
                        const CompatRefId effectId = esm.getCompatRefIdValue();
                        effect.mEffectId = RefIdCompat::magicEffectIndex(effectId);
                    }

                    effect.mArg = -1;
                    if (format <= 34)
                        esm.getHNOT(effect.mArg, "ARG_");
                    else if (esm.peekNextSub("ARG_"))
                    {
                        const CompatRefId arg = esm.getHNCompatRefIdValue("ARG_");
                        effect.mArg = RefIdCompat::effectArgumentIndex(arg, effect.mEffectId);
                    }
                    else if (esm.peekNextSub("SUM_"))
                    {
                        RefNum summon;
                        summon.unset();
                        summon.load(esm, true, "SUM_");
                        if (summon.mIndex != std::numeric_limits<unsigned int>::max())
                            effect.mArg = static_cast<int>(summon.mIndex);
                    }

                    esm.getHNT(effect.mMagnitude, "MAGN");
                    if (format > 16)
                    {
                        float ignoredMin = 0.f;
                        float ignoredMax = 0.f;
                        esm.getHNT(ignoredMin, "MAGN");
                        esm.getHNT(ignoredMax, "MAGN");
                    }

                    esm.getHNT(effect.mDuration, "DURA");
                    effect.mEffectIndex = -1;
                    esm.getHNOT(effect.mEffectIndex, "EIND");

                    if (format < 9)
                        effect.mTimeLeft = effect.mDuration;
                    else
                        esm.getHNT(effect.mTimeLeft, "LEFT");

                    if (format > 16)
                    {
                        int effectFlags = 0;
                        esm.getHNT(effectFlags, "FLAG");
                    }

                    if (effect.mEffectId >= 0)
                        params.mEffects.push_back(effect);
                }

                if (keep)
                {
                    if (sourceSpellId.empty())
                        sourceSpellId = activeSpellId;
                    if (!sourceSpellId.empty())
                        mSpells.insert(std::make_pair(sourceSpellId, params));
                }
            }
        };

        loadBlocks("ID__", true);
        loadBlocks("QID_", false); // 0.51+ queued effects have no 0.47 equivalent.
    }
}
