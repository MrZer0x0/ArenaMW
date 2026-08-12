#ifndef OPENMW_MWGUI_DRAGANDDROP_H
#define OPENMW_MWGUI_DRAGANDDROP_H

#include "itemmodel.hpp"

namespace MyGUI
{
    class Widget;
}

namespace MWGui
{

    class ItemView;
    class SortFilterItemModel;

    class DragAndDrop
    {
    public:
        bool mIsOnDragAndDrop;
        MyGUI::Widget* mDraggedWidget;
        ItemModel* mSourceModel;
        ItemView* mSourceView;
        SortFilterItemModel* mSourceSortModel;
        ItemStack mItem;
        int mDraggedCount;
        int mSourceIndex;

        enum DragMode
        {
            Drag_Normal,
            Drag_BarterPreview
        };

        DragAndDrop();

        void startDrag (int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView, int count);
        /// Visual-only barter drag. The item is only staged into the transaction
        /// when released over the opposite barter ItemView.
        void startBarterDrag(int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView, int count);
        bool isBarterDrag() const { return mDragMode == Drag_BarterPreview; }
        void setTransferTargetView(ItemView* view) { mTransferTargetView = view; }
        void clearTransferTargetView(ItemView* view) { if (mTransferTargetView == view) mTransferTargetView = nullptr; }
        ItemView* getTransferTargetView() const { return mTransferTargetView; }
        void drop (ItemModel* targetModel, ItemView* targetView);
        void onFrame();

        void finish();

    private:
        DragMode mDragMode;
        ItemView* mTransferTargetView;
        void createDragWidget();
    };

}

#endif
