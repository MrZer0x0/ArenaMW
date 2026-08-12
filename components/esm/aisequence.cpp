#include "aisequence.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "cellref.hpp"

#include <limits>
#include <memory>

namespace ESM
{
namespace
{
    int loadCompatActorId(ESMReader& esm, const char* name, bool optional = true)
    {
        if (esm.getFormat() <= 34)
        {
            int actorId = -1;
            if (optional)
                esm.getHNOT(actorId, name);
            else
                esm.getHNT(actorId, name);
            return actorId;
        }

        if (optional && !esm.peekNextSub(name))
            return -1;

        RefNum ref;
        ref.unset();
        ref.load(esm, true, name);
        if (ref.mIndex == std::numeric_limits<unsigned int>::max())
            return -1;
        return static_cast<int>(ref.mIndex);
    }
}

namespace AiSequence
{

    void AiWander::load(ESMReader &esm)
    {
        esm.getHNT (mData, "DATA");
        esm.getHNT(mDurationData, "STAR"); // was mStartTime
        mStoredInitialActorPosition = false;
        if (esm.isNextSub("POS_"))
        {
            mStoredInitialActorPosition = true;
            esm.getHT(mInitialActorPosition);
        }
    }

    void AiWander::save(ESMWriter &esm) const
    {
        esm.writeHNT ("DATA", mData);
        esm.writeHNT ("STAR", mDurationData);
        if (mStoredInitialActorPosition)
            esm.writeHNT ("POS_", mInitialActorPosition);
    }

    void AiTravel::load(ESMReader &esm)
    {
        esm.getHNT (mData, "DATA");
        esm.getHNOT (mHidden, "HIDD");
        if (esm.isNextSub("REPT"))
            esm.skipHSub();
    }

    void AiTravel::save(ESMWriter &esm) const
    {
        esm.writeHNT ("DATA", mData);
        esm.writeHNT ("HIDD", mHidden);
    }

    void AiEscort::load(ESMReader &esm)
    {
        esm.getHNT (mData, "DATA");
        mTargetId = esm.getHNCompatRefId("TARG");
        mTargetActorId = loadCompatActorId(esm, "TAID");
        esm.getHNT (mRemainingDuration, "DURA");
        mCellId = esm.getHNOString ("CELL");
        if (esm.isNextSub("REPT"))
            esm.skipHSub();
    }

    void AiEscort::save(ESMWriter &esm) const
    {
        esm.writeHNT ("DATA", mData);
        esm.writeHNString ("TARG", mTargetId);
        esm.writeHNT ("TAID", mTargetActorId);
        esm.writeHNT ("DURA", mRemainingDuration);
        if (!mCellId.empty())
            esm.writeHNString ("CELL", mCellId);
    }

    void AiFollow::load(ESMReader &esm)
    {
        esm.getHNT (mData, "DATA");
        mTargetId = esm.getHNCompatRefId("TARG");
        mTargetActorId = loadCompatActorId(esm, "TAID");
        esm.getHNT (mRemainingDuration, "DURA");
        mCellId = esm.getHNOString ("CELL");
        esm.getHNT (mAlwaysFollow, "ALWY");
        mCommanded = false;
        esm.getHNOT (mCommanded, "CMND");
        mActive = false;
        esm.getHNOT (mActive, "ACTV");
        if (esm.isNextSub("REPT"))
            esm.skipHSub();
    }

    void AiFollow::save(ESMWriter &esm) const
    {
        esm.writeHNT ("DATA", mData);
        esm.writeHNString("TARG", mTargetId);
        esm.writeHNT ("TAID", mTargetActorId);
        esm.writeHNT ("DURA", mRemainingDuration);
        if (!mCellId.empty())
            esm.writeHNString ("CELL", mCellId);
        esm.writeHNT ("ALWY", mAlwaysFollow);
        esm.writeHNT ("CMND", mCommanded);
        if (mActive)
            esm.writeHNT("ACTV", mActive);
    }

    void AiActivate::load(ESMReader &esm)
    {
        mTargetId = esm.getHNCompatRefId("TARG");
        if (esm.isNextSub("REPT"))
            esm.skipHSub();
    }

    void AiActivate::save(ESMWriter &esm) const
    {
        esm.writeHNString("TARG", mTargetId);
    }

    void AiCombat::load(ESMReader &esm)
    {
        mTargetActorId = loadCompatActorId(esm, "TARG", false);
    }

    void AiCombat::save(ESMWriter &esm) const
    {
        esm.writeHNT ("TARG", mTargetActorId);
    }

    void AiPursue::load(ESMReader &esm)
    {
        mTargetActorId = loadCompatActorId(esm, "TARG", false);
    }

    void AiPursue::save(ESMWriter &esm) const
    {
        esm.writeHNT ("TARG", mTargetActorId);
    }

    AiSequence::~AiSequence()
    {
        for (std::vector<AiPackageContainer>::iterator it = mPackages.begin(); it != mPackages.end(); ++it)
            delete it->mPackage;
    }

    void AiSequence::save(ESMWriter &esm) const
    {
        for (std::vector<AiPackageContainer>::const_iterator it = mPackages.begin(); it != mPackages.end(); ++it)
        {
            esm.writeHNT ("AIPK", it->mType);
            switch (it->mType)
            {
            case Ai_Wander:
                static_cast<const AiWander*>(it->mPackage)->save(esm);
                break;
            case Ai_Travel:
                static_cast<const AiTravel*>(it->mPackage)->save(esm);
                break;
            case Ai_Escort:
                static_cast<const AiEscort*>(it->mPackage)->save(esm);
                break;
            case Ai_Follow:
                static_cast<const AiFollow*>(it->mPackage)->save(esm);
                break;
            case Ai_Activate:
                static_cast<const AiActivate*>(it->mPackage)->save(esm);
                break;
            case Ai_Combat:
                static_cast<const AiCombat*>(it->mPackage)->save(esm);
                break;
            case Ai_Pursue:
                static_cast<const AiPursue*>(it->mPackage)->save(esm);
                break;

            default:
                break;
            }
        }

        esm.writeHNT ("LAST", mLastAiPackage);
    }

    void AiSequence::load(ESMReader &esm)
    {
        while (esm.isNextSub("AIPK"))
        {
            int type;
            esm.getHT(type);

            mPackages.emplace_back();
            mPackages.back().mType = type;

            switch (type)
            {
            case Ai_Wander:
            {
                std::unique_ptr<AiWander> ptr (new AiWander());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            case Ai_Travel:
            {
                std::unique_ptr<AiTravel> ptr (new AiTravel());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            case Ai_Escort:
            {
                std::unique_ptr<AiEscort> ptr (new AiEscort());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            case Ai_Follow:
            {
                std::unique_ptr<AiFollow> ptr (new AiFollow());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            case Ai_Activate:
            {
                std::unique_ptr<AiActivate> ptr (new AiActivate());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            case Ai_Combat:
            {
                std::unique_ptr<AiCombat> ptr (new AiCombat());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            case Ai_Pursue:
            {
                std::unique_ptr<AiPursue> ptr (new AiPursue());
                ptr->load(esm);
                mPackages.back().mPackage = ptr.release();
                break;
            }
            default:
                return;
            }
        }

        esm.getHNOT (mLastAiPackage, "LAST");
    }
}
}
