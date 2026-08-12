#include "companionwindow.hpp"

#include <algorithm>
#include <cmath>

#include <MyGUI_InputManager.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/class.hpp"

#include "messagebox.hpp"
#include "inventorywindow.hpp"
#include "itemview.hpp"
#include "sortfilteritemmodel.hpp"
#include "companionitemmodel.hpp"
#include "draganddrop.hpp"
#include "countdialog.hpp"
#include "widgets.hpp"
#include "tooltips.hpp"

namespace
{

    int getProfit(const MWWorld::Ptr& actor)
    {
        std::string script = actor.getClass().getScript(actor);
        if (!script.empty())
        {
            return actor.getRefData().getLocals().getIntVar(script, "minimumprofit");
        }
        return 0;
    }

}

namespace MWGui
{

CompanionWindow::CompanionWindow(DragAndDrop *dragAndDrop, MessageBoxManager* manager)
    : WindowBase("openmw_companion_window.layout")
    , mSortModel(nullptr)
    , mModel(nullptr)
    , mSelectedItem(-1)
    , mDragAndDrop(dragAndDrop)
    , mMessageBoxManager(manager)
{
    getWidget(mCloseButton, "CloseButton");
    getWidget(mFilterAll, "AllButton");
    getWidget(mFilterWeapon, "WeaponButton");
    getWidget(mFilterApparel, "ApparelButton");
    getWidget(mFilterMagic, "MagicButton");
    getWidget(mFilterMisc, "MiscButton");
    getWidget(mProfitLabel, "ProfitLabel");
    getWidget(mEncumbranceBar, "EncumbranceBar");
    getWidget(mFilterEdit, "FilterEdit");
    getWidget(mItemView, "ItemView");
    mItemView->setExtendedMode(true);
    mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &CompanionWindow::onBackgroundSelected);
    mItemView->eventItemClicked += MyGUI::newDelegate(this, &CompanionWindow::onItemSelected);
    mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &CompanionWindow::onItemDragStarted);
    mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &CompanionWindow::onItemDoubleClicked);
    mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &CompanionWindow::onNameFilterChanged);
    mFilterAll->setStateSelected(true);
    mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);
    mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onFilterChanged);

    mCloseButton->eventMouseButtonClick += MyGUI::newDelegate(this, &CompanionWindow::onCloseButtonClicked);

    setCoord(160, 20, 680, 380);
}

void CompanionWindow::onItemSelected(int index)
{
    if (mDragAndDrop->mIsOnDragAndDrop)
    {
        mDragAndDrop->drop(mModel, mItemView);
        updateEncumbranceBar();
        return;
    }

    const ItemStack& item = mSortModel->getItem(index);

    // We can't take conjured items from a companion NPC
    if (item.mFlags & ItemStack::Flag_Bound)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    MWWorld::Ptr object = item.mBase;
    int count = item.mCount;
    bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
    if (MyGUI::InputManager::getInstance().isControlPressed())
        count = 1;

    mSelectedItem = mSortModel->mapToSource(index);

    if (count > 1 && !shift)
    {
        CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
        std::string name = object.getClass().getName(object) + MWGui::ToolTips::getSoulString(object.getCellRef());
        dialog->openCountDialog(name, "#{sTake}", count);
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked += MyGUI::newDelegate(this, &CompanionWindow::dragItem);
    }
    else
        dragItem (nullptr, count);
}

void CompanionWindow::onNameFilterChanged(MyGUI::EditBox* _sender)
    {
        mSortModel->setNameFilter(_sender->getCaption());
        mItemView->update();
    }

void CompanionWindow::onFilterChanged(MyGUI::Widget* sender)
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

    mFilterAll->setStateSelected(sender == mFilterAll);
    mFilterWeapon->setStateSelected(sender == mFilterWeapon);
    mFilterApparel->setStateSelected(sender == mFilterApparel);
    mFilterMagic->setStateSelected(sender == mFilterMagic);
    mFilterMisc->setStateSelected(sender == mFilterMisc);
    mItemView->update();
    mItemView->resetScrollBars();
}

