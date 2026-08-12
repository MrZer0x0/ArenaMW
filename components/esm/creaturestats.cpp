#include "creaturestats.hpp"
#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "cellref.hpp"
#include "refidcompat.hpp"

#include <limits>

namespace
{
    int loadCompatActorId(ESM::ESMReader& esm, const char* name, bool optional = true)
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

        ESM::RefNum ref;
        ref.unset();
        ref.load(esm, true, name);
        return ref.mIndex == std::numeric_limits<unsigned int>::max()
            ? -1 : static_cast<int>(ref.mIndex);
    }
}

void ESM::CreatureStats::load (ESMReader &esm)
{
    bool intFallback = esm.getFormat() < 11;
    for (int i=0; i<8; ++i)
        mAttributes[i].load (esm, intFallback);

    for (int i=0; i<3; ++i)
        mDynamic[i].load (esm);

    mGoldPool = 0;
    esm.getHNOT (mGoldPool, "GOLD");

    mTradeTime.mDay = 0;
    mTradeTime.mHour = 0;
    esm.getHNOT (mTradeTime, "TIME");

    int flags = 0;
    mDead = false;
    mDeathAnimationFinished = false;
    mDied = false;
    mMurdered = false;
    mTalkedTo = false;
    mAlarmed = false;
    mAttacked = false;
    mKnockdown = false;
    mKnockdownOneFrame = false;
    mKnockdownOverOneFrame = false;
    mHitRecovery = false;
    mBlock = false;
    mRecalcDynamicStats = false;
    if (esm.getFormat() < 8)
    {
        esm.getHNOT (mDead, "DEAD");
        esm.getHNOT (mDeathAnimationFinished, "DFNT");
        if (esm.getFormat() < 3 && mDead)
            mDeathAnimationFinished = true;
        esm.getHNOT (mDied, "DIED");
        esm.getHNOT (mMurdered, "MURD");
        if (esm.isNextSub("FRHT"))
            esm.skipHSub(); // Friendly hits, no longer used
        esm.getHNOT (mTalkedTo, "TALK");
        esm.getHNOT (mAlarmed, "ALRM");
        esm.getHNOT (mAttacked, "ATKD");
        if (esm.isNextSub("HOST"))
            esm.skipHSub(); // Hostile, no longer used
        if (esm.isNextSub("ATCK"))
            esm.skipHSub(); // attackingOrSpell, no longer used
        esm.getHNOT (mKnockdown, "KNCK");
        esm.getHNOT (mKnockdownOneFrame, "KNC1");
        esm.getHNOT (mKnockdownOverOneFrame, "KNCO");
        esm.getHNOT (mHitRecovery, "HITR");
        esm.getHNOT (mBlock, "BLCK");
    }
    else
    {
        esm.getHNOT(flags, "AFLG");
        mDead = flags & Dead;
        mDeathAnimationFinished = flags & DeathAnimationFinished;
        mDied = flags & Died;
        mMurdered = flags & Murdered;
        mTalkedTo = flags & TalkedTo;
        mAlarmed = flags & Alarmed;
        mAttacked = flags & Attacked;
        mKnockdown = flags & Knockdown;
        mKnockdownOneFrame = flags & KnockdownOneFrame;
        mKnockdownOverOneFrame = flags & KnockdownOverOneFrame;
        mHitRecovery = flags & HitRecovery;
        mBlock = flags & Block;
        mRecalcDynamicStats = flags & RecalcDynamicStats;
    }

    mMovementFlags = 0;
    esm.getHNOT (mMovementFlags, "MOVE");

    if (esm.isNextSub("ASTR"))
        esm.skipHSub(); // attackStrength, no longer used

    mFallHeight = 0;
    esm.getHNOT (mFallHeight, "FALL");

    mLastHitObject = esm.getHNOCompatRefId ("LHIT");

    mLastHitAttemptObject = esm.getHNOCompatRefId ("LHAT");

    if (esm.getFormat() < 8)
        esm.getHNOT (mRecalcDynamicStats, "CALC");

    mDrawState = 0;
    esm.getHNOT (mDrawState, "DRAW");

    mLevel = 1;
    esm.getHNOT (mLevel, "LEVL");

    mActorId = loadCompatActorId(esm, "ACID");

    mDeathAnimation = -1;
    esm.getHNOT (mDeathAnimation, "DANM");

    mTimeOfDeath.mDay = 0;
    mTimeOfDeath.mHour = 0;
    esm.getHNOT (mTimeOfDeath, "DTIM");

    mSpells.load(esm);
    mActiveSpells.load(esm);
    mAiSequence.load(esm);
    mMagicEffects.load(esm);

    while (esm.isNextSub("SUMM"))
    {
        int magicEffect = -1;
        if (esm.getFormat() <= 35)
            esm.getHT(magicEffect);
        else
        {
            const CompatRefId effect = esm.getCompatRefIdValue();
            magicEffect = RefIdCompat::magicEffectIndex(effect);
        }

        std::string source = esm.getHNOCompatRefId("SOUR");
        int effectIndex = -1;
        esm.getHNOT (effectIndex, "EIND");
        const int actorId = loadCompatActorId(esm, "ACID", false);

        if (magicEffect >= 0 && actorId >= 0)
            mSummonedCreatureMap[SummonKey(magicEffect, source, effectIndex)] = actorId;
    }

    while (esm.isNextSub("GRAV"))
    {
        int actorId = -1;
        if (esm.getFormat() <= 34)
            esm.getHT(actorId);
        else
        {
            RefNum actor;
            actor.unset();
            esm.getHT(actor, 8);
            if (actor.mIndex != std::numeric_limits<unsigned int>::max())
                actorId = static_cast<int>(actor.mIndex);
        }

        if (actorId >= 0)
            mSummonGraveyard.push_back(actorId);
    }

    mHasAiSettings = false;
    esm.getHNOT(mHasAiSettings, "AISE");

    if (mHasAiSettings)
    {
        for (int i=0; i<4; ++i)
            mAiSettings[i].load(esm);
    }

    while (esm.isNextSub("CORP"))
    {
        std::string id = esm.getCompatRefId();

        CorprusStats stats;
        esm.getHNT(stats.mWorsenings, "WORS");
        esm.getHNT(stats.mNextWorsening, "TIME");

        mCorprusSpells[id] = stats;
    }
}

