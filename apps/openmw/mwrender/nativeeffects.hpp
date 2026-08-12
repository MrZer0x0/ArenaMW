#ifndef OPENMW_MWRENDER_NATIVEEFFECTS_H
#define OPENMW_MWRENDER_NATIVEEFFECTS_H

#include <atomic>

#include <osg/Camera>
#include <osg/ref_ptr>
#include <osg/observer_ptr>

namespace osg
{
    class Group;
    class Geode;
    class Program;
    class StateSet;
    class Texture2D;
    class Uniform;
}

namespace Shader
{
    class ShaderManager;
}

namespace MWRender
{
    class NativeEffectsProcessor
    {
    public:
        NativeEffectsProcessor(osg::Camera* mainCamera, osg::Group* rootNode,
            Shader::ShaderManager& shaderManager);
        ~NativeEffectsProcessor();

        void reloadSettings();
        void update();

        bool isEnabled() const { return mEnabled; }

    private:
        class FramebufferCopyCallback;

        static osg::ref_ptr<osg::Texture2D> createTexture(
            int internalFormat, unsigned int sourceFormat, unsigned int sourceType, bool linear = true);
        static osg::ref_ptr<osg::Camera> createCamera(int orderNum, bool renderToTexture);
        static osg::ref_ptr<osg::Geode> createFullscreenPass(osg::Program* program);

        void copyFramebuffer(osg::RenderInfo& renderInfo);
        void resizeTargets(int width, int height);
        void updateMatrices();
        void updateSourceBindings();
        void applyPassVisibility();

        osg::observer_ptr<osg::Camera> mMainCamera;
        osg::observer_ptr<osg::Group> mRootNode;
        osg::ref_ptr<osg::Camera::DrawCallback> mOriginalPostDrawCallback;
        osg::ref_ptr<FramebufferCopyCallback> mFramebufferCopyCallback;

        bool mReady = false;
        bool mEnabled = false;
        bool mSmaaEnabled = false;
        bool mSsrEnabled = false;
        bool mBloomEnabled = false;
        std::atomic<bool> mCaptureReady{false};
        int mWidth = 0;
        int mHeight = 0;

        osg::ref_ptr<osg::Texture2D> mSceneTexture;
        osg::ref_ptr<osg::Texture2D> mDepthTexture;
        osg::ref_ptr<osg::Texture2D> mEffectsTexture;
        osg::ref_ptr<osg::Texture2D> mBloomHorizontalTexture;
        osg::ref_ptr<osg::Texture2D> mBloomVerticalTexture;
        osg::ref_ptr<osg::Texture2D> mEdgeTexture;
        osg::ref_ptr<osg::Texture2D> mWeightTexture;

        osg::ref_ptr<osg::Camera> mEffectsCamera;
        osg::ref_ptr<osg::Camera> mBloomHorizontalCamera;
        osg::ref_ptr<osg::Camera> mBloomVerticalCamera;
        osg::ref_ptr<osg::Camera> mEdgeCamera;
        osg::ref_ptr<osg::Camera> mWeightCamera;
        osg::ref_ptr<osg::Camera> mFinalCamera;

        osg::ref_ptr<osg::StateSet> mEffectsState;
        osg::ref_ptr<osg::StateSet> mBloomHorizontalState;
        osg::ref_ptr<osg::StateSet> mEdgeState;
        osg::ref_ptr<osg::StateSet> mFinalState;

        osg::ref_ptr<osg::Uniform> mInverseSceneSizeEffects;
        osg::ref_ptr<osg::Uniform> mInverseSceneSizeEdge;
        osg::ref_ptr<osg::Uniform> mInverseSceneSizeWeight;
        osg::ref_ptr<osg::Uniform> mInverseSceneSizeFinal;
        osg::ref_ptr<osg::Uniform> mInverseBloomSizeHorizontal;
        osg::ref_ptr<osg::Uniform> mInverseBloomSizeVertical;
        osg::ref_ptr<osg::Uniform> mInverseProjectionEffects;
        osg::ref_ptr<osg::Uniform> mProjectionEffects;

        osg::ref_ptr<osg::Uniform> mSsrEnabledUniform;
        osg::ref_ptr<osg::Uniform> mSsrStrengthUniform;
        osg::ref_ptr<osg::Uniform> mSsrDistanceUniform;
        osg::ref_ptr<osg::Uniform> mSmaaEnabledUniform;
        osg::ref_ptr<osg::Uniform> mSmaaThresholdUniform;
        osg::ref_ptr<osg::Uniform> mBloomEnabledUniform;
        osg::ref_ptr<osg::Uniform> mBloomThresholdUniform;
        osg::ref_ptr<osg::Uniform> mBloomSoftKneeUniform;
        osg::ref_ptr<osg::Uniform> mBloomRadiusHorizontalUniform;
        osg::ref_ptr<osg::Uniform> mBloomRadiusVerticalUniform;
        osg::ref_ptr<osg::Uniform> mBloomIntensityUniform;
    };
}

#endif