void CompanionWindow::onItemDragStarted(int index)
{
    if (!mSortModel || !mModel || mDragAndDrop->mIsOnDragAndDrop)
        return;

    const ItemStack& item = mSortModel->getItem(index);
    if (item.mFlags & ItemStack::Flag_Bound)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;
    mSelectedItem = mSortModel->mapToSource(index);
    mDragAndDrop->startDrag(mSelectedItem, mSortModel, mModel, mItemView, count);
}

void CompanionWindow::onItemDoubleClicked(int index)
{
    if (!mSortModel || !mModel || mDragAndDrop->mIsOnDragAndDrop)
        return;

    const ItemStack item = mSortModel->getItem(index);
    if (item.mFlags & ItemStack::Flag_Bound)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    const int count = MyGUI::InputManager::getInstance().isControlPressed() ? 1 : item.mCount;
    const int sourceIndex = mSortModel->mapToSource(index);
    ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
    const std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
    MWBase::Environment::get().getWindowManager()->playSound(sound);
    mModel->moveItem(mModel->getItem(sourceIndex), count, playerModel);
    mModel->update();
    mItemView->update();
    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
    updateEncumbranceBar();
}

void CompanionWindow::dragItem(MyGUI::Widget* sender, int count)
{
    mDragAndDrop->startDrag(mSelectedItem, mSortModel, mModel, mItemView, count);
}

void CompanionWindow::onBackgroundSelected()
{
    if (mDragAndDrop->mIsOnDragAndDrop)
    {
        mDragAndDrop->drop(mModel, mItemView);
        updateEncumbranceBar();
    }
}

void CompanionWindow::setPtr(const MWWorld::Ptr& npc)
{
    mPtr = npc;
    updateEncumbranceBar();

    mModel = new CompanionItemModel(npc);
    mSortModel = new SortFilterItemModel(mModel);
    mFilterEdit->setCaption(std::string());
    mSortModel->setCategory(SortFilterItemModel::Category_All);
    mFilterAll->setStateSelected(true);
    mFilterWeapon->setStateSelected(false);
    mFilterApparel->setStateSelected(false);
    mFilterMagic->setStateSelected(false);
    mFilterMisc->setStateSelected(false);
    mItemView->setModel(mSortModel);
    mItemView->resetScrollBars();
    mDragAndDrop->setTransferTargetView(mItemView);

    setTitle(npc.getClass().getName(npc));
}

void CompanionWindow::onFrame(float dt)
{
    checkReferenceAvailable();
    updateEncumbranceBar();
}

void CompanionWindow::updateEncumbranceBar()
{
    if (mPtr.isEmpty())
        return;
    float capacity = mPtr.getClass().getCapacity(mPtr);
    float encumbrance = mPtr.getClass().getEncumbrance(mPtr);
    mEncumbranceBar->setValue(std::ceil(encumbrance), static_cast<int>(capacity));

    if (mModel && mModel->hasProfit(mPtr))
    {
        mProfitLabel->setCaptionWithReplacing("#{sProfitValue} " + MyGUI::utility::toString(getProfit(mPtr)));
    }
    else
        mProfitLabel->setCaption("");
}

void CompanionWindow::onCloseButtonClicked(MyGUI::Widget* _sender)
{
    if (exit())
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
}

bool CompanionWindow::exit()
{
    if (mModel && mModel->hasProfit(mPtr) && getProfit(mPtr) < 0)
    {
        std::vector<std::string> buttons;
        buttons.emplace_back("#{sCompanionWarningButtonOne}");
        buttons.emplace_back("#{sCompanionWarningButtonTwo}");
        mMessageBoxManager->createInteractiveMessageBox("#{sCompanionWarningMessage}", buttons);
        mMessageBoxManager->eventButtonPressed += MyGUI::newDelegate(this, &CompanionWindow::onMessageBoxButtonClicked);
        return false;
    }
    return true;
}

void CompanionWindow::onMessageBoxButtonClicked(int button)
{
    if (button == 0)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
        // Important for Calvus' contract script to work properly
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }
}

void CompanionWindow::onReferenceUnavailable()
{
    MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Companion);
}

void CompanionWindow::resetReference()
{
    mDragAndDrop->clearTransferTargetView(mItemView);
    ReferenceInterface::resetReference();
    mItemView->setModel(nullptr);
    mModel = nullptr;
    mSortModel = nullptr;
}


}