void ESM::CreatureStats::save (ESMWriter &esm) const
{
    for (int i=0; i<8; ++i)
        mAttributes[i].save (esm);

    for (int i=0; i<3; ++i)
        mDynamic[i].save (esm);

    if (mGoldPool)
        esm.writeHNT ("GOLD", mGoldPool);

    if (mTradeTime.mDay != 0 || mTradeTime.mHour != 0)
        esm.writeHNT ("TIME", mTradeTime);

    int flags = 0;
    if (mDead) flags |= Dead;
    if (mDeathAnimationFinished) flags |= DeathAnimationFinished;
    if (mDied) flags |= Died;
    if (mMurdered) flags |= Murdered;
    if (mTalkedTo) flags |= TalkedTo;
    if (mAlarmed) flags |= Alarmed;
    if (mAttacked) flags |= Attacked;
    if (mKnockdown) flags |= Knockdown;
    if (mKnockdownOneFrame) flags |= KnockdownOneFrame;
    if (mKnockdownOverOneFrame) flags |= KnockdownOverOneFrame;
    if (mHitRecovery) flags |= HitRecovery;
    if (mBlock) flags |= Block;
    if (mRecalcDynamicStats) flags |= RecalcDynamicStats;

    if (flags)
        esm.writeHNT ("AFLG", flags);

    if (mMovementFlags)
        esm.writeHNT ("MOVE", mMovementFlags);

    if (mFallHeight)
        esm.writeHNT ("FALL", mFallHeight);

    if (!mLastHitObject.empty())
        esm.writeHNString ("LHIT", mLastHitObject);

    if (!mLastHitAttemptObject.empty())
        esm.writeHNString ("LHAT", mLastHitAttemptObject);

    if (mDrawState)
        esm.writeHNT ("DRAW", mDrawState);

    if (mLevel != 1)
        esm.writeHNT ("LEVL", mLevel);

    if (mActorId != -1)
        esm.writeHNT ("ACID", mActorId);

    if (mDeathAnimation != -1)
        esm.writeHNT ("DANM", mDeathAnimation);

    if (mTimeOfDeath.mHour != 0 || mTimeOfDeath.mDay != 0)
        esm.writeHNT ("DTIM", mTimeOfDeath);

    mSpells.save(esm);
    mActiveSpells.save(esm);
    mAiSequence.save(esm);
    mMagicEffects.save(esm);

    for (const auto& summon : mSummonedCreatureMap)
    {
        esm.writeHNT ("SUMM", summon.first.mEffectId);
        esm.writeHNString ("SOUR", summon.first.mSourceId);
        int effectIndex = summon.first.mEffectIndex;
        if (effectIndex != -1)
            esm.writeHNT ("EIND", effectIndex);
        esm.writeHNT ("ACID", summon.second);
    }

    for (int key : mSummonGraveyard)
    {
        esm.writeHNT ("GRAV", key);
    }

    esm.writeHNT("AISE", mHasAiSettings);
    if (mHasAiSettings)
    {
        for (int i=0; i<4; ++i)
            mAiSettings[i].save(esm);
    }

    for (const auto& corprusSpell : mCorprusSpells)
    {
        esm.writeHNString("CORP", corprusSpell.first);

        const CorprusStats & stats = corprusSpell.second;
        esm.writeHNT("WORS", stats.mWorsenings);
        esm.writeHNT("TIME", stats.mNextWorsening);
    }
}

void ESM::CreatureStats::blank()
{
    mTradeTime.mHour = 0;
    mTradeTime.mDay = 0;
    mGoldPool = 0;
    mActorId = -1;
    mHasAiSettings = false;
    mDead = false;
    mDeathAnimationFinished = false;
    mDied = false;
    mMurdered = false;
    mTalkedTo = false;
    mAlarmed = false;
    mAttacked = false;
    mKnockdown = false;
    mKnockdownOneFrame = false;
    mKnockdownOverOneFrame = false;
    mHitRecovery = false;
    mBlock = false;
    mMovementFlags = 0;
    mFallHeight = 0.f;
    mRecalcDynamicStats = false;
    mDrawState = 0;
    mDeathAnimation = -1;
    mLevel = 1;
    mCorprusSpells.clear();
}
