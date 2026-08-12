#include "dialogue.hpp"

#include <MyGUI_LanguageManager.h>
#include <MyGUI_Window.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_InputManager.h>

#include <osg/Math>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <typeinfo>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/esm/loadarmo.hpp>
#include <components/esm/loadligh.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>
#include <components/widgets/list.hpp>
#include <components/translation/translation.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/dialoguemanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/character.hpp"

#include "../mwrender/animation.hpp"

#include "bookpage.hpp"
#include "textcolours.hpp"

#include "journalbooks.hpp" // to_utf8_span

namespace
{
    struct DialogueAnimation
    {
        const char* mGroup;
        int mBlendMask;
        float mSpeed;
        std::size_t mLoops;
    };

    float normalizeAngle(float angle)
    {
        while (angle > osg::PI)
            angle -= static_cast<float>(osg::PI) * 2.f;
        while (angle < -osg::PI)
            angle += static_cast<float>(osg::PI) * 2.f;
        return angle;
    }

    float randomRange(float minimum, float maximum)
    {
        return minimum + (maximum - minimum) * Misc::Rng::rollProbability();
    }

    std::string gameSettingString(const std::string& id, const std::string& fallback)
    {
        return MWBase::Environment::get().getWindowManager()->getGameSettingString(id, fallback);
    }

    bool dynamicActorLeftArmOccupied(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || !ptr.getClass().hasInventoryStore(ptr))
            return false;

        MWWorld::InventoryStore& inventory = ptr.getClass().getInventoryStore(ptr);
        const MWWorld::ContainerStoreIterator carried
            = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (carried == inventory.end())
            return false;

        const std::string& type = carried->getTypeName();
        return type == typeid(ESM::Armor).name() || type == typeid(ESM::Light).name();
    }
}

namespace MWGui
{

    class ResponseCallback : public MWBase::DialogueManager::ResponseCallback
    {
    public:
        ResponseCallback(DialogueWindow* win, bool needMargin=true)
            : mWindow(win)
            , mNeedMargin(needMargin)
        {

        }

        void addResponse(const std::string& title, const std::string& text) override
        {
            mWindow->addResponse(title, text, mNeedMargin);
        }

        void updateTopics()
        {
            mWindow->updateTopics();
        }

    private:
        DialogueWindow* mWindow;
        bool mNeedMargin;
    };

    // --------------------------------------------------------------------------------------------------

    Response::Response(const std::string &text, const std::string &title, bool needMargin)
        : mTitle(title), mNeedMargin(needMargin)
    {
        mText = text;
    }

    void Response::write(BookTypesetter::Ptr typesetter, KeywordSearchT* keywordSearch, std::map<std::string, Link*>& topicLinks) const
    {
        typesetter->sectionBreak(mNeedMargin ? 9 : 0);

        if (mTitle != "")
        {
            const MyGUI::Colour& headerColour = MWBase::Environment::get().getWindowManager()->getTextColours().header;
            BookTypesetter::Style* title = typesetter->createStyle("", headerColour, false);
            typesetter->write(title, to_utf8_span(mTitle.c_str()));
            typesetter->sectionBreak();
        }

        typedef std::pair<size_t, size_t> Range;
        std::map<Range, intptr_t> hyperLinks;

        // We need this copy for when @# hyperlinks are replaced
        std::string text = mText;

        size_t pos_end = std::string::npos;
        for(;;)
        {
            size_t pos_begin = text.find('@');
            if (pos_begin != std::string::npos)
                pos_end = text.find('#', pos_begin);

            if (pos_begin != std::string::npos && pos_end != std::string::npos)
            {
                std::string link = text.substr(pos_begin + 1, pos_end - pos_begin - 1);
                const char specialPseudoAsteriskCharacter = 127;
                std::replace(link.begin(), link.end(), specialPseudoAsteriskCharacter, '*');
                std::string topicName = MWBase::Environment::get().getWindowManager()->
                        getTranslationDataStorage().topicStandardForm(link);

                std::string displayName = link;
                while (displayName[displayName.size()-1] == '*')
                    displayName.erase(displayName.size()-1, 1);

                text.replace(pos_begin, pos_end+1-pos_begin, displayName);

                if (topicLinks.find(Misc::StringUtils::lowerCase(topicName)) != topicLinks.end())
                    hyperLinks[std::make_pair(pos_begin, pos_begin+displayName.size())] = intptr_t(topicLinks[Misc::StringUtils::lowerCase(topicName)]);
            }
            else
                break;
        }

        typesetter->addContent(to_utf8_span(text.c_str()));

        if (hyperLinks.size() && MWBase::Environment::get().getWindowManager()->getTranslationDataStorage().hasTranslation())
        {
            const TextColours& textColours = MWBase::Environment::get().getWindowManager()->getTextColours();

            BookTypesetter::Style* style = typesetter->createStyle("", textColours.normal, false);
            size_t formatted = 0; // points to the first character that is not laid out yet
            for (auto& hyperLink : hyperLinks)
            {
                intptr_t topicId = hyperLink.second;
                BookTypesetter::Style* hotStyle = typesetter->createHotStyle (style, textColours.link,
                                                                              textColours.linkOver, textColours.linkPressed,
                                                                              topicId);
                if (formatted < hyperLink.first.first)
                    typesetter->write(style, formatted, hyperLink.first.first);
                typesetter->write(hotStyle, hyperLink.first.first, hyperLink.first.second);
                formatted = hyperLink.first.second;
            }
            if (formatted < text.size())
                typesetter->write(style, formatted, text.size());
        }
        else
        {
            std::vector<KeywordSearchT::Match> matches;
            keywordSearch->highlightKeywords(text.begin(), text.end(), matches);

            std::string::const_iterator i = text.begin ();
            for (KeywordSearchT::Match& match : matches)
            {
                if (i != match.mBeg)
                    addTopicLink (typesetter, 0, i - text.begin (), match.mBeg - text.begin ());

                addTopicLink (typesetter, match.mValue, match.mBeg - text.begin (), match.mEnd - text.begin ());

                i = match.mEnd;
            }
            if (i != text.end ())
                addTopicLink (typesetter, 0, i - text.begin (), text.size ());
        }
    }

    void Response::addTopicLink(BookTypesetter::Ptr typesetter, intptr_t topicId, size_t begin, size_t end) const
    {
        const TextColours& textColours = MWBase::Environment::get().getWindowManager()->getTextColours();

        BookTypesetter::Style* style = typesetter->createStyle("", textColours.normal, false);


        if (topicId)
            style = typesetter->createHotStyle (style, textColours.link, textColours.linkOver, textColours.linkPressed, topicId);
        typesetter->write (style, begin, end);
    }

    Message::Message(const std::string& text)
    {
        mText = text;
    }

    void Message::write(BookTypesetter::Ptr typesetter, KeywordSearchT* keywordSearch, std::map<std::string, Link*>& topicLinks) const
    {
        const MyGUI::Colour& textColour = MWBase::Environment::get().getWindowManager()->getTextColours().notify;
        BookTypesetter::Style* title = typesetter->createStyle("", textColour, false);
        typesetter->sectionBreak(9);
        typesetter->write(title, to_utf8_span(mText.c_str()));
    }

