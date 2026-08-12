#ifndef MWGUI_DIALOGE_H
#define MWGUI_DIALOGE_H

#include "windowbase.hpp"
#include "referenceinterface.hpp"

#include "bookpage.hpp"

#include "../mwdialogue/keywordsearch.hpp"

#include <MyGUI_Delegate.h>
#include <MyGUI_KeyCode.h>
#include <MyGUI_Types.h>

namespace Gui
{
    class MWList;
}

namespace MWGui
{
    class ResponseCallback;

    struct Link
    {
        virtual ~Link() {}
        virtual void activated () = 0;
    };

    struct Topic : Link
    {
        typedef MyGUI::delegates::CMultiDelegate1<const std::string&> EventHandle_TopicId;
        EventHandle_TopicId eventTopicActivated;
        Topic(const std::string& id) : mTopicId(id) {}
        std::string mTopicId;
        void activated () override;
    };

    struct Choice : Link
    {
        typedef MyGUI::delegates::CMultiDelegate1<int> EventHandle_ChoiceId;
        EventHandle_ChoiceId eventChoiceActivated;
        Choice(int id) : mChoiceId(id) {}
        int mChoiceId;
        void activated () override;
    };

    struct Goodbye : Link
    {
        typedef MyGUI::delegates::CMultiDelegate0 Event_Activated;
        Event_Activated eventActivated;
        void activated () override;
    };

    typedef MWDialogue::KeywordSearch <std::string, intptr_t> KeywordSearchT;

    struct DialogueText
    {
        virtual ~DialogueText() {}
        virtual void write (BookTypesetter::Ptr typesetter, KeywordSearchT* keywordSearch, std::map<std::string, Link*>& topicLinks) const = 0;
        std::string mText;
    };

    struct Response : DialogueText
    {
        Response(const std::string& text, const std::string& title = "", bool needMargin = true);
        void write (BookTypesetter::Ptr typesetter, KeywordSearchT* keywordSearch, std::map<std::string, Link*>& topicLinks) const override;
        void addTopicLink (BookTypesetter::Ptr typesetter, intptr_t topicId, size_t begin, size_t end) const;
        std::string mTitle;
        bool mNeedMargin;
    };

    struct Message : DialogueText
    {
        Message(const std::string& text);
        void write (BookTypesetter::Ptr typesetter, KeywordSearchT* keywordSearch, std::map<std::string, Link*>& topicLinks) const override;
    };

    class DialogueWindow: public WindowBase, public ReferenceInterface
    {
    public:
        DialogueWindow();
        ~DialogueWindow();

        void onTradeComplete();

        bool exit() override;
        bool handleKeyPress(MyGUI::KeyCode key, bool repeat);
        void onOpen() override;
        void onResChange(int width, int height) override;

        // Events
        typedef MyGUI::delegates::CMultiDelegate0 EventHandle_Void;

        void notifyLinkClicked (TypesetBook::InteractiveId link);

        /// Actor currently shown by the dialogue window. Used by the dynamic
        /// actor animation system to avoid applying ambient idles to them.
        MWWorld::Ptr getPtr();
        void setPtr(const MWWorld::Ptr& actor) override;

        /// @return true if stale keywords were updated successfully
        bool setKeywords(std::list<std::string> keyWord);

        void addResponse (const std::string& title, const std::string& text, bool needMargin = true);

        void addMessageBox(const std::string& text);

        void onFrame(float dt) override;
        void clear() override { mPersuasionMode = false; stopDynamicDialogueActor(); stopDialogueCamera(); resetReference(); }

        void updateTopics();

        void onClose() override;

    protected:
        void updateTopicsPane();
        bool isCompanion(const MWWorld::Ptr& actor);
        bool isCompanion();

        void onSelectListItem(const std::string& topic, int id);
        void onChoiceListItem(const std::string& choice, int id);
        void onByeClicked(MyGUI::Widget* _sender);
        void onNavigateUp(MyGUI::Widget* sender);
        void onNavigateDown(MyGUI::Widget* sender);
        void onNavigateSelect(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* _sender, int _rel);
        void onHistoryDragStart(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onHistoryDrag(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void onWindowResize(MyGUI::Window* _sender);
        void onTopicActivated(const std::string& topicId);
        void onChoiceActivated(int id);
        void onGoodbyeActivated();

        void onScrollbarMoved (MyGUI::ScrollBar* sender, size_t pos);

        void updateHistory(bool scrollbar=false);

        void onReferenceUnavailable() override;

    private:
        void updateDisposition();
        void updateActorStatus();
        void restock();
        void deleteLater();
        void updateChoicePane();
        void openPersuasionPane();
        void closePersuasionPane();
        void performPersuasion(int index);
        void rebuildPersuasionChoices();
        bool moveSelection(int direction);
        bool activateSelection();
        void selectInitialItem();
        void positionDialogueWindow();
        void startDialogueCamera();
        void stopDialogueCamera();
        void startDynamicDialogueActor();
        void updateDynamicDialogueActor(float dt);
        void stopDynamicDialogueActor();
        void playDynamicDialogueAnimation(bool speaking, bool force = false);

        bool mIsCompanion;
        std::list<std::string> mKeywords;

        std::vector<DialogueText*> mHistoryContents;
        std::vector<std::pair<std::string, int> > mChoices;
        std::vector<int> mPersuasionChoices;
        bool mGoodbye;
        bool mPersuasionMode;

        std::vector<Link*> mLinks;
        std::map<std::string, Link*> mTopicLinks;

        std::vector<Link*> mDeleteLater;

        KeywordSearchT mKeywordSearch;

        BookPage* mHistory;
        Gui::MWList* mChoicesList;
        Gui::MWList* mTopicsList;
        MyGUI::ScrollBar* mScrollBar;
        MyGUI::TextBox* mNpcName;
        MyGUI::ProgressBar* mNpcHealthBar;
        MyGUI::TextBox* mNpcHealthText;
        MyGUI::TextBox* mChoicesLabel;
        MyGUI::TextBox* mTopicsLabel;
        MyGUI::ProgressBar* mDispositionBar;
        MyGUI::TextBox*     mDispositionText;
        MyGUI::Button* mGoodbyeButton;
        MyGUI::Button* mUpButton;
        MyGUI::Button* mDownButton;
        MyGUI::Button* mSelectButton;

        MyGUI::IntSize mCurrentWindowSize;
        MyGUI::IntPoint mHistoryDragStart;
        MyGUI::IntPoint mHistoryLastDragPosition;
        bool mHistoryWasDragged;
        bool mDialogueCameraActive;
        bool mDynamicDialogueActorActive;
        bool mDynamicDialogueActorHasOriginalYaw;
        float mDynamicDialogueActorOriginalYaw;
        float mDynamicDialogueActorAnimationTimer;
        float mDynamicDialogueActorTransitionTimer;
        float mDynamicDialogueActorSpeechCooldown;
        bool mDynamicDialogueActorAnimationEnding;
        bool mDynamicDialogueActorPendingSpeaking;
        bool mDynamicDialogueActorWasSpeaking;
        bool mDynamicDialogueActorLeftArmProtected;
        std::string mDynamicDialogueActorAnimation;

        std::unique_ptr<ResponseCallback> mCallback;
        std::unique_ptr<ResponseCallback> mGreetingCallback;

        void updateTopicFormat();
    };
}
#endif
