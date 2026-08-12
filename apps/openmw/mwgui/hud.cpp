#include "hud.hpp"

#include <osg/Math>

#include <MyGUI_RenderManager.h>
#include <MyGUI_ProgressBar.h>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>



#include <components/settings/settings.hpp>
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "inventorywindow.hpp"
#include "spellicons.hpp"
#include "itemmodel.hpp"
#include "draganddrop.hpp"

#include "itemwidget.hpp"

namespace
{
    std::string getWeaponSpellBoxMode()
    {
        const auto modeKey = std::make_pair(std::string("GUI"), std::string("weapon spell box mode"));
        const auto legacyKey = std::make_pair(std::string("GUI"), std::string("persistent weapon spell boxes"));
        if (Settings::Manager::mUserSettings.find(modeKey) == Settings::Manager::mUserSettings.end())
        {
            const auto legacyIt = Settings::Manager::mUserSettings.find(legacyKey);
            if (legacyIt != Settings::Manager::mUserSettings.end())
                return (legacyIt->second == "false" || legacyIt->second == "0") ? "hidden" : "transparent";
        }

        const std::string mode = Settings::Manager::getString("weapon spell box mode", "GUI");
        if (mode == "hidden" || mode == "transparent" || mode == "visible")
            return mode;
        return Settings::Manager::getBool("persistent weapon spell boxes", "GUI")
            ? "transparent" : "hidden";
    }

    std::string getResourceBarMode()
    {
        const std::string mode = Settings::Manager::getString("resource bars mode", "HUD");
        if (mode == "always" || mode == "automatic" || mode == "hidden")
            return mode;
        return Settings::Manager::getBool("auto hide resource bars", "GUI")
            ? "automatic" : "always";
    }

    std::string getNpcBarMode()
    {
        const std::string mode = Settings::Manager::getString("npc bar mode", "HUD");
        if (mode == "off" || mode == "combat" || mode == "hover" || mode == "both")
            return mode;
        return Settings::Manager::getBool("target info panel", "GUI") ? "both" : "combat";
    }

    bool npcBarShowsHover(const std::string& mode)
    {
        return mode == "hover" || mode == "both";
    }

    bool npcBarShowsCombat(const std::string& mode)
    {
        return mode == "combat" || mode == "both";
    }

    enum class HorizontalCompassMarkerKind
    {
        Enemy = 0,
        Ally = 1
    };

    struct HorizontalCompassMarkerCandidate
    {
        MWWorld::Ptr mActor;
        int mLeft = 0;
        float mDistanceSquared = 0.f;
        HorizontalCompassMarkerKind mKind = HorizontalCompassMarkerKind::Ally;
    };

    MyGUI::Colour getHorizontalCompassMarkerColour(HorizontalCompassMarkerKind kind)
    {
        switch (kind)
        {
            case HorizontalCompassMarkerKind::Enemy:
                return MyGUI::Colour(1.00f, 0.18f, 0.16f);
            case HorizontalCompassMarkerKind::Ally:
                return MyGUI::Colour(0.14f, 0.74f, 0.22f);
        }
        return MyGUI::Colour(1.f, 1.f, 1.f);
    }
}

namespace MWGui
{

    /**
     * Makes it possible to use ItemModel::moveItem to move an item from an inventory to the world.
     */
    class WorldItemModel : public ItemModel
    {
    public:
        WorldItemModel(float left, float top) : mLeft(left), mTop(top) {}
        virtual ~WorldItemModel() override {}
        MWWorld::Ptr copyItem (const ItemStack& item, size_t count, bool /*allowAutoEquip*/) override
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();

            MWWorld::Ptr dropped;
            if (world->canPlaceObject(mLeft, mTop))
                dropped = world->placeObject(item.mBase, mLeft, mTop, count);
            else
                dropped = world->dropObjectOnGround(world->getPlayerPtr(), item.mBase, count);
            dropped.getCellRef().setOwner("");

            

            

            return dropped;
        }

        void removeItem (const ItemStack& item, size_t count) override { throw std::runtime_error("removeItem not implemented"); }
        ModelIndex getIndex (ItemStack item) override { throw std::runtime_error("getIndex not implemented"); }
        void update() override {}
        size_t getItemCount() override { return 0; }
        ItemStack getItem (ModelIndex index) override { throw std::runtime_error("getItem not implemented"); }

