#ifndef OPENMW_ESM_REFIDCOMPAT_H
#define OPENMW_ESM_REFIDCOMPAT_H

#include "attr.hpp"
#include "esmreader.hpp"
#include "loadmgef.hpp"
#include "loadskil.hpp"

#include <components/misc/stringops.hpp>

#include <limits>
#include <string>

namespace ESM
{
    namespace RefIdCompat
    {
        inline int checkedIndex(const CompatRefId& id)
        {
            if (id.mType != CompatRefId::Index
                || id.mIndex > static_cast<uint32_t>(std::numeric_limits<int>::max()))
                return -1;
            return static_cast<int>(id.mIndex);
        }

        inline int magicEffectIndex(const CompatRefId& id)
        {
            const int numeric = checkedIndex(id);
            if (numeric >= 0)
                return numeric < MagicEffect::Length ? numeric : -1;

            if (!id.isString())
                return -1;

            // ArenaMW 0.47 already has the complete canonical effect-name table,
            // exposed through IDs such as "#090ResistFire". Compare the textual
            // suffix used by typed RefIds in save format 36+.
            for (int i = 0; i < MagicEffect::Length; ++i)
            {
                const std::string legacyId = MagicEffect::indexToId(i);
                if (legacyId.size() > 4
                    && Misc::StringUtils::ciEqual(legacyId.substr(4), id.mString))
                    return i;
            }
            return -1;
        }

        inline int attributeIndex(const CompatRefId& id)
        {
            const int numeric = checkedIndex(id);
            if (numeric >= 0)
                return numeric < Attribute::Length ? numeric : -1;

            if (!id.isString())
                return -1;

            for (int i = 0; i < Attribute::Length; ++i)
            {
                if (Misc::StringUtils::ciEqual(Attribute::sAttributeNames[i], id.mString))
                    return i;
            }
            return -1;
        }

        inline int skillIndex(const CompatRefId& id)
        {
            const int numeric = checkedIndex(id);
            if (numeric >= 0)
                return numeric < Skill::Length ? numeric : -1;

            if (!id.isString())
                return -1;

            for (int i = 0; i < Skill::Length; ++i)
            {
                if (Misc::StringUtils::ciEqual(Skill::sSkillNames[i], id.mString))
                    return i;
            }
            return -1;
        }

        inline bool targetsAttribute(int effectId)
        {
            switch (effectId)
            {
                case MagicEffect::DrainAttribute:
                case MagicEffect::DamageAttribute:
                case MagicEffect::RestoreAttribute:
                case MagicEffect::FortifyAttribute:
                case MagicEffect::AbsorbAttribute:
                    return true;
                default:
                    return false;
            }
        }

        inline bool targetsSkill(int effectId)
        {
            switch (effectId)
            {
                case MagicEffect::DrainSkill:
                case MagicEffect::DamageSkill:
                case MagicEffect::RestoreSkill:
                case MagicEffect::FortifySkill:
                case MagicEffect::AbsorbSkill:
                    return true;
                default:
                    return false;
            }
        }

        inline int effectArgumentIndex(const CompatRefId& id, int effectId)
        {
            const int numeric = checkedIndex(id);
            if (numeric >= 0)
                return numeric;

            if (targetsAttribute(effectId))
                return attributeIndex(id);
            if (targetsSkill(effectId))
                return skillIndex(id);

            // Be permissive for modded effects whose target flag is not known to
            // this older engine. Both index spaces intentionally start at zero.
            const int attribute = attributeIndex(id);
            return attribute >= 0 ? attribute : skillIndex(id);
        }
    }
}

#endif
