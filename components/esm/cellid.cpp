#include "cellid.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "defs.hpp"

const std::string ESM::CellId::sDefaultWorldspace = "sys::default";

void ESM::CellId::load (ESMReader &esm)
{
    // Newer OpenMW saves encode a cell identifier as a typed RefId in NAME.
    // Legacy ArenaMW saves keep using SPAC/CIDX and are written unchanged.
    if (esm.getFormat() > 26 && esm.isNextSub("NAME"))
    {
        esm.getSubHeader();
        uint32_t size = esm.getSubSize();
        if (size == 0)
            esm.fail("Empty cell RefId");

        uint8_t type = 0;
        esm.getT(type);
        --size;

        mIndex.mX = 0;
        mIndex.mY = 0;
        mWorldspace = sDefaultWorldspace;

        if (type == 6) // ESM3ExteriorCellRefId
        {
            if (size != 8)
                esm.fail("Invalid exterior cell RefId size");
            esm.getT(mIndex.mX);
            esm.getT(mIndex.mY);
            mPaged = true;
            return;
        }

        if (type == 2) // UnsizedStringRefId, interior cell name
        {
            mWorldspace = esm.getString(static_cast<int>(size));
            mPaged = false;
            return;
        }

        if (type == 5) // IndexRefId; OpenMW uses Index(CSTA, 0) for the draft cell
        {
            if (size != 8)
                esm.fail("Invalid index cell RefId size");

            uint32_t recordType = 0;
            uint32_t index = 0;
            esm.getT(recordType);
            esm.getT(index);

            if (recordType == ESM::REC_CSTA && index == 0)
            {
                mWorldspace = "sys::compat::draft-cell";
                mPaged = false;
                return;
            }

            esm.fail("Unsupported index cell RefId");
        }

        if (type == 1) // SizedStringRefId, accepted for forward compatibility
        {
            if (size < 4)
                esm.fail("Invalid sized cell RefId");
            uint32_t length = 0;
            esm.getT(length);
            size -= 4;
            if (length > size)
                esm.fail("Cell RefId string exceeds subrecord size");
            mWorldspace = esm.getString(static_cast<int>(length));
            if (size > length)
                esm.skip(static_cast<int>(size - length));
            mPaged = false;
            return;
        }

        if (size)
            esm.skip(static_cast<int>(size));
        esm.fail("Unsupported cell RefId type");
    }

    mWorldspace = esm.getHNString ("SPAC");

    if (esm.isNextSub ("CIDX"))
    {
        esm.getHT (mIndex, 8);
        mPaged = true;
    }
    else
    {
        mIndex.mX = 0;
        mIndex.mY = 0;
        mPaged = false;
    }
}

void ESM::CellId::save (ESMWriter &esm) const
{
    esm.writeHNString ("SPAC", mWorldspace);

    if (mPaged)
        esm.writeHNT ("CIDX", mIndex, 8);
}

bool ESM::operator== (const CellId& left, const CellId& right)
{
    return left.mWorldspace==right.mWorldspace && left.mPaged==right.mPaged &&
        (!left.mPaged || (left.mIndex.mX==right.mIndex.mX && left.mIndex.mY==right.mIndex.mY));
}

bool ESM::operator!= (const CellId& left, const CellId& right)
{
    return !(left==right);
}

bool ESM::operator < (const CellId& left, const CellId& right)
{
    if (left.mPaged < right.mPaged)
        return true;
    if (left.mPaged > right.mPaged)
        return false;

    if (left.mPaged)
    {
        if (left.mIndex.mX < right.mIndex.mX)
            return true;
        if (left.mIndex.mX > right.mIndex.mX)
            return false;

        if (left.mIndex.mY < right.mIndex.mY)
            return true;
        if (left.mIndex.mY > right.mIndex.mY)
            return false;
    }

    return left.mWorldspace < right.mWorldspace;
}