    private:
        // Where to drop the item
        float mLeft;
        float mTop;
    };


    HUD::HUD(CustomMarkerCollection &customMarkers, DragAndDrop* dragAndDrop, MWRender::LocalMap* localMapRender)
        : WindowBase("openmw_hud.layout")
        , LocalMapBase(customMarkers, localMapRender, Settings::Manager::getBool("local map hud fog of war", "Map"))
        , mGameplayHud(nullptr)
        , mHealth(nullptr)
        , mMagicka(nullptr)
        , mStamina(nullptr)
        , mDrowning(nullptr)
        , mHealthText(nullptr)
        , mMagickaText(nullptr)
        , mStaminaText(nullptr)
        , mFpsBox(nullptr)
        , mEnemyName(nullptr)
        , mEnemySummary(nullptr)
        , mWeapImage(nullptr)
        , mSpellImage(nullptr)
        , mWeapStatus(nullptr)
        , mSpellStatus(nullptr)
        , mEffectBox(nullptr)
        , mMinimap(nullptr)
        , mCrosshair(nullptr)
        , mCellNameClip(nullptr)
        , mCellNameBox(nullptr)
        , mGameTimeBox(nullptr)
        , mHorizontalCompass(nullptr)
        , mHorizontalCompassCenter(nullptr)
        , mDrowningFrame(nullptr)
        , mDrowningFlash(nullptr)
        , mHealthManaStaminaBaseLeft(0)
        , mWeapBoxBaseLeft(0)
        , mSpellBoxBaseLeft(0)
        , mMinimapBoxBaseRight(0)
        , mEffectBoxBaseRight(0)
        , mDragAndDrop(dragAndDrop)
        , mCellNameTimer(0.0f)
        , mCellNameScrollOffset(0.f)
        , mCellNameScrollPause(0.f)
        , mCellNameScrollDirection(-1)
        , mCellNameScrolling(false)
        , mWeaponSpellTimer(0.f)
        , mGameTimeUpdateTimer(0.f)
        , mMapVisible(true)
        , mMinimapBaseVisible(true)
        , mEffectBaseVisible(true)
        , mCrosshairBaseVisible(true)
        , mWeaponVisible(true)
        , mSpellVisible(true)
        , mWorldMouseOver(false)
        , mHorizontalCompassAngle(0.f)
        , mHorizontalCompassDirty(true)
        , mHorizontalCompassMarkerTimer(0.f)
        , mEnemyActorId(-1)
        , mEnemyHealthTimer(-1)
        , mFocusActorScreenX(0.5f)
        , mFocusActorScreenY(0.f)
        , mFocusActorDistance(-1.f)
        , mFocusActorPanelAlpha(0.f)
        , mTargetPanelCenterX(0.f)
        , mFocusActorCurrentlyFaced(false)
        , mTargetPanelPositionInitialized(false)
        , mFpsUpdateTimer(0.f)
        , mFpsAccumulatedTime(0.f)
        , mFpsFrameCount(0)
        , mIsDrowning(false)
        , mDrowningFlashTheta(0.f)
        , mHmsBaseVisible(true)
    {
        mMainWidget->setSize(MyGUI::RenderManager::getInstance().getViewSize());
        getWidget(mGameplayHud, "GameplayHud");

        // Do not expose the gameplay HUD while a new single-player character is
        // still being registered. Character-generation windows are separate widgets.
        mGameplayHud->setVisible(false);

        // Energy bars
        getWidget(mHealthFrame, "HealthFrame");
        getWidget(mMagickaFrame, "MagickaFrame");
        getWidget(mFatigueFrame, "FatigueFrame");
        getWidget(mHealth, "Health");
        getWidget(mMagicka, "Magicka");
        getWidget(mStamina, "Stamina");
        getWidget(mEnemyHealth, "EnemyHealth");
        getWidget(mEnemyName, "EnemyName");
        getWidget(mEnemySummary, "EnemySummary");
        getWidget(mHealthText, "HealthText");
        getWidget(mMagickaText, "MagickaText");
        getWidget(mStaminaText, "StaminaText");
        getWidget(mFpsBox, "FpsText");
        mHealthManaStaminaBaseLeft = mHealthFrame->getLeft();

        mHealthFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mMagickaFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);
        mFatigueFrame->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onHMSClicked);

        //Drowning bar
        getWidget(mDrowningFrame, "DrowningFrame");
        getWidget(mDrowning, "Drowning");
        getWidget(mDrowningFlash, "Flash");
        mDrowning->setProgressRange(200);

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        // Item and spell images and status bars
        getWidget(mWeapBox, "WeapBox");
        getWidget(mWeapImage, "WeapImage");
        getWidget(mWeapStatus, "WeapStatus");
        mWeapBoxBaseLeft = mWeapBox->getLeft();
        mWeapBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWeaponClicked);

        getWidget(mSpellBox, "SpellBox");
        getWidget(mSpellImage, "SpellImage");
        getWidget(mSpellStatus, "SpellStatus");
        mSpellBoxBaseLeft = mSpellBox->getLeft();
        mSpellBox->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMagicClicked);

        getWidget(mSneakBox, "SneakBox");
        mSneakBoxBaseLeft = mSneakBox->getLeft();

        getWidget(mEffectBox, "EffectBox");
        mEffectBoxBaseRight = viewSize.width - mEffectBox->getRight();

        getWidget(mMinimapBox, "MiniMapBox");
        mMinimapBoxBaseRight = viewSize.width - mMinimapBox->getRight();
        getWidget(mMinimap, "MiniMap");
        getWidget(mCompass, "Compass");
        getWidget(mMinimapButton, "MiniMapButton");
        mMinimapButton->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);

        getWidget(mCellNameClip, "CellNameClip");
        getWidget(mCellNameBox, "CellName");
        getWidget(mWeaponSpellBox, "WeaponSpellName");
        getWidget(mGameTimeBox, "GameTime");

        getWidget(mHorizontalCompass, "HorizontalCompass");
        constexpr int horizontalCompassTickCount = 11;
        for (int i = 0; i < horizontalCompassTickCount; ++i)
        {
            MyGUI::TextBox* tick = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
                MyGUI::IntCoord(0, 1, 48, 20), MyGUI::Align::Default);
            tick->setTextAlign(MyGUI::Align::Center);
            tick->setTextShadow(true);
            tick->setTextShadowColour(MyGUI::Colour::Black);
            tick->setNeedMouseFocus(false);
            mHorizontalCompassTicks.push_back(tick);
        }
        mHorizontalCompassCenter = mHorizontalCompass->createWidget<MyGUI::TextBox>("SandBrightText",
            MyGUI::IntCoord(mHorizontalCompass->getWidth() / 2 - 12, 14, 24, 10), MyGUI::Align::Default);
        mHorizontalCompassCenter->setCaption("^");
        mHorizontalCompassCenter->setTextAlign(MyGUI::Align::Center);
        mHorizontalCompassCenter->setTextShadow(true);
        mHorizontalCompassCenter->setTextShadowColour(MyGUI::Colour::Black);
        mHorizontalCompassCenter->setNeedMouseFocus(false);

        // Reuse a fixed pool so actors entering/leaving range never allocate GUI widgets mid-frame.
        constexpr int horizontalCompassMarkerCount = 48;
        for (int i = 0; i < horizontalCompassMarkerCount; ++i)
        {
            MyGUI::TextBox* marker = mHorizontalCompass->createWidget<MyGUI::TextBox>("NormalText",
                MyGUI::IntCoord(0, 4, 22, 22), MyGUI::Align::Default);
            marker->setCaption("●");
            marker->setFontName("CompassMarkerFont");
            marker->setFontHeight(21);
            marker->setTextAlign(MyGUI::Align::Center);
            marker->setTextShadow(true);
            marker->setTextShadowColour(MyGUI::Colour::Black);
            marker->setNeedMouseFocus(false);
            marker->setAlpha(0.f);
            marker->setVisible(false);

            HorizontalCompassMarkerState state;
            state.mWidget = marker;
            mHorizontalCompassMarkers.push_back(state);
        }

        getWidget(mCrosshair, "Crosshair");
        // The crosshair lives outside the chargen-hidden GameplayHud container,
        // so it is available as soon as the camera enters game mode.
        mCrosshair->setVisible(Settings::Manager::getBool("crosshair", "HUD"));

        LocalMapBase::init(mMinimap, mCompass);

        mMainWidget->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onWorldClicked);
        mMainWidget->eventMouseMove += MyGUI::newDelegate(this, &HUD::onWorldMouseOver);
        mMainWidget->eventMouseLostFocus += MyGUI::newDelegate(this, &HUD::onWorldMouseLostFocus);

        mSpellIcons = new SpellIcons();
    }

    HUD::~HUD()
    {
        mMainWidget->eventMouseLostFocus.clear();
        mMainWidget->eventMouseMove.clear();
        mMainWidget->eventMouseButtonClick.clear();

        delete mSpellIcons;
    }

    void HUD::setValue(const std::string& id, const MWMechanics::DynamicStat<float>& value)
    {
        int current = static_cast<int>(value.getCurrent());
        int modified = static_cast<int>(value.getModified());

        // Fatigue can be negative
        if (id != "FBar")
            current = std::max(0, current);

        std::string valStr = MyGUI::utility::toString(current) + " / " + MyGUI::utility::toString(modified);
        if (id == "HBar")
        {
            mHealth->setProgressRange(std::max(0, modified));
            mHealth->setProgressPosition(std::max(0, current));
            if (mHealthText)
                mHealthText->setCaption(valStr);
            mHealthFrame->setUserString("Caption_HealthDescription", "#{sHealthDesc}\n" + valStr);
            registerBarChange(mHealthBarState, current, modified);
        }
        else if (id == "MBar")
        {
            mMagicka->setProgressRange(std::max(0, modified));
            mMagicka->setProgressPosition(std::max(0, current));
            if (mMagickaText)
                mMagickaText->setCaption(valStr);
            mMagickaFrame->setUserString("Caption_HealthDescription", "#{sMagDesc}\n" + valStr);
            registerBarChange(mMagickaBarState, current, modified);
        }
        else if (id == "FBar")
        {
            mStamina->setProgressRange(std::max(0, modified));
            mStamina->setProgressPosition(std::max(0, current));
            if (mStaminaText)
                mStaminaText->setCaption(valStr);
            mFatigueFrame->setUserString("Caption_HealthDescription", "#{sFatDesc}\n" + valStr);
            registerBarChange(mStaminaBarState, current, modified);
        }
    }

    void HUD::setDrowningTimeLeft(float time, float maxTime)
    {
        size_t progress = static_cast<size_t>(time / maxTime * 200);
        mDrowning->setProgressPosition(progress);

        bool isDrowning = (progress == 0);
        if (isDrowning && !mIsDrowning) // Just started drowning
            mDrowningFlashTheta = 0.0f; // Start out on bright red every time.

        mDrowningFlash->setVisible(isDrowning);
        mIsDrowning = isDrowning;
    }

    void HUD::setDrowningBarVisible(bool visible)
    {
        mDrowningFrame->setVisible(visible);
    }

    void HUD::onWorldClicked(MyGUI::Widget* _sender)
    {
        if (!MWBase::Environment::get().getWindowManager ()->isGuiMode ())
            return;

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            // drop item into the gameworld
            MWBase::Environment::get().getWorld()->breakInvisibility(
                        MWMechanics::getPlayer());

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            WorldItemModel drop (mouseX, mouseY);
            mDragAndDrop->drop(&drop, nullptr);

            winMgr->changePointer("arrow");
        }
        else
        {
            GuiMode mode = winMgr->getMode();

            if (!winMgr->isConsoleMode() && (mode != GM_Container) && (mode != GM_Inventory))
                return;

            MWWorld::Ptr object = MWBase::Environment::get().getWorld()->getFacedObject();

            if (winMgr->isConsoleMode())
                winMgr->setConsoleSelectedObject(object);
            else //if ((mode == GM_Container) || (mode == GM_Inventory))
            {
                // pick up object
                if (!object.isEmpty())
                    winMgr->getInventoryWindow()->pickUpObject(object);
            }
        }
    }

    void HUD::onWorldMouseOver(MyGUI::Widget* _sender, int x, int y)
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            mWorldMouseOver = false;

            MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            MyGUI::IntPoint cursorPosition = MyGUI::InputManager::getInstance().getMousePosition();
            float mouseX = cursorPosition.left / float(viewSize.width);
            float mouseY = cursorPosition.top / float(viewSize.height);

            MWBase::World* world = MWBase::Environment::get().getWorld();

            // if we can't drop the object at the wanted position, show the "drop on ground" cursor.
            bool canDrop = world->canPlaceObject(mouseX, mouseY);

            if (!canDrop)
                MWBase::Environment::get().getWindowManager()->changePointer("drop_ground");
            else
                MWBase::Environment::get().getWindowManager()->changePointer("arrow");

        }
        else
        {
            MWBase::Environment::get().getWindowManager()->changePointer("arrow");
            mWorldMouseOver = true;
        }
    }

    void HUD::onWorldMouseLostFocus(MyGUI::Widget* _sender, MyGUI::Widget* _new)
    {
        MWBase::Environment::get().getWindowManager()->changePointer("arrow");
        mWorldMouseOver = false;
    }

    void HUD::onHMSClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Stats);
    }

    void HUD::onMapClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Map);
    }

    void HUD::onWeaponClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Inventory);
    }

    void HUD::onMagicClicked(MyGUI::Widget* _sender)
    {
        const MWWorld::Ptr& player = MWMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Magic);
    }


    void HUD::onResChange(int width, int height)
    {
        mMainWidget->setSize(width, height);
        updatePositions();
    }

    void HUD::setCellName(const std::string& cellName)
    {
        if (mCellName != cellName)
        {
            mCellNameTimer = 5.0f;
            mCellName = cellName;

            mCellNameBox->setCaptionWithReplacing("#{sCell=" + mCellName + "}");

            const int clipWidth = mCellNameClip ? mCellNameClip->getWidth() : mCellNameBox->getWidth();
            const int clipHeight = mCellNameClip ? mCellNameClip->getHeight() : mCellNameBox->getHeight();
            const int textWidth = mCellNameBox->getTextSize().width + 8;
            mCellNameScrolling = textWidth > clipWidth;
            mCellNameScrollOffset = 0.f;
            mCellNameScrollPause = mCellNameScrolling ? 0.75f : 0.f;
            mCellNameScrollDirection = -1;

            if (mCellNameScrolling)
            {
                mCellNameBox->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
                mCellNameBox->setCoord(0, 0, textWidth, clipHeight);
            }
            else
            {
                mCellNameBox->setTextAlign(MyGUI::Align::Center);
                mCellNameBox->setCoord(0, 0, clipWidth, clipHeight);
            }

            mCellNameBox->setVisible(mMapVisible && Settings::Manager::getBool("show cell name", "HUD"));
        }
    }

    void HUD::onFrame(float dt)
    {
        // In single-player, chargenstate becomes -1 only after the complete
        // character creation/registration sequence has finished. Toggling one
        // parent preserves every child's own map/autohide/setting visibility.
        const bool characterRegistrationFinished
            = MWBase::Environment::get().getWorld()->getGlobalInt("chargenstate") == -1;
        if (mGameplayHud && mGameplayHud->getVisible() != characterRegistrationFinished)
            mGameplayHud->setVisible(characterRegistrationFinished);

        LocalMapBase::onFrame(dt);

        // Apply HUD preferences live so changes made in the dedicated HUD tab do
        // not require reopening the game or rebuilding the HUD layout.
        const bool showMinimap = mMinimapBaseVisible && Settings::Manager::getBool("show minimap", "HUD");
        if (mMinimapBox->getVisible() != showMinimap)
        {
            mMinimapBox->setVisible(showMinimap);
            updatePositions();
        }
        if (mCompass)
            mCompass->setVisible(showMinimap);
        if (mGameTimeBox)
            mGameTimeBox->setVisible(Settings::Manager::getBool("show game time", "HUD"));
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible && Settings::Manager::getBool("show status effects", "HUD"));
        if (mFpsBox)
            mFpsBox->setVisible(Settings::Manager::getBool("show fps", "HUD"));
        if (mCrosshair)
            mCrosshair->setVisible(mCrosshairBaseVisible && Settings::Manager::getBool("crosshair", "HUD"));

        updateHorizontalCompass();
        updateHorizontalCompassMarkers(dt);

        if (mGameTimeBox && mGameTimeBox->getVisible())
        {
            mGameTimeUpdateTimer -= dt;
            if (mGameTimeUpdateTimer <= 0.f)
            {
                const float gameHour = MWBase::Environment::get().getWorld()->getTimeStamp().getHour();
                int hours = static_cast<int>(std::floor(gameHour)) % 24;
                int minutes = static_cast<int>(std::floor((gameHour - std::floor(gameHour)) * 60.f + 0.5f));
                if (minutes >= 60)
                {
                    minutes = 0;
                    hours = (hours + 1) % 24;
                }

                std::ostringstream stream;
                stream << std::setfill('0') << std::setw(2) << hours << ':'
                       << std::setfill('0') << std::setw(2) << minutes;
                mGameTimeBox->setCaption(stream.str());

                mGameTimeUpdateTimer = 0.2f;
            }
        }

        mCellNameTimer -= dt;
        mWeaponSpellTimer -= dt;
        if (mCellNameTimer < 0 || !Settings::Manager::getBool("show cell name", "HUD"))
            mCellNameBox->setVisible(false);
        else if (mCellNameBox->getVisible() && mCellNameScrolling && mCellNameClip)
        {
            if (mCellNameScrollPause > 0.f)
                mCellNameScrollPause = std::max(0.f, mCellNameScrollPause - dt);
            else
            {
                constexpr float cellNameScrollSpeed = 34.f;
                mCellNameScrollOffset += mCellNameScrollDirection * cellNameScrollSpeed * std::max(0.f, dt);
                const float minimumOffset = static_cast<float>(mCellNameClip->getWidth() - mCellNameBox->getWidth());

                if (mCellNameScrollOffset <= minimumOffset)
                {
                    mCellNameScrollOffset = minimumOffset;
                    mCellNameScrollDirection = 1;
                    mCellNameScrollPause = 0.75f;
                }
                else if (mCellNameScrollOffset >= 0.f)
                {
                    mCellNameScrollOffset = 0.f;
                    mCellNameScrollDirection = -1;
                    mCellNameScrollPause = 0.75f;
                }

                mCellNameBox->setPosition(static_cast<int>(std::lround(mCellNameScrollOffset)), 0);
            }
        }
        if (mWeaponSpellTimer < 0)
            mWeaponSpellBox->setVisible(false);

        mFpsAccumulatedTime += dt;
        ++mFpsFrameCount;
        mFpsUpdateTimer -= dt;
        if (mFpsBox && mFpsUpdateTimer <= 0.f)
        {
            const float safeTime = std::max(0.0001f, mFpsAccumulatedTime);
            const int fps = static_cast<int>(std::lround(
                static_cast<double>(mFpsFrameCount) / safeTime));
            mFpsBox->setCaption(MyGUI::utility::toString(fps));
            mFpsUpdateTimer = 0.25f;
            mFpsAccumulatedTime = 0.f;
            mFpsFrameCount = 0;
        }

        updateFocusedTargetPanel(dt);

        const std::string npcBarMode = getNpcBarMode();
        const bool showHoverNpcBar = npcBarShowsHover(npcBarMode);
        const bool showCombatNpcBar = npcBarShowsCombat(npcBarMode);
        const bool focusedTargetAlive = !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool focusedTargetPanel = showHoverNpcBar && focusedTargetAlive && !dialogueOpen
            && mFocusActorPanelAlpha >= 0.5f;
        const bool combatTargetPanel = showCombatNpcBar && mEnemyActorId != -1;

        mEnemyHealthTimer -= dt;
        if (mEnemyHealth->getVisible() && mEnemyHealthTimer < 0 && !focusedTargetPanel)
        {
            mEnemyHealth->setVisible(false);
        }

        if (mIsDrowning)
            mDrowningFlashTheta += dt * osg::PI*2;

        mSpellIcons->updateWidgets(mEffectBox, true);
        if (mEffectBox)
            mEffectBox->setVisible(mEffectBaseVisible
                && Settings::Manager::getBool("show status effects", "HUD")
                && mEffectBox->getChildCount() > 0);

        if ((focusedTargetPanel || combatTargetPanel) && mEnemyHealth->getVisible())
        {
            updateEnemyHealthBar();
        }

        if (focusedTargetPanel)
        {
            mEnemyHealth->setVisible(true);
            if (mEnemyName)
                mEnemyName->setVisible(true);
            if (mEnemySummary)
                mEnemySummary->setVisible(true);
            updateEnemyHealthBar();
        }

        if (mIsDrowning)
        {
            float intensity = (cos(mDrowningFlashTheta) + 2.0f) / 3.0f;

            mDrowningFlash->setAlpha(intensity);
        }

        MWMechanics::DrawState_ drawState = MWMechanics::DrawState_Nothing;
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (!player.isEmpty())
            drawState = player.getClass().getCreatureStats(player).getDrawState();

        if ((!showHoverNpcBar && !showCombatNpcBar) || (dialogueOpen && mEnemyActorId == -1))
            mEnemyHealth->setVisible(false);

        const bool showFocusedTargetInfo = focusedTargetPanel && mEnemyHealth->getVisible();
        if (mEnemyName)
            mEnemyName->setVisible(showFocusedTargetInfo);
        if (mEnemySummary)
            mEnemySummary->setVisible(showFocusedTargetInfo);

        updateAutoHideBar(mHealthFrame, mHealthBarState, dt, false);
        updateAutoHideBar(mMagickaFrame, mMagickaBarState, dt,
            drawState == MWMechanics::DrawState_Spell, mSpellBox);
        updateAutoHideBar(mFatigueFrame, mStaminaBarState, dt,
            drawState == MWMechanics::DrawState_Weapon, mWeapBox);
    }


    void HUD::setPlayerDir(float x, float y)
    {
        LocalMapBase::setPlayerDir(x, y);
        const float angle = std::atan2(x, y);
        if (angle != mHorizontalCompassAngle)
        {
            mHorizontalCompassAngle = angle;
            mHorizontalCompassDirty = true;
        }
    }

    void HUD::updateHorizontalCompass()
    {
        if (!mHorizontalCompass)
            return;

        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD");
        if (mHorizontalCompass->getVisible() != enabled)
            mHorizontalCompass->setVisible(enabled);
        if (!enabled || !mHorizontalCompassDirty)
            return;

        static const char* directions[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        constexpr float stepDegrees = 15.f;
        constexpr float pixelsPerDegree = 4.f;
        constexpr int labelWidth = 48;

        float heading = osg::RadiansToDegrees(mHorizontalCompassAngle);
        while (heading < 0.f)
            heading += 360.f;
        while (heading >= 360.f)
            heading -= 360.f;

        const int nearestStep = static_cast<int>(std::floor(heading / stepDegrees + 0.5f));
        const int halfCount = static_cast<int>(mHorizontalCompassTicks.size()) / 2;
        for (size_t i = 0; i < mHorizontalCompassTicks.size(); ++i)
        {
            const int markerStep = nearestStep + static_cast<int>(i) - halfCount;
            const float markerDegrees = markerStep * stepDegrees;
            const float delta = markerDegrees - heading;
            const int left = static_cast<int>(std::lround(
                mHorizontalCompass->getWidth() * 0.5f + delta * pixelsPerDegree - labelWidth * 0.5f));
            MyGUI::TextBox* tick = mHorizontalCompassTicks[i];
            tick->setPosition(left, 1);

            int wrappedStep = markerStep % 24;
            if (wrappedStep < 0)
                wrappedStep += 24;
            if (wrappedStep % 3 == 0)
                tick->setCaption(directions[(wrappedStep / 3) % 8]);
            else
                tick->setCaption("|");
        }

        mHorizontalCompassDirty = false;
    }

    void HUD::updateHorizontalCompassMarkers(float dt)
    {
        if (!mHorizontalCompass)
            return;

        dt = std::max(0.f, dt);
        const bool enabled = Settings::Manager::getBool("horizontal compass", "HUD")
            && mGameplayHud && mGameplayHud->getVisible();
        if (!enabled)
        {
            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                state.mActor = MWWorld::Ptr();
                state.mAlpha = 0.f;
                state.mTargetAlpha = 0.f;
                state.mSeen = false;
                state.mWidget->setAlpha(0.f);
                state.mWidget->setVisible(false);
            }
            mHorizontalCompassMarkerTimer = 0.f;
            return;
        }

        mHorizontalCompassMarkerTimer -= dt;
        if (mHorizontalCompassMarkerTimer <= 0.f)
        {
            mHorizontalCompassMarkerTimer = 0.075f;

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                state.mSeen = false;

            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            if (world && mechanics)
            {
                const MWWorld::Ptr player = world->getPlayerPtr();
                if (!player.isEmpty() && player.isInCell())
                {
                    // Lower enum value wins if an actor temporarily appears in both collections.
                    std::map<MWWorld::Ptr, HorizontalCompassMarkerKind> actors;
                    const auto addActor = [&actors](const MWWorld::Ptr& actor, HorizontalCompassMarkerKind kind)
                    {
                        if (actor.isEmpty())
                            return;
                        const auto result = actors.emplace(actor, kind);
                        if (!result.second && static_cast<int>(kind) < static_cast<int>(result.first->second))
                            result.first->second = kind;
                    };

                    // Only local single-player actors are queried. No TES3MP player list or packets.
                    for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(player))
                        addActor(enemy, HorizontalCompassMarkerKind::Enemy);

                    std::set<MWWorld::Ptr> allies;
                    mechanics->getActorsSidingWith(player, allies);
                    for (const MWWorld::Ptr& ally : allies)
                        addActor(ally, HorizontalCompassMarkerKind::Ally);

                    constexpr float markerRange = 8192.f;
                    constexpr float markerRangeSquared = markerRange * markerRange;
                    constexpr float pixelsPerDegree = 4.f;
                    constexpr int compassInnerPadding = 10;
                    const int markerWidth = mHorizontalCompassMarkers.empty()
                        ? 22
                        : mHorizontalCompassMarkers.front().mWidget->getWidth();
                    const osg::Vec3f playerPosition = player.getRefData().getPosition().asVec3();
                    const int compassWidth = mHorizontalCompass->getWidth();
                    const float compassCenter = compassWidth * 0.5f;
                    const float maximumMarkerOffset = std::max(1.f,
                        compassCenter - compassInnerPadding - markerWidth * 0.5f);
                    const float maximumVisibleDegrees = maximumMarkerOffset / pixelsPerDegree;

                    std::vector<HorizontalCompassMarkerCandidate> candidates;
                    candidates.reserve(actors.size());
                    for (const auto& actorEntry : actors)
                    {
                        const MWWorld::Ptr& actor = actorEntry.first;
                        const HorizontalCompassMarkerKind kind = actorEntry.second;
                        if (actor == player || !actor.isInCell() || actor.getCell() != player.getCell())
                            continue;
                        if (actor.getRefData().getCount() <= 0 || !actor.getRefData().isEnabled()
                            || actor.getRefData().isDeleted())
                            continue;
                        if (!actor.getClass().isActor() || actor.getClass().getCreatureStats(actor).isDead())
                            continue;

                        const osg::Vec3f offset = actor.getRefData().getPosition().asVec3() - playerPosition;
                        const float distanceSquared = offset.x() * offset.x() + offset.y() * offset.y();
                        if (distanceSquared > markerRangeSquared || distanceSquared < 1.f)
                            continue;

                        const float bearing = std::atan2(offset.x(), offset.y());
                        float relativeAngle = bearing - mHorizontalCompassAngle;
                        while (relativeAngle > osg::PI)
                            relativeAngle -= static_cast<float>(osg::PI) * 2.f;
                        while (relativeAngle < -osg::PI)
                            relativeAngle += static_cast<float>(osg::PI) * 2.f;

                        // Skyrim-style edge behaviour: actors behind the player stay pinned
                        // to the nearest compass edge instead of disappearing.
                        const float relativeDegrees = osg::RadiansToDegrees(relativeAngle);
                        const float displayedDegrees = std::max(-maximumVisibleDegrees,
                            std::min(maximumVisibleDegrees, relativeDegrees));
                        const int left = static_cast<int>(std::lround(
                            compassCenter + displayedDegrees * pixelsPerDegree - markerWidth * 0.5f));

                        HorizontalCompassMarkerCandidate candidate;
                        candidate.mActor = actor;
                        candidate.mLeft = left;
                        candidate.mDistanceSquared = distanceSquared;
                        candidate.mKind = kind;
                        candidates.push_back(candidate);
                    }

                    std::sort(candidates.begin(), candidates.end(),
                        [](const HorizontalCompassMarkerCandidate& left,
                            const HorizontalCompassMarkerCandidate& right)
                        {
                            if (left.mDistanceSquared != right.mDistanceSquared)
                                return left.mDistanceSquared < right.mDistanceSquared;
                            return static_cast<int>(left.mKind) < static_cast<int>(right.mKind);
                        });

                    const size_t candidateCount = std::min(candidates.size(), mHorizontalCompassMarkers.size());
                    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
                    {
                        const HorizontalCompassMarkerCandidate& candidate = candidates[candidateIndex];
                        HorizontalCompassMarkerState* selectedState = nullptr;

                        // Keep one widget attached to the same actor so nearby markers never swap.
                        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                        {
                            if (!state.mActor.isEmpty() && state.mActor == candidate.mActor)
                            {
                                selectedState = &state;
                                break;
                            }
                        }

                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (!state.mSeen && state.mActor.isEmpty())
                                {
                                    selectedState = &state;
                                    break;
                                }
                            }
                        }
                        if (!selectedState)
                        {
                            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
                            {
                                if (state.mSeen)
                                    continue;
                                if (!selectedState || state.mAlpha < selectedState->mAlpha)
                                    selectedState = &state;
                            }
                        }
                        if (!selectedState)
                            continue;

                        const bool newActor = selectedState->mActor.isEmpty()
                            || selectedState->mActor != candidate.mActor;
                        const float oldTargetLeft = selectedState->mTargetLeft;
                        selectedState->mActor = candidate.mActor;
                        selectedState->mSeen = true;
                        selectedState->mTargetLeft = static_cast<float>(candidate.mLeft);
                        selectedState->mTargetAlpha = 1.f;
                        selectedState->mWidget->setTextColour(getHorizontalCompassMarkerColour(candidate.mKind));

                        if (newActor)
                        {
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                            selectedState->mAlpha = 0.f;
                            selectedState->mWidget->setAlpha(0.f);
                        }
                        else if (std::abs(selectedState->mTargetLeft - oldTargetLeft) > compassWidth * 0.65f)
                        {
                            // Crossing the exact rear direction changes the nearest edge.
                            selectedState->mCurrentLeft = selectedState->mTargetLeft;
                        }
                    }
                }
            }

            for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
            {
                if (!state.mSeen)
                    state.mTargetAlpha = 0.f;
            }
        }

        const float positionBlend = 1.f - std::exp(-14.f * dt);
        constexpr float fadeInSpeed = 7.5f;
        constexpr float fadeOutSpeed = 4.5f;
        for (HorizontalCompassMarkerState& state : mHorizontalCompassMarkers)
        {
            state.mCurrentLeft += (state.mTargetLeft - state.mCurrentLeft) * positionBlend;
            if (state.mTargetAlpha > state.mAlpha)
                state.mAlpha = std::min(state.mTargetAlpha, state.mAlpha + fadeInSpeed * dt);
            else
                state.mAlpha = std::max(state.mTargetAlpha, state.mAlpha - fadeOutSpeed * dt);

            const bool visible = state.mAlpha > 0.01f || state.mTargetAlpha > 0.f;
            state.mWidget->setVisible(visible);
            if (visible)
            {
                constexpr int compassInnerPadding = 10;
                const int markerWidth = state.mWidget->getWidth();
                const float minimumLeft = static_cast<float>(compassInnerPadding);
                const float maximumLeft = static_cast<float>(std::max(
                    compassInnerPadding, mHorizontalCompass->getWidth() - compassInnerPadding - markerWidth));
                state.mCurrentLeft = std::max(minimumLeft, std::min(maximumLeft, state.mCurrentLeft));
                state.mWidget->setPosition(static_cast<int>(std::lround(state.mCurrentLeft)), 4);
                state.mWidget->setAlpha(state.mAlpha);
            }
            else
            {
                state.mActor = MWWorld::Ptr();
                state.mAlpha = 0.f;
                state.mWidget->setAlpha(0.f);
            }
        }
    }

    void HUD::registerBarChange(AutoHideBarState& state, int current, int modified)
    {
        const bool firstUpdate = !state.initialized;
        const bool maximumChanged = state.initialized && state.modified != modified;
        const bool valueDecreased = state.initialized && current < state.current;

        state.current = current;
        state.modified = modified;
        state.initialized = true;

        // Show a bar when the resource is actually spent/damaged or its maximum changes.
        // Passive regeneration must not continuously restart the auto-hide timer.
        if (firstUpdate || maximumChanged || valueDecreased)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
        }
    }

    void HUD::applyBarAlpha(MyGUI::Widget* widget, float alpha)
    {
        if (!widget)
            return;

        widget->setAlpha(std::max(0.f, std::min(1.f, alpha)));
    }

    void HUD::updateAutoHideBar(MyGUI::Widget* frame, AutoHideBarState& state, float dt,
        bool forceVisible, MyGUI::Widget* persistentIcon)
    {
        if (!frame || !state.initialized)
            return;

        const bool barEnabled = frame == mHealthFrame
            ? Settings::Manager::getBool("show health bar", "HUD")
            : (frame == mMagickaFrame
                ? Settings::Manager::getBool("show magicka bar", "HUD")
                : Settings::Manager::getBool("show stamina bar", "HUD"));

        if (!mHmsBaseVisible)
        {
            frame->setVisible(false);
            return;
        }

        const auto applyResourceState = [&](float alpha)
        {
            alpha = std::max(0.f, std::min(1.f, alpha));
            const std::string boxMode = persistentIcon ? getWeaponSpellBoxMode() : "hidden";
            const bool keepIcon = persistentIcon && boxMode != "hidden";

            if (!keepIcon)
            {
                frame->setVisible(alpha > 0.f);
                applyBarAlpha(frame, alpha);
                if (alpha > 0.f)
                {
                    for (unsigned int i = 0; i < frame->getChildCount(); ++i)
                    {
                        MyGUI::Widget* child = frame->getChildAt(i);
                        const bool iconAllowed = child != persistentIcon
                            || (persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible);
                        child->setVisible(iconAllowed);
                        applyBarAlpha(child, 1.f);
                    }
                }
                return;
            }

            // Weapon and spell boxes live inside the stamina/magicka frame. Keep the
            // parent alive, fade only the bar children, and apply the selected box mode.
            frame->setVisible(true);
            applyBarAlpha(frame, 1.f);
            for (unsigned int i = 0; i < frame->getChildCount(); ++i)
            {
                MyGUI::Widget* child = frame->getChildAt(i);
                if (child == persistentIcon)
                    continue;
                child->setVisible(alpha > 0.f);
                applyBarAlpha(child, alpha);
            }

            const bool iconAllowed = persistentIcon == mWeapBox ? mWeaponVisible : mSpellVisible;
            persistentIcon->setVisible(iconAllowed);
            const float persistentAlpha = boxMode == "visible" ? 1.f : 0.4f;
            applyBarAlpha(persistentIcon, std::max(persistentAlpha, alpha));
        };

        const std::string resourceMode = getResourceBarMode();
        if (!barEnabled || resourceMode == "hidden")
        {
            applyResourceState(0.f);
            return;
        }

        if (resourceMode == "always")
        {
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        // Keep the relevant resource bar visible for as long as the player is
        // actively holding a weapon or has magic readied. Start the normal
        // auto-hide delay only after the weapon/spell is put away.
        if (forceVisible)
        {
            state.idleTimer = 0.f;
            state.alpha = 1.f;
            applyResourceState(1.f);
            return;
        }

        state.idleTimer += dt;

        const bool isFull = state.modified <= 0 || state.current >= state.modified;
        const float hideDelay = isFull ? 7.f : 20.f;
        const float fadeDuration = 0.35f;

        float targetAlpha = 1.f;
        if (state.idleTimer > hideDelay)
            targetAlpha = std::max(0.f, 1.f - (state.idleTimer - hideDelay) / fadeDuration);

        state.alpha = targetAlpha;
        applyResourceState(state.alpha);
    }

    void HUD::setSelectedSpell(const std::string& spellId, int successChancePercent)
    {
        const ESM::Spell* spell =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().find(spellId);

        std::string spellName = spell->mName;
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(successChancePercent);

        mSpellBox->setUserString("ToolTipType", "Spell");
        mSpellBox->setUserString("Spell", spellId);

        // use the icon of the first effect
        const ESM::MagicEffect* effect =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mEffectID);

        std::string icon = effect->mIcon;
        int slashPos = icon.rfind('\\');
        icon.insert(slashPos+1, "b_");
        icon = MWBase::Environment::get().getWindowManager()->correctIconPath(icon);

        mSpellImage->setSpellIcon(icon);
    }

    void HUD::setSelectedEnchantItem(const MWWorld::Ptr& item, int chargePercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = itemName;
            mWeaponSpellBox->setCaption(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(chargePercent);

        mSpellBox->setUserString("ToolTipType", "ItemPtr");
        mSpellBox->setUserData(MWWorld::Ptr(item));

        mSpellImage->setItem(item);
    }

    void HUD::setSelectedWeapon(const MWWorld::Ptr& item, int durabilityPercent)
    {
        std::string itemName = item.getClass().getName(item);
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaption(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "ItemPtr");
        mWeapBox->setUserData(MWWorld::Ptr(item));

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(durabilityPercent);

        mWeapImage->setItem(item);
    }

    void HUD::unsetSelectedSpell()
    {
        std::string spellName = "#{sNone}";
        if (spellName != mSpellName && mSpellVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mSpellName = spellName;
            mWeaponSpellBox->setCaptionWithReplacing(mSpellName);
            mWeaponSpellBox->setVisible(true);
        }

        mSpellStatus->setProgressRange(100);
        mSpellStatus->setProgressPosition(0);
        mSpellImage->setItem(MWWorld::Ptr());
        mSpellBox->clearUserStrings();
    }

    void HUD::unsetSelectedWeapon()
    {
        std::string itemName = "#{sSkillHandtohand}";
        if (itemName != mWeaponName && mWeaponVisible)
        {
            mWeaponSpellTimer = 5.0f;
            mWeaponName = itemName;
            mWeaponSpellBox->setCaptionWithReplacing(mWeaponName);
            mWeaponSpellBox->setVisible(true);
        }

        mWeapStatus->setProgressRange(100);
        mWeapStatus->setProgressPosition(0);

        MWBase::World *world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();

        mWeapImage->setItem(MWWorld::Ptr());
        std::string icon = (player.getClass().getNpcStats(player).isWerewolf()) ? "icons\\k\\tx_werewolf_hand.dds" : "icons\\k\\stealth_handtohand.dds";
        mWeapImage->setIcon(icon);

        mWeapBox->clearUserStrings();
        mWeapBox->setUserString("ToolTipType", "Layout");
        mWeapBox->setUserString("ToolTipLayout", "HandToHandToolTip");
        mWeapBox->setUserString("Caption_HandToHandText", itemName);
        mWeapBox->setUserString("ImageTexture_HandToHandImage", icon);
    }

    void HUD::setCrosshairVisible(bool visible)
    {
        mCrosshairBaseVisible = visible;
        mCrosshair->setVisible(visible && Settings::Manager::getBool("crosshair", "HUD"));
    }
    
    void HUD::setCrosshairOwned(bool owned)
    {
        const int size = owned ? 32 : 64;
        mCrosshair->changeWidgetSkin(owned ? "HUD_Crosshair_Owned" : "HUD_Crosshair");

        // Keep both reticles exactly centred. The ownership hand is intentionally
        // half the size of the normal crosshair and must not inherit its 64x64 box.
        mCrosshair->setCoord(
            (mMainWidget->getWidth() - size) / 2,
            (mMainWidget->getHeight() - size) / 2,
            size, size);
    }
    
    void HUD::setHmsVisible(bool visible)
    {
        mHmsBaseVisible = visible;

        mHealth->setVisible(visible);
        mMagicka->setVisible(visible);
        mStamina->setVisible(visible);

        if (!visible)
        {
            mHealthFrame->setVisible(false);
            mMagickaFrame->setVisible(false);
            mFatigueFrame->setVisible(false);
        }
        else
        {
            registerBarChange(mHealthBarState, mHealthBarState.current, mHealthBarState.modified);
            registerBarChange(mMagickaBarState, mMagickaBarState.current, mMagickaBarState.modified);
            registerBarChange(mStaminaBarState, mStaminaBarState.current, mStaminaBarState.modified);

            mHealthFrame->setVisible(true);
            mMagickaFrame->setVisible(true);
            mFatigueFrame->setVisible(true);
            applyBarAlpha(mHealthFrame, 1.f);
            applyBarAlpha(mMagickaFrame, 1.f);
            applyBarAlpha(mFatigueFrame, 1.f);
        }

        updatePositions();
    }

    void HUD::setWeapVisible(bool visible)
    {
        mWeapBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSpellVisible(bool visible)
    {
        mSpellBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setSneakVisible(bool visible)
    {
        mSneakBox->setVisible(visible);
        updatePositions();
    }

    void HUD::setEffectVisible(bool visible)
    {
        mEffectBaseVisible = visible;
        mEffectBox->setVisible(visible && Settings::Manager::getBool("show status effects", "HUD"));
        updatePositions();
    }

    void HUD::setMinimapVisible(bool visible)
    {
        mMinimapBaseVisible = visible;
        mMinimapBox->setVisible(visible && Settings::Manager::getBool("show minimap", "HUD"));
        updatePositions();
    }

    void HUD::updatePositions()
    {
        int weapDx = 0, spellDx = 0;
        if (!mHealth->getVisible())
            spellDx = weapDx = mWeapBoxBaseLeft - mHealthManaStaminaBaseLeft;

        if (!mWeapBox->getVisible())
            spellDx += mSpellBoxBaseLeft - mWeapBoxBaseLeft;

        mWeaponVisible = mWeapBox->getVisible();
        mSpellVisible = mSpellBox->getVisible();
        if (!mWeaponVisible && !mSpellVisible)
            mWeaponSpellBox->setVisible(false);

        mWeapBox->setPosition(mWeapBoxBaseLeft - weapDx, mWeapBox->getTop());
        mSpellBox->setPosition(mSpellBoxBaseLeft - spellDx, mSpellBox->getTop());

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        mSneakBox->setPosition((viewSize.width - mSneakBox->getWidth()) / 2,
                               (viewSize.height - mSneakBox->getHeight()) / 2);

        // effect box can have variable width -> variable left coordinate
        int effectsDx = 0;
        if (!mMinimapBox->getVisible ())
            effectsDx = mEffectBoxBaseRight - mMinimapBoxBaseRight;

        mMapVisible = mMinimapBox->getVisible ();
        if (!mMapVisible)
            mCellNameBox->setVisible(false);

        mEffectBox->setPosition((viewSize.width - mEffectBoxBaseRight) - mEffectBox->getWidth() + effectsDx, mEffectBox->getTop());
    }

    void HUD::setFocusObject(const MWWorld::Ptr& focus)
    {
        const bool validActor = !focus.isEmpty() && focus.getClass().isActor()
            && focus != MWMechanics::getPlayer()
            && !focus.getClass().getCreatureStats(focus).isDead();

        if (validActor)
        {
            if (mFocusActor.isEmpty() || mFocusActor != focus)
            {
                mFocusActor = focus;
                mFocusActorPanelAlpha = 0.f;
                mTargetPanelPositionInitialized = false;
            }
            mFocusActorCurrentlyFaced = true;
            mFocusActorDistance = MWBase::Environment::get().getWorld()->getDistanceToFacedObject();
        }
        else
        {
            // Keep the previous actor long enough to fade the panel out. Clearing it
            // immediately made name/health/level blink whenever the ray briefly left
            // the actor between two frames.
            mFocusActorCurrentlyFaced = false;
            mFocusActorDistance = -1.f;
        }
    }

    void HUD::setFocusObjectScreenCoords(float min_x, float min_y, float max_x, float max_y)
    {
        mFocusActorScreenX = (min_x + max_x) * 0.5f;
        mFocusActorScreenY = min_y;
    }

    void HUD::updateFocusedTargetPanel(float dt)
    {
        dt = std::max(0.f, dt);

        bool actorAlive = !mFocusActor.isEmpty();
        if (actorAlive)
        {
            actorAlive = mFocusActor.isInCell()
                && mFocusActor.getRefData().getCount() > 0
                && mFocusActor.getRefData().isEnabled()
                && !mFocusActor.getRefData().isDeleted()
                && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead();
        }

        const bool dialogueOpen = MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue);
        const bool hoverEnabled = npcBarShowsHover(getNpcBarMode());
        float desiredAlpha = 0.f;
        bool fastPeacefulPointBlankHide = false;

        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        if (actorAlive && mFocusActorCurrentlyFaced && hoverEnabled && !dialogueOpen)
        {
            // Binary visibility: the panel is either fully visible or completely
            // hidden. Keep a generous centre band for large animated actors.
            const float horizontalOffset = std::abs(mFocusActorScreenX - 0.5f);
            constexpr float centreVisibleEnd = 0.46f;
            const bool nearScreenCentre = horizontalOffset <= centreVisibleEnd;

            bool withinActivationDistance = true;
            bool hidePeacefulAtPointBlank = false;
            if (mFocusActorDistance >= 0.f)
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                const float maximumDistance = std::max(1.f, world->getMaxActivationDistance());
                withinActivationDistance = mFocusActorDistance <= maximumDistance * 1.05f;

                const MWWorld::Ptr player = MWMechanics::getPlayer();
                const MWMechanics::CreatureStats& targetStats
                    = mFocusActor.getClass().getCreatureStats(mFocusActor);
                const bool aggressiveTarget = targetStats.getAiSequence().isInCombat(player)
                    || MWBase::Environment::get().getMechanicsManager()->isAggressive(mFocusActor, player);

                // Only first-person view hides a peaceful target panel, and only
                // almost face-to-face. Third-person view and hostile targets keep
                // the panel visible at any close distance.
                constexpr float pointBlankDistance = 48.f;
                const float pointBlankLimit = std::min(pointBlankDistance, maximumDistance * 0.25f);
                hidePeacefulAtPointBlank = world->isFirstPerson()
                    && !aggressiveTarget
                    && mFocusActorDistance <= pointBlankLimit;
                fastPeacefulPointBlankHide = hidePeacefulAtPointBlank;
            }

            desiredAlpha = nearScreenCentre && withinActivationDistance
                && !hidePeacefulAtPointBlank ? 1.f : 0.f;

            // The panel remains in the upper centre and follows left/right only
            // slightly. This keeps it readable and prevents jitter from the actor's
            // animated bounding box.
            const float screenCentre = viewSize.width * 0.5f;
            const float maximumTravel = viewSize.width * 0.12f;
            const float actorOffset = (mFocusActorScreenX - 0.5f) * viewSize.width * 0.35f;
            const float desiredCentre = screenCentre
                + std::max(-maximumTravel, std::min(maximumTravel, actorOffset));

            if (!mTargetPanelPositionInitialized)
            {
                mTargetPanelCenterX = desiredCentre;
                mTargetPanelPositionInitialized = true;
            }
            else
            {
                const float positionBlend = 1.f - std::exp(-7.f * dt);
                mTargetPanelCenterX += (desiredCentre - mTargetPanelCenterX) * positionBlend;
            }
        }

        // Appearance is three times faster than before (7 -> 21). The accumulator
        // provides a tiny anti-flicker delay, but rendering below uses a hard 0/1
        // threshold, so no semi-transparent panel is ever shown.
        const float transitionSpeed = desiredAlpha > mFocusActorPanelAlpha
            ? 21.f
            : (fastPeacefulPointBlankHide ? 18.f : 2.4f);
        const float alphaBlend = 1.f - std::exp(-transitionSpeed * dt);
        mFocusActorPanelAlpha += (desiredAlpha - mFocusActorPanelAlpha) * alphaBlend;
        mFocusActorPanelAlpha = std::max(0.f, std::min(1.f, mFocusActorPanelAlpha));

        if ((!actorAlive || !mFocusActorCurrentlyFaced) && mFocusActorPanelAlpha < 0.5f)
        {
            mFocusActor = MWWorld::Ptr();
            mFocusActorPanelAlpha = 0.f;
            mTargetPanelPositionInitialized = false;
        }
    }

    void HUD::updateEnemyHealthBar()
    {
        const std::string npcBarMode = getNpcBarMode();
        const bool usingFocusActor = npcBarShowsHover(npcBarMode)
            && !mFocusActor.isEmpty()
            && !mFocusActor.getClass().getCreatureStats(mFocusActor).isDead()
            && !MWBase::Environment::get().getWindowManager()->containsMode(GM_Dialogue)
            && mFocusActorPanelAlpha >= 0.5f;

        MWWorld::Ptr enemy;
        if (usingFocusActor)
            enemy = mFocusActor;
        else if (npcBarShowsCombat(npcBarMode) && mEnemyActorId != -1)
            enemy = MWBase::Environment::get().getWorld()->searchPtrViaActorId(mEnemyActorId);

        if (enemy.isEmpty())
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        MWMechanics::CreatureStats& stats = enemy.getClass().getCreatureStats(enemy);
        if (stats.isDead() || stats.getHealth().getCurrent() <= 0.f)
        {
            mEnemyHealth->setVisible(false);
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);
            return;
        }

        const float maximumHealth = stats.getHealth().getModified();
        const float currentHealth = stats.getHealth().getCurrent();
        const int maximumHealthPoints = std::max(1, static_cast<int>(std::lround(maximumHealth)));
        const int currentHealthPoints = std::max(0, std::min(maximumHealthPoints,
            static_cast<int>(std::lround(currentHealth))));

        mEnemyHealth->setProgressRange(static_cast<size_t>(maximumHealthPoints));
        mEnemyHealth->setProgressPosition(static_cast<size_t>(currentHealthPoints));

        static const float fNPCHealthBarFade = MWBase::Environment::get().getWorld()->getStore()
            .get<ESM::GameSetting>().find("fNPCHealthBarFade")->mValue.getFloat();
        const float alpha = usingFocusActor ? 1.f
            : (fNPCHealthBarFade > 0.f
                ? std::max(0.f, std::min(1.f, mEnemyHealthTimer / fNPCHealthBarFade))
                : 1.f);
        mEnemyHealth->setAlpha(alpha);

        if (usingFocusActor)
        {
            // Faced actor: stable name/level/health panel in the upper HUD.
            mEnemyHealth->setSize(190, 16);
            if (mEnemyName)
            {
                mEnemyName->setSize(240, 20);
                mEnemyName->setCaption(enemy.getClass().getName(enemy) + "  -  Уровень "
                    + MyGUI::utility::toString(stats.getLevel()));
                mEnemyName->setAlpha(alpha);
            }
            if (mEnemySummary)
            {
                mEnemySummary->setSize(190, 16);
                mEnemySummary->setCaption(MyGUI::utility::toString(currentHealthPoints) + " / "
                    + MyGUI::utility::toString(maximumHealthPoints));
                mEnemySummary->setAlpha(alpha);
            }

            const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
            const int nameWidth = mEnemyName ? mEnemyName->getWidth() : 0;
            const int nameHeight = mEnemyName ? mEnemyName->getHeight() : 0;
            const int barWidth = mEnemyHealth->getWidth();
            const int totalWidth = std::max(nameWidth, barWidth);
            const int totalHeight = nameHeight + 2 + mEnemyHealth->getHeight();

            constexpr int targetPanelSafeMargin = 14;
            const int horizontalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.width - totalWidth) / 2));
            const int verticalMargin = std::min(targetPanelSafeMargin,
                std::max(0, (viewSize.height - totalHeight) / 2));
            const int maximumLeft = std::max(horizontalMargin,
                viewSize.width - horizontalMargin - totalWidth);
            const int maximumTop = std::max(verticalMargin,
                viewSize.height - verticalMargin - totalHeight);

            const int stableCentreX = mTargetPanelPositionInitialized
                ? static_cast<int>(std::lround(mTargetPanelCenterX))
                : viewSize.width / 2;
            const int panelLeft = std::max(horizontalMargin,
                std::min(stableCentreX - totalWidth / 2, maximumLeft));

            // Reserve both the compass and the location-name region. This keeps the
            // panel stable when the city/cell caption appears or fades out.
            int baseY = verticalMargin;
            if (mHorizontalCompass && mHorizontalCompass->getVisible())
            {
                const MyGUI::IntCoord compassCoord = mHorizontalCompass->getAbsoluteCoord();
                constexpr int targetPanelCompassGap = 7;
                baseY = std::max(baseY,
                    compassCoord.top + compassCoord.height + targetPanelCompassGap);
            }
            if (mCellNameClip)
            {
                const MyGUI::IntCoord locationCoord = mCellNameClip->getAbsoluteCoord();
                constexpr int targetPanelLocationGap = 6;
                baseY = std::max(baseY,
                    locationCoord.top + locationCoord.height + targetPanelLocationGap);
            }
            baseY = std::min(baseY, maximumTop);

            if (mEnemyName)
                mEnemyName->setPosition(panelLeft + (totalWidth - nameWidth) / 2, baseY);
            const int barLeft = panelLeft + (totalWidth - barWidth) / 2;
            mEnemyHealth->setPosition(barLeft, baseY + nameHeight + 2);
            if (mEnemySummary)
                mEnemySummary->setPosition(barLeft, baseY + nameHeight + 2);
        }
        else
        {
            // Combat feedback when the compact target panel is disabled: a thin red bar above player health.
            if (mEnemyName)
                mEnemyName->setVisible(false);
            if (mEnemySummary)
                mEnemySummary->setVisible(false);

            const MyGUI::IntCoord playerHealth = mHealth->getAbsoluteCoord();
            mEnemyHealth->setCoord(playerHealth.left, std::max(0, playerHealth.top - 9), playerHealth.width, 7);
        }
    }

    void HUD::setEnemy(const MWWorld::Ptr &enemy)
    {
        mEnemyActorId = enemy.getClass().getCreatureStats(enemy).getActorId();
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
        mEnemyHealthTimer = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fNPCHealthBarTime")->mValue.getFloat();
        const bool showCombatBar = npcBarShowsCombat(getNpcBarMode());
        mEnemyHealth->setVisible(showCombatBar);
        if (showCombatBar)
            updateEnemyHealthBar();
    }

    void HUD::resetEnemy()
    {
        mEnemyActorId = -1;
        mEnemyHealthTimer = -1;
        if (mEnemyName) mEnemyName->setVisible(false);
        if (mEnemySummary) mEnemySummary->setVisible(false);
    }

    void HUD::clear()
    {
        unsetSelectedSpell();
        unsetSelectedWeapon();
        resetEnemy();
        mFocusActor = MWWorld::Ptr();
        mFocusActorCurrentlyFaced = false;
        mFocusActorDistance = -1.f;
        mFocusActorPanelAlpha = 0.f;
        mTargetPanelPositionInitialized = false;
        if (mEnemyName)
            mEnemyName->setVisible(false);
        if (mEnemySummary)
            mEnemySummary->setVisible(false);
    }

    void HUD::customMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

    void HUD::doorMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseButtonClick += MyGUI::newDelegate(this, &HUD::onMapClicked);
    }

}
