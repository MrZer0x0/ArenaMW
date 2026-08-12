#include "player.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

void ESM::Player::load (ESMReader &esm)
{
    mObject.mRef.loadId(esm, true);
    mObject.load (esm);

    mCellId.load (esm);

    esm.getHNT (mLastKnownExteriorPosition, "LKEP", 12);

    if (esm.isNextSub ("MARK"))
    {
        mHasMark = true;
        esm.getHT (mMarkedPosition, 24);
        mMarkedCell.load (esm);
    }
    else
        mHasMark = false;

    // Automove, no longer used.
    if (esm.isNextSub("AMOV"))
        esm.skipHSub();

    mBirthsign = esm.getHNCompatRefId ("SIGN");

    mCurrentCrimeId = -1;
    esm.getHNOT (mCurrentCrimeId, "CURD");
    mPaidCrimeId = -1;
    esm.getHNOT (mPaidCrimeId, "PAYD");

    bool checkPrevItems = true;
    while (checkPrevItems)
    {
        std::string boundItemId = esm.getHNOCompatRefId("BOUN");
        std::string prevItemId = esm.getHNOCompatRefId("PREV");

        if (!boundItemId.empty())
            mPreviousItems[boundItemId] = prevItemId;
        else
            checkPrevItems = false;
    }

    if (esm.getFormat() > 18)
    {
        float attributes[ESM::Attribute::Length] = {};
        float skills[ESM::Skill::Length] = {};
        esm.getHNT(attributes, "WWAT");
        esm.getHNT(skills, "WWSK");
        for (int i=0; i<ESM::Attribute::Length; ++i)
        {
            mSaveAttributes[i] = StatState<float>();
            mSaveAttributes[i].mBase = attributes[i];
        }
        for (int i=0; i<ESM::Skill::Length; ++i)
        {
            mSaveSkills[i] = StatState<float>();
            mSaveSkills[i].mBase = skills[i];
        }
    }
    else
    {
        bool intFallback = esm.getFormat() < 11;
        if (esm.hasMoreSubs())
        {
            for (int i=0; i<ESM::Attribute::Length; ++i)
                mSaveAttributes[i].load(esm, intFallback);
            for (int i=0; i<ESM::Skill::Length; ++i)
                mSaveSkills[i].load(esm, intFallback);
        }
    }
}

void ESM::Player::save (ESMWriter &esm) const
{
    mObject.save (esm);

    mCellId.save (esm);

    esm.writeHNT ("LKEP", mLastKnownExteriorPosition);

    if (mHasMark)
    {
        esm.writeHNT ("MARK", mMarkedPosition, 24);
        mMarkedCell.save (esm);
    }

    esm.writeHNString ("SIGN", mBirthsign);

    esm.writeHNT ("CURD", mCurrentCrimeId);
    esm.writeHNT ("PAYD", mPaidCrimeId);

    for (PreviousItems::const_iterator it=mPreviousItems.begin(); it != mPreviousItems.end(); ++it)
    {
        esm.writeHNString ("BOUN", it->first);
        esm.writeHNString ("PREV", it->second);
    }

    for (int i=0; i<ESM::Attribute::Length; ++i)
        mSaveAttributes[i].save(esm);
    for (int i=0; i<ESM::Skill::Length; ++i)
        mSaveSkills[i].save(esm);
}
