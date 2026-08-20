// Copyright 2026 Corwin Hicks. All Rights Reserved.

#if WITH_EDITOR

#include "SGraphBridgePanel.h"
#include "GraphBridgev2.h"
#include "GraphBridgeSettings.h"
#include "GraphBridgeAutomationLibrary.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "GraphBridgePanel"

void SGraphBridgePanel::Construct(const FArguments& InArgs)
{
    ModelOptions = {
        // Anthropic
        MakeShared<FString>(TEXT("claude-sonnet-4-6")),
        MakeShared<FString>(TEXT("claude-opus-4-6")),
        MakeShared<FString>(TEXT("claude-haiku-4-5")),
        // OpenAI
        MakeShared<FString>(TEXT("gpt-4o")),
        MakeShared<FString>(TEXT("gpt-4o-mini")),
        MakeShared<FString>(TEXT("gpt-4-turbo")),
        MakeShared<FString>(TEXT("gpt-4")),
        MakeShared<FString>(TEXT("gpt-3.5-turbo")),
        MakeShared<FString>(TEXT("o1")),
        MakeShared<FString>(TEXT("o1-mini")),
        MakeShared<FString>(TEXT("o3")),
        MakeShared<FString>(TEXT("o3-mini")),
        MakeShared<FString>(TEXT("o4-mini")),
    };

    UGraphBridgeSettings* Settings = UGraphBridgeSettings::Get();

    for (auto& Opt : ModelOptions)
    {
        if (*Opt == Settings->SelectedModel)
        {
            SelectedModel = Opt;
            break;
        }
    }
    if (!SelectedModel.IsValid() && ModelOptions.Num() > 0)
        SelectedModel = ModelOptions[0];

    bServerRunning = UGraphBridgeAutomationLibrary::IsServerRunning();

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .Padding(8.0f)
        [
            SNew(SVerticalBox)

            // ── Server section ────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ServerSectionLabel", "GraphBridge Server"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 12, 0)
                [
                    SAssignNew(StatusText, STextBlock)
                    .Text(MakeAttributeLambda([this]() -> FText
                    {
                        if (bServerRunning)
                            return FText::FromString(FString::Printf(
                                TEXT("● Running on port %d"),
                                UGraphBridgeSettings::Get()->ServerPort));
                        return LOCTEXT("StatusStopped", "● Stopped");
                    }))
                    .ColorAndOpacity(MakeAttributeLambda([this]() -> FSlateColor
                    {
                        return bServerRunning
                            ? FLinearColor(0.0f, 0.8f, 0.2f, 1.0f)
                            : FLinearColor(0.8f, 0.1f, 0.1f, 1.0f);
                    }))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SAssignNew(ToggleServerButton, SButton)
                    .Text(MakeAttributeLambda([this]() -> FText
                    {
                        return bServerRunning
                            ? LOCTEXT("StopServer", "Stop Server")
                            : LOCTEXT("StartServer", "Start Server");
                    }))
                    .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                    {
                        OnToggleServer();
                        return FReply::Handled();
                    }))
                ]
            ]

            // ── Separator ─────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SSeparator)
            ]

            // ── Settings section ──────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SettingsSectionLabel", "Settings"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 8, 0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ModelLabel", "Model:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ModelComboBox, SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&ModelOptions)
                    .OnGenerateWidget(SComboBox<TSharedPtr<FString>>::FOnGenerateWidget::CreateLambda(
                        [](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
                        {
                            return SNew(STextBlock).Text(FText::FromString(*Item));
                        }
                    ))
                    .OnSelectionChanged(SComboBox<TSharedPtr<FString>>::FOnSelectionChanged::CreateLambda(
                        [this](TSharedPtr<FString> NewSel, ESelectInfo::Type)
                        {
                            SelectedModel = NewSel;
                        }
                    ))
                    [
                        SNew(STextBlock)
                        .Text(MakeAttributeLambda([this]() -> FText
                        {
                            return SelectedModel.IsValid()
                                ? FText::FromString(*SelectedModel)
                                : FText::GetEmpty();
                        }))
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 8, 0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ApiKeyLabel", "API Key:"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SAssignNew(ApiKeyBox, SEditableTextBox)
                    .IsPassword(true)
                    .Text(FText::FromString(Settings->ApiKey))
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SButton)
                .Text(LOCTEXT("SaveSettings", "Save Settings"))
                .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                {
                    OnSaveSettings();
                    return FReply::Handled();
                }))
            ]

            // ── Separator ─────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SSeparator)
            ]

            // ── Task / Chat section ───────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("TaskSectionLabel", "Task"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]

            // Preset buttons
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(SScrollBox)
                .Orientation(EOrientation::Orient_Horizontal)
                + SScrollBox::Slot()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Wire BeginPlay")))
                        .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                        {
                            OnPresetClicked(TEXT("Wire up BeginPlay: find the BeginPlay node and connect any unconnected execution pins to a Print String node that says 'BeginPlay fired'."));
                            return FReply::Handled();
                        }))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("List All Nodes")))
                        .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                        {
                            OnPresetClicked(TEXT("List all nodes in the currently open Blueprint and summarise what the graph does."));
                            return FReply::Handled();
                        }))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Add Component")))
                        .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                        {
                            OnPresetClicked(TEXT("Add a Static Mesh Component to the currently open Blueprint Actor."));
                            return FReply::Handled();
                        }))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Compile & Save")))
                        .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                        {
                            OnPresetClicked(TEXT("Compile and save the currently open Blueprint."));
                            return FReply::Handled();
                        }))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Add IMC to BeginPlay")))
                        .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                        {
                            OnPresetClicked(TEXT("Add an Add Input Mapping Context node to BeginPlay with Priority 0."));
                            return FReply::Handled();
                        }))
                    ]
                ]
            ]

            // New Chat button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(SButton)
                .Text(LOCTEXT("NewChat", "New Chat"))
                .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                {
                    OnNewChat();
                    return FReply::Handled();
                }))
            ]

            // Chat log
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0, 0, 0, 4)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                [
                    SAssignNew(ChatScrollBox, SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SAssignNew(ChatContentBox, SVerticalBox)
                    ]
                ]
            ]

            // Thinking indicator
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SAssignNew(ThinkingIndicatorBorder, SBorder)
                .Visibility(EVisibility::Collapsed)
                .Padding(4.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0, 0, 8, 0)
                    [
                        SNew(SThrobber)
                    ]
                    + SHorizontalBox::Slot()
                    .VAlign(VAlign_Center)
                    [
                        SAssignNew(ThinkingIndicatorText, STextBlock)
                        .Text(LOCTEXT("ThinkingDefault", "GraphBridge is thinking... (iteration 1)"))
                    ]
                ]
            ]

            // Task input
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(SBox)
                .MinDesiredHeight(80.0f)
                [
                    SAssignNew(TaskInputBox, SMultiLineEditableTextBox)
                ]
            ]

            // Run Task + Cancel buttons
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0, 0, 4, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("RunTask", "Run Task"))
                    .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                    {
                        OnRunTask();
                        return FReply::Handled();
                    }))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SAssignNew(CancelButton, SButton)
                    .Text(LOCTEXT("CancelTask", "Cancel"))
                    .Visibility(EVisibility::Collapsed)
                    .OnClicked(FOnClicked::CreateLambda([this]() -> FReply
                    {
                        OnCancelClicked();
                        return FReply::Handled();
                    }))
                ]
            ]
        ]
    ];

    if (ModelComboBox.IsValid() && SelectedModel.IsValid())
        ModelComboBox->SetSelectedItem(SelectedModel);

    // Wire up the LLM client — owned by the module so it survives panel close
    FGraphBridgev2Module& Module = FModuleManager::GetModuleChecked<FGraphBridgev2Module>(TEXT("GraphBridgev2"));
    LLMClient = Module.GetOrCreateLLMClient();

    TSharedRef<SGraphBridgePanel> ThisRef = StaticCastSharedRef<SGraphBridgePanel>(AsShared());
    TWeakPtr<SGraphBridgePanel>   WeakSelf(ThisRef);

    LLMClient->OnResponse.BindLambda([WeakSelf](const FString& Message)
    {
        if (TSharedPtr<SGraphBridgePanel> Panel = WeakSelf.Pin())
        {
            Panel->SetThinkingState(false);
            Panel->AppendChatEntry(TEXT("assistant"), Message);
        }
    });
    LLMClient->OnError.BindLambda([WeakSelf](const FString& ErrorMessage)
    {
        if (TSharedPtr<SGraphBridgePanel> Panel = WeakSelf.Pin())
        {
            Panel->SetThinkingState(false);
            Panel->AppendChatEntry(TEXT("system"), FString::Printf(TEXT("Error: %s"), *ErrorMessage));
        }
    });
    LLMClient->OnLLMIteration.BindLambda([WeakSelf](int32 Iteration)
    {
        if (TSharedPtr<SGraphBridgePanel> Panel = WeakSelf.Pin())
            Panel->OnIterationUpdate(Iteration);
    });
}