    // --------------------------------------------------------------------------------------------------

    void Choice::activated()
    {
        MWBase::Environment::get().getWindowManager()->playSound("Menu Click");
        eventChoiceActivated(mChoiceId);
    }

    void Topic::activated()
    {
        MWBase::Environment::get().getWindowManager()->playSound("Menu Click");
        eventTopicActivated(mTopicId);
    }

    void Goodbye::activated()
    {
        MWBase::Environment::get().getWindowManager()->playSound("Menu Click");
        eventActivated();
    }

    // --------------------------------------------------------------------------------------------------

    DialogueWindow::DialogueWindow()
        : WindowBase("openmw_dialogue_window.layout")
        , mIsCompanion(false)
        , mGoodbye(false)
        , mPersuasionMode(false)
        , mHistoryWasDragged(false)
        , mDialogueCameraActive(false)
        , mDynamicDialogueActorActive(false)
        , mDynamicDialogueActorHasOriginalYaw(false)
        , mDynamicDialogueActorOriginalYaw(0.f)
        , mDynamicDialogueActorAnimationTimer(0.f)
        , mDynamicDialogueActorTransitionTimer(0.f)
        , mDynamicDialogueActorSpeechCooldown(0.f)
        , mDynamicDialogueActorAnimationEnding(false)
        , mDynamicDialogueActorPendingSpeaking(false)
        , mDynamicDialogueActorWasSpeaking(false)
        , mDynamicDialogueActorLeftArmProtected(false)
        , mCallback(new ResponseCallback(this))
        , mGreetingCallback(new ResponseCallback(this, false))
    {
        // Centre dialog
        center();

        // History view
        getWidget(mHistory, "History");
        mHistory->setNeedMouseFocus(true);
        mHistory->eventMouseWheel += MyGUI::newDelegate(this, &DialogueWindow::onMouseWheel);
        mHistory->eventMouseButtonPressed += MyGUI::newDelegate(this, &DialogueWindow::onHistoryDragStart);
        mHistory->eventMouseDrag += MyGUI::newDelegate(this, &DialogueWindow::onHistoryDrag);

        // Answers and topics/actions lists
        getWidget(mChoicesList, "ChoicesList");
        mChoicesList->eventItemSelected += MyGUI::newDelegate(this, &DialogueWindow::onChoiceListItem);
        getWidget(mTopicsList, "TopicsList");
        mTopicsList->eventItemSelected += MyGUI::newDelegate(this, &DialogueWindow::onSelectListItem);

        getWidget(mNpcName, "NpcName");
        getWidget(mNpcHealthBar, "NpcHealth");
        getWidget(mNpcHealthText, "NpcHealthText");
        getWidget(mChoicesLabel, "ChoicesLabel");
        getWidget(mTopicsLabel, "TopicsLabel");
        // The disposition bar occupies this row in the ArenaMP dialogue layout.
        // Keep the label widget as an anchor, but do not draw the legacy
        // "Topics" caption over the disposition value.
        mTopicsLabel->setCaption(MyGUI::UString());

        getWidget(mGoodbyeButton, "ByeButton");
        mGoodbyeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &DialogueWindow::onByeClicked);
        getWidget(mUpButton, "UpButton");
        getWidget(mDownButton, "DownButton");
        getWidget(mSelectButton, "SelectButton");
        mUpButton->eventMouseButtonClick += MyGUI::newDelegate(this, &DialogueWindow::onNavigateUp);
        mDownButton->eventMouseButtonClick += MyGUI::newDelegate(this, &DialogueWindow::onNavigateDown);
        mSelectButton->eventMouseButtonClick += MyGUI::newDelegate(this, &DialogueWindow::onNavigateSelect);

        getWidget(mDispositionBar, "Disposition");
        getWidget(mDispositionText,"DispositionText");
        getWidget(mScrollBar, "VScroll");

        mScrollBar->eventScrollChangePosition += MyGUI::newDelegate(this, &DialogueWindow::onScrollbarMoved);

        BookPage::ClickCallback callback = std::bind (&DialogueWindow::notifyLinkClicked, this, std::placeholders::_1);
        mHistory->adviseLinkClicked(callback);

