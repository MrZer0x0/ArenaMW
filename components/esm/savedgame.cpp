#include "savedgame.hpp"

#include <cstdint>
#include <cstring>

#include <components/debug/debuglog.hpp>

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace
{
    const std::uint32_t sMaxProfileStringSize = 1024 * 1024;
    const std::uint32_t sMaxScreenshotSize = 64 * 1024 * 1024;

    void skipMalformedSubrecord(ESM::ESMReader& esm, const ESM::NAME& name, std::uint32_t size,
        std::uint32_t expectedSize)
    {
        Log(Debug::Warning) << "Warning: Ignoring malformed SAVE subrecord " << name.toString()
                            << " (size " << size << ", expected " << expectedSize << ")";
        esm.skip(static_cast<int>(size));
    }

    bool readProfileString(ESM::ESMReader& esm, const ESM::NAME& name, std::string& value)
    {
        esm.getSubHeader();
        const std::uint32_t size = esm.getSubSize();
        if (size > sMaxProfileStringSize)
        {
            Log(Debug::Warning) << "Warning: Ignoring oversized SAVE string subrecord " << name.toString()
                                << " (" << size << " bytes)";
            esm.skip(static_cast<int>(size));
            return false;
        }

        value = esm.getString(static_cast<int>(size));
        return true;
    }


    std::string decodeCompatibleRefIdString(const std::string& value, int format)
    {
        // RefIds became tagged values after format 23. Saved-game class
        // identifiers are normally string RefIds; decode the two string
        // representations so the load menu can still resolve the class.
        if (format <= 23 || value.empty())
            return value;

        const unsigned char type = static_cast<unsigned char>(value[0]);
        if (type == 2) // UnsizedString
            return value.substr(1);

        if (type == 1 && value.size() >= 5) // SizedString
        {
            std::uint32_t size = 0;
            std::memcpy(&size, value.data() + 1, sizeof(size));
            if (size <= value.size() - 5)
                return value.substr(5, size);
        }

        // Non-string RefIds cannot be represented by this old branch.
        return std::string();
    }

    template <class T>
    bool readProfileValue(ESM::ESMReader& esm, const ESM::NAME& name, T& value)
    {
        esm.getSubHeader();
        const std::uint32_t size = esm.getSubSize();
        if (size != sizeof(T))
        {
            skipMalformedSubrecord(esm, name, size, static_cast<std::uint32_t>(sizeof(T)));
            return false;
        }

        esm.getExact(&value, static_cast<int>(sizeof(T)));
        return true;
    }
}

unsigned int ESM::SavedGame::sRecordId = ESM::REC_SAVE;
int ESM::SavedGame::sCurrentFormat = 15;

void ESM::SavedGame::load (ESMReader &esm)
{
    // SAVE is also read while building the load-game menu. Keep this parser
    // deliberately order-independent and tolerant of fields introduced by
    // newer OpenMW versions, so a future save can still be listed and a
    // best-effort load can be attempted.
    mContentFiles.clear();
    mPlayerName.clear();
    mPlayerLevel = 0;
    mPlayerClassId.clear();
    mPlayerClassName.clear();
    mPlayerCell.clear();
    mInGameTime = EpochTimeStamp();
    mTimePlayed = 0.0;
    mDescription.clear();
    mScreenshot.clear();

    while (esm.hasMoreSubs())
    {
        esm.getSubName();
        const NAME name = esm.retSubName();

        if (name == "PLNA")
            readProfileString(esm, name, mPlayerName);
        else if (name == "PLLE")
            readProfileValue(esm, name, mPlayerLevel);
        else if (name == "PLCL")
        {
            std::string classId;
            if (readProfileString(esm, name, classId))
                mPlayerClassId = decodeCompatibleRefIdString(classId, esm.getFormat());
        }
        else if (name == "PLCN")
            readProfileString(esm, name, mPlayerClassName);
        else if (name == "PLCE")
            readProfileString(esm, name, mPlayerCell);
        else if (name == "TSTM")
            readProfileValue(esm, name, mInGameTime);
        else if (name == "TIME")
            readProfileValue(esm, name, mTimePlayed);
        else if (name == "DESC")
            readProfileString(esm, name, mDescription);
        else if (name == "DEPE")
        {
            std::string dependency;
            if (readProfileString(esm, name, dependency))
                mContentFiles.push_back(dependency);
        }
        else if (name == "SCRN")
        {
            esm.getSubHeader();
            const std::uint32_t size = esm.getSubSize();
            if (size > sMaxScreenshotSize)
            {
                Log(Debug::Warning) << "Warning: Ignoring oversized save screenshot (" << size << " bytes)";
                esm.skip(static_cast<int>(size));
            }
            else
            {
                mScreenshot.resize(size);
                if (!mScreenshot.empty())
                    esm.getExact(mScreenshot.data(), static_cast<int>(mScreenshot.size()));
            }
        }
        else
        {
            // Newer OpenMW versions add profile-only fields such as CDAY,
            // CHLT and MHLT. They are intentionally ignored during conversion;
            // the state manager emits one concise conversion warning instead
            // of one line for every unsupported field and every scanned save.
            esm.skipHSub();
        }
    }

    // Some future writers may omit the legacy DEPE list and rely only on the
    // masters in the file header. Use those as a safe fallback for profile
    // filtering and missing-master checks.
    if (mContentFiles.empty())
    {
        const std::vector<Header::MasterData>& masters = esm.getGameFiles();
        for (std::vector<Header::MasterData>::const_iterator it = masters.begin(); it != masters.end(); ++it)
            mContentFiles.push_back(it->name);
    }
}

void ESM::SavedGame::save (ESMWriter &esm) const
{
    esm.writeHNString ("PLNA", mPlayerName);
    esm.writeHNT ("PLLE", mPlayerLevel);

    if (!mPlayerClassId.empty())
        esm.writeHNString ("PLCL", mPlayerClassId);
    else
        esm.writeHNString ("PLCN", mPlayerClassName);

    esm.writeHNString ("PLCE", mPlayerCell);
    esm.writeHNT ("TSTM", mInGameTime, 16);
    esm.writeHNT ("TIME", mTimePlayed);
    esm.writeHNString ("DESC", mDescription);

    for (std::vector<std::string>::const_iterator iter (mContentFiles.begin());
         iter!=mContentFiles.end(); ++iter)
         esm.writeHNString ("DEPE", *iter);

    esm.startSubRecord("SCRN");
    esm.write(&mScreenshot[0], mScreenshot.size());
    esm.endRecord("SCRN");
}