void SGraphBridgePanel::OnToggleServer()
{
    if (bServerRunning)
    {
        UGraphBridgeAutomationLibrary::StopGraphBridgeServer();
        bServerRunning = false;
    }
    else
    {
        UGraphBridgeAutomationLibrary::StartGraphBridgeServer(
            UGraphBridgeSettings::Get()->ServerPort);
        bServerRunning = UGraphBridgeAutomationLibrary::IsServerRunning();
    }
}

void SGraphBridgePanel::OnSaveSettings()
{
    UGraphBridgeSettings* Settings = UGraphBridgeSettings::Get();
    if (ApiKeyBox.IsValid())
        Settings->ApiKey = ApiKeyBox->GetText().ToString();
    if (SelectedModel.IsValid())
    {
        Settings->SelectedModel = *SelectedModel;
        if (SelectedModel->StartsWith(TEXT("claude-")))
            Settings->ApiEndpoint = TEXT("https://api.anthropic.com/v1/messages");
        else
            Settings->ApiEndpoint = TEXT("");
    }
    Settings->SaveConfig();
}

void SGraphBridgePanel::OnRunTask()
{
    if (!LLMClient.IsValid()) return;

    FString UserText = TaskInputBox.IsValid() ? TaskInputBox->GetText().ToString() : TEXT("");
    if (UserText.IsEmpty()) return;

    if (TaskInputBox.IsValid())
        TaskInputBox->SetText(FText::GetEmpty());

    AppendChatEntry(TEXT("user"), UserText);
    SetThinkingState(true);
    LLMClient->SendMessage(UserText);
}

