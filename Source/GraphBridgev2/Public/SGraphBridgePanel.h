// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "GraphBridgeLLMClient.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SThrobber.h"

class SGraphBridgePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGraphBridgePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    struct FChatEntry
    {
        FString Role;
        FString Text;
    };

    // Server / status
    TSharedPtr<STextBlock>                     StatusText;
    TSharedPtr<SButton>                        ToggleServerButton;
    bool                                       bServerRunning = false;

    // Settings
    TSharedPtr<SEditableTextBox>               ApiKeyBox;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelComboBox;
    TArray<TSharedPtr<FString>>                ModelOptions;
    TSharedPtr<FString>                        SelectedModel;

    // Chat
    TArray<FChatEntry>                         ChatHistory;
    TSharedPtr<SScrollBox>                     ChatScrollBox;
    TSharedPtr<SVerticalBox>                   ChatContentBox;
    TSharedPtr<STextBlock>                     ThinkingIndicatorText;
    TSharedPtr<SBorder>                        ThinkingIndicatorBorder;
    TSharedPtr<SButton>                        CancelButton;
    int32                                      IterationCount = 0;
    bool                                       bIsRunning = false;

    // Task input
    TSharedPtr<SMultiLineEditableTextBox>       TaskInputBox;

    // LLM
    TSharedPtr<FGraphBridgeLLMClient>          LLMClient;

    void OnToggleServer();
    void OnSaveSettings();
    void OnRunTask();
    void OnNewChat();
    void OnCancelClicked();
    void OnPresetClicked(FString PresetText);
    void AppendChatEntry(const FString& Role, const FString& Text);
    void SetThinkingState(bool bThinking);
    void RebuildChatLog();
    void OnIterationUpdate(int32 IterationNumber);
};

#endif // WITH_EDITOR
