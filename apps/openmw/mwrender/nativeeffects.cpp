#include "nativeeffects.hpp"

#include <algorithm>

#include <osg/Camera>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Group>
#include <osg/Matrix>
#include <osg/Program>
#include <osg/State>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Viewport>

#include <components/debug/debuglog.hpp>
#include <components/settings/settings.hpp>
#include <components/shader/shadermanager.hpp>

#include "vismask.hpp"

namespace MWRender
{
    class NativeEffectsProcessor::FramebufferCopyCallback : public osg::Camera::DrawCallback
    {
    public:
        explicit FramebufferCopyCallback(NativeEffectsProcessor* owner)
            : mOwner(owner)
        {
        }

        void detach() { mOwner.store(nullptr, std::memory_order_release); }

        void operator()(osg::RenderInfo& renderInfo) const override
        {
            NativeEffectsProcessor* owner = mOwner.load(std::memory_order_acquire);
            if (!owner)
                return;

            owner->copyFramebuffer(renderInfo);
            if (owner->mOriginalPostDrawCallback)
                (*owner->mOriginalPostDrawCallback)(renderInfo);
        }

    private:
        std::atomic<NativeEffectsProcessor*> mOwner;
    };

    NativeEffectsProcessor::NativeEffectsProcessor(osg::Camera* mainCamera, osg::Group* rootNode,
        Shader::ShaderManager& shaderManager)
        : mMainCamera(mainCamera)
        , mRootNode(rootNode)
        , mOriginalPostDrawCallback(mainCamera ? mainCamera->getPostDrawCallback() : nullptr)
    {
        if (!mainCamera || !rootNode)
        {
            Log(Debug::Error) << "Cannot create ArenaMW native effects without a main camera/root node";
            return;
        }

        // Keep this compositor deliberately small: only SSR and SMAA are native
        // effects. Experimental scene-atmosphere replacement passes are not
        // part of this processor, so ArenaMW keeps its established world/sky path.
        mSceneTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mDepthTexture = createTexture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, false);
        mEffectsTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mBloomHorizontalTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mBloomVerticalTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
        mEdgeTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false);
        mWeightTexture = createTexture(GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);

        const Shader::ShaderManager::DefineMap defines;
        osg::ref_ptr<osg::Shader> vertex = shaderManager.getShader("fullscreen_tri.vert", defines, osg::Shader::VERTEX);
        auto getProgram = [&](const char* fragmentName) -> osg::ref_ptr<osg::Program>
        {
            osg::ref_ptr<osg::Shader> fragment = shaderManager.getShader(fragmentName, defines, osg::Shader::FRAGMENT);
            if (!vertex || !fragment)
                return nullptr;
            return shaderManager.getProgram(vertex, fragment);
        };

        osg::ref_ptr<osg::Program> effectsProgram = getProgram("native_effects_composite.frag");
        osg::ref_ptr<osg::Program> bloomHorizontalProgram = getProgram("bloom_extract_horizontal.frag");
        osg::ref_ptr<osg::Program> bloomVerticalProgram = getProgram("bloom_vertical.frag");
        osg::ref_ptr<osg::Program> edgeProgram = getProgram("native_smaa_edge.frag");
        osg::ref_ptr<osg::Program> weightProgram = getProgram("native_smaa_weights.frag");
        osg::ref_ptr<osg::Program> finalProgram = getProgram("native_final.frag");

        if (!effectsProgram || !bloomHorizontalProgram || !bloomVerticalProgram
            || !edgeProgram || !weightProgram || !finalProgram)
        {
            Log(Debug::Error) << "Failed to create ArenaMW native SSR/SMAA programs";
            return;
        }

        // All native passes finish before MyGUI (POST_RENDER order 0), keeping
        // HUD/menu rendering untouched.
        mEffectsCamera = createCamera(-10, true);
        mBloomHorizontalCamera = createCamera(-9, true);
        mBloomVerticalCamera = createCamera(-8, true);
        mEdgeCamera = createCamera(-7, true);
        mWeightCamera = createCamera(-6, true);
        mFinalCamera = createCamera(-5, false);

        mEffectsCamera->attach(osg::Camera::COLOR_BUFFER0, mEffectsTexture);
        mBloomHorizontalCamera->attach(osg::Camera::COLOR_BUFFER0, mBloomHorizontalTexture);
        mBloomVerticalCamera->attach(osg::Camera::COLOR_BUFFER0, mBloomVerticalTexture);
        mEdgeCamera->attach(osg::Camera::COLOR_BUFFER0, mEdgeTexture);
        mWeightCamera->attach(osg::Camera::COLOR_BUFFER0, mWeightTexture);

        osg::ref_ptr<osg::Geode> effectsPass = createFullscreenPass(effectsProgram);
        mEffectsState = effectsPass->getOrCreateStateSet();
        mEffectsState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mEffectsState->setTextureAttributeAndModes(1, mDepthTexture, osg::StateAttribute::ON);
        mEffectsState->addUniform(new osg::Uniform("sceneTexture", 0));
        mEffectsState->addUniform(new osg::Uniform("depthTexture", 1));
        mInverseSceneSizeEffects = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        mInverseProjectionEffects = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "inverseProjectionMatrix");
        mProjectionEffects = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "projectionMatrix");
        mSsrEnabledUniform = new osg::Uniform("ssrEnabled", 0.f);
        mSsrStrengthUniform = new osg::Uniform("ssrStrength", 0.25f);
        mSsrDistanceUniform = new osg::Uniform("ssrDistance", 4096.f);
        mEffectsState->addUniform(mInverseSceneSizeEffects);
        mEffectsState->addUniform(mInverseProjectionEffects);
        mEffectsState->addUniform(mProjectionEffects);
        mEffectsState->addUniform(mSsrEnabledUniform);
        mEffectsState->addUniform(mSsrStrengthUniform);
        mEffectsState->addUniform(mSsrDistanceUniform);
        mEffectsCamera->addChild(effectsPass);

        osg::ref_ptr<osg::Geode> bloomHorizontalPass = createFullscreenPass(bloomHorizontalProgram);
        mBloomHorizontalState = bloomHorizontalPass->getOrCreateStateSet();
        mBloomHorizontalState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mBloomHorizontalState->addUniform(new osg::Uniform("sceneTexture", 0));
        mInverseBloomSizeHorizontal = new osg::Uniform("inverseBloomSize", osg::Vec2f(1.f, 1.f));
        mBloomHorizontalState->addUniform(new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f)));
        mBloomHorizontalState->addUniform(mInverseBloomSizeHorizontal);
        mBloomThresholdUniform = new osg::Uniform("bloomThreshold", 0.40f);
        mBloomSoftKneeUniform = new osg::Uniform("bloomSoftKnee", 0.67f);
        mBloomRadiusHorizontalUniform = new osg::Uniform("bloomRadius", 3.f);
        mBloomHorizontalState->addUniform(mBloomThresholdUniform);
        mBloomHorizontalState->addUniform(mBloomSoftKneeUniform);
        mBloomHorizontalState->addUniform(mBloomRadiusHorizontalUniform);
        mBloomHorizontalCamera->addChild(bloomHorizontalPass);

        osg::ref_ptr<osg::Geode> bloomVerticalPass = createFullscreenPass(bloomVerticalProgram);
        osg::StateSet* bloomVerticalState = bloomVerticalPass->getOrCreateStateSet();
        bloomVerticalState->setTextureAttributeAndModes(0, mBloomHorizontalTexture, osg::StateAttribute::ON);
        bloomVerticalState->addUniform(new osg::Uniform("bloomTexture", 0));
        mInverseBloomSizeVertical = new osg::Uniform("inverseBloomSize", osg::Vec2f(1.f, 1.f));
        mBloomRadiusVerticalUniform = new osg::Uniform("bloomRadius", 3.f);
        bloomVerticalState->addUniform(mInverseBloomSizeVertical);
        bloomVerticalState->addUniform(mBloomRadiusVerticalUniform);
        mBloomVerticalCamera->addChild(bloomVerticalPass);

        osg::ref_ptr<osg::Geode> edgePass = createFullscreenPass(edgeProgram);
        mEdgeState = edgePass->getOrCreateStateSet();
        mEdgeState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mEdgeState->setTextureAttributeAndModes(1, mDepthTexture, osg::StateAttribute::ON);
        mEdgeState->addUniform(new osg::Uniform("sceneTexture", 0));
        mEdgeState->addUniform(new osg::Uniform("depthTexture", 1));
        mInverseSceneSizeEdge = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        mSmaaThresholdUniform = new osg::Uniform("smaaThreshold", 0.10f);
        mEdgeState->addUniform(mInverseSceneSizeEdge);
        mEdgeState->addUniform(mSmaaThresholdUniform);
        mEdgeCamera->addChild(edgePass);

        osg::ref_ptr<osg::Geode> weightPass = createFullscreenPass(weightProgram);
        osg::StateSet* weightState = weightPass->getOrCreateStateSet();
        weightState->setTextureAttributeAndModes(0, mEdgeTexture, osg::StateAttribute::ON);
        weightState->addUniform(new osg::Uniform("edgeTexture", 0));
        mInverseSceneSizeWeight = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        weightState->addUniform(mInverseSceneSizeWeight);
        mWeightCamera->addChild(weightPass);

        osg::ref_ptr<osg::Geode> finalPass = createFullscreenPass(finalProgram);
        mFinalState = finalPass->getOrCreateStateSet();
        mFinalState->setTextureAttributeAndModes(0, mSceneTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(1, mWeightTexture, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(2, mBloomVerticalTexture, osg::StateAttribute::ON);
        mFinalState->addUniform(new osg::Uniform("sceneTexture", 0));
        mFinalState->addUniform(new osg::Uniform("weightTexture", 1));
        mFinalState->addUniform(new osg::Uniform("bloomTexture", 2));
        mInverseSceneSizeFinal = new osg::Uniform("inverseSceneSize", osg::Vec2f(1.f, 1.f));
        mSmaaEnabledUniform = new osg::Uniform("smaaEnabled", 0.f);
        mBloomEnabledUniform = new osg::Uniform("bloomEnabled", 0.f);
        mBloomIntensityUniform = new osg::Uniform("bloomIntensity", 0.50f);
        mFinalState->addUniform(mInverseSceneSizeFinal);
        mFinalState->addUniform(mSmaaEnabledUniform);
        mFinalState->addUniform(mBloomEnabledUniform);
        mFinalState->addUniform(mBloomIntensityUniform);
        mFinalCamera->addChild(finalPass);

        mRootNode->addChild(mEffectsCamera);
        mRootNode->addChild(mBloomHorizontalCamera);
        mRootNode->addChild(mBloomVerticalCamera);
        mRootNode->addChild(mEdgeCamera);
        mRootNode->addChild(mWeightCamera);
        mRootNode->addChild(mFinalCamera);

        mFramebufferCopyCallback = new FramebufferCopyCallback(this);
        if (mMainCamera.valid())
            mMainCamera->setPostDrawCallback(mFramebufferCopyCallback.get());

        mReady = true;
        reloadSettings();
        applyPassVisibility();
    }

    NativeEffectsProcessor::~NativeEffectsProcessor()
    {
        mEnabled = false;
        applyPassVisibility();
        if (mFramebufferCopyCallback)
            mFramebufferCopyCallback->detach();
        if (mMainCamera.valid() && mMainCamera->getPostDrawCallback() == mFramebufferCopyCallback.get())
            mMainCamera->setPostDrawCallback(mOriginalPostDrawCallback.get());

        if (mRootNode.valid())
        {
            if (mEffectsCamera) mRootNode->removeChild(mEffectsCamera);
            if (mBloomHorizontalCamera) mRootNode->removeChild(mBloomHorizontalCamera);
            if (mBloomVerticalCamera) mRootNode->removeChild(mBloomVerticalCamera);
            if (mEdgeCamera) mRootNode->removeChild(mEdgeCamera);
            if (mWeightCamera) mRootNode->removeChild(mWeightCamera);
            if (mFinalCamera) mRootNode->removeChild(mFinalCamera);
        }
    }

    osg::ref_ptr<osg::Texture2D> NativeEffectsProcessor::createTexture(
        int internalFormat, unsigned int sourceFormat, unsigned int sourceType, bool linear)
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setInternalFormat(internalFormat);
        texture->setSourceFormat(sourceFormat);
        texture->setSourceType(sourceType);
        texture->setFilter(osg::Texture::MIN_FILTER, linear ? osg::Texture::LINEAR : osg::Texture::NEAREST);
        texture->setFilter(osg::Texture::MAG_FILTER, linear ? osg::Texture::LINEAR : osg::Texture::NEAREST);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setResizeNonPowerOfTwoHint(false);
        return texture;
    }

    osg::ref_ptr<osg::Camera> NativeEffectsProcessor::createCamera(int orderNum, bool renderToTexture)
    {
        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        camera->setProjectionResizePolicy(osg::Camera::FIXED);
        camera->setProjectionMatrix(osg::Matrix::identity());
        camera->setViewMatrix(osg::Matrix::identity());
        camera->setRenderOrder(osg::Camera::POST_RENDER, orderNum);
        camera->setRenderTargetImplementation(renderToTexture ? osg::Camera::FRAME_BUFFER_OBJECT : osg::Camera::FRAME_BUFFER);
        camera->setClearMask(renderToTexture ? GL_COLOR_BUFFER_BIT : 0);
        camera->setClearColor(osg::Vec4f(0.f, 0.f, 0.f, 0.f));
        camera->setImplicitBufferAttachmentMask(0, 0);
        camera->setAllowEventFocus(false);
        camera->setCullingActive(false);
        return camera;
    }

    osg::ref_ptr<osg::Geode> NativeEffectsProcessor::createFullscreenPass(osg::Program* program)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);
        geometry->setCullingActive(false);
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(-1.f, -1.f, 0.f));
        vertices->push_back(osg::Vec3f(-1.f, 3.f, 0.f));
        vertices->push_back(osg::Vec3f(3.f, -1.f, 0.f));
        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::TRIANGLES, 0, 3));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->setCullingActive(false);
        geode->addDrawable(geometry);
        osg::StateSet* state = geode->getOrCreateStateSet();
        state->setAttributeAndModes(program, osg::StateAttribute::ON);
        state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        state->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        state->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        state->setMode(GL_BLEND, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        return geode;
    }

    void NativeEffectsProcessor::copyFramebuffer(osg::RenderInfo& renderInfo)
    {
        if (!mEnabled || !mSceneTexture || !mDepthTexture || !renderInfo.getState() || !mMainCamera.valid())
            return;
        osg::Viewport* viewport = mMainCamera->getViewport();
        if (!viewport || viewport->width() <= 0.0 || viewport->height() <= 0.0)
            return;

        const int x = static_cast<int>(viewport->x());
        const int y = static_cast<int>(viewport->y());
        const int w = std::max(1, static_cast<int>(viewport->width()));
        const int h = std::max(1, static_cast<int>(viewport->height()));
        mSceneTexture->copyTexImage2D(*renderInfo.getState(), x, y, w, h);
        mDepthTexture->copyTexImage2D(*renderInfo.getState(), x, y, w, h);
        mCaptureReady.store(true, std::memory_order_release);
    }

    void NativeEffectsProcessor::reloadSettings()
    {
        if (!mReady)
            return;

        mSmaaEnabled = Settings::Manager::getBool("smaa enabled", "Shaders");
        mSsrEnabled = Settings::Manager::getBool("native ssr enabled", "Shaders");
        mBloomEnabled = Settings::Manager::getBool("bloom enabled", "Shaders");

        mSsrEnabledUniform->set(mSsrEnabled ? 1.f : 0.f);
        mSsrStrengthUniform->set(std::clamp(Settings::Manager::getFloat("ssr strength", "Shaders"), 0.f, 1.f));
        mSsrDistanceUniform->set(static_cast<float>(std::clamp(Settings::Manager::getInt("ssr distance", "Shaders"), 512, 8192)));
        mSmaaEnabledUniform->set(mSmaaEnabled ? 1.f : 0.f);
        mSmaaThresholdUniform->set(std::clamp(Settings::Manager::getFloat("smaa threshold", "Shaders"), 0.03f, 0.30f));

        mBloomEnabledUniform->set(mBloomEnabled ? 1.f : 0.f);
        mBloomThresholdUniform->set(std::clamp(Settings::Manager::getFloat("bloom threshold", "Shaders"), 0.f, 2.f));
        mBloomSoftKneeUniform->set(std::clamp(Settings::Manager::getFloat("bloom soft knee", "Shaders"), 0.f, 1.f));
        const float bloomRadius = std::clamp(Settings::Manager::getFloat("bloom radius", "Shaders"), 0.5f, 8.f);
        mBloomRadiusHorizontalUniform->set(bloomRadius);
        mBloomRadiusVerticalUniform->set(bloomRadius);
        mBloomIntensityUniform->set(std::clamp(Settings::Manager::getFloat("bloom intensity", "Shaders"), 0.f, 3.f));

        const bool wasEnabled = mEnabled;
        mEnabled = mSmaaEnabled || mSsrEnabled;
        if (!mEnabled || !wasEnabled)
            mCaptureReady.store(false, std::memory_order_release);
        if (!mEnabled)
            mWidth = mHeight = 0;

        updateSourceBindings();
        applyPassVisibility();
    }

    void NativeEffectsProcessor::updateSourceBindings()
    {
        if (!mReady)
            return;

        osg::Texture2D* working = mSsrEnabled ? mEffectsTexture.get() : mSceneTexture.get();
        mBloomHorizontalState->setTextureAttributeAndModes(0, working, osg::StateAttribute::ON);
        mEdgeState->setTextureAttributeAndModes(0, working, osg::StateAttribute::ON);
        mFinalState->setTextureAttributeAndModes(0, working, osg::StateAttribute::ON);
    }

    void NativeEffectsProcessor::applyPassVisibility()
    {
        const bool visible = mReady && mEnabled && mCaptureReady.load(std::memory_order_acquire);
        const unsigned int on = Mask_RenderToTexture;

        if (mEffectsCamera) mEffectsCamera->setNodeMask(visible && mSsrEnabled ? on : 0u);
        if (mBloomHorizontalCamera) mBloomHorizontalCamera->setNodeMask(visible && mBloomEnabled ? on : 0u);
        if (mBloomVerticalCamera) mBloomVerticalCamera->setNodeMask(visible && mBloomEnabled ? on : 0u);
        if (mEdgeCamera) mEdgeCamera->setNodeMask(visible && mSmaaEnabled ? on : 0u);
        if (mWeightCamera) mWeightCamera->setNodeMask(visible && mSmaaEnabled ? on : 0u);
        if (mFinalCamera) mFinalCamera->setNodeMask(visible ? on : 0u);
    }

    void NativeEffectsProcessor::resizeTargets(int width, int height)
    {
        mWidth = width;
        mHeight = height;
        const int bloomWidth = std::max(1, width / 2);
        const int bloomHeight = std::max(1, height / 2);

        mSceneTexture->setTextureSize(width, height);
        mDepthTexture->setTextureSize(width, height);
        mEffectsTexture->setTextureSize(width, height);
        mEdgeTexture->setTextureSize(width, height);
        mWeightTexture->setTextureSize(width, height);
        mBloomHorizontalTexture->setTextureSize(bloomWidth, bloomHeight);
        mBloomVerticalTexture->setTextureSize(bloomWidth, bloomHeight);

        mEffectsCamera->setViewport(0, 0, width, height);
        mBloomHorizontalCamera->setViewport(0, 0, bloomWidth, bloomHeight);
        mBloomVerticalCamera->setViewport(0, 0, bloomWidth, bloomHeight);
        mEdgeCamera->setViewport(0, 0, width, height);
        mWeightCamera->setViewport(0, 0, width, height);
        mFinalCamera->setViewport(0, 0, width, height);

        const osg::Vec2f invScene(1.f/static_cast<float>(width), 1.f/static_cast<float>(height));
        const osg::Vec2f invBloom(1.f/static_cast<float>(bloomWidth), 1.f/static_cast<float>(bloomHeight));
        mInverseSceneSizeEffects->set(invScene);
        mInverseSceneSizeEdge->set(invScene);
        mInverseSceneSizeWeight->set(invScene);
        mInverseSceneSizeFinal->set(invScene);
        mInverseBloomSizeHorizontal->set(invBloom);
        mInverseBloomSizeVertical->set(invBloom);
        if (osg::Uniform* u = mBloomHorizontalState->getUniform("inverseSceneSize"))
            u->set(invScene);

        mCaptureReady.store(false, std::memory_order_release);
        applyPassVisibility();
    }

    void NativeEffectsProcessor::updateMatrices()
    {
        if (!mMainCamera.valid())
            return;
        osg::Matrixf projection(mMainCamera->getProjectionMatrix());
        osg::Matrixf inverseProjection;
        inverseProjection.invert(projection);
        mInverseProjectionEffects->set(inverseProjection);
        mProjectionEffects->set(projection);
    }

    void NativeEffectsProcessor::update()
    {
        if (!mEnabled || !mMainCamera.valid() || !mMainCamera->getViewport())
        {
            applyPassVisibility();
            return;
        }

        const int width = std::max(1, static_cast<int>(mMainCamera->getViewport()->width()));
        const int height = std::max(1, static_cast<int>(mMainCamera->getViewport()->height()));
        if (width != mWidth || height != mHeight)
            resizeTargets(width, height);

        updateMatrices();
        updateSourceBindings();
        applyPassVisibility();
    }
}
