#include "loadglob.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "defs.hpp"

namespace ESM
{
    unsigned int Global::sRecordId = REC_GLOB;

    void Global::load (ESMReader &esm, bool &isDeleted)
    {
        isDeleted = false;

        // OpenMW save format 24+ stores record identifiers as typed RefIds.
        // getHNCompatRefId keeps legacy plain strings working and strips the
        // typed prefix from newer saves (for example 0x02 + CharGenState).
        mId = esm.getHNCompatRefId("NAME");

        if (esm.isNextSub ("DELE"))
        {
            esm.skipHSub();
            isDeleted = true;
        }
        else
        {
            mValue.read (esm, ESM::Variant::Format_Global);
        }
    }

    void Global::save (ESMWriter &esm, bool isDeleted) const
    {
        esm.writeHNCString ("NAME", mId);

        if (isDeleted)
        {
            esm.writeHNCString ("DELE", "");
        }
        else
        {
            mValue.write (esm, ESM::Variant::Format_Global);
        }
    }

    void Global::blank()
    {
        mValue.setType (ESM::VT_None);
    }

    bool operator== (const Global& left, const Global& right)
    {
        return left.mId==right.mId && left.mValue==right.mValue;
    }
}
