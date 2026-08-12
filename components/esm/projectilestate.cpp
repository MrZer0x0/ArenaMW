#include "projectilestate.hpp"

#include "esmwriter.hpp"
#include "esmreader.hpp"
#include "cellref.hpp"

#include <limits>

namespace ESM
{

    void BaseProjectileState::save(ESMWriter &esm) const
    {
        esm.writeHNString ("ID__", mId);
        esm.writeHNT ("VEC3", mPosition);
        esm.writeHNT ("QUAT", mOrientation);
        esm.writeHNT ("ACTO", mActorId);
    }

    void BaseProjectileState::load(ESMReader &esm)
    {
        mId = esm.getHNCompatRefId("ID__");
        esm.getHNT (mPosition, "VEC3");
        esm.getHNT (mOrientation, "QUAT");
        if (esm.getFormat() <= 34)
            esm.getHNT (mActorId, "ACTO");
        else
        {
            RefNum caster;
            caster.unset();
            caster.load(esm, true, "ACTO");
            mActorId = caster.mIndex == std::numeric_limits<unsigned int>::max()
                ? -1 : static_cast<int>(caster.mIndex);
        }
    }

    void MagicBoltState::save(ESMWriter &esm) const
    {
        BaseProjectileState::save(esm);

        esm.writeHNString ("SPEL", mSpellId);
        esm.writeHNT ("SPED", mSpeed);
    }

    void MagicBoltState::load(ESMReader &esm)
    {
        BaseProjectileState::load(esm);

        mSpellId = esm.getHNCompatRefId("SPEL");
        if (esm.isNextSub("SRCN")) // for backwards compatibility
            esm.skipHSub();
        ESM::EffectList().load(esm); // for backwards compatibility
        esm.getHNT (mSpeed, "SPED");
        if (esm.isNextSub("ITEM"))
            esm.skipHSub(); // 0.51+ projectile source item form ID
        if (esm.isNextSub("SLOT")) // newer/older compatibility field
            esm.skipHSub();
        if (esm.isNextSub("STCK")) // for backwards compatibility
            esm.skipHSub();
        if (esm.isNextSub("SOUN")) // for backwards compatibility
            esm.skipHSub();
    }

    void ProjectileState::save(ESMWriter &esm) const
    {
        BaseProjectileState::save(esm);

        esm.writeHNString ("BOW_", mBowId);
        esm.writeHNT ("VEL_", mVelocity);
        esm.writeHNT ("STR_", mAttackStrength);
    }

    void ProjectileState::load(ESMReader &esm)
    {
        BaseProjectileState::load(esm);

        mBowId = esm.getHNCompatRefId ("BOW_");
        esm.getHNT (mVelocity, "VEL_");

        mAttackStrength = 1.f;
        esm.getHNOT(mAttackStrength, "STR_");
        if (esm.isNextSub("WIND"))
            esm.skipHSub();
    }

}
