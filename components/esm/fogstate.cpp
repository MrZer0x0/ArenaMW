#include "fogstate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

#include <osgDB/ReadFile>

#include <components/debug/debuglog.hpp>
#include <components/files/memorystream.hpp>

#include "savedgame.hpp"

void convertFogOfWar(std::vector<char>& imageData)
{
    if (imageData.empty())
    {
        return;
    }

    osgDB::ReaderWriter* tgaReader = osgDB::Registry::instance()->getReaderWriterForExtension("tga");
    if (!tgaReader)
    {
        Log(Debug::Error) << "Error: Unable to load fog, can't find a tga ReaderWriter";
        return;
    }

    Files::IMemStream in(&imageData[0], imageData.size());

    osgDB::ReaderWriter::ReadResult result = tgaReader->readImage(in);
    if (!result.success())
    {
        Log(Debug::Error) << "Error: Failed to read fog: " << result.message() << " code " << result.status();
        return;
    }

    osgDB::ReaderWriter* pngWriter = osgDB::Registry::instance()->getReaderWriterForExtension("png");
    if (!pngWriter)
    {
        Log(Debug::Error) << "Error: Unable to write fog, can't find a png ReaderWriter";
        return;
    }

    std::ostringstream ostream;
    osgDB::ReaderWriter::WriteResult png = pngWriter->writeImage(*result.getImage(), ostream);
    if (!png.success())
    {
        Log(Debug::Error) << "Error: Unable to write fog: " << png.message() << " code " << png.status();
        return;
    }

    std::string str = ostream.str();
    imageData = std::vector<char>(str.begin(), str.end());
}

void ESM::FogState::load (ESMReader &esm)
{
    mBounds.mMinX = 0.f;
    mBounds.mMinY = 0.f;
    mBounds.mMaxX = 0.f;
    mBounds.mMaxY = 0.f;
    mNorthMarkerAngle = 0.f;
    mFogTextures.clear();

    esm.getHNOT(mBounds, "BOUN");
    esm.getHNOT(mNorthMarkerAngle, "ANGL");

    // OpenMW 0.50+ writes the map rotation centre before the fog textures.
    // ArenaMW 0.47 used to leave CNTR unread, so FTEX was never loaded and
    // LocalMap later indexed an empty texture vector when entering a visited
    // interior. For older saves, use the historical bounds centre.
    mCenterX = (mBounds.mMinX + mBounds.mMaxX) * 0.5f;
    mCenterY = (mBounds.mMinY + mBounds.mMaxY) * 0.5f;
    if (esm.isNextSub("CNTR"))
    {
        struct Center
        {
            float mX;
            float mY;
        } center;

        esm.getHT(center);
        mCenterX = center.mX;
        mCenterY = center.mY;
    }

    int dataFormat = esm.getFormat();
    while (esm.isNextSub("FTEX"))
    {
        esm.getSubHeader();
        FogTexture tex;

        esm.getT(tex.mX);
        esm.getT(tex.mY);

        size_t imageSize = esm.getSubSize()-sizeof(int)*2;
        tex.mImageData.resize(imageSize);
        esm.getExact(&tex.mImageData[0], imageSize);

        if (dataFormat < 7)
            convertFogOfWar(tex.mImageData);

        mFogTextures.push_back(tex);
    }
}

void ESM::FogState::save (ESMWriter &esm, bool interiorCell) const
{
    if (interiorCell)
    {
        esm.writeHNT("BOUN", mBounds);
        esm.writeHNT("ANGL", mNorthMarkerAngle);
    }
    for (std::vector<FogTexture>::const_iterator it = mFogTextures.begin(); it != mFogTextures.end(); ++it)
    {
        esm.startSubRecord("FTEX");
        esm.writeT(it->mX);
        esm.writeT(it->mY);
        esm.write(&it->mImageData[0], it->mImageData.size());
        esm.endRecord("FTEX");
    }
}
