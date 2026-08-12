#include "magiceffects.hpp"

#include "esmwriter.hpp"
#include "esmreader.hpp"
#include "refidcompat.hpp"

namespace ESM
{

void MagicEffects::save(ESMWriter &esm) const
{
    for (std::map<int, int>::const_iterator it = mEffects.begin(); it != mEffects.end(); ++it)
    {
        esm.writeHNT("EFID", it->first);
        esm.writeHNT("BASE", it->second);
    }
}

void MagicEffects::load(ESMReader &esm)
{
    const int format = esm.getFormat();

    while (esm.isNextSub("EFID"))
    {
        int id = -1;
        if (format <= 35)
            esm.getHT(id);
        else
        {
            const CompatRefId effect = esm.getCompatRefIdValue();
            id = RefIdCompat::magicEffectIndex(effect);
        }

        int base = 0;
        esm.getHNT(base, "BASE");

        // 0.47 has no separate modifier field. Consume it to keep the reader
        // aligned, while preserving the base value ArenaMW can represent.
        if (format > 16)
        {
            float modifier = 0.f;
            esm.getHNOT(modifier, "MODI");
        }

        if (id >= 0)
            mEffects[id] = base;
    }
}

}
