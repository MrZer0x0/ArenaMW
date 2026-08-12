#ifndef MWGUI_SETTINGS_H
#define MWGUI_SETTINGS_H

#include "windowbase.hpp"

#include <string>
#include <vector>

namespace MWGui
{
    class SettingsWindow : public WindowBase
    {
        public:
            SettingsWindow();

            void onOpen() override;

            void updateControlsBox();

            void updateLightSettings();

            void onResChange(int, int) override { center(); }

    protected:
            MyGUI::TabControl* mSettingsTab;
            MyGUI::ListBox* mSectionList;
            MyGUI::ScrollView* mInterfaceScroll;
            MyGUI::ScrollView* mHudScroll;
            MyGUI::ScrollView* mDisplayScroll;
            MyGUI::ScrollView* mWaterScroll;
            MyGUI::ScrollView* mPbrScroll;
            MyGUI::ScrollView* mEffectsScroll;
            MyGUI::ScrollView* mHdrScroll;
            MyGUI::Button* mOkButton;

            // graphics
            MyGUI::ListBox* mResolutionList;
            MyGUI::Button* mFullscreenButton;
            MyGUI::Button* mWindowBorderButton;
            MyGUI::ComboBox* mTextureFilteringButton;
            MyGUI::Widget* mAnisotropyBox;

            MyGUI::ComboBox* mWaterTextureSize;
            MyGUI::ComboBox* mWaterReflectionDetail;
            MyGUI::Button* mWaterResetButton;

            MyGUI::ComboBox* mMaxLights;
            MyGUI::ComboBox* mLightingMethodButton;
            std::vector<std::string> mLightingMethodValues;
            MyGUI::Button* mLightsResetButton;
            MyGUI::Button* mPbrResetButton;
            MyGUI::ComboBox* mHdrTonemapper;
            MyGUI::Button* mHdrResetButton;

            MyGUI::ComboBox* mWeaponSpellBoxMode;
            MyGUI::ComboBox* mResourceBarMode;
            MyGUI::ComboBox* mNpcBarMode;
            MyGUI::ComboBox* mQuickLootMode;
            MyGUI::ComboBox* mTerrainPreset;
            MyGUI::ComboBox* mMaterialQuality;
            MyGUI::ComboBox* mLandOptimizationMode;
            MyGUI::ScrollBar* mOptimizedRenderDistanceSlider;
            MyGUI::Widget* mManualRenderDistanceBox;
            MyGUI::Widget* mOptimizedRenderDistanceBox;
            MyGUI::ComboBox* mShadowPreset;
            MyGUI::ComboBox* mShadowMapQuality;

            // controls
            MyGUI::ScrollView* mControlsBox;
            MyGUI::Button* mResetControlsButton;
            MyGUI::Button* mKeyboardSwitch;
            MyGUI::Button* mControllerSwitch;
            bool mKeyboardMode; //if true, setting up the keyboard. Otherwise, it's controller

            void onTabChanged(MyGUI::TabControl* _sender, size_t index);
            void onSectionSelected(MyGUI::ListBox* _sender, size_t index);
            void onOkButtonClicked(MyGUI::Widget* _sender);
            void onTextureFilteringChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onSliderChangePosition(MyGUI::ScrollBar* scroller, size_t pos);
            void onButtonToggled(MyGUI::Widget* _sender);
            void onResolutionSelected(MyGUI::ListBox* _sender, size_t index);
            void onResolutionAccept();
            void onResolutionCancel();
            void highlightCurrentResolution();

            void onWaterTextureSizeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWaterReflectionDetailChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWaterResetButtonClicked(MyGUI::Widget* _sender);

            void onLightingMethodButtonChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onLightsResetButtonClicked(MyGUI::Widget* _sender);
            void onPbrResetButtonClicked(MyGUI::Widget* _sender);
            void onHdrTonemapperChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onHdrResetButtonClicked(MyGUI::Widget* _sender);
            void onMaxLightsChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onWeaponSpellBoxModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onResourceBarModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onNpcBarModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onQuickLootModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onTerrainPresetChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onMaterialQualityChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onLandOptimizationModeChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onShadowPresetChanged(MyGUI::ComboBox* _sender, size_t pos);
            void onShadowMapQualityChanged(MyGUI::ComboBox* _sender, size_t pos);
            void updateWeaponSpellBoxModeCombo();
            void updateResourceBarModeCombo();
            void updateNpcBarModeCombo();
            void updateQuickLootModeCombo();
            void updateTerrainPresetCombo();
            void updateMaterialQualityCombo();
            void updateLandOptimizationModeCombo();
            void updateLandDistanceControls();
            void updateShadowCombos();
            void selectLightingMethod(const std::string& value);
            void updateHdrTonemapperCombo();

            void onRebindAction(MyGUI::Widget* _sender);
            void onInputTabMouseWheel(MyGUI::Widget* _sender, int _rel);
            void onResetDefaultBindings(MyGUI::Widget* _sender);
            void onResetDefaultBindingsAccept ();
            void onKeyboardSwitchClicked(MyGUI::Widget* _sender);
            void onControllerSwitchClicked(MyGUI::Widget* _sender);

            void onWindowResize(MyGUI::Window* _sender);

            void apply();

            void configureWidgets(MyGUI::Widget* widget, bool init);
            void updateSliderLabel(MyGUI::ScrollBar* scroller, const std::string& value);

            void layoutControlsBox();
        
        private:
            void resetScrollbars();
    };
}

#endif
