#include "creaturelevliststate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

#include <limits>

namespace ESM
{

    void CreatureLevListState::load(ESMReader &esm)
    {
        ObjectState::load(esm);

        mSpawnActorId = -1;
        if (esm.isNextSub("SPAW"))
        {
            if (esm.getFormat() > 34)
            {
                // isNextSub() has already consumed the SPAW subrecord header.
                // Newer saves store the spawned actor as an 8-byte FormId,
                // whose memory layout matches the legacy wide RefNum here.
                RefNum spawned;
                spawned.unset();
                esm.getHT(spawned, 8);
                if (spawned.mIndex != 0 && spawned.mIndex != std::numeric_limits<unsigned int>::max())
                    mSpawnActorId = static_cast<int>(spawned.mIndex);
            }
            else
                esm.getHT(mSpawnActorId);
        }

        mSpawn = false;
        esm.getHNOT (mSpawn, "RESP");
    }

    void CreatureLevListState::save(ESMWriter &esm, bool inInventory) const
    {
        ObjectState::save(esm, inInventory);

        if (mSpawnActorId != -1)
            esm.writeHNT ("SPAW", mSpawnActorId);

        if (mSpawn)
            esm.writeHNT ("RESP", mSpawn);
    }

}