void SGraphBridgePanel::OnNewChat()
{
    if (LLMClient.IsValid())
        LLMClient->ResetConversation();
    ChatHistory.Empty();
    RebuildChatLog();
    AppendChatEntry(TEXT("system"), TEXT("New conversation started."));
}

void SGraphBridgePanel::OnCancelClicked()
{
    if (LLMClient.IsValid())
        LLMClient->RequestCancel();
    SetThinkingState(false);
}

void SGraphBridgePanel::OnPresetClicked(FString PresetText)
{
    if (TaskInputBox.IsValid())
        TaskInputBox->SetText(FText::FromString(PresetText));
}

void SGraphBridgePanel::AppendChatEntry(const FString& Role, const FString& Text)
{
    ChatHistory.Add({ Role, Text });

    if (!ChatContentBox.IsValid()) return;

    FLinearColor BubbleColor = FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);
    if      (Role == TEXT("user"))   BubbleColor = FLinearColor(0.1f, 0.2f, 0.4f, 1.0f);
    else if (Role == TEXT("system")) BubbleColor = FLinearColor(0.25f, 0.15f, 0.0f, 1.0f);

    FString Label;
    if      (Role == TEXT("user"))      Label = TEXT("You");
    else if (Role == TEXT("assistant")) Label = TEXT("GraphBridge");
    else                                Label = TEXT("System");

    ChatContentBox->AddSlot()
    .AutoHeight()
    .Padding(4.0f, 2.0f)
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .BorderBackgroundColor(BubbleColor)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Label))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
                .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 2, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .AutoWrapText(true)
            ]
        ]
    ];

    if (ChatScrollBox.IsValid())
        ChatScrollBox->ScrollToEnd();
}

void SGraphBridgePanel::SetThinkingState(bool bThinking)
{
    bIsRunning = bThinking;
    if (!bThinking)
        IterationCount = 0;

    if (ThinkingIndicatorBorder.IsValid())
        ThinkingIndicatorBorder->SetVisibility(bThinking ? EVisibility::Visible : EVisibility::Collapsed);

    if (CancelButton.IsValid())
        CancelButton->SetVisibility(bThinking ? EVisibility::Visible : EVisibility::Collapsed);
}

void SGraphBridgePanel::RebuildChatLog()
{
    if (!ChatContentBox.IsValid()) return;
    TArray<FChatEntry> Snapshot = MoveTemp(ChatHistory);
    ChatContentBox->ClearChildren();
    for (const FChatEntry& Entry : Snapshot)
        AppendChatEntry(Entry.Role, Entry.Text);
}

void SGraphBridgePanel::OnIterationUpdate(int32 IterationNumber)
{
    IterationCount = IterationNumber;
    if (ThinkingIndicatorText.IsValid())
    {
        ThinkingIndicatorText->SetText(FText::FromString(
            FString::Printf(TEXT("GraphBridge is thinking... (iteration %d)"), IterationNumber + 1)));
    }
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
