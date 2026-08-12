#include "container.hpp"

#include <algorithm>
#include <MyGUI_InputManager.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>

#include <cmath>



#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "countdialog.hpp"
#include "inventorywindow.hpp"

#include "itemview.hpp"
#include "inventoryitemmodel.hpp"
#include "containeritemmodel.hpp"
#include "sortfilteritemmodel.hpp"
#include "pickpocketitemmodel.hpp"
#include "draganddrop.hpp"
#include "tooltips.hpp"
#include "widgets.hpp"

namespace MWGui
{

    ContainerWindow::ContainerWindow(DragAndDrop* dragAndDrop)
        : WindowBase("openmw_container_window.layout")
        , mDragAndDrop(dragAndDrop)
        , mSortModel(nullptr)
        , mModel(nullptr)
        , mSelectedItem(-1)
    {
        getWidget(mTakeButton, "TakeButton");
        getWidget(mCloseButton, "CloseButton");
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");
        getWidget(mFilterKeys, "KeysButton");
        getWidget(mFilterEdit, "FilterEdit");
        getWidget(mEncumbranceBar, "EncumbranceBar");
        getWidget(mBottomBar, "BottomBar");

        getWidget(mItemView, "ItemView");
        mItemView->setExtendedMode(true);
        mItemView->setSingleClickActionEnabled(true);
        mItemView->setInternalViewModeButtonVisible(false);
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &ContainerWindow::onBackgroundSelected);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &ContainerWindow::onItemSelected);
        mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &ContainerWindow::onItemDragStarted);
        mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &ContainerWindow::onItemDoubleClicked);

        mViewModeButton = mBottomBar->createWidget<MyGUI::Button>("MW_Button",
            MyGUI::IntCoord(126, 2, 30, 24), MyGUI::Align::Left | MyGUI::Align::Top, "ContainerViewModeButton");
        mViewModeButton->setCaption("");
        mViewModeIcon = mViewModeButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(6, 3, 18, 18), MyGUI::Align::Center, "ContainerViewModeIcon");
        mViewModeIcon->setNeedMouseFocus(false);
        mViewModeIcon->setColour(MyGUI::Colour(0.93f, 0.82f, 0.58f));
        mViewModeIcon->setImageTexture("icons/inventoryextender/Base/view_grid.dds");

        mFilterAll->setStateSelected(true);
        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mFilterKeys->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onFilterChanged);
        mViewModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onViewModeClicked);
        mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onCloseButtonClicked);
        mTakeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ContainerWindow::onTakeAllButtonClicked);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &ContainerWindow::onNameFilterChanged);

        updateBottomBarLayout();

        setCoord(160, 20, 680, 380);
    }


    void ContainerWindow::onItemSelected(int index)
    {
        if (!mSortModel || !mModel || index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        if (mDragAndDrop->mIsOnDragAndDrop)
        {
            dropItem();
            return;
        }

        const ItemStack item = mSortModel->getItem(index);

        // We can't take a conjured item from a container (some NPC we're pickpocketing, a box, etc)
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sContentsMessage1}");
            return;
        }

        const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;
        const int sourceIndex = mSortModel->mapToSource(index);
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(mModel->getItemCount()))
            return;
        if (!onTakeItem(mModel->getItem(sourceIndex), count))
            return;

        // One click now performs the transfer immediately. Drag-and-drop still
        // uses onItemDragStarted, while Ctrl+click transfers a single unit.
        ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
        const std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
        MWBase::Environment::get().getWindowManager()->playSound(sound);
        mModel->moveItem(mModel->getItem(sourceIndex), count, playerModel);
        mModel->update();
        mItemView->update();
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
        updateEncumbranceBar();
    }

    void ContainerWindow::onItemDragStarted(int index)
    {
        if (!mSortModel || !mModel || mDragAndDrop->mIsOnDragAndDrop)
            return;

        const ItemStack& item = mSortModel->getItem(index);
        if (item.mFlags & ItemStack::Flag_Bound)
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sContentsMessage1}");
            return;
        }

        const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;
        mSelectedItem = mSortModel->mapToSource(index);
        if (!onTakeItem(mModel->getItem(mSelectedItem), count))
            return;
        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mModel, mItemView, count);
    }

    void ContainerWindow::onItemDoubleClicked(int index)
    {
        (void)index;
    }

    void ContainerWindow::dragItem(MyGUI::Widget* sender, int count)
    {
        if (!mModel)
            return;

        if (!onTakeItem(mModel->getItem(mSelectedItem), count))
            return;

        mDragAndDrop->startDrag(mSelectedItem, mSortModel, mModel, mItemView, count);
    }

    void ContainerWindow::dropItem()
    {
        if (!mModel)
            return;

        bool success = mModel->onDropItem(mDragAndDrop->mItem.mBase, mDragAndDrop->mDraggedCount);

        if (success)
            mDragAndDrop->drop(mModel, mItemView);
    }

    void ContainerWindow::onBackgroundSelected()
    {
        if (mDragAndDrop->mIsOnDragAndDrop)
            dropItem();
    }

    void ContainerWindow::setPtr(const MWWorld::Ptr& container)
    {
        mPtr = container;

        bool loot = mPtr.getClass().isActor() && mPtr.getClass().getCreatureStats(mPtr).isDead();

        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            if (mPtr.getClass().isNpc() && !loot)
            {
                // we are stealing stuff
                mModel = new PickpocketItemModel(mPtr, new InventoryItemModel(container),
                                                 !mPtr.getClass().getCreatureStats(mPtr).getKnockedDown());
            }
            else
                mModel = new InventoryItemModel(container);
        }
        else
        {
            mModel = new ContainerItemModel(container);
        }

        mSortModel = new SortFilterItemModel(mModel);
        mFilterEdit->setCaption("");
        mSortModel->setNameFilter("");
        mSortModel->setCategory(SortFilterItemModel::Category_All);
        mFilterAll->setStateSelected(true);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);
        mFilterKeys->setStateSelected(false);

        mItemView->setModel (mSortModel);
        mItemView->setViewMode(ItemView::View_List);
        if (mViewModeButton)
            mViewModeButton->setStateSelected(true);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture("icons/inventoryextender/Base/view_grid.dds");
        updateBottomBarLayout();
        mItemView->resetScrollBars();
        mDragAndDrop->setTransferTargetView(mItemView);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        setTitle(container.getClass().getName(container));
        updateEncumbranceBar();
    }

    void ContainerWindow::resetReference()
    {
        mDragAndDrop->clearTransferTargetView(mItemView);
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mModel = nullptr;
        mSortModel = nullptr;
    }

    void ContainerWindow::onFrame(float dt)
    {
        (void)dt;
        checkReferenceAvailable();
        if (!mPtr.isEmpty())
            updateEncumbranceBar();
        updateBottomBarLayout();
    }

    void ContainerWindow::onClose()
    {
        WindowBase::onClose();

        // Make sure the window was actually closed and not temporarily hidden.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Container))
            return;

        if (mModel)
            mModel->onClose();

        if (!mPtr.isEmpty())
            MWBase::Environment::get().getMechanicsManager()->onClose(mPtr);
        resetReference();
    }

    void ContainerWindow::onCloseButtonClicked(MyGUI::Widget* _sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onTakeAllButtonClicked(MyGUI::Widget* _sender)
    {
        if(mDragAndDrop != nullptr && mDragAndDrop->mIsOnDragAndDrop)
            return;

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mCloseButton);

        // transfer everything into the player's inventory
        ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
        assert(mModel);
        mModel->update();

        // unequip all items to avoid unequipping/reequipping
        if (mPtr.getClass().hasInventoryStore(mPtr))
        {
            MWWorld::InventoryStore& invStore = mPtr.getClass().getInventoryStore(mPtr);
            for (size_t i=0; i<mModel->getItemCount(); ++i)
            {
                const ItemStack& item = mModel->getItem(i);
                if (invStore.isEquipped(item.mBase) == false)
                    continue;

                invStore.unequipItem(item.mBase, mPtr);
            }
        }

        mModel->update();

        for (size_t i=0; i<mModel->getItemCount(); ++i)
        {
            if (i==0)
            {
                // play the sound of the first object
                MWWorld::Ptr item = mModel->getItem(i).mBase;
                std::string sound = item.getClass().getUpSoundId(item);
                MWBase::Environment::get().getWindowManager()->playSound(sound);
            }

            const ItemStack& item = mModel->getItem(i);

            if (!onTakeItem(item, item.mCount))
                break;

            mModel->moveItem(item, item.mCount, playerModel);
        }

        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onReferenceUnavailable()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Container);
    }

    void ContainerWindow::onNameFilterChanged(MyGUI::EditBox* sender)
    {
        if (!mSortModel)
            return;

        mSortModel->setNameFilter(sender->getCaption());
        mItemView->update();
        mItemView->resetScrollBars();
    }

    void ContainerWindow::onFilterChanged(MyGUI::Widget* sender)
    {
        if (!mSortModel)
            return;

        if (sender == mFilterAll)
            mSortModel->setCategory(SortFilterItemModel::Category_All);
        else if (sender == mFilterWeapon)
            mSortModel->setCategory(SortFilterItemModel::Category_Weapon);
        else if (sender == mFilterApparel)
            mSortModel->setCategory(SortFilterItemModel::Category_Apparel);
        else if (sender == mFilterMagic)
            mSortModel->setCategory(SortFilterItemModel::Category_Magic);
        else if (sender == mFilterMisc)
            mSortModel->setCategory(SortFilterItemModel::Category_Misc);
        else if (sender == mFilterKeys)
            mSortModel->setCategory(SortFilterItemModel::Category_Keys);

        mFilterAll->setStateSelected(sender == mFilterAll);
        mFilterWeapon->setStateSelected(sender == mFilterWeapon);
        mFilterApparel->setStateSelected(sender == mFilterApparel);
        mFilterMagic->setStateSelected(sender == mFilterMagic);
        mFilterMisc->setStateSelected(sender == mFilterMisc);
        mFilterKeys->setStateSelected(sender == mFilterKeys);
        mItemView->update();
        mItemView->resetScrollBars();
    }

    void ContainerWindow::onViewModeClicked(MyGUI::Widget* sender)
    {
        (void)sender;
        if (!mItemView)
            return;

        ItemView::ViewMode mode = mItemView->getViewMode();
        mItemView->setViewMode(mode == ItemView::View_List ? ItemView::View_Grid : ItemView::View_List);

        if (mViewModeButton)
            mViewModeButton->setStateSelected(mItemView->getViewMode() == ItemView::View_List);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture(std::string("icons/inventoryextender/Base/")
                + (mItemView->getViewMode() == ItemView::View_List ? "view_grid.dds" : "view_table.dds"));
    }

    void ContainerWindow::updateBottomBarLayout()
    {
        if (!mBottomBar || !mEncumbranceBar || !mFilterEdit || !mViewModeButton || !mTakeButton || !mCloseButton)
            return;

        const int width = mBottomBar->getWidth();
        const int gap = 6;
        const int encWidth = std::max(90, std::min(120, width / 5));
        const int buttonW = 30;
        const int closeW = std::max(48, mCloseButton->getWidth());
        const int takeW = std::max(86, mTakeButton->getWidth());

        int x = 0;
        mEncumbranceBar->setCoord(x, 2, encWidth, 24);
        x += encWidth + gap;

        mViewModeButton->setCoord(x, 2, buttonW, 24);
        x += buttonW + gap;

        const int rightButtonsWidth = takeW + gap + closeW;
        const int filterWidth = std::max(80, width - x - gap - rightButtonsWidth);
        mFilterEdit->setCoord(x, 2, filterWidth, 24);
        x += filterWidth + gap;

        mTakeButton->setCoord(x, 2, takeW, 24);
        x += takeW + gap;
        mCloseButton->setCoord(x, 2, closeW, 24);
    }

    void ContainerWindow::updateEncumbranceBar()
    {
        if (mPtr.isEmpty() || !mEncumbranceBar)
            return;

        float capacity = mPtr.getClass().getCapacity(mPtr);
        float encumbrance = mPtr.getClass().getEncumbrance(mPtr);
        mEncumbranceBar->setValue(std::ceil(encumbrance), static_cast<int>(capacity));
    }

    bool ContainerWindow::onTakeItem(const ItemStack &item, int count)
    {
        return mModel->onTakeItem(item.mBase, count);
    }

}
