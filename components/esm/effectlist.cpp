#include "effectlist.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "refidcompat.hpp"

namespace ESM {

void EffectList::load(ESMReader &esm)
{
    mList.clear();
    while (esm.isNextSub("ENAM")) {
        add(esm);
    }
}

void EffectList::add(ESMReader &esm)
{
    ENAMstruct s;

    if (esm.getFormat() <= 35)
        esm.getHT(s, 24);
    else
    {
        // Save format 36+ moved effect, skill and attribute IDs out of ENAM
        // into typed RefId subrecords. Keep the numeric indexes used by 0.47.
        struct EffectParams
        {
            int mRange;
            int mArea;
            int mDuration;
            int mMagnMin;
            int mMagnMax;
        } params;

        esm.getHT(params, 20);
        s.mRange = params.mRange;
        s.mArea = params.mArea;
        s.mDuration = params.mDuration;
        s.mMagnMin = params.mMagnMin;
        s.mMagnMax = params.mMagnMax;
        s.mEffectID = -1;
        s.mSkill = -1;
        s.mAttribute = -1;

        const CompatRefId effect = esm.getHNCompatRefIdValue("ENID");
        s.mEffectID = static_cast<short>(RefIdCompat::magicEffectIndex(effect));

        if (esm.peekNextSub("ENSK"))
        {
            const CompatRefId skill = esm.getHNCompatRefIdValue("ENSK");
            s.mSkill = static_cast<signed char>(RefIdCompat::skillIndex(skill));
        }

        if (esm.peekNextSub("ENAT"))
        {
            const CompatRefId attribute = esm.getHNCompatRefIdValue("ENAT");
            s.mAttribute = static_cast<signed char>(RefIdCompat::attributeIndex(attribute));
        }
    }

    mList.push_back(s);
}

void EffectList::save(ESMWriter &esm) const
{
    for (std::vector<ENAMstruct>::const_iterator it = mList.begin(); it != mList.end(); ++it) {
        esm.writeHNT<ENAMstruct>("ENAM", *it, 24);
    }
}

} // end namespace
