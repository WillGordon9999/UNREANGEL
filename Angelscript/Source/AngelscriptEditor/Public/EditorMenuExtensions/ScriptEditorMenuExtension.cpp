#include "ScriptEditorMenuExtension.h"
#include "AngelscriptManager.h"
#include "ClassGenerator/AngelscriptClassGenerator.h"
#include "ClassGenerator/ASClass.h"
#include "AngelscriptCodeModule.h"
#include "IStructureDetailsView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "ScriptEditorPrompts.h"
#include "LevelEditor.h"
#include "SourceCodeNavigation.h"
#include "ContentBrowserDelegates.h"
#include "ContentBrowserModule.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "ScopedTransaction.h"

void UScriptEditorMenuExtension::InitializeExtensions()
{
	FAngelscriptClassGenerator::OnPostReload.AddLambda([](bool bFullReload)
	{
		if (bFullReload)
		{
			UScriptEditorMenuExtension::UnregisterExtensions();
			UScriptEditorMenuExtension::RegisterExtensions();
		}
	});

	FCoreDelegates::OnEnginePreExit.AddLambda([]()
	{
		UScriptEditorMenuExtension::UnregisterExtensions();
	});

	if (FAngelscriptManager::bIsInitialCompileFinished)
	{
		UScriptEditorMenuExtension::RegisterExtensions();
	}
}