        mMainWidget->castType<MyGUI::Window>()->eventWindowChangeCoord += MyGUI::newDelegate(this, &DialogueWindow::onWindowResize);
        updateChoicePane();
    }

    DialogueWindow::~DialogueWindow()
    {
        deleteLater();
        for (Link* link : mLinks)
            delete link;
        for (const auto& link : mTopicLinks)
            delete link.second;
        for (auto history : mHistoryContents)
            delete history;
    }

    void DialogueWindow::onTradeComplete()
    {
        addResponse("", MyGUI::LanguageManager::getInstance().replaceTags("#{sBarterDialog5}"));
    }

    bool DialogueWindow::exit()
    {
        if (mPersuasionMode)
        {
            closePersuasionPane();
            return false;
        }

        if ((MWBase::Environment::get().getDialogueManager()->isInChoice()))
        {
            return false;
        }
        else
        {
            stopDynamicDialogueActor();
            stopDialogueCamera();
            resetReference();
            MWBase::Environment::get().getDialogueManager()->goodbyeSelected();
            mTopicsList->scrollToTop();
            return true;
        }
    }

    void DialogueWindow::onOpen()
    {
        positionDialogueWindow();
        startDialogueCamera();
        startDynamicDialogueActor();
        selectInitialItem();
    }

    void DialogueWindow::onResChange(int width, int height)
    {
        positionDialogueWindow();
        mChoicesList->adjustSize();
        mTopicsList->adjustSize();
        updateChoicePane();
        updateHistory();
    }

    bool DialogueWindow::handleKeyPress(MyGUI::KeyCode key, bool repeat)
    {
        switch (key.getValue())
        {
            case MyGUI::KeyCode::W:
            case MyGUI::KeyCode::ArrowUp:
                return moveSelection(-1);
            case MyGUI::KeyCode::S:
            case MyGUI::KeyCode::ArrowDown:
                return moveSelection(1);
            case MyGUI::KeyCode::E:
            case MyGUI::KeyCode::Return:
            case MyGUI::KeyCode::NumpadEnter:
                if (repeat)
                    return true;
                return activateSelection();
            default:
                return false;
        }
    }

    void DialogueWindow::positionDialogueWindow()
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        MyGUI::IntSize size = mMainWidget->getSize();
        size.width = std::min(680, std::max(620, view.width - 16));
        size.height = std::min(400, std::max(330, static_cast<int>(view.height * 0.43f)));
        mMainWidget->setSize(size);

        const int x = std::max(8, (view.width - size.width) / 2);
        // Keep the panel close to the lower edge so the actor's upper body remains unobstructed.
        const int y = std::max(4, view.height - size.height - 6);
        mMainWidget->setPosition(x, y);
    }

    void DialogueWindow::startDialogueCamera()
    {
        if (mPtr.isEmpty() || !Settings::Manager::getBool("cinematic dialogue camera", "GUI"))
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world->getGlobalInt("chargenstate") != -1)
            return;

        world->setDialogueCameraTarget(mPtr);
        mDialogueCameraActive = true;
    }

    void DialogueWindow::stopDialogueCamera()
    {
        if (!mDialogueCameraActive)
            return;
        MWBase::Environment::get().getWorld()->clearDialogueCameraTarget();
        mDialogueCameraActive = false;
    }

    void DialogueWindow::startDynamicDialogueActor()
    {
        if (mDynamicDialogueActorActive || mPtr.isEmpty() || !mPtr.getClass().isNpc()
            || !Settings::Manager::getBool("dynamic dialogue actors", "GUI"))
            return;

        MWMechanics::CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
        if (stats.isDead() || stats.getAiSequence().isInCombat())
            return;

        // A model assigned directly to the NPC record is the Construction Set
        // mechanism used by Animated Morrowind-style actors (fishers, workers,
        // etc.). Their authored controller owns both pose and facing, so do not
        // inject dialogue gestures or rotate them toward the player.
        const MWWorld::LiveCellRef<ESM::NPC>* npc = mPtr.get<ESM::NPC>();
        if (npc && !npc->mBase->mModel.empty())
            return;

        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr);
        if (!animation)
            return;

        mDynamicDialogueActorActive = true;
        mDynamicDialogueActorHasOriginalYaw = true;
        mDynamicDialogueActorOriginalYaw = mPtr.getRefData().getPosition().rot[2];
        mDynamicDialogueActorAnimationTimer = 0.2f;
        mDynamicDialogueActorTransitionTimer = 0.f;
        mDynamicDialogueActorSpeechCooldown = 0.f;
        mDynamicDialogueActorAnimationEnding = false;
        mDynamicDialogueActorPendingSpeaking = false;
        mDynamicDialogueActorWasSpeaking = false;
        mDynamicDialogueActorLeftArmProtected = false;
        mDynamicDialogueActorAnimation.clear();
    }

    void DialogueWindow::playDynamicDialogueAnimation(bool speaking, bool force)
    {
        if (!mDynamicDialogueActorActive || mPtr.isEmpty()
            || !Settings::Manager::getBool("dynamic dialogue actor animations", "GUI"))
            return;

        MWMechanics::CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
        if (stats.isDead() || stats.getAiSequence().isInCombat()
            || stats.getDrawState() != MWMechanics::DrawState_Nothing)
        {
            mDynamicDialogueActorAnimationTimer = 1.f;
            return;
        }

        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr);
        if (!animation)
        {
            mDynamicDialogueActorAnimationTimer = 1.f;
            return;
        }

        if (speaking && !force)
        {
            if (mDynamicDialogueActorSpeechCooldown > 0.f)
                return;

            // A voiced line should not force a new gesture every time. Most lines keep the
            // current pose, while occasional lines receive a speaking gesture.
            if (Misc::Rng::rollProbability() > 0.38f)
            {
                mDynamicDialogueActorSpeechCooldown = randomRange(5.f, 9.f);
                return;
            }
        }

        static const DialogueAnimation sSpeechAnimations[] = {
            { "idlespeak_idlef", MWRender::Animation::BlendMask_UpperBody, 0.88f, 0 },
            { "idlespeak_handhip", MWRender::Animation::BlendMask_UpperBody, 0.88f, 0 },
            { "idlespeak_ready", MWRender::Animation::BlendMask_UpperBody, 0.88f, 0 },
            { "idlespeak", MWRender::Animation::BlendMask_UpperBody, 0.88f, 0 },
        };
        static const DialogueAnimation sIdleAnimations[] = {
            { "armsakimbo", MWRender::Animation::BlendMask_UpperBody, 0.66f, 10 },
            { "armsfolded", MWRender::Animation::BlendMask_UpperBody, 0.66f, 10 },
            { "armsatback", MWRender::Animation::BlendMask_UpperBody, 0.66f, 10 },
            { "armsalmapray", MWRender::Animation::BlendMask_UpperBody, 0.72f, 6 },
            { "handhippose", MWRender::Animation::BlendMask_UpperBody, 0.60f, 10 },
            { "readypose", MWRender::Animation::BlendMask_UpperBody, 0.66f, 8 },
            { "posealma3", MWRender::Animation::BlendMask_UpperBody, 0.80f, 4 },
            { "idle2_copy", MWRender::Animation::BlendMask_UpperBody, 0.88f, 1 },
            { "idle3_copy", MWRender::Animation::BlendMask_UpperBody, 0.78f, 1 },
            { "idle6_copy", MWRender::Animation::BlendMask_UpperBody, 0.72f, 1 },
            { "idle7_copy", MWRender::Animation::BlendMask_UpperBody, 0.92f, 1 },
            { "idle8_copy", MWRender::Animation::BlendMask_UpperBody, 0.92f, 1 },
            { "armsgesture", MWRender::Animation::BlendMask_UpperBody, 0.88f, 1 },
            { "armssunshield", MWRender::Animation::BlendMask_UpperBody, 0.65f, 1 },
        };

        std::vector<const DialogueAnimation*> available;
        const DialogueAnimation* begin = speaking ? std::begin(sSpeechAnimations) : std::begin(sIdleAnimations);
        const DialogueAnimation* end = speaking ? std::end(sSpeechAnimations) : std::end(sIdleAnimations);
        for (const DialogueAnimation* entry = begin; entry != end; ++entry)
        {
            if (animation->hasAnimation(entry->mGroup)
                && (mDynamicDialogueActorAnimation.empty()
                    || mDynamicDialogueActorAnimation != entry->mGroup))
                available.push_back(entry);
        }

        if (available.empty())
        {
            for (const DialogueAnimation* entry = begin; entry != end; ++entry)
            {
                if (animation->hasAnimation(entry->mGroup))
                    available.push_back(entry);
            }
        }

        if (available.empty())
        {
            mDynamicDialogueActorAnimationTimer = speaking ? 3.f : 12.f;
            return;
        }

        const DialogueAnimation& selected
            = *available[Misc::Rng::rollDice(static_cast<int>(available.size()))];

        if (!mDynamicDialogueActorAnimation.empty()
            && animation->isPlaying(mDynamicDialogueActorAnimation))
        {
            if (mDynamicDialogueActorAnimation == selected.mGroup)
            {
                mDynamicDialogueActorAnimationTimer
                    = speaking ? randomRange(7.f, 11.f) : randomRange(22.f, 40.f);
                if (speaking)
                    mDynamicDialogueActorSpeechCooldown = randomRange(10.f, 18.f);
                return;
            }

            if (!force)
            {
                // OpenMW 0.47 has no transform cross-fade. Let the current KF leave its
                // loop through the authored stop segment, then start the next pose.
                animation->setLoopingEnabled(mDynamicDialogueActorAnimation, false);
                mDynamicDialogueActorAnimationEnding = true;
                mDynamicDialogueActorPendingSpeaking = speaking;
                mDynamicDialogueActorTransitionTimer = 2.5f;
                return;
            }

            animation->disable(mDynamicDialogueActorAnimation);
        }

        const bool leftArmProtected = dynamicActorLeftArmOccupied(mPtr);
        int blendMask = selected.mBlendMask;
        if (leftArmProtected)
            blendMask &= ~MWRender::Animation::BlendMask_LeftArm;

        MWRender::Animation::AnimPriority priority(MWMechanics::Priority_Default);
        if (blendMask & MWRender::Animation::BlendMask_Torso)
            priority[MWRender::Animation::BoneGroup_Torso] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_LeftArm)
            priority[MWRender::Animation::BoneGroup_LeftArm] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_RightArm)
            priority[MWRender::Animation::BoneGroup_RightArm] = MWMechanics::Priority_Weapon;
        if (animation->isPlaying(selected.mGroup))
            animation->disable(selected.mGroup);
        animation->play(selected.mGroup, priority, blendMask, true, selected.mSpeed,
            "start", "stop", 0.f, selected.mLoops, true);

        if (!animation->isPlaying(selected.mGroup))
        {
            mDynamicDialogueActorAnimation.clear();
            mDynamicDialogueActorLeftArmProtected = false;
            mDynamicDialogueActorAnimationTimer = speaking ? 5.f : 14.f;
            return;
        }

        mDynamicDialogueActorAnimation = selected.mGroup;
        mDynamicDialogueActorLeftArmProtected = leftArmProtected;
        mDynamicDialogueActorPendingSpeaking = false;
        mDynamicDialogueActorAnimationEnding = false;
        mDynamicDialogueActorTransitionTimer = 0.f;
        mDynamicDialogueActorAnimationTimer
            = speaking ? randomRange(7.f, 11.f) : randomRange(22.f, 40.f);
        if (speaking)
            mDynamicDialogueActorSpeechCooldown = randomRange(10.f, 18.f);
    }

    void DialogueWindow::updateDynamicDialogueActor(float dt)
    {
        if (!mDynamicDialogueActorActive || mPtr.isEmpty() || dt <= 0.f)
            return;

        MWMechanics::CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
        if (stats.isDead() || stats.getAiSequence().isInCombat())
        {
            if (!mDynamicDialogueActorAnimation.empty())
            {
                if (MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr))
                    animation->disable(mDynamicDialogueActorAnimation);
                mDynamicDialogueActorAnimation.clear();
                mDynamicDialogueActorLeftArmProtected = false;
            }
            mDynamicDialogueActorAnimationEnding = false;
            return;
        }

        if (Settings::Manager::getBool("dynamic dialogue actor turning", "GUI"))
        {
            const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            if (!player.isEmpty())
            {
                const osg::Vec3f delta = player.getRefData().getPosition().asVec3()
                    - mPtr.getRefData().getPosition().asVec3();
                if (delta.x() * delta.x() + delta.y() * delta.y() > 1.f)
                {
                    const float targetYaw = std::atan2(delta.x(), delta.y());
                    const ESM::Position& position = mPtr.getRefData().getPosition();
                    const float difference = normalizeAngle(targetYaw - position.rot[2]);
                    const float easedStep = difference * (1.f - std::exp(-4.5f * dt));
                    const float maxStep = osg::DegreesToRadians(105.f) * dt;
                    const float step = std::max(-maxStep, std::min(maxStep, easedStep));
                    if (std::abs(step) > 0.0001f)
                    {
                        MWBase::Environment::get().getWorld()->rotateObject(mPtr,
                            position.rot[0], position.rot[1], normalizeAngle(position.rot[2] + step));
                    }
                }
            }
        }

        if (!Settings::Manager::getBool("dynamic dialogue actor animations", "GUI"))
        {
            if (!mDynamicDialogueActorAnimation.empty())
            {
                if (MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr))
                    animation->disable(mDynamicDialogueActorAnimation);
                mDynamicDialogueActorAnimation.clear();
                mDynamicDialogueActorLeftArmProtected = false;
            }
            mDynamicDialogueActorAnimationEnding = false;
            return;
        }

        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr);
        if (!animation)
            return;

        mDynamicDialogueActorSpeechCooldown
            = std::max(0.f, mDynamicDialogueActorSpeechCooldown - dt);

        const bool speaking = MWBase::Environment::get().getSoundManager()->sayActive(mPtr);
        if (!mDynamicDialogueActorAnimation.empty()
            && dynamicActorLeftArmOccupied(mPtr) != mDynamicDialogueActorLeftArmProtected)
        {
            animation->disable(mDynamicDialogueActorAnimation);
            mDynamicDialogueActorAnimation.clear();
            mDynamicDialogueActorAnimationEnding = false;
            mDynamicDialogueActorPendingSpeaking = false;
            mDynamicDialogueActorTransitionTimer = 0.f;
            mDynamicDialogueActorLeftArmProtected = false;
            playDynamicDialogueAnimation(speaking, true);
            return;
        }

        if (mDynamicDialogueActorAnimationEnding)
        {
            mDynamicDialogueActorTransitionTimer -= dt;
            if (mDynamicDialogueActorAnimation.empty()
                || !animation->isPlaying(mDynamicDialogueActorAnimation)
                || mDynamicDialogueActorTransitionTimer <= 0.f)
            {
                if (!mDynamicDialogueActorAnimation.empty()
                    && animation->isPlaying(mDynamicDialogueActorAnimation))
                    animation->disable(mDynamicDialogueActorAnimation);

                bool pendingSpeaking = mDynamicDialogueActorPendingSpeaking;
                if (pendingSpeaking
                    && !MWBase::Environment::get().getSoundManager()->sayActive(mPtr))
                    pendingSpeaking = false;

                mDynamicDialogueActorAnimation.clear();
                mDynamicDialogueActorLeftArmProtected = false;
                mDynamicDialogueActorAnimationEnding = false;
                mDynamicDialogueActorTransitionTimer = 0.f;
                playDynamicDialogueAnimation(pendingSpeaking, true);
            }
            return;
        }

        if (!mDynamicDialogueActorAnimation.empty()
            && !animation->isPlaying(mDynamicDialogueActorAnimation))
        {
            mDynamicDialogueActorAnimation.clear();
            mDynamicDialogueActorLeftArmProtected = false;
            mDynamicDialogueActorAnimationTimer = randomRange(6.f, 12.f);
        }

        if (speaking)
        {
            if (!mDynamicDialogueActorWasSpeaking)
            {
                mDynamicDialogueActorWasSpeaking = true;
                playDynamicDialogueAnimation(true);
            }
            return;
        }

        if (mDynamicDialogueActorWasSpeaking)
        {
            mDynamicDialogueActorWasSpeaking = false;
            if (mDynamicDialogueActorAnimation.compare(0, 9, "idlespeak") == 0
                && animation->isPlaying(mDynamicDialogueActorAnimation))
            {
                animation->setLoopingEnabled(mDynamicDialogueActorAnimation, false);
                mDynamicDialogueActorAnimationEnding = true;
                mDynamicDialogueActorPendingSpeaking = false;
                mDynamicDialogueActorTransitionTimer = 2.5f;
                return;
            }
        }

        mDynamicDialogueActorAnimationTimer -= dt;
        if (mDynamicDialogueActorAnimationTimer <= 0.f)
            playDynamicDialogueAnimation(false);
    }

    void DialogueWindow::stopDynamicDialogueActor()
    {
        if (!mDynamicDialogueActorActive)
            return;

        if (!mPtr.isEmpty())
        {
            if (!mDynamicDialogueActorAnimation.empty())
            {
                if (MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr))
                    animation->setLoopingEnabled(mDynamicDialogueActorAnimation, false);
            }
            // Keep the final dialogue-facing direction. Restoring the original
            // yaw here made the NPC snap back or turn away as soon as the window
            // closed, which made the dialogue movement look as if it never
            // happened. The normal AI controller can rotate the NPC again after
            // dialogue when its package requires it.
        }

        mDynamicDialogueActorActive = false;
        mDynamicDialogueActorHasOriginalYaw = false;
        mDynamicDialogueActorAnimationTimer = 0.f;
        mDynamicDialogueActorTransitionTimer = 0.f;
        mDynamicDialogueActorSpeechCooldown = 0.f;
        mDynamicDialogueActorAnimationEnding = false;
        mDynamicDialogueActorPendingSpeaking = false;
        mDynamicDialogueActorWasSpeaking = false;
        mDynamicDialogueActorLeftArmProtected = false;
        mDynamicDialogueActorAnimation.clear();
    }

    bool DialogueWindow::moveSelection(int direction)
    {
        if (mChoicesList->getVisible() && mChoicesList->getEnabled() && mChoicesList->getItemCount() > 0)
            return mChoicesList->selectNext(direction, true);
        if (mTopicsList->getVisible() && mTopicsList->getEnabled() && mTopicsList->getItemCount() > 0)
            return mTopicsList->selectNext(direction, true);
        if (mGoodbyeButton->getEnabled())
        {
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);
            return true;
        }
        return false;
    }

    bool DialogueWindow::activateSelection()
    {
        if (mChoicesList->getVisible() && mChoicesList->getEnabled() && mChoicesList->activateSelected())
            return true;
        if (mTopicsList->getVisible() && mTopicsList->getEnabled() && mTopicsList->activateSelected())
            return true;
        if (mGoodbyeButton->getEnabled())
        {
            onByeClicked(mGoodbyeButton);
            return true;
        }
        return false;
    }

    void DialogueWindow::selectInitialItem()
    {
        if (mChoicesList->getVisible() && mChoicesList->getEnabled() && mChoicesList->getItemCount() > 0)
        {
            mTopicsList->clearSelection();
            if (mChoicesList->getSelectedIndex() < 0)
                mChoicesList->selectNext(1, true);
            return;
        }
        mChoicesList->clearSelection();
        if (mTopicsList->getEnabled() && mTopicsList->getItemCount() > 0)
        {
            if (mTopicsList->getSelectedIndex() < 0)
                mTopicsList->selectNext(1, true);
            return;
        }
        if (mGoodbyeButton->getEnabled())
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);
    }

    void DialogueWindow::onNavigateUp(MyGUI::Widget* sender)
    {
        moveSelection(-1);
    }

    void DialogueWindow::onNavigateDown(MyGUI::Widget* sender)
    {
        moveSelection(1);
    }

    void DialogueWindow::onNavigateSelect(MyGUI::Widget* sender)
    {
        activateSelection();
    }

    void DialogueWindow::onChoiceListItem(const std::string& choice, int id)
    {
        if (mPersuasionMode)
        {
            performPersuasion(id);
            return;
        }

        if (id < 0 || static_cast<std::size_t>(id) >= mChoices.size())
            return;
        onChoiceActivated(mChoices[static_cast<std::size_t>(id)].second);
    }

    void DialogueWindow::rebuildPersuasionChoices()
    {
        mPersuasionChoices.clear();
        mPersuasionChoices.push_back(MWBase::MechanicsManager::PT_Admire);
        mPersuasionChoices.push_back(MWBase::MechanicsManager::PT_Intimidate);
        mPersuasionChoices.push_back(MWBase::MechanicsManager::PT_Taunt);
        mPersuasionChoices.push_back(MWBase::MechanicsManager::PT_Bribe10);
        mPersuasionChoices.push_back(MWBase::MechanicsManager::PT_Bribe100);
        mPersuasionChoices.push_back(MWBase::MechanicsManager::PT_Bribe1000);
    }

    void DialogueWindow::openPersuasionPane()
    {
        if (mPtr.isEmpty() || !mPtr.getClass().isNpc() || mGoodbye
            || MWBase::Environment::get().getDialogueManager()->isInChoice())
            return;

        // Set the mode before resizing the window. MyGUI emits the resize callback
        // synchronously, so the first opening must already use the persuasion layout.
        mPersuasionMode = true;
        positionDialogueWindow();
        updateHistory();
        selectInitialItem();
    }

    void DialogueWindow::closePersuasionPane()
    {
        if (!mPersuasionMode)
            return;

        mPersuasionMode = false;
        mPersuasionChoices.clear();
        updateHistory();
        selectInitialItem();
    }

    void DialogueWindow::performPersuasion(int index)
    {
        if (!mPersuasionMode || index < 0
            || static_cast<std::size_t>(index) >= mPersuasionChoices.size())
            return;

        const int type = mPersuasionChoices[static_cast<std::size_t>(index)];
        int goldCost = 0;
        if (type == MWBase::MechanicsManager::PT_Bribe10)
            goldCost = 10;
        else if (type == MWBase::MechanicsManager::PT_Bribe100)
            goldCost = 100;
        else if (type == MWBase::MechanicsManager::PT_Bribe1000)
            goldCost = 1000;

        if (goldCost > 0)
        {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            const int playerGold
                = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);
            if (playerGold < goldCost)
            {
                MWBase::Environment::get().getWindowManager()->messageBox(
                    gameSettingString("sGold", "Gold") + ": "
                    + MyGUI::utility::toString(playerGold) + " / "
                    + MyGUI::utility::toString(goldCost));
                return;
            }
        }

        MWBase::DialogueManager* dialogueManager = MWBase::Environment::get().getDialogueManager();
        dialogueManager->persuade(type, mCallback.get());
        mCallback->updateTopics();

        // A persuasion result script is allowed to start a regular answer choice
        // or end the conversation. Those states take priority over the embedded
        // persuasion list. Otherwise keep persuasion open for another attempt.
        mChoices = dialogueManager->getChoices();
        mGoodbye = dialogueManager->isGoodbye();
        if (!mChoices.empty() || mGoodbye)
            mPersuasionMode = false;

        updateHistory();
        updateDisposition();
        selectInitialItem();
    }

    void DialogueWindow::updateChoicePane()
    {
        const int rightX = mTopicsList->getLeft();
        const int rightWidth = mTopicsList->getWidth();
        const int contentBottom = std::max(132, mSelectButton->getTop() - 8);

        mChoicesList->clear();
        if (mPersuasionMode)
        {
            rebuildPersuasionChoices();

            for (const int type : mPersuasionChoices)
            {
                if (type == MWBase::MechanicsManager::PT_Admire)
                    mChoicesList->addItem(gameSettingString("sAdmire", "Admire"));
                else if (type == MWBase::MechanicsManager::PT_Intimidate)
                    mChoicesList->addItem(gameSettingString("sIntimidate", "Intimidate"));
                else if (type == MWBase::MechanicsManager::PT_Taunt)
                    mChoicesList->addItem(gameSettingString("sTaunt", "Taunt"));
                else if (type == MWBase::MechanicsManager::PT_Bribe10)
                    mChoicesList->addItem(gameSettingString("sBribe 10 Gold", "Bribe 10 Gold"));
                else if (type == MWBase::MechanicsManager::PT_Bribe100)
                    mChoicesList->addItem(gameSettingString("sBribe 100 Gold", "Bribe 100 Gold"));
                else if (type == MWBase::MechanicsManager::PT_Bribe1000)
                    mChoicesList->addItem(gameSettingString("sBribe 1000 Gold", "Bribe 1000 Gold"));
            }

            const MWWorld::Ptr player = MWMechanics::getPlayer();
            const int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);
            mChoicesLabel->setCaption(gameSettingString("sPersuasionMenuTitle", "Persuasion")
                + " - " + gameSettingString("sGold", "Gold") + ": "
                + MyGUI::utility::toString(playerGold));
        }
        else
        {
            for (const auto& choice : mChoices)
                mChoicesList->addItem(choice.first);
            // The disposition value occupies this row. Do not draw the legacy
            // "Responses" caption over the disposition progress bar.
            mChoicesLabel->setCaption(MyGUI::UString());
        }
        mChoicesList->adjustSize();

        const bool hasChoices = mPersuasionMode || !mChoices.empty();
        mChoicesLabel->setVisible(mPersuasionMode);
        mChoicesList->setVisible(hasChoices);
        mChoicesList->setEnabled(hasChoices);

        // Answers temporarily replace services, persuasion and regular topics.
        // Keep the topic list contents intact so it can be restored immediately
        // after the answer has been selected without rebuilding dialogue state.
        mTopicsLabel->setVisible(!hasChoices);
        mTopicsList->setVisible(!hasChoices);

        if (hasChoices)
        {
            // The disposition bar occupies the top of the right pane. Keep the
            // embedded persuasion title and options below it; ordinary scripted
            // answers retain their compact original placement.
            const int choicesLabelTop = mPersuasionMode ? 68 : 44;
            const int choicesTop = mPersuasionMode ? 92 : 68;
            const int choicesHeight = std::max(80, contentBottom - choicesTop);
            mChoicesLabel->setCoord(rightX, choicesLabelTop, rightWidth, 18);
            mChoicesList->setCoord(rightX, choicesTop, rightWidth, choicesHeight);

            // Rebuild after the final coordinates are known. Without this, the first
            // opening can retain wrapped captions from the ordinary topic pane.
            mChoicesList->adjustSize();
        }
        else
        {
            mTopicsLabel->setCoord(rightX, 44, rightWidth, 18);
            mTopicsList->setCoord(rightX, 72, rightWidth, std::max(80, contentBottom - 72));
            mTopicsList->adjustSize();
        }
    }

    void DialogueWindow::onHistoryDragStart(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left)
            return;
        mHistoryDragStart = MyGUI::IntPoint(left, top);
        mHistoryLastDragPosition = mHistoryDragStart;
        mHistoryWasDragged = false;
    }

    void DialogueWindow::onHistoryDrag(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id)
    {
        if (id != MyGUI::MouseButton::Left || !mScrollBar->getVisible())
            return;

        const MyGUI::IntPoint current(left, top);
        const MyGUI::IntPoint total = current - mHistoryDragStart;
        if (std::abs(total.left) > 4 || std::abs(total.top) > 4)
            mHistoryWasDragged = true;

        if (mHistoryWasDragged)
        {
            const int delta = current.top - mHistoryLastDragPosition.top;
            const int maxPosition = std::max(0, static_cast<int>(mScrollBar->getScrollRange()) - 1);
            const int position = std::max(0, std::min(maxPosition, static_cast<int>(mScrollBar->getScrollPosition()) - delta));
            mScrollBar->setScrollPosition(position);
            onScrollbarMoved(mScrollBar, position);
        }
        mHistoryLastDragPosition = current;
    }

    void DialogueWindow::onWindowResize(MyGUI::Window* _sender)
    {
        // if the window has only been moved, not resized, we don't need to update
        if (mCurrentWindowSize == _sender->getSize()) return;

        mChoicesList->adjustSize();
        mTopicsList->adjustSize();
        updateChoicePane();
        updateHistory();
        updateTopicFormat();
        mCurrentWindowSize = _sender->getSize();
    }

    void DialogueWindow::onMouseWheel(MyGUI::Widget* _sender, int _rel)
    {
        if (!mScrollBar->getVisible())
            return;
        mScrollBar->setScrollPosition(std::min(static_cast<int>(mScrollBar->getScrollRange()-1),
                                               std::max(0, static_cast<int>(mScrollBar->getScrollPosition() - _rel*0.3))));
        onScrollbarMoved(mScrollBar, mScrollBar->getScrollPosition());
    }

    void DialogueWindow::onByeClicked(MyGUI::Widget* _sender)
    {
        if (mPersuasionMode)
        {
            closePersuasionPane();
            return;
        }

        if (exit())
        {
            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Dialogue);
        }
    }

    void DialogueWindow::onSelectListItem(const std::string& topic, int id)
    {
        MWBase::DialogueManager* dialogueManager = MWBase::Environment::get().getDialogueManager();

        if (mGoodbye || dialogueManager->isInChoice())
            return;

        const std::string sPersuasion = gameSettingString("sPersuasion", "Persuasion");
        const std::string sCompanionShare = gameSettingString("sCompanionShare", "Companion Share");
        const std::string sBarter = gameSettingString("sBarter", "Barter");
        const std::string sSpells = gameSettingString("sSpells", "Spells");
        const std::string sTravel = gameSettingString("sTravel", "Travel");
        const std::string sSpellMakingMenuTitle = gameSettingString("sSpellmakingMenuTitle", "Spellmaking");
        const std::string sEnchanting = gameSettingString("sEnchanting", "Enchanting");
        const std::string sServiceTrainingTitle = gameSettingString("sServiceTrainingTitle", "Training");
        const std::string sRepair = gameSettingString("sRepair", "Repair");

        if (topic != sPersuasion && topic != sCompanionShare && topic != sBarter 
         && topic != sSpells && topic != sTravel && topic != sSpellMakingMenuTitle 
         && topic != sEnchanting && topic != sServiceTrainingTitle && topic != sRepair)
        {
            onTopicActivated(topic);
            if (mGoodbyeButton->getEnabled())
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);
        }
        else if (topic == sPersuasion)
            openPersuasionPane();
        else if (topic == sCompanionShare)
            MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Companion, mPtr);
        else if (!dialogueManager->checkServiceRefused(mCallback.get()))
        {
            if (topic == sBarter && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Barter))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Barter, mPtr);
            else if (topic == sSpells && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Spells))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_SpellBuying, mPtr);
            else if (topic == sTravel && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Travel))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Travel, mPtr);
            else if (topic == sSpellMakingMenuTitle && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Spellmaking))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_SpellCreation, mPtr);
            else if (topic == sEnchanting && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Enchanting))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Enchanting, mPtr);
            else if (topic == sServiceTrainingTitle && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Training))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Training, mPtr);
            else if (topic == sRepair && !dialogueManager->checkServiceRefused(mCallback.get(), MWBase::DialogueManager::Repair))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_MerchantRepair, mPtr);
        }
        else
            updateTopics();
    }

    void DialogueWindow::setPtr(const MWWorld::Ptr& actor)
    {
        if (!actor.getClass().isActor())
        {
            Log(Debug::Warning) << "Warning: can not talk with non-actor object.";
            return;
        }

        bool sameActor = (mPtr == actor);
        if (!sameActor)
        {
            stopDynamicDialogueActor();
            // The history is not reset here
            mKeywords.clear();
            mTopicsList->clear();
            for (Link* link : mLinks)
                mDeleteLater.push_back(link); // Links are not deleted right away to prevent issues with event handlers
            mLinks.clear();
        }

        mPtr = actor;
        mGoodbye = false;
        mPersuasionMode = false;
        mPersuasionChoices.clear();
        mTopicsList->setEnabled(true);

        if (!MWBase::Environment::get().getDialogueManager()->startDialogue(actor, mGreetingCallback.get()))
        {
            // No greetings found. The dialogue window should not be shown.
            // If this is a companion, we must show the companion window directly (used by BM_bear_be_unique).
            stopDialogueCamera();
            MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Dialogue);
            mPtr = MWWorld::Ptr();
            if (isCompanion(actor))
                MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Companion, actor);
            return;
        }

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);

        const std::string actorName = mPtr.getClass().getName(mPtr);
        setTitle(actorName);
        updateActorStatus();

        updateTopics();
        updateTopicsPane(); // force update for new services

        updateDisposition();
        restock();
        startDialogueCamera();
        startDynamicDialogueActor();
        playDynamicDialogueAnimation(false);
        selectInitialItem();
    }

    void DialogueWindow::restock()
    {
        MWMechanics::CreatureStats &sellerStats = mPtr.getClass().getCreatureStats(mPtr);
        float delay = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fBarterGoldResetDelay")->mValue.getFloat();

        // Gold is restocked every 24h
        if (MWBase::Environment::get().getWorld()->getTimeStamp() >= sellerStats.getLastRestockTime() + delay)
        {
            sellerStats.setGoldPool(mPtr.getClass().getBaseGold(mPtr));
            sellerStats.setLastRestockTime(MWBase::Environment::get().getWorld()->getTimeStamp());
        }
    }

    void DialogueWindow::deleteLater()
    {
        for (Link* link : mDeleteLater)
            delete link;
        mDeleteLater.clear();
    }

    void DialogueWindow::onClose()
    {
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue))
            return;
        stopDynamicDialogueActor();
        stopDialogueCamera();
        mPersuasionMode = false;
        mPersuasionChoices.clear();
        // Reset history
        for (DialogueText* text : mHistoryContents)
            delete text;
        mHistoryContents.clear();
    }

    MWWorld::Ptr DialogueWindow::getPtr()
    {
        return mPtr;
    }

    bool DialogueWindow::setKeywords(std::list<std::string> keyWords)
    {
        if (mKeywords == keyWords && isCompanion() == mIsCompanion)
            return false;
        mIsCompanion = isCompanion();
        mKeywords = keyWords;
        updateTopicsPane();
        return true;
    }

    void DialogueWindow::updateTopicsPane()
    {
        mTopicsList->clear();
        for (auto& linkPair : mTopicLinks)
            mDeleteLater.push_back(linkPair.second);
        mTopicLinks.clear();
        mKeywordSearch.clear();

        int services = mPtr.getClass().getServices(mPtr);

        bool travel = (mPtr.getTypeName() == typeid(ESM::NPC).name() && !mPtr.get<ESM::NPC>()->mBase->getTransport().empty())
                || (mPtr.getTypeName() == typeid(ESM::Creature).name() && !mPtr.get<ESM::Creature>()->mBase->getTransport().empty());

        if (mPtr.getTypeName() == typeid(ESM::NPC).name())
            mTopicsList->addItem(gameSettingString("sPersuasion", "Persuasion"));

        if (services & ESM::NPC::AllItems)
            mTopicsList->addItem(gameSettingString("sBarter", "Barter"));

        if (services & ESM::NPC::Spells)
            mTopicsList->addItem(gameSettingString("sSpells", "Spells"));

        if (travel)
            mTopicsList->addItem(gameSettingString("sTravel", "Travel"));

        if (services & ESM::NPC::Spellmaking)
            mTopicsList->addItem(gameSettingString("sSpellmakingMenuTitle", "Spellmaking"));

        if (services & ESM::NPC::Enchanting)
            mTopicsList->addItem(gameSettingString("sEnchanting", "Enchanting"));

        if (services & ESM::NPC::Training)
            mTopicsList->addItem(gameSettingString("sServiceTrainingTitle", "Training"));

        if (services & ESM::NPC::Repair)
            mTopicsList->addItem(gameSettingString("sRepair", "Repair"));

        if (isCompanion())
            mTopicsList->addItem(gameSettingString("sCompanionShare", "Companion Share"));

        if (mTopicsList->getItemCount() > 0)
            mTopicsList->addSeparator();


        for(const auto& keyword : mKeywords)
        {
            std::string topicId = Misc::StringUtils::lowerCase(keyword);
            mTopicsList->addItem(keyword);

            Topic* t = new Topic(keyword);
            t->eventTopicActivated += MyGUI::newDelegate(this, &DialogueWindow::onTopicActivated);
            
            mTopicLinks[topicId] = t;

            mKeywordSearch.seed(topicId, intptr_t(t));
        }
        mTopicsList->adjustSize();

        updateHistory();
        // The topics list has been regenerated so topic formatting needs to be updated
        updateTopicFormat();
        selectInitialItem();
    }

    void DialogueWindow::updateHistory(bool scrollbar)
    {
        if (!scrollbar && mScrollBar->getVisible())
        {
            mHistory->setSize(mHistory->getSize()+MyGUI::IntSize(mScrollBar->getWidth(),0));
            mScrollBar->setVisible(false);
        }
        if (scrollbar && !mScrollBar->getVisible())
        {
            mHistory->setSize(mHistory->getSize()-MyGUI::IntSize(mScrollBar->getWidth(),0));
            mScrollBar->setVisible(true);
        }

        BookTypesetter::Ptr typesetter = BookTypesetter::create (mHistory->getWidth(), std::numeric_limits<int>::max());

        for (DialogueText* text : mHistoryContents)
            text->write(typesetter, &mKeywordSearch, mTopicLinks);

        mChoices = MWBase::Environment::get().getDialogueManager()->getChoices();
        mGoodbye = MWBase::Environment::get().getDialogueManager()->isGoodbye();
        if ((!mChoices.empty() || mGoodbye) && mPersuasionMode)
        {
            mPersuasionMode = false;
            mPersuasionChoices.clear();
        }
        updateChoicePane();

        TypesetBook::Ptr book = typesetter->complete();
        mHistory->showPage(book, 0);
        size_t viewHeight = mHistory->getParent()->getHeight();
        if (!scrollbar && book->getSize().second > viewHeight)
            updateHistory(true);
        else if (scrollbar)
        {
            mHistory->setSize(MyGUI::IntSize(mHistory->getWidth(), book->getSize().second));
            size_t range = book->getSize().second - viewHeight;
            mScrollBar->setScrollRange(range);
            mScrollBar->setScrollPosition(range-1);
            mScrollBar->setTrackSize(static_cast<int>(viewHeight / static_cast<float>(book->getSize().second) * mScrollBar->getLineSize()));
            onScrollbarMoved(mScrollBar, range-1);
        }
        else
        {
            // no scrollbar
            onScrollbarMoved(mScrollBar, 0);
        }

        mGoodbyeButton->setCaption(mPersuasionMode
            ? gameSettingString("sBack", "Back")
            : gameSettingString("sGoodbye", "Goodbye"));

        bool goodbyeEnabled = mPersuasionMode
            || !MWBase::Environment::get().getDialogueManager()->isInChoice() || mGoodbye;
        bool goodbyeWasEnabled = mGoodbyeButton->getEnabled();
        mGoodbyeButton->setEnabled(goodbyeEnabled);
        if (goodbyeEnabled && !goodbyeWasEnabled)
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mGoodbyeButton);

        bool topicsEnabled = !mPersuasionMode
            && !MWBase::Environment::get().getDialogueManager()->isInChoice() && !mGoodbye;
        mTopicsList->setEnabled(topicsEnabled);
        selectInitialItem();
    }

    void DialogueWindow::notifyLinkClicked (TypesetBook::InteractiveId link)
    {
        if (mHistoryWasDragged)
        {
            mHistoryWasDragged = false;
            return;
        }
        reinterpret_cast<Link*>(link)->activated();
    }

    void DialogueWindow::onTopicActivated(const std::string &topicId)
    {
        if (mGoodbye)
            return;

        MWBase::Environment::get().getDialogueManager()->keywordSelected(topicId, mCallback.get());
        updateTopics();
    }

    void DialogueWindow::onChoiceActivated(int id)
    {
        if (mGoodbye)
        {
            onGoodbyeActivated();
            return;
        }
        MWBase::Environment::get().getDialogueManager()->questionAnswered(id, mCallback.get());
        updateTopics();
    }

    void DialogueWindow::onGoodbyeActivated()
    {
        stopDynamicDialogueActor();
        stopDialogueCamera();
        MWBase::Environment::get().getDialogueManager()->goodbyeSelected();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Dialogue);
        resetReference();
    }

    void DialogueWindow::onScrollbarMoved(MyGUI::ScrollBar *sender, size_t pos)
    {
        mHistory->setPosition(0, static_cast<int>(pos) * -1);
    }

    void DialogueWindow::addResponse(const std::string &title, const std::string &text, bool needMargin)
    {
        mHistoryContents.push_back(new Response(text, title, needMargin));
        updateHistory();
        playDynamicDialogueAnimation(true);
    }

    void DialogueWindow::addMessageBox(const std::string& text)
    {
        mHistoryContents.push_back(new Message(text));
        updateHistory();
    }

    void DialogueWindow::updateActorStatus()
    {
        if (mPtr.isEmpty() || !mPtr.getClass().isActor())
        {
            mNpcHealthBar->setVisible(false);
            mNpcHealthText->setVisible(false);
            return;
        }

        MWMechanics::CreatureStats& stats = mPtr.getClass().getCreatureStats(mPtr);
        const int level = stats.getLevel();
        const int maximumHealth = std::max(1, static_cast<int>(std::lround(stats.getHealth().getModified())));
        const int currentHealth = std::max(0, std::min(maximumHealth,
            static_cast<int>(std::lround(stats.getHealth().getCurrent()))));

        mNpcName->setCaption(mPtr.getClass().getName(mPtr) + " - "
            + MyGUI::utility::toString(level) + " lvl");
        mNpcHealthBar->setProgressRange(static_cast<size_t>(maximumHealth));
        mNpcHealthBar->setProgressPosition(static_cast<size_t>(currentHealth));
        mNpcHealthText->setCaption(MyGUI::utility::toString(currentHealth) + " / "
            + MyGUI::utility::toString(maximumHealth));
        mNpcHealthBar->setVisible(!stats.isDead());
        mNpcHealthText->setVisible(!stats.isDead());
    }

    void DialogueWindow::updateDisposition()
    {
        bool dispositionVisible = false;
        if (!mPtr.isEmpty() && mPtr.getClass().isNpc())
        {
            dispositionVisible = true;
            mDispositionBar->setProgressRange(100);
            mDispositionBar->setProgressPosition(MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(mPtr));
            mDispositionText->setCaption(MyGUI::utility::toString(MWBase::Environment::get().getMechanicsManager()->getDerivedDisposition(mPtr))+std::string("/100"));
        }

        mDispositionBar->setVisible(dispositionVisible);
        mDispositionText->setVisible(dispositionVisible);
    }

    void DialogueWindow::onReferenceUnavailable()
    {
        stopDynamicDialogueActor();
        stopDialogueCamera();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Dialogue);
    }

    void DialogueWindow::onFrame(float dt)
    {
        checkReferenceAvailable();
        if (mPtr.isEmpty())
            return;

        updateDynamicDialogueActor(dt);
        updateActorStatus();
        updateDisposition();
        deleteLater();

        if (mChoices != MWBase::Environment::get().getDialogueManager()->getChoices()
                || mGoodbye != MWBase::Environment::get().getDialogueManager()->isGoodbye())
            updateHistory();
    }

    void DialogueWindow::updateTopicFormat()
    {
        if (!Settings::Manager::getBool("color topic enable", "GUI"))
            return;

        std::string specialColour = Settings::Manager::getString("color topic specific", "GUI");
        std::string oldColour = Settings::Manager::getString("color topic exhausted", "GUI");

        for (const std::string& keyword : mKeywords)
        {
            int flag = MWBase::Environment::get().getDialogueManager()->getTopicFlag(keyword);
            MyGUI::Button* button = mTopicsList->getItemWidget(keyword);
            if (!button)
                continue;

            if (!specialColour.empty() && flag & MWBase::DialogueManager::TopicType::Specific)
                button->getSubWidgetText()->setTextColour(MyGUI::Colour::parse(specialColour));
            else if (!oldColour.empty() && flag & MWBase::DialogueManager::TopicType::Exhausted)
                button->getSubWidgetText()->setTextColour(MyGUI::Colour::parse(oldColour));
        }

        const int selected = mTopicsList->getSelectedIndex();
        if (selected >= 0)
            mTopicsList->setSelectedIndex(selected, false);
    }

    void DialogueWindow::updateTopics()
    {
        // Topic formatting needs to be updated regardless of whether the topic list has changed
        if (!setKeywords(MWBase::Environment::get().getDialogueManager()->getAvailableTopics()))
            updateTopicFormat();
    }

    bool DialogueWindow::isCompanion()
    {
        return isCompanion(mPtr);
    }

    bool DialogueWindow::isCompanion(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty())
            return false;

        return !actor.getClass().getScript(actor).empty()
                && actor.getRefData().getLocals().getIntVar(actor.getClass().getScript(actor), "companion");
    }

}
