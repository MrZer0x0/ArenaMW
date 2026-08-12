#include "loadtes3.hpp"

#include "esmcommon.hpp"
#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "defs.hpp"

void ESM::Header::blank()
{
    mData.version = ESM::VER_13;
    mData.type = 0;
    mData.author.clear();
    mData.desc.clear();
    mData.records = 0;
    mFormat = CurrentFormat;
    mMaster.clear();
}

void ESM::Header::load (ESMReader &esm)
{
    if (esm.isNextSub ("FORM"))
    {
        esm.getHT (mFormat);
        if (mFormat<0)
            esm.fail ("invalid format code");
    }
    else
        mFormat = 0;

    if (esm.isNextSub("HEDR"))
    {
      esm.getSubHeader();
      const uint32_t hedrSize = esm.getSubSize();
      uint32_t consumed = 0;

      // Legacy ESM headers use fixed 32/256-byte author and description
      // fields (300 bytes total). Newer OpenMW save formats store both
      // strings with explicit lengths, so an empty save header is only
      // 20 bytes. Reading it as the legacy layout advances into the SAVE
      // record and makes the slot disappear from the load menu.
      if (hedrSize < 12)
          esm.fail("HEDR subrecord is too small");

      esm.getT(mData.version);
      esm.getT(mData.type);
      consumed += 8;

      const bool variableSizedStrings = mFormat > 22 || hedrSize < 300;
      if (variableSizedStrings)
      {
          if (hedrSize < 20)
          {
              // Unknown compact header variant: preserve the mandatory
              // record count at the end and ignore the middle payload.
              esm.skip(static_cast<int>(hedrSize - 12));
              consumed = hedrSize - 4;
              mData.author.clear();
              mData.desc.clear();
          }
          else
          {
              uint32_t authorSize = 0;
              esm.getT(authorSize);
              consumed += 4;
              if (authorSize > hedrSize - consumed - 8)
                  esm.fail("invalid variable-size HEDR author length");
              mData.author.assign(esm.getString(static_cast<int>(authorSize)));
              consumed += authorSize;

              uint32_t descriptionSize = 0;
              esm.getT(descriptionSize);
              consumed += 4;
              if (descriptionSize > hedrSize - consumed - 4)
                  esm.fail("invalid variable-size HEDR description length");
              mData.desc.assign(esm.getString(static_cast<int>(descriptionSize)));
              consumed += descriptionSize;
          }
      }
      else
      {
          mData.author.assign(esm.getString(32));
          mData.desc.assign(esm.getString(256));
          consumed += 288;
      }

      if (consumed + 4 > hedrSize)
          esm.fail("HEDR record count does not fit subrecord");

      // Future versions may append more header metadata. Keep the last
      // four bytes reserved for the record count and skip only the tail
      // that this branch does not understand.
      if (consumed + 4 < hedrSize)
      {
          esm.skip(static_cast<int>(hedrSize - consumed - 4));
          consumed = hedrSize - 4;
      }

      esm.getT(mData.records);
    }

    while (esm.isNextSub ("MAST"))
    {
        MasterData m;
        m.name = esm.getHString();
        esm.getHNT(m.size, "DATA");
        mMaster.push_back (m);
    }

    if (esm.isNextSub("GMDT"))
    {
        esm.getHT(mGameData);
    }
    if (esm.isNextSub("SCRD"))
    {
        esm.getSubHeader();
        mSCRD.resize(esm.getSubSize());
        if (!mSCRD.empty())
            esm.getExact(mSCRD.data(), mSCRD.size());
    }
    if (esm.isNextSub("SCRS"))
    {
        esm.getSubHeader();
        mSCRS.resize(esm.getSubSize());
        if (!mSCRS.empty())
            esm.getExact(mSCRS.data(), mSCRS.size());
    }
}

void ESM::Header::save (ESMWriter &esm)
{
    if (mFormat>0)
        esm.writeHNT ("FORM", mFormat);

    esm.startSubRecord("HEDR");
    esm.writeT(mData.version);
    esm.writeT(mData.type);
    esm.writeFixedSizeString(mData.author, 32);
    esm.writeFixedSizeString(mData.desc, 256);
    esm.writeT(mData.records);
    esm.endRecord("HEDR");

    for (const Header::MasterData& data : mMaster)
    {
        esm.writeHNCString ("MAST", data.name);
        esm.writeHNT ("DATA", data.size);
    }
}
