#include "draganddrop.hpp"

#include <MyGUI_Gui.h>
#include <MyGUI_ControllerManager.h>

#include "../mwbase/windowmanager.hpp"
#include "../mwbase/environment.hpp"

#include "../mwworld/class.hpp"

#include "sortfilteritemmodel.hpp"
#include "inventorywindow.hpp"
#include "itemwidget.hpp"
#include "itemview.hpp"
#include "controllers.hpp"

namespace MWGui
{


DragAndDrop::DragAndDrop()
    : mIsOnDragAndDrop(false)
    , mDraggedWidget(nullptr)
    , mSourceModel(nullptr)
    , mSourceView(nullptr)
    , mSourceSortModel(nullptr)
    , mDraggedCount(0)
    , mSourceIndex(-1)
    , mDragMode(Drag_Normal)
    , mTransferTargetView(nullptr)
{
}

void DragAndDrop::createDragWidget()
{
    std::string sound = mItem.mBase.getClass().getUpSoundId(mItem.mBase);
    MWBase::Environment::get().getWindowManager()->playSound(sound);

    if (mSourceSortModel)
    {
        mSourceSortModel->clearDragItems();
        mSourceSortModel->addDragItem(mItem.mBase, mDraggedCount);
    }

    ItemWidget* baseWidget = MyGUI::Gui::getInstance().createWidget<ItemWidget>(
        "MW_ItemIcon", 0, 0, 42, 42, MyGUI::Align::Default, "DragAndDrop");

    Controllers::ControllerFollowMouse* controller =
        MyGUI::ControllerManager::getInstance().createItem(Controllers::ControllerFollowMouse::getClassTypeName())
        ->castType<Controllers::ControllerFollowMouse>();
    MyGUI::ControllerManager::getInstance().addItem(baseWidget, controller);

    mDraggedWidget = baseWidget;
    baseWidget->setItem(mItem.mBase);
    baseWidget->setNeedMouseFocus(false);
    baseWidget->setCount(mDraggedCount);

    // Do not rebuild the source ItemView here. In list mode the pressed row must
    // remain alive until MouseButtonReleased so hold-drag-release can auto-drop
    // into the ItemView under the cursor. Views are refreshed on finish/drop.
    MWBase::Environment::get().getWindowManager()->setDragDrop(true);
    mIsOnDragAndDrop = true;
}

void DragAndDrop::startDrag(int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView, int count)
{
    mDragMode = Drag_Normal;
    mSourceIndex = index;
    mItem = sourceModel->getItem(index);
    mDraggedCount = count;
    mSourceModel = sourceModel;
    mSourceView = sourceView;
    mSourceSortModel = sortModel;

    // Vanilla/OpenMW semantics: an item picked up from a non-player inventory is
    // temporarily moved into the player backend immediately. This is required by
    // several quests; dropping it back moves it out again.
    ItemModel* playerModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getModel();
    if (mSourceModel != playerModel)
    {
        MWWorld::Ptr item = mSourceModel->moveItem(mItem, mDraggedCount, playerModel);
        playerModel->update();

        ItemModel::ModelIndex newIndex = -1;
        for (unsigned int i = 0; i < playerModel->getItemCount(); ++i)
        {
            if (playerModel->getItem(i).mBase == item)
            {
                newIndex = i;
                break;
            }
        }
        mItem = playerModel->getItem(newIndex);
        mSourceModel = playerModel;
        mSourceSortModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getSortFilterModel();
    }

    createDragWidget();
}

void DragAndDrop::startBarterDrag(int index, SortFilterItemModel* sortModel, ItemModel* sourceModel,
    ItemView* sourceView, int count)
{
    mDragMode = Drag_BarterPreview;
    mSourceIndex = index;
    mItem = sourceModel->getItem(index);
    mDraggedCount = count;
    mSourceModel = sourceModel;
    mSourceView = sourceView;
    mSourceSortModel = sortModel;
    createDragWidget();
}

void DragAndDrop::drop(ItemModel *targetModel, ItemView *targetView)
{
    if (mDragMode == Drag_BarterPreview)
        return;

    std::string sound = mItem.mBase.getClass().getDownSoundId(mItem.mBase);
    MWBase::Environment::get().getWindowManager()->playSound(sound);

    // We can't drop a conjured item to the ground; the target container should always be the source container
    if (mItem.mFlags & ItemStack::Flag_Bound && targetModel != mSourceModel)
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sBarterDialog12}");
        return;
    }

    // If item is dropped where it was taken from, we don't need to do anything -
    // otherwise, do the transfer
    if (targetModel != mSourceModel)
    {
        mSourceModel->moveItem(mItem, mDraggedCount, targetModel);
    }

    mSourceModel->update();

    finish();
    if (targetView)
        targetView->update();

    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();

    // We need to update the view since an other item could be auto-equipped.
    mSourceView->update();
}

void DragAndDrop::onFrame()
{
    if (mIsOnDragAndDrop && mItem.mBase.getRefData().getCount() == 0)
        finish();
}

void DragAndDrop::finish()
{
    mIsOnDragAndDrop = false;
    if (mSourceSortModel)
        mSourceSortModel->clearDragItems();

    MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
    if (mSourceView)
        mSourceView->update();

    if (mDraggedWidget)
        MyGUI::Gui::getInstance().destroyWidget(mDraggedWidget);
    mDraggedWidget = nullptr;
    mSourceIndex = -1;
    mDragMode = Drag_Normal;
    MWBase::Environment::get().getWindowManager()->setDragDrop(false);
}

}