UWorld* UScriptEditorMenuExtension::GetWorld() const
{
	return (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
}

TArray<UFunction*> UScriptEditorMenuExtension::GatherExtensionFunctions() const
{
	TArray<UFunction*> Functions;

	const static FName NAME_CallInEditor(TEXT("CallInEditor"));
	for (TFieldIterator<UFunction> FunctionIt(GetClass(), EFieldIterationFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		UFunction* Function = *FunctionIt;
		if (Function == nullptr)
			continue;
		if (!Function->HasMetaData(NAME_CallInEditor))
			continue;
		if (Function->GetReturnProperty() != nullptr)
			continue;

		Functions.Add(Function);
	}

	return Functions;
}

FName UScriptEditorMenuExtension::GetExtensionPointOrDefault() const
{
	if (!ExtensionPoint.IsNone())
		return ExtensionPoint;

	switch (ExtensionMenu)
	{
		case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
			return "ActorOptions";
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu:
			return "CommonAssetActions";
	}

	return NAME_None;
}

TSharedRef<FExtender> UScriptEditorMenuExtension::Extend(const TSharedRef<FUICommandList> CommandList, FExtenderSelection Selection) const
{
	FEditorScriptExecutionGuard ScriptGuard;
	TGuardValue<FExtenderSelection> ScopeSelection(CurrentSelection, Selection);

	TSharedRef<FExtender> Extender(new FExtender());

	if (!ShouldExtend())
		return Extender;

	TArray<UFunction*> Functions = GatherExtensionFunctions();
	if (Functions.Num() == 0)
		return Extender;

	Extender->AddMenuExtension(
		GetExtensionPointOrDefault(),
		(EExtensionHook::Position)ExtensionOrder,
		CommandList,
		FMenuExtensionDelegate::CreateLambda(
			[Extension=this, Functions, Selection](FMenuBuilder& MenuBuilder)
			{
				Extension->BuildMenu(MenuBuilder, Functions, Selection);
			}
		));

	return Extender;
}

void UScriptEditorMenuExtension::CallFunctionOnSelection(UFunction* Function, FExtenderSelection Selection) const
{
	FScriptEditorPromptOptions Options;
	FScriptEditorPrompts::ShowPromptToCallFunction(
		this,
		Function->GetFName(),
		Options,
		TArray<UObject*>()
	);
}

void UScriptEditorMenuExtension::AddMenuEntry(class FMenuBuilder& MenuBuilder, UFunction* Function, FExtenderSelection Selection) const
{
	const static FName NAME_EditorIcon(TEXT("EditorIcon"));
	FString IconName = Function->GetMetaData(NAME_EditorIcon);
	if (IconName.IsEmpty())
		IconName = TEXT("GraphEditor.Event_16x");

	MenuBuilder.AddMenuEntry(
		Function->GetDisplayNameText(),
		Function->GetToolTipText(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), FName(*IconName)),
		FUIAction
		(
			FExecuteAction::CreateLambda([Extension=this, Function, Selection]
			{
				if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				{
					// Browse to the function in script
				   const bool bFoundFunction = FSourceCodeNavigation::NavigateToFunction(Function);
				   if (!bFoundFunction)
				   {
					  FSourceCodeNavigation::NavigateToClass(Function->GetOuterUClass());
				   }
				}
				else
				{
					UScriptEditorMenuExtension* TempObject = NewObject<UScriptEditorMenuExtension>(GetTransientPackage(), Extension->GetClass());
					TempObject->AddToRoot();

					FEditorScriptExecutionGuard ScriptGuard;
					TGuardValue<FExtenderSelection> ScopeSelection(TempObject->CurrentSelection, Selection);
					FScopedTransaction Transaction(FText::FromString(Function->GetDisplayNameText().ToString()));

					TempObject->CallFunctionOnSelection(Function, Selection);
					TempObject->RemoveFromRoot();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return true;
			})
		)
	);
}

void UScriptEditorMenuExtension::AddToolMenuEntry(FToolMenuSection& MenuSection, UFunction* Function, FExtenderSelection Selection) const
{
	const static FName NAME_EditorIcon(TEXT("EditorIcon"));
	FString IconName = Function->GetMetaData(NAME_EditorIcon);
	if (IconName.IsEmpty())
		IconName = TEXT("GraphEditor.Event_16x");

	MenuSection.AddMenuEntry(
		Function->GetFName(),
		Function->GetDisplayNameText(),
		Function->GetToolTipText(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), FName(*IconName)),
		FUIAction
		(
			FExecuteAction::CreateLambda([Extension=this, Function, Selection]
			{
				if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				{
					// Browse to the function in script
				   const bool bFoundFunction = FSourceCodeNavigation::NavigateToFunction(Function);
				   if (!bFoundFunction)
				   {
					  FSourceCodeNavigation::NavigateToClass(Function->GetOuterUClass());
				   }
				}
				else
				{
					UScriptEditorMenuExtension* TempObject = NewObject<UScriptEditorMenuExtension>(GetTransientPackage(), Extension->GetClass());
					TempObject->AddToRoot();

					FEditorScriptExecutionGuard ScriptGuard;
					TGuardValue<FExtenderSelection> ScopeSelection(TempObject->CurrentSelection, Selection);
					FScopedTransaction Transaction(FText::FromString(Function->GetDisplayNameText().ToString()));

					TempObject->CallFunctionOnSelection(Function, Selection);
					TempObject->RemoveFromRoot();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return true;
			})
		)
	);
}

void UScriptEditorMenuExtension::AddToolMenuButton(FToolMenuSection& MenuSection, UFunction* Function, FExtenderSelection Selection) const
{
	const static FName NAME_EditorIcon(TEXT("EditorIcon"));
	const static FName NAME_EditorButtonStyle(TEXT("EditorButtonStyle"));

	FString IconName = Function->GetMetaData(NAME_EditorIcon);
	if (IconName.IsEmpty())
		IconName = TEXT("GraphEditor.Event_16x");

	auto Entry = FToolMenuEntry::InitToolBarButton(
		Function->GetFName(),
		FUIAction
		(
			FExecuteAction::CreateLambda([Extension=this, Function, Selection]
			{
				if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				{
					// Browse to the function in script
				   const bool bFoundFunction = FSourceCodeNavigation::NavigateToFunction(Function);
				   if (!bFoundFunction)
				   {
					  FSourceCodeNavigation::NavigateToClass(Function->GetOuterUClass());
				   }
				}
				else
				{
					UScriptEditorMenuExtension* TempObject = NewObject<UScriptEditorMenuExtension>(GetTransientPackage(), Extension->GetClass());
					TempObject->AddToRoot();

					FEditorScriptExecutionGuard ScriptGuard;
					TGuardValue<FExtenderSelection> ScopeSelection(TempObject->CurrentSelection, Selection);
					FScopedTransaction Transaction(FText::FromString(Function->GetDisplayNameText().ToString()));

					TempObject->CallFunctionOnSelection(Function, Selection);
					TempObject->RemoveFromRoot();
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return true;
			})
		),
		Function->GetDisplayNameText(),
		Function->GetToolTipText(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), FName(*IconName))
	);

	FString StyleName = Function->GetMetaData(NAME_EditorButtonStyle);
	if (!StyleName.IsEmpty())
		Entry.StyleNameOverride = *StyleName;

	MenuSection.AddEntry(Entry);
}

struct FSortedFunction
{
	UFunction* Function;
	int SortOrder;

	bool operator<(const FSortedFunction& Other) const
	{
		if (SortOrder < Other.SortOrder)
			return true;
		if (SortOrder > Other.SortOrder)
			return false;
		return Function->GetName() < Other.Function->GetName();
	}
};

struct FFunctionCategory
{
	FString Name;
	TArray<FSortedFunction> Functions;
	TArray<FFunctionCategory> SubCategories;

	bool operator<(const FFunctionCategory& Other) const
	{
		return Name < Other.Name;
	}

	void Sort()
	{
		Functions.Sort();
		SubCategories.Sort();
		for (auto& Category : SubCategories)
			Category.Sort();
	}
};

static TMap<FString, FFunctionCategory> SortFunctionsByCategory(TArray<UFunction*> Functions)
{
	const static FName NAME_Category(TEXT("Category"));
	const static FName NAME_SortOrder(TEXT("SortOrder"));
	TMap<FString, FFunctionCategory> FunctionCategories;

	for (UFunction* Function : Functions)
	{
		FString CategoryString = Function->GetMetaData(NAME_Category);

		TArray<FString> CategoryElements;
		CategoryString.ParseIntoArray(CategoryElements, TEXT("|"), true);

		if (CategoryElements.Num() == 0)
			CategoryElements.Add(TEXT(""));

		FSortedFunction SortFun;
		SortFun.Function = Function;

		FString SortOrder = Function->GetMetaData(NAME_SortOrder);
		if (!SortOrder.IsEmpty())
			LexFromString(SortFun.SortOrder, *SortOrder);
		else
			SortFun.SortOrder = 0;

		FFunctionCategory* CheckCategory = nullptr;
		for (int i = 0, Count = CategoryElements.Num(); i < Count; ++i)
		{
			FString ElementName = CategoryElements[i].TrimStartAndEnd();

			if (CheckCategory == nullptr)
			{
				CheckCategory = &FunctionCategories.FindOrAdd(ElementName);
				CheckCategory->Name = ElementName;
			}
			else
			{
				auto* SubCategory = CheckCategory->SubCategories.FindByPredicate([&](FFunctionCategory& Cat)
				{
					return Cat.Name == ElementName;
				});

				if (SubCategory == nullptr)
				{
					SubCategory = &CheckCategory->SubCategories.Emplace_GetRef();
					SubCategory->Name = ElementName;
				}

				CheckCategory = SubCategory;
			}
		}

		if (CheckCategory != nullptr)
			CheckCategory->Functions.Add(SortFun);
	}

	for (auto& Category : FunctionCategories)
		Category.Value.Sort();
	return FunctionCategories;
}

void UScriptEditorMenuExtension::BuildMenu(class FMenuBuilder& MenuBuilder, TArray<UFunction*> Functions, FExtenderSelection Selection) const
{
	if (bAddSeparatorBeforeOptions)
		MenuBuilder.AddSeparator();
	if (!MenuSectionHeader.IsEmpty())
		MenuBuilder.BeginSection(GetClass()->GetFName(), MenuSectionHeader);

	TMap<FString, FFunctionCategory> FunctionCategories = SortFunctionsByCategory(Functions);

	auto AddEntriesWithCategory = [&](FMenuBuilder& SubMenu, const FFunctionCategory& Category)
	{
		// Add entries for functions with no category
		for (auto FunctionElem : Category.Functions)
			AddMenuEntry(SubMenu, FunctionElem.Function, Selection);
	};

	struct FSubMenuHelper
	{
		static void AddSubMenuForCategory(const UScriptEditorMenuExtension* Extension, FExtenderSelection Selection, FMenuBuilder& SubMenu, const FFunctionCategory& Category)
		{
			SubMenu.AddSubMenu(
				FText::FromString(Category.Name),
				FText(),
				FNewMenuDelegate::CreateLambda([Extension, Category, Selection](FMenuBuilder& SubSubMenu)
				{
					for (auto FunctionElem : Category.Functions)
						Extension->AddMenuEntry(SubSubMenu, FunctionElem.Function, Selection);

					for (const FFunctionCategory& SubCategoryElem : Category.SubCategories)
						AddSubMenuForCategory(Extension, Selection, SubSubMenu, SubCategoryElem);
				}),
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Event_16x")
			);
		}
	};

	// Add submenus for categories
	for (auto& Category : FunctionCategories)
	{
		if (Category.Key == TEXT(""))
			AddEntriesWithCategory(MenuBuilder, Category.Value);
		else
			FSubMenuHelper::AddSubMenuForCategory(this, Selection, MenuBuilder, Category.Value);
	}

	if (!MenuSectionHeader.IsEmpty())
		MenuBuilder.EndSection();
	if (bAddSeparatorAfterOptions)
		MenuBuilder.AddSeparator();
}

void UScriptEditorMenuExtension::BuildToolMenuSection(FToolMenuSection& MenuSection, FExtenderSelection Selection, bool bIsMenu) const
{
	FEditorScriptExecutionGuard ScriptGuard;
	TGuardValue<FExtenderSelection> ScopeSelection(CurrentSelection, Selection);

	if (!ShouldExtend())
		return;

	TArray<UFunction*> Functions = GatherExtensionFunctions();
	if (Functions.Num() == 0)
		return;

	TMap<FString, FFunctionCategory> FunctionCategories = SortFunctionsByCategory(Functions);

	if (bAddSeparatorBeforeOptions)
		MenuSection.AddSeparator(NAME_None);

	auto AddEntriesWithCategory = [&](FToolMenuSection& SubMenu, const FFunctionCategory& Category)
	{
		// Add entries for functions with no category
		for (auto FunctionElem : Category.Functions)
		{
			if (bIsMenu)
				AddToolMenuEntry(SubMenu, FunctionElem.Function, Selection);
			else
				AddToolMenuButton(SubMenu, FunctionElem.Function, Selection);
		}
	};

	struct FSubMenuHelper
	{
		static void AddSubMenuForCategory(const UScriptEditorMenuExtension* Extension, FExtenderSelection Selection, FToolMenuSection& SubMenu, const FFunctionCategory& Category)
		{
			SubMenu.AddSubMenu(
				*Category.Name,
				FText::FromString(Category.Name),
				FText(),
				FNewToolMenuDelegate::CreateLambda([Extension, Category, Selection](UToolMenu* SubSubMenu)
				{
					FToolMenuSection& SubSection = SubSubMenu->AddSection(NAME_None, FText());

					for (auto FunctionElem : Category.Functions)
						Extension->AddToolMenuEntry(SubSection, FunctionElem.Function, Selection);

					for (const FFunctionCategory& SubCategoryElem : Category.SubCategories)
						AddSubMenuForCategory(Extension, Selection, SubSection, SubCategoryElem);
				}),
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Event_16x")
			);
		}
	};

	// Add submenus for categories
	for (auto& Category : FunctionCategories)
	{
		if (Category.Key == TEXT(""))
		{
			AddEntriesWithCategory(MenuSection, Category.Value);
		}
		else
		{
			if (bIsMenu)
			{
				FSubMenuHelper::AddSubMenuForCategory(this, Selection, MenuSection, Category.Value);
			}
			else
			{
				MenuSection.AddEntry(FToolMenuEntry::InitComboButton(
					*Category.Value.Name,
					FUIAction(),
					FNewToolMenuDelegate::CreateLambda([Extension=this, Category=Category.Value, Selection](UToolMenu* SubSubMenu)
					{
						FToolMenuSection& SubSection = SubSubMenu->AddSection(NAME_None, FText());

						for (auto FunctionElem : Category.Functions)
							Extension->AddToolMenuEntry(SubSection, FunctionElem.Function, Selection);

						for (const FFunctionCategory& SubCategoryElem : Category.SubCategories)
							FSubMenuHelper::AddSubMenuForCategory(Extension, Selection, SubSection, SubCategoryElem);
					}),
					FText::FromString(Category.Value.Name),
					FText(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Event_16x"),
					false
				));
			}
		}
	}

	if (bAddSeparatorAfterOptions)
		MenuSection.AddSeparator(NAME_None);
}

TArray<UScriptEditorMenuExtension::FRegisteredExtender> UScriptEditorMenuExtension::RegisteredExtensions;

void UScriptEditorMenuExtension::UnregisterExtensions()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Unregister all previous extensions
	for (auto Extension : RegisteredExtensions)
	{
		switch (Extension.Location)
		{
		case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_DragDropContextMenu:
			LevelEditorModule.GetAllLevelViewportDragDropContextMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedObjects& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_OptionsMenu:
			LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ShowMenu:
			LevelEditorModule.GetAllLevelViewportShowMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ViewMenu:
			LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BuildMenu:
			LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CompileMenu:
			LevelEditorModule.GetAllLevelEditorToolbarCompileMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_SourceControlMenu:
			LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CreateMenu:
			LevelEditorModule.GetAllLevelEditorToolbarCreateMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_PlayMenu:
			LevelEditorModule.GetAllLevelEditorToolbarPlayMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BlueprintsMenu:
			LevelEditorModule.GetAllLevelEditorToolbarBlueprintsMenuExtenders().RemoveAll(
				[&](const TSharedPtr<FExtender>& Extender)
				{
					return Extender == Extension.Extender;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CinematicsMenu:
			LevelEditorModule.GetAllLevelEditorToolbarCinematicsMenuExtenders().RemoveAll(
				[&](const TSharedPtr<FExtender>& Extender)
				{
					return Extender == Extension.Extender;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_LevelMenu:
			LevelEditorModule.GetAllLevelEditorLevelMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetContextMenu:
			ContentBrowserModule.GetAllAssetContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender_SelectedPaths& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_PathViewContextMenu:
			ContentBrowserModule.GetAllPathViewContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender_SelectedPaths& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionListContextMenu:
			ContentBrowserModule.GetAllCollectionListContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionViewContextMenu:
			ContentBrowserModule.GetAllCollectionViewContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu:
			ContentBrowserModule.GetAllAssetViewContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender_SelectedAssets& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewViewMenu:
			ContentBrowserModule.GetAllAssetViewViewMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ToolMenu:
		{
			auto* Menu = UToolMenus::Get()->ExtendMenu(Extension.ExtensionPoint);
			Menu->RemoveSection(Extension.SectionName);
		}
		break;
		}
	}

	RegisteredExtensions.Reset();
}

void UScriptEditorMenuExtension::RegisterExtensions()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Register new extensions
	for (TObjectIterator<UASClass> ScriptClass; ScriptClass; ++ScriptClass)
	{
		if (!ScriptClass->IsChildOf(UScriptEditorMenuExtension::StaticClass()))
			continue;
		if (ScriptClass->HasAnyClassFlags(CLASS_Abstract))
			continue;
		if (ScriptClass->HasAnyClassFlags(CLASS_NewerVersionExists))
			continue;
		if (ScriptClass->ScriptTypePtr == nullptr)
			continue;

		auto* CDO = ScriptClass->GetDefaultObject<UScriptEditorMenuExtension>();

		FRegisteredExtender Registered;
		Registered.Location = CDO->ExtensionMenu;
		Registered.ExtensionPoint = CDO->ExtensionPoint;

		switch (CDO->ExtensionMenu)
		{
		case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Add(
				FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda(
					[CDO](const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors) -> TSharedRef<FExtender>
					{
						FExtenderSelection Selection;
						for (auto* Actor : SelectedActors)
							Selection.SelectedObjects.Add(Actor);
						return CDO->Extend(CommandList, Selection);
					}
				)
			);
			Registered.DelegateHandle = LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Last().GetHandle();
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_DragDropContextMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelViewportMenuExtender_SelectedObjects::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> SelectedObjects) -> TSharedRef<FExtender>
				{
					FExtenderSelection Selection;
					Selection.SelectedObjects = SelectedObjects;
					return CDO->Extend(CommandList, Selection);
				}
			);
			LevelEditorModule.GetAllLevelViewportDragDropContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_OptionsMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ShowMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelViewportShowMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ViewMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BuildMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CompileMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarCompileMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_SourceControlMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CreateMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarCreateMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_PlayMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarPlayMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BlueprintsMenu:
		{
			Registered.Extender = CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			LevelEditorModule.GetAllLevelEditorToolbarBlueprintsMenuExtenders().Add(Registered.Extender);
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CinematicsMenu:
		{
			Registered.Extender = CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			LevelEditorModule.GetAllLevelEditorToolbarCinematicsMenuExtenders().Add(Registered.Extender);
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_LevelMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorLevelMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
				[CDO](const TArray<FString>& Paths) -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllAssetContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_PathViewContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
				[CDO](const TArray<FString>& Paths) -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllPathViewContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionListContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender::CreateLambda(
				[CDO]() -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllCollectionListContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionViewContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender::CreateLambda(
				[CDO]() -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllCollectionViewContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender_SelectedAssets::CreateLambda(
				[CDO](const TArray<FAssetData>& Assets) -> TSharedRef<FExtender>
				{
					FExtenderSelection Selection;
					Selection.SelectedAssets = Assets;
					return CDO->Extend(MakeShared<FUICommandList>(), Selection);
				}
			);
			ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewViewMenu:
		{
			auto Delegate = FContentBrowserMenuExtender::CreateLambda(
				[CDO]() -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllAssetViewViewMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ToolMenu:
		{
			Registered.SectionName = CDO->GetClass()->GetFName();

			auto* Menu = UToolMenus::Get()->ExtendMenu(Registered.ExtensionPoint);
			FText SectionHeader = CDO->MenuSectionHeader;
			if (SectionHeader.IsEmpty())
				SectionHeader = CDO->GetClass()->GetDisplayNameText();

			FToolMenuSection& Section = Menu->AddSection(Registered.SectionName, SectionHeader);
			Section.AddDynamicEntry(Registered.SectionName, FNewToolMenuSectionDelegate::CreateLambda([CDO, Menu](FToolMenuSection& DynamicSection)
			{
				bool bIsMenu = (Menu->MenuType == EMultiBoxType::MenuBar) || (Menu->MenuType == EMultiBoxType::Menu);
				CDO->BuildToolMenuSection(DynamicSection, FExtenderSelection(), bIsMenu);
			}));
		}
		break;
		}

		RegisteredExtensions.Add(Registered);
	}
}
