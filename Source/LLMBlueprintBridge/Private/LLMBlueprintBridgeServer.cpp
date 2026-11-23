#include "LLMBlueprintBridgeServer.h"

#include "GitAssetDiffModule.h"
#include "LLMBlueprintBridgeSettings.h"
#include "Abilities/GameplayAbility.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AttributeSet.h"
#include "Async/Async.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintAssetNodeSpawner.h"
#include "BlueprintFieldNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintVariableNodeSpawner.h"
#include "Blueprint/BlueprintSupport.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "DataTableEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Factories/DataTableFactory.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayCueNotify_Actor.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayModMagnitudeCalculation.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "JsonObjectConverter.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialFunction.h"
#include "Misc/App.h"
#include "Misc/SecureHash.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeCompilerManager.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeEvents.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeFactory.h"
#include "StateTreeInstanceData.h"
#include "StateTreeReference.h"
#include "StateTreeState.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Components/StateTreeComponentSchema.h"
#include "Components/StateTreeComponent.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "WidgetBlueprint.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/StringOutputDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMBlueprintBridge, Log, All);

namespace
{
	constexpr uint32 DefaultPort = 9877;
	const TCHAR* JsonContentType = TEXT("application/json; charset=utf-8");
	const TCHAR* HeaderToken = TEXT("X-LLM-Bridge-Token");

	TSharedRef<FJsonObject> MakeEnvelope(bool bOk, const FString& Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), bOk);
		if (!Code.IsEmpty())
		{
			Root->SetStringField(TEXT("code"), Code);
		}
		if (!Message.IsEmpty())
		{
			Root->SetStringField(TEXT("message"), Message);
		}
		return Root;
	}

	TSharedRef<FJsonObject> MakeErrorObject(const FString& Code, const FString& Message)
	{
		return MakeEnvelope(false, Code, Message);
	}

	TSharedRef<FJsonObject> MakeOk(const TSharedPtr<FJsonObject>& Data = nullptr)
	{
		TSharedRef<FJsonObject> Root = MakeEnvelope(true, FString(), FString());
		if (Data.IsValid())
		{
			Root->SetObjectField(TEXT("data"), Data);
		}
		return Root;
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Body;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
		FJsonSerializer::Serialize(Object, Writer);
		return Body;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedRef<FJsonObject>& Object, EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(SerializeJson(Object), JsonContentType);
		Response->Code = Code;
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("http://127.0.0.1"), TEXT("http://localhost") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, X-LLM-Bridge-Token") });
		return Response;
	}

	bool TryGetObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, TSharedPtr<FJsonObject>& OutObject)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* FoundObject = nullptr;
		if (Object->TryGetObjectField(FieldName, FoundObject) && FoundObject && FoundObject->IsValid())
		{
			OutObject = *FoundObject;
			return true;
		}
		return false;
	}

	bool TryGetArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
	{
		return Object.IsValid() && Object->TryGetArrayField(FieldName, OutArray);
	}

	FString GetOptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const FString& DefaultValue = FString())
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(FieldName, Value) ? Value : DefaultValue;
	}

	bool GetOptionalBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool bDefaultValue = false)
	{
		bool bValue = bDefaultValue;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	int32 GetOptionalInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int32 DefaultValue = 0)
	{
		int32 Value = DefaultValue;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(FieldName, Value);
		}
		return Value;
	}

	bool RequireString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FString& OutValue, FString& OutError)
	{
		if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Missing required string field '%s'."), FieldName);
			return false;
		}
		return true;
	}

	FString ReadRequestBody(const FHttpServerRequest& Request)
	{
		FString Body;
		if (!Request.Body.IsEmpty())
		{
			FFileHelper::BufferToString(Body, Request.Body.GetData(), Request.Body.Num());
		}
		return Body;
	}

	FString StripObjectExportWrapper(FString Path)
	{
		Path.TrimStartAndEndInline();
		int32 FirstQuote = INDEX_NONE;
		int32 LastQuote = INDEX_NONE;
		if (Path.FindChar(TEXT('\''), FirstQuote) && Path.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
		{
			Path = Path.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}
		return Path;
	}

	FString NormalizeAssetObjectPath(FString AssetPath)
	{
		AssetPath = StripObjectExportWrapper(AssetPath);
		if (!AssetPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
			if (!AssetName.IsEmpty())
			{
				AssetPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
			}
		}
		return AssetPath;
	}

	FString NormalizePackagePath(FString AssetPath)
	{
		AssetPath = NormalizeAssetObjectPath(AssetPath);
		FString PackagePath;
		FString AssetName;
		if (AssetPath.Split(TEXT("."), &PackagePath, &AssetName))
		{
			return PackagePath;
		}
		return AssetPath;
	}

	bool IsGameContentPath(const FString& LongPackagePath)
	{
		return LongPackagePath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase) || LongPackagePath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase);
	}

	FString ObjectPathFromPackagePath(const FString& PackagePath)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		return AssetName.IsEmpty() ? PackagePath : FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
	}

	bool SplitAssetPackagePath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName, FString& OutError)
	{
		FString PackagePath = NormalizePackagePath(InPath);
		PackagePath.TrimStartAndEndInline();
		if (PackagePath.IsEmpty())
		{
			OutError = TEXT("Asset path is empty.");
			return false;
		}
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(PackagePath, false, &Reason))
		{
			OutError = Reason.ToString();
			return false;
		}

		OutAssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (OutAssetName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Asset path '%s' does not include an asset name."), *InPath);
			return false;
		}

		OutPackagePath = FPackageName::GetLongPackagePath(PackagePath);
		return true;
	}

	bool NormalizeFolderPath(const FString& InPath, FString& OutFolderPath, FString& OutError)
	{
		OutFolderPath = StripObjectExportWrapper(InPath);
		OutFolderPath.TrimStartAndEndInline();
		if (OutFolderPath.Contains(TEXT(".")))
		{
			OutFolderPath = NormalizePackagePath(OutFolderPath);
			OutFolderPath = FPackageName::GetLongPackagePath(OutFolderPath);
		}
		if (OutFolderPath.EndsWith(TEXT("/")))
		{
			OutFolderPath.LeftChopInline(1);
		}
		if (OutFolderPath.IsEmpty())
		{
			OutError = TEXT("Folder path is empty.");
			return false;
		}
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(OutFolderPath, true, &Reason))
		{
			OutError = Reason.ToString();
			return false;
		}
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> StringArrayToJsonArray(const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& String : Strings)
		{
			Values.Add(MakeShared<FJsonValueString>(String));
		}
		return Values;
	}

	bool AppendExcludedPathValue(const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutExcludedPaths, FString& OutError)
	{
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}

		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				if (!AppendExcludedPathValue(Entry, OutExcludedPaths, OutError))
				{
					return false;
				}
			}
			return true;
		}

		if (Value->Type != EJson::String)
		{
			OutError = TEXT("Excluded paths must be strings or arrays of strings.");
			return false;
		}

		TArray<FString> RawPaths;
		Value->AsString().ParseIntoArray(RawPaths, TEXT(","), true);
		for (FString RawPath : RawPaths)
		{
			RawPath.TrimStartAndEndInline();
			if (RawPath.IsEmpty())
			{
				continue;
			}

			FString NormalizedPath;
			if (!NormalizeFolderPath(RawPath, NormalizedPath, OutError))
			{
				OutError = FString::Printf(TEXT("Invalid excluded path '%s': %s"), *RawPath, *OutError);
				return false;
			}
			OutExcludedPaths.AddUnique(NormalizedPath);
		}
		return true;
	}

	bool GetExcludedPackagePaths(const TSharedPtr<FJsonObject>& Arguments, TArray<FString>& OutExcludedPaths, FString& OutError)
	{
		static const TCHAR* FieldNames[] = {
			TEXT("exclude_paths"),
			TEXT("excluded_paths"),
			TEXT("exclude_path_prefixes"),
			TEXT("excluded_path_prefixes")
		};

		for (const TCHAR* FieldName : FieldNames)
		{
			if (!Arguments.IsValid() || !Arguments->HasField(FieldName))
			{
				continue;
			}
			if (!AppendExcludedPathValue(Arguments->TryGetField(FieldName), OutExcludedPaths, OutError))
			{
				return false;
			}
		}

		OutExcludedPaths.Sort();
		return true;
	}

	bool IsPackagePathExcluded(const FString& PackageName, const TArray<FString>& ExcludedPaths)
	{
		for (const FString& ExcludedPath : ExcludedPaths)
		{
			if (PackageName.Equals(ExcludedPath, ESearchCase::IgnoreCase) ||
				PackageName.StartsWith(ExcludedPath + TEXT("/"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool IsAssetDataExcluded(const FAssetData& AssetData, const TArray<FString>& ExcludedPaths)
	{
		return IsPackagePathExcluded(AssetData.PackageName.ToString(), ExcludedPaths);
	}

	TSharedRef<FJsonObject> AssetDataToJson(const FAssetData& AssetData)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		Object->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		Object->SetStringField(TEXT("object_path"), AssetData.GetObjectPathString());
		Object->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
		Object->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		Object->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
		Object->SetBoolField(TEXT("loaded"), AssetData.IsAssetLoaded());
		return Object;
	}

	FString GetAssetTagString(const FAssetData& AssetData, const FName TagName)
	{
		FString Value;
		AssetData.GetTagValue(TagName, Value);
		return Value;
	}

	FString CleanAssetClassTagPath(const FString& Path)
	{
		return StripObjectExportWrapper(Path);
	}

	bool IsBlueprintAssetData(const FAssetData& AssetData)
	{
		return AssetData.AssetClassPath.ToString().Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase) ||
			!GetAssetTagString(AssetData, FBlueprintTags::GeneratedClassPath).IsEmpty() ||
			!GetAssetTagString(AssetData, FBlueprintTags::ParentClassPath).IsEmpty();
	}

	bool ClassPathTextMatches(const FString& CandidatePath, const FString& FilterPath)
	{
		if (FilterPath.IsEmpty())
		{
			return true;
		}
		const FString Candidate = CleanAssetClassTagPath(CandidatePath);
		const FString Filter = CleanAssetClassTagPath(FilterPath);
		const FString CandidateShort = FPackageName::GetShortName(Candidate).Replace(TEXT("_C"), TEXT(""));
		const FString FilterShort = FPackageName::GetShortName(Filter).Replace(TEXT("_C"), TEXT(""));
		return Candidate.Equals(Filter, ESearchCase::IgnoreCase) ||
			CandidateShort.Equals(Filter, ESearchCase::IgnoreCase) ||
			CandidateShort.Equals(FilterShort, ESearchCase::IgnoreCase) ||
			Candidate.EndsWith(FString::Printf(TEXT(".%s"), *FilterShort), ESearchCase::IgnoreCase);
	}

	bool BlueprintAssetClassMatches(const FAssetData& AssetData, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		const FString AssetClass = AssetData.AssetClassPath.ToString();
		const FString ShortClass = AssetData.AssetClassPath.GetAssetName().ToString();
		return AssetClass.Equals(Filter, ESearchCase::IgnoreCase) ||
			AssetClass.Contains(Filter, ESearchCase::IgnoreCase) ||
			ShortClass.Equals(Filter, ESearchCase::IgnoreCase);
	}

	FString BlueprintStatusToString(EBlueprintStatus Status)
	{
		switch (Status)
		{
		case BS_Unknown:
			return TEXT("Unknown");
		case BS_Dirty:
			return TEXT("Dirty");
		case BS_Error:
			return TEXT("Error");
		case BS_UpToDate:
			return TEXT("UpToDate");
		case BS_BeingCreated:
			return TEXT("BeingCreated");
		case BS_UpToDateWithWarnings:
			return TEXT("UpToDateWithWarnings");
		default:
			return TEXT("Invalid");
		}
	}

	TSharedRef<FJsonObject> BlueprintAssetDataToJson(const FAssetData& AssetData, const UBlueprint* Blueprint)
	{
		TSharedRef<FJsonObject> Object = AssetDataToJson(AssetData);
		const FString GeneratedClassTag = CleanAssetClassTagPath(GetAssetTagString(AssetData, FBlueprintTags::GeneratedClassPath));
		const FString ParentClassTag = CleanAssetClassTagPath(GetAssetTagString(AssetData, FBlueprintTags::ParentClassPath));
		const FString NativeParentClassTag = CleanAssetClassTagPath(GetAssetTagString(AssetData, FBlueprintTags::NativeParentClassPath));

		Object->SetStringField(TEXT("blueprint_asset_class"), AssetData.AssetClassPath.ToString());
		Object->SetStringField(TEXT("generated_class_tag"), GeneratedClassTag);
		Object->SetStringField(TEXT("parent_class_tag"), ParentClassTag);
		Object->SetStringField(TEXT("native_parent_class_tag"), NativeParentClassTag);
		Object->SetStringField(TEXT("blueprint_type"), GetAssetTagString(AssetData, FBlueprintTags::BlueprintType));
		Object->SetStringField(TEXT("blueprint_display_name"), GetAssetTagString(AssetData, FBlueprintTags::BlueprintDisplayName));
		Object->SetStringField(TEXT("blueprint_category"), GetAssetTagString(AssetData, FBlueprintTags::BlueprintCategory));
		Object->SetStringField(TEXT("is_data_only"), GetAssetTagString(AssetData, FBlueprintTags::IsDataOnly));

		if (Blueprint)
		{
			Object->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
			Object->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
			Object->SetStringField(TEXT("name"), Blueprint->GetName());
			Object->SetStringField(TEXT("status"), BlueprintStatusToString(Blueprint->Status));
			Object->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
			Object->SetStringField(TEXT("generated_class"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : FString());
			Object->SetStringField(TEXT("skeleton_generated_class"), Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass->GetPathName() : FString());
		}
		else
		{
			Object->SetStringField(TEXT("parent_class"), ParentClassTag);
			Object->SetStringField(TEXT("generated_class"), GeneratedClassTag);
			Object->SetStringField(TEXT("skeleton_generated_class"), FString());
		}

		return Object;
	}

	FString PinDirectionToString(EEdGraphPinDirection Direction)
	{
		return Direction == EGPD_Input ? TEXT("input") : TEXT("output");
	}

	EPinContainerType ContainerFromString(const FString& Container)
	{
		if (Container.Equals(TEXT("array"), ESearchCase::IgnoreCase))
		{
			return EPinContainerType::Array;
		}
		if (Container.Equals(TEXT("set"), ESearchCase::IgnoreCase))
		{
			return EPinContainerType::Set;
		}
		if (Container.Equals(TEXT("map"), ESearchCase::IgnoreCase))
		{
			return EPinContainerType::Map;
		}
		return EPinContainerType::None;
	}

	FString ContainerToString(EPinContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EPinContainerType::Array:
			return TEXT("array");
		case EPinContainerType::Set:
			return TEXT("set");
		case EPinContainerType::Map:
			return TEXT("map");
		default:
			return TEXT("none");
		}
	}

	FString GuidToString(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphens);
	}

	struct FBlueprintJsonOptions
	{
		bool bIncludeNodes = true;
		bool bIncludePins = true;
		bool bIncludePinTypes = true;
		bool bIncludePinLinks = true;
		bool bOmitEmpty = false;
		TSet<FString> GraphNames;
	};

	void AddStringFilterValue(const TSharedPtr<FJsonValue>& Value, TSet<FString>& OutValues)
	{
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return;
		}

		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				AddStringFilterValue(Entry, OutValues);
			}
			return;
		}

		if (Value->Type != EJson::String)
		{
			return;
		}

		TArray<FString> RawValues;
		Value->AsString().ParseIntoArray(RawValues, TEXT(","), true);
		for (FString RawValue : RawValues)
		{
			RawValue.TrimStartAndEndInline();
			if (!RawValue.IsEmpty())
			{
				OutValues.Add(RawValue);
			}
		}
	}

	FBlueprintJsonOptions GetBlueprintJsonOptions(const TSharedPtr<FJsonObject>& Arguments, const bool bDefaultIncludeNodes)
	{
		FBlueprintJsonOptions Options;
		Options.bIncludeNodes = GetOptionalBool(Arguments, TEXT("include_nodes"), bDefaultIncludeNodes);
		Options.bIncludePins = GetOptionalBool(Arguments, TEXT("include_pins"), true);
		Options.bIncludePinTypes = GetOptionalBool(Arguments, TEXT("include_pin_types"), true);
		Options.bIncludePinLinks = GetOptionalBool(Arguments, TEXT("include_pin_links"), true);
		Options.bOmitEmpty = GetOptionalBool(Arguments, TEXT("omit_empty"), true);

		if (Arguments.IsValid())
		{
			AddStringFilterValue(Arguments->TryGetField(TEXT("graph")), Options.GraphNames);
			AddStringFilterValue(Arguments->TryGetField(TEXT("graphs")), Options.GraphNames);
		}
		return Options;
	}

	bool ShouldIncludeGraph(const UEdGraph* Graph, const FBlueprintJsonOptions& Options)
	{
		if (!Graph || Options.GraphNames.IsEmpty())
		{
			return true;
		}

		for (const FString& GraphName : Options.GraphNames)
		{
			// A graph path (graphs[].path) picks one graph out of several with the same name.
			if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase) ||
				Graph->GetPathName(Graph->GetTypedOuter<UBlueprint>()).Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	TSharedRef<FJsonObject> PinTypeToJson(const FEdGraphPinType& PinType)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
		Object->SetStringField(TEXT("subcategory"), PinType.PinSubCategory.ToString());
		Object->SetStringField(TEXT("container"), ContainerToString(PinType.ContainerType));
		Object->SetBoolField(TEXT("is_reference"), PinType.bIsReference);
		Object->SetBoolField(TEXT("is_const"), PinType.bIsConst);
		if (UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get())
		{
			Object->SetStringField(TEXT("subcategory_object"), SubCategoryObject->GetPathName());
		}
		return Object;
	}

	TSharedRef<FJsonObject> PinToJson(const UEdGraphPin* Pin, const FBlueprintJsonOptions& Options = FBlueprintJsonOptions())
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), GuidToString(Pin->PinId));
		Object->SetStringField(TEXT("name"), Pin->PinName.ToString());
		Object->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
		if (Options.bIncludePinTypes)
		{
			Object->SetObjectField(TEXT("type"), PinTypeToJson(Pin->PinType));
		}

		const FString DefaultValue = Pin->GetDefaultAsString();
		if (!Options.bOmitEmpty || !DefaultValue.IsEmpty())
		{
			Object->SetStringField(TEXT("default"), DefaultValue);
		}

		if (Options.bIncludePinLinks)
		{
			TArray<TSharedPtr<FJsonValue>> Links;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNodeUnchecked())
				{
					continue;
				}

				TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
				Link->SetStringField(TEXT("node_id"), GuidToString(LinkedPin->GetOwningNode()->NodeGuid));
				Link->SetStringField(TEXT("node_name"), LinkedPin->GetOwningNode()->GetName());
				Link->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());
				Links.Add(MakeShared<FJsonValueObject>(Link));
			}

			if (!Options.bOmitEmpty || !Links.IsEmpty())
			{
				Object->SetArrayField(TEXT("links"), Links);
			}
		}
		return Object;
	}

	TSharedRef<FJsonObject> NodeToJson(const UEdGraphNode* Node, bool bIncludePins = true, const FBlueprintJsonOptions& Options = FBlueprintJsonOptions())
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), GuidToString(Node->NodeGuid));
		Object->SetStringField(TEXT("object_name"), Node->GetName());
		Object->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Object->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Object->SetNumberField(TEXT("x"), Node->NodePosX);
		Object->SetNumberField(TEXT("y"), Node->NodePosY);
		if (!Node->NodeComment.IsEmpty())
		{
			Object->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->WasTrashed())
				{
					Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin, Options)));
				}
			}
			if (!Options.bOmitEmpty || !Pins.IsEmpty())
			{
				Object->SetArrayField(TEXT("pins"), Pins);
			}
		}
		return Object;
	}

	TSharedRef<FJsonObject> BlueprintToJson(UBlueprint* Blueprint, const FBlueprintJsonOptions& Options)
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		if (!Blueprint)
		{
			return Data;
		}

		Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
		Data->SetStringField(TEXT("package"), Blueprint->GetOutermost() ? Blueprint->GetOutermost()->GetName() : FString());
		Data->SetStringField(TEXT("name"), Blueprint->GetName());
		Data->SetStringField(TEXT("status"), BlueprintStatusToString(Blueprint->Status));
		Data->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
		Data->SetStringField(TEXT("generated_class"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : FString());

		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			TSharedRef<FJsonObject> VarObject = MakeShared<FJsonObject>();
			VarObject->SetStringField(TEXT("name"), Variable.VarName.ToString());
			VarObject->SetObjectField(TEXT("type"), PinTypeToJson(Variable.VarType));
			VarObject->SetStringField(TEXT("category"), Variable.Category.ToString());
			VarObject->SetStringField(TEXT("default"), Variable.DefaultValue);
			Variables.Add(MakeShared<FJsonValueObject>(VarObject));
		}
		Data->SetArrayField(TEXT("variables"), Variables);

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		TArray<TSharedPtr<FJsonValue>> GraphValues;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || !ShouldIncludeGraph(Graph, Options))
			{
				continue;
			}

			TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
			GraphObject->SetStringField(TEXT("name"), Graph->GetName());
			// Unique even when names repeat (every transition rule graph is named "Transition"); FindGraph accepts it.
			GraphObject->SetStringField(TEXT("path"), Graph->GetPathName(Blueprint));
			GraphObject->SetStringField(TEXT("schema"), Graph->Schema ? Graph->Schema->GetPathName() : FString());
			GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

			if (Options.bIncludeNodes)
			{
				TArray<TSharedPtr<FJsonValue>> Nodes;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node)
					{
						Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Node, Options.bIncludePins, Options)));
					}
				}
				if (!Options.bOmitEmpty || !Nodes.IsEmpty())
				{
					GraphObject->SetArrayField(TEXT("nodes"), Nodes);
				}
			}
			GraphValues.Add(MakeShared<FJsonValueObject>(GraphObject));
		}
		Data->SetArrayField(TEXT("graphs"), GraphValues);
		return Data;
	}

	UEdGraph* FindBlueprintGraphByName(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}
		return nullptr;
	}

	TSharedRef<FJsonObject> GraphToJson(UEdGraph* Graph, const FBlueprintJsonOptions& Options)
	{
		TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
		if (!Graph)
		{
			return GraphObject;
		}

		GraphObject->SetStringField(TEXT("name"), Graph->GetName());
		// Unique even when names repeat (every transition rule graph is named "Transition"); FindGraph accepts it.
		GraphObject->SetStringField(TEXT("path"), Graph->GetPathName(Graph->GetTypedOuter<UBlueprint>()));
		GraphObject->SetStringField(TEXT("schema"), Graph->Schema ? Graph->Schema->GetPathName() : FString());
		GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		if (Options.bIncludeNodes)
		{
			TArray<TSharedPtr<FJsonValue>> Nodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node)
				{
					Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Node, Options.bIncludePins, Options)));
				}
			}
			if (!Options.bOmitEmpty || !Nodes.IsEmpty())
			{
				GraphObject->SetArrayField(TEXT("nodes"), Nodes);
			}
		}
		return GraphObject;
	}

	TSharedRef<FJsonObject> NodeSemanticSignatureToJson(const UEdGraphNode* Node)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (!Node)
		{
			return Object;
		}

		Object->SetStringField(TEXT("class"), Node->GetClass() ? Node->GetClass()->GetPathName() : FString());
		Object->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		if (!Node->NodeComment.IsEmpty())
		{
			Object->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->WasTrashed())
			{
				continue;
			}

			TSharedRef<FJsonObject> PinObject = MakeShared<FJsonObject>();
			PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObject->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
			PinObject->SetObjectField(TEXT("type"), PinTypeToJson(Pin->PinType));
			PinObject->SetStringField(TEXT("default"), Pin->DefaultValue);

			TArray<FString> LinkKeys;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode())
				{
					continue;
				}
				LinkKeys.Add(FString::Printf(TEXT("%s.%s"), *GuidToString(LinkedPin->GetOwningNode()->NodeGuid), *LinkedPin->PinName.ToString()));
			}
			LinkKeys.Sort();
			TArray<TSharedPtr<FJsonValue>> Links;
			for (const FString& LinkKey : LinkKeys)
			{
				Links.Add(MakeShared<FJsonValueString>(LinkKey));
			}
			PinObject->SetArrayField(TEXT("links"), Links);
			Pins.Add(MakeShared<FJsonValueObject>(PinObject));
		}
		Object->SetArrayField(TEXT("pins"), Pins);
		return Object;
	}

	FString JsonObjectStableString(const TSharedRef<FJsonObject>& Object)
	{
		return SerializeJson(Object);
	}

	TSharedRef<FJsonObject> NodeDiffPayloadToJson(UEdGraphNode* Node, const bool bIncludeNodeDetails)
	{
		if (bIncludeNodeDetails)
		{
			return NodeToJson(Node);
		}

		TSharedRef<FJsonObject> Object = NodeSemanticSignatureToJson(Node);
		if (Node)
		{
			Object->SetStringField(TEXT("object_name"), Node->GetName());
		}
		return Object;
	}

	TSharedRef<FJsonObject> BlueprintGraphDiffToJson(UEdGraph* RevisionGraph, UEdGraph* CurrentGraph, const bool bIncludeNodeDetails, const bool bIncludeUnchanged)
	{
		TMap<FString, UEdGraphNode*> RevisionNodes;
		TMap<FString, FString> RevisionSignatures;
		TMap<FString, UEdGraphNode*> CurrentNodes;
		TMap<FString, FString> CurrentSignatures;

		auto AddGraphNodes = [](UEdGraph* Graph, TMap<FString, UEdGraphNode*>& OutNodes, TMap<FString, FString>& OutSignatures)
		{
			if (!Graph)
			{
				return;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}
				const FString Key = GuidToString(Node->NodeGuid);
				OutNodes.Add(Key, Node);
				OutSignatures.Add(Key, JsonObjectStableString(NodeSemanticSignatureToJson(Node)));
			}
		};

		AddGraphNodes(RevisionGraph, RevisionNodes, RevisionSignatures);
		AddGraphNodes(CurrentGraph, CurrentNodes, CurrentSignatures);

		TArray<TSharedPtr<FJsonValue>> Added;
		TArray<TSharedPtr<FJsonValue>> Removed;
		TArray<TSharedPtr<FJsonValue>> Changed;
		TArray<TSharedPtr<FJsonValue>> Unchanged;

		for (const TPair<FString, UEdGraphNode*>& Pair : CurrentNodes)
		{
			const FString& Key = Pair.Key;
			UEdGraphNode* CurrentNode = Pair.Value;
			if (!RevisionNodes.Contains(Key))
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("node_id"), Key);
				Item->SetObjectField(TEXT("current"), NodeDiffPayloadToJson(CurrentNode, bIncludeNodeDetails));
				Added.Add(MakeShared<FJsonValueObject>(Item));
				continue;
			}

			const FString* RevisionSignature = RevisionSignatures.Find(Key);
			const FString* CurrentSignature = CurrentSignatures.Find(Key);
			if (RevisionSignature && CurrentSignature && *RevisionSignature != *CurrentSignature)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("node_id"), Key);
				Item->SetObjectField(TEXT("revision"), NodeDiffPayloadToJson(RevisionNodes[Key], bIncludeNodeDetails));
				Item->SetObjectField(TEXT("current"), NodeDiffPayloadToJson(CurrentNode, bIncludeNodeDetails));
				Changed.Add(MakeShared<FJsonValueObject>(Item));
			}
			else if (bIncludeUnchanged)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("node_id"), Key);
				Item->SetObjectField(TEXT("node"), NodeToJson(CurrentNode, false));
				Unchanged.Add(MakeShared<FJsonValueObject>(Item));
			}
		}

		for (const TPair<FString, UEdGraphNode*>& Pair : RevisionNodes)
		{
			const FString& Key = Pair.Key;
			if (CurrentNodes.Contains(Key))
			{
				continue;
			}

			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("node_id"), Key);
			Item->SetObjectField(TEXT("revision"), NodeDiffPayloadToJson(Pair.Value, bIncludeNodeDetails));
			Removed.Add(MakeShared<FJsonValueObject>(Item));
		}

		TSharedRef<FJsonObject> Diff = MakeShared<FJsonObject>();
		Diff->SetNumberField(TEXT("added_count"), Added.Num());
		Diff->SetNumberField(TEXT("removed_count"), Removed.Num());
		Diff->SetNumberField(TEXT("changed_count"), Changed.Num());
		Diff->SetNumberField(TEXT("unchanged_count"), Unchanged.Num());
		Diff->SetArrayField(TEXT("added"), Added);
		Diff->SetArrayField(TEXT("removed"), Removed);
		Diff->SetArrayField(TEXT("changed"), Changed);
		if (bIncludeUnchanged)
		{
			Diff->SetArrayField(TEXT("unchanged"), Unchanged);
		}
		return Diff;
	}

	TSharedRef<FJsonObject> FunctionToJson(const UFunction* Function)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Function->GetName());
		Object->SetStringField(TEXT("owner_class"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetPathName() : FString());
		Object->SetBoolField(TEXT("blueprint_callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
		Object->SetBoolField(TEXT("blueprint_pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
		Object->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));

		TArray<TSharedPtr<FJsonValue>> Parameters;
		for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
			Parameter->SetStringField(TEXT("name"), Property->GetName());
			Parameter->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
			Parameter->SetBoolField(TEXT("return"), Property->HasAnyPropertyFlags(CPF_ReturnParm));
			Parameter->SetBoolField(TEXT("out"), Property->HasAnyPropertyFlags(CPF_OutParm));
			Parameter->SetBoolField(TEXT("reference"), Property->HasAnyPropertyFlags(CPF_ReferenceParm));
			Parameters.Add(MakeShared<FJsonValueObject>(Parameter));
		}
		Object->SetArrayField(TEXT("parameters"), Parameters);
		return Object;
	}

	FString NormalizeEventLookupName(FString Name)
	{
		Name.TrimStartAndEndInline();
		Name.RemoveSpacesInline();
		Name.ReplaceInline(TEXT("_"), TEXT(""));
		Name.ReplaceInline(TEXT("-"), TEXT(""));
		Name.ReplaceInline(TEXT("."), TEXT(""));
		if (Name.StartsWith(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			Name.RightChopInline(5);
		}
		if (Name.StartsWith(TEXT("Receive"), ESearchCase::IgnoreCase))
		{
			Name.RightChopInline(7);
		}
		return Name.ToLower();
	}

	FString GetNativeEventDisplayName(const UFunction* Function)
	{
		if (!Function)
		{
			return FString();
		}

		FString DisplayName = UEdGraphSchema_K2::GetFriendlySignatureName(Function).ToString();
		DisplayName.TrimStartAndEndInline();
		return DisplayName.IsEmpty() ? Function->GetName() : DisplayName;
	}

	bool IsNativeEventFunction(const UFunction* Function)
	{
		const UClass* OwnerClass = Function ? Function->GetOwnerClass() : nullptr;
		return Function &&
			OwnerClass &&
			OwnerClass->HasAnyClassFlags(CLASS_Native) &&
			Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) &&
			!Function->HasAnyFunctionFlags(FUNC_Static | FUNC_Delegate) &&
			UEdGraphSchema_K2::CanKismetOverrideFunction(Function) &&
			UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function);
	}

	UClass* GetNativeEventSignatureClass(const UFunction* Function)
	{
		UClass* OwnerClass = Function ? Function->GetOwnerClass() : nullptr;
		return OwnerClass ? OwnerClass->GetAuthoritativeClass() : nullptr;
	}

	TSharedRef<FJsonObject> NativeEventToJson(const UBlueprint* Blueprint, UFunction* Function)
	{
		UClass* SignatureClass = GetNativeEventSignatureClass(Function);
		UK2Node_Event* ExistingNode = SignatureClass ? FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, SignatureClass, Function->GetFName()) : nullptr;

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), GetNativeEventDisplayName(Function));
		Object->SetStringField(TEXT("function"), Function->GetName());
		Object->SetStringField(TEXT("owner_class"), SignatureClass ? SignatureClass->GetPathName() : FString());
		Object->SetStringField(TEXT("category"), Function->GetMetaData(TEXT("Category")));
		Object->SetStringField(TEXT("keywords"), Function->GetMetaData(TEXT("Keywords")));
		Object->SetBoolField(TEXT("implemented"), ExistingNode != nullptr);
		if (ExistingNode)
		{
			Object->SetObjectField(TEXT("existing_node"), NodeToJson(ExistingNode, false));
		}

		TArray<TSharedPtr<FJsonValue>> Parameters;
		for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
			Parameter->SetStringField(TEXT("name"), Property->GetName());
			Parameter->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
			Parameters.Add(MakeShared<FJsonValueObject>(Parameter));
		}
		Object->SetArrayField(TEXT("parameters"), Parameters);
		return Object;
	}

	void GetNativeEventFunctionsForBlueprint(const UBlueprint* Blueprint, TArray<UFunction*>& OutFunctions)
	{
		OutFunctions.Reset();
		UClass* ParentClass = Blueprint ? Blueprint->ParentClass : nullptr;
		if (!ParentClass)
		{
			return;
		}

		TSet<FName> SeenNames;
		for (TFieldIterator<UFunction> FunctionIt(ParentClass, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
		{
			UFunction* Function = *FunctionIt;
			if (!IsNativeEventFunction(Function) || SeenNames.Contains(Function->GetFName()))
			{
				continue;
			}

			SeenNames.Add(Function->GetFName());
			OutFunctions.Add(Function);
		}

		TArray<UClass*> implementedInterfaces;
		FBlueprintEditorUtils::FindImplementedInterfaces(Blueprint, true, implementedInterfaces);
		for (UClass* interfaceClass : implementedInterfaces)
		{
			if (!interfaceClass)
			{
				continue;
			}

			for (TFieldIterator<UFunction> functionIt(interfaceClass, EFieldIteratorFlags::IncludeSuper); functionIt; ++functionIt)
			{
				UFunction* function = *functionIt;
				if (!IsNativeEventFunction(function) || SeenNames.Contains(function->GetFName()))
				{
					continue;
				}

				SeenNames.Add(function->GetFName());
				OutFunctions.Add(function);
			}
		}

		OutFunctions.Sort([](const UFunction& A, const UFunction& B)
		{
			return GetNativeEventDisplayName(&A) < GetNativeEventDisplayName(&B);
		});
	}

	UFunction* ResolveNativeEventFunction(const UBlueprint* Blueprint, const FString& EventName, const FString& OwnerClassPath, FString& OutError)
	{
		const FString LookupName = NormalizeEventLookupName(EventName);
		if (LookupName.IsEmpty())
		{
			OutError = TEXT("Missing required field 'event_name'.");
			return nullptr;
		}

		TArray<UFunction*> Functions;
		GetNativeEventFunctionsForBlueprint(Blueprint, Functions);
		UFunction* BestMatch = nullptr;
		for (UFunction* Function : Functions)
		{
			UClass* SignatureClass = GetNativeEventSignatureClass(Function);
			if (!OwnerClassPath.IsEmpty() && (!SignatureClass || !SignatureClass->GetPathName().Equals(OwnerClassPath, ESearchCase::IgnoreCase)))
			{
				continue;
			}

			const FString FunctionName = Function->GetName();
			const FString DisplayName = GetNativeEventDisplayName(Function);
			if (FunctionName.Equals(EventName, ESearchCase::IgnoreCase))
			{
				return Function;
			}
			if (NormalizeEventLookupName(DisplayName) == LookupName || NormalizeEventLookupName(FunctionName) == LookupName)
			{
				BestMatch = Function;
			}
		}

		if (!BestMatch)
		{
			OutError = FString::Printf(TEXT("Native event '%s' was not found for Blueprint parent class '%s'."), *EventName, Blueprint && Blueprint->ParentClass ? *Blueprint->ParentClass->GetPathName() : TEXT("<none>"));
		}
		return BestMatch;
	}

	uint32 GetAllContextTargetFlags()
	{
		uint32 Flags = 0;
		for (int32 FlagIndex = 0; FlagIndex < EContextTargetFlags::ContextTargetFlagsEnd; ++FlagIndex)
		{
			Flags |= (1 << FlagIndex);
		}
		return Flags;
	}

	void BuildBlueprintActionMenu(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<UEdGraphPin*>& ContextPins, FBlueprintActionMenuBuilder& MenuBuilder)
	{
		FBlueprintActionContext Context;
		Context.Blueprints.Add(Blueprint);
		Context.Graphs.Add(Graph);
		Context.Pins.Append(ContextPins);

		const bool bIsContextSensitive = ContextPins.Num() > 0;
		FBlueprintActionMenuUtils::MakeContextMenu(Context, bIsContextSensitive, GetAllContextTargetFlags(), MenuBuilder);

		if (const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr)
		{
			Schema->InsertAdditionalActions(Context.Blueprints, Context.Graphs, Context.Pins, MenuBuilder);
		}
	}

	FString GetActionNodeClassPath(const FEdGraphSchemaAction& Action)
	{
		if (Action.GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
		{
			const FBlueprintActionMenuItem& MenuItem = static_cast<const FBlueprintActionMenuItem&>(Action);
			const UBlueprintNodeSpawner* Spawner = MenuItem.GetRawAction();
			if (Spawner && Spawner->NodeClass)
			{
				return Spawner->NodeClass->GetPathName();
			}
		}
		return FString();
	}

	FString GetActionSpawnerSignature(const FEdGraphSchemaAction& Action)
	{
		if (Action.GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
		{
			const FBlueprintActionMenuItem& MenuItem = static_cast<const FBlueprintActionMenuItem&>(Action);
			if (const UBlueprintNodeSpawner* Spawner = MenuItem.GetRawAction())
			{
				return Spawner->GetSpawnerSignature().ToString();
			}
		}
		return FString();
	}

	FString GetBlueprintActionStableKey(const FEdGraphSchemaAction& Action)
	{
		return FString::Printf(TEXT("%s|%s|%s|%s|%s"),
			*Action.GetTypeId().ToString(),
			*GetActionNodeClassPath(Action),
			*Action.GetMenuDescription().ToString(),
			*Action.GetCategory().ToString(),
			*GetActionSpawnerSignature(Action));
	}

	FString GetBlueprintActionId(const FEdGraphSchemaAction& Action)
	{
		return FString::Printf(TEXT("bpact:%s"), *FMD5::HashAnsiString(*GetBlueprintActionStableKey(Action)));
	}

	bool ActionMatchesQuery(const FEdGraphSchemaAction& Action, const FString& Query)
	{
		if (Query.IsEmpty())
		{
			return true;
		}

		const FString QueryLower = Query.ToLower();
		const FString SearchText = FString::Printf(TEXT("%s %s %s %s %s %s"),
			*Action.GetFullSearchText(),
			*Action.GetMenuDescription().ToString(),
			*Action.GetCategory().ToString(),
			*Action.GetKeywords().ToString(),
			*GetActionNodeClassPath(Action),
			*GetActionSpawnerSignature(Action)).ToLower();
		return SearchText.Contains(QueryLower);
	}

	void AddFunctionMetadata(TSharedRef<FJsonObject> Object, const UFunction* Function)
	{
		if (!Function)
		{
			return;
		}

		TSharedRef<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
		FunctionObject->SetStringField(TEXT("name"), Function->GetName());
		FunctionObject->SetStringField(TEXT("path"), Function->GetPathName());
		FunctionObject->SetStringField(TEXT("owner_class"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetPathName() : FString());
		Object->SetObjectField(TEXT("function"), FunctionObject);
	}

	void AddFieldMetadata(TSharedRef<FJsonObject> Object, const FFieldVariant& Field)
	{
		if (!Field.IsValid())
		{
			return;
		}

		TSharedRef<FJsonObject> FieldObject = MakeShared<FJsonObject>();
		if (const UField* UFieldObject = Cast<UField>(Field.ToUObject()))
		{
			FieldObject->SetStringField(TEXT("name"), UFieldObject->GetName());
			FieldObject->SetStringField(TEXT("path"), UFieldObject->GetPathName());
			FieldObject->SetStringField(TEXT("owner_class"), UFieldObject->GetOwnerClass() ? UFieldObject->GetOwnerClass()->GetPathName() : FString());
		}
		else if (const FField* RawField = Field.ToField())
		{
			FieldObject->SetStringField(TEXT("name"), RawField->GetName());
			FieldObject->SetStringField(TEXT("path"), RawField->GetPathName());
			FieldObject->SetStringField(TEXT("owner_class"), RawField->GetOwnerClass() ? RawField->GetOwnerClass()->GetPathName() : FString());
		}
		Object->SetObjectField(TEXT("member"), FieldObject);
	}

	TSharedRef<FJsonObject> BlueprintActionToJson(const TSharedPtr<FEdGraphSchemaAction>& Action)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (!Action.IsValid())
		{
			return Object;
		}

		Object->SetStringField(TEXT("action_id"), GetBlueprintActionId(*Action));
		Object->SetStringField(TEXT("signature"), GetActionSpawnerSignature(*Action));
		Object->SetStringField(TEXT("display_name"), Action->GetMenuDescription().ToString());
		Object->SetStringField(TEXT("category"), Action->GetCategory().ToString());
		Object->SetStringField(TEXT("menu_path"), Action->GetCategory().ToString());
		Object->SetStringField(TEXT("keywords"), Action->GetKeywords().ToString());
		Object->SetStringField(TEXT("tooltip"), Action->GetTooltipDescription().ToString());
		Object->SetStringField(TEXT("action_type"), Action->GetTypeId().ToString());
		Object->SetStringField(TEXT("node_class"), GetActionNodeClassPath(*Action));

		if (Action->GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
		{
			const FBlueprintActionMenuItem& MenuItem = static_cast<const FBlueprintActionMenuItem&>(*Action);
			const UBlueprintNodeSpawner* Spawner = MenuItem.GetRawAction();
			if (Spawner)
			{
				Object->SetStringField(TEXT("spawner_class"), Spawner->GetClass()->GetPathName());

				if (const UBlueprintFunctionNodeSpawner* FunctionSpawner = Cast<UBlueprintFunctionNodeSpawner>(Spawner))
				{
					AddFunctionMetadata(Object, FunctionSpawner->GetFunction());
				}
				else if (const UBlueprintFieldNodeSpawner* FieldSpawner = Cast<UBlueprintFieldNodeSpawner>(Spawner))
				{
					AddFieldMetadata(Object, FieldSpawner->GetField());
				}
				else if (const UBlueprintAssetNodeSpawner* AssetSpawner = Cast<UBlueprintAssetNodeSpawner>(Spawner))
				{
					const FAssetData& AssetData = AssetSpawner->GetAssetData();
					TSharedRef<FJsonObject> AssetObject = MakeShared<FJsonObject>();
					AssetObject->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
					AssetObject->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
					Object->SetObjectField(TEXT("asset"), AssetObject);
				}

				if (const UK2Node_DynamicCast* CastTemplate = Cast<UK2Node_DynamicCast>(Spawner->GetTemplateNode()))
				{
					TSharedRef<FJsonObject> ClassObject = MakeShared<FJsonObject>();
					ClassObject->SetStringField(TEXT("path"), CastTemplate->TargetType ? CastTemplate->TargetType->GetPathName() : FString());
					ClassObject->SetStringField(TEXT("name"), CastTemplate->TargetType ? CastTemplate->TargetType->GetName() : FString());
					Object->SetObjectField(TEXT("target_class"), ClassObject);
				}
			}
		}

		return Object;
	}

	bool BlueprintActionMatchesMetadataFilters(
		const TSharedPtr<FEdGraphSchemaAction>& Action,
		const FString& NodeClassFilter,
		const FString& FunctionPathFilter,
		const FString& MemberPathFilter,
		const FString& DisplayNameFilter)
	{
		if (!Action.IsValid())
		{
			return false;
		}

		if (!NodeClassFilter.IsEmpty() && !GetActionNodeClassPath(*Action).Equals(NodeClassFilter, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (!DisplayNameFilter.IsEmpty() && !Action->GetMenuDescription().ToString().Equals(DisplayNameFilter, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (FunctionPathFilter.IsEmpty() && MemberPathFilter.IsEmpty())
		{
			return true;
		}

		if (Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
		{
			return false;
		}

		const FBlueprintActionMenuItem& MenuItem = static_cast<const FBlueprintActionMenuItem&>(*Action);
		const UBlueprintNodeSpawner* Spawner = MenuItem.GetRawAction();
		if (!Spawner)
		{
			return false;
		}

		if (!FunctionPathFilter.IsEmpty())
		{
			const UBlueprintFunctionNodeSpawner* FunctionSpawner = Cast<UBlueprintFunctionNodeSpawner>(Spawner);
			const UFunction* Function = FunctionSpawner ? FunctionSpawner->GetFunction() : nullptr;
			if (!Function || !Function->GetPathName().Equals(FunctionPathFilter, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}

		if (!MemberPathFilter.IsEmpty())
		{
			const UBlueprintFieldNodeSpawner* FieldSpawner = Cast<UBlueprintFieldNodeSpawner>(Spawner);
			const FFieldVariant Field = FieldSpawner ? FieldSpawner->GetField() : FFieldVariant();
			FString FieldPath;
			if (const UField* UFieldObject = Cast<UField>(Field.ToUObject()))
			{
				FieldPath = UFieldObject->GetPathName();
			}
			else if (const FField* RawField = Field.ToField())
			{
				FieldPath = RawField->GetPathName();
			}
			if (!FieldPath.Equals(MemberPathFilter, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}

		return true;
	}

	UFunction* GetCallNodeTargetFunction(const UK2Node_CallFunction* CallNode)
	{
		if (!CallNode)
		{
			return nullptr;
		}
		return CallNode->GetTargetFunction();
	}

	FName GetCallNodeFunctionName(const UK2Node_CallFunction* CallNode)
	{
		if (UFunction* Function = GetCallNodeTargetFunction(CallNode))
		{
			return Function->GetFName();
		}
		return CallNode ? CallNode->FunctionReference.GetMemberName() : NAME_None;
	}

	UClass* GetCallNodeOwnerClass(const UK2Node_CallFunction* CallNode)
	{
		if (UFunction* Function = GetCallNodeTargetFunction(CallNode))
		{
			return Function->GetOwnerClass();
		}
		return CallNode ? CallNode->FunctionReference.GetMemberParentClass() : nullptr;
	}

	bool GeneratedClassMatches(UClass* A, UClass* B)
	{
		if (!A || !B)
		{
			return false;
		}
		if (A == B || A->GetAuthoritativeClass() == B->GetAuthoritativeClass())
		{
			return true;
		}
		if (A->GetPathName().Equals(B->GetPathName(), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (A->ClassGeneratedBy && A->ClassGeneratedBy == B->ClassGeneratedBy)
		{
			return true;
		}
		return false;
	}

	bool CallNodeMatchesFunction(const UK2Node_CallFunction* CallNode, const FString& FunctionName, UClass* OwnerClassFilter, const FString& FunctionPathFilter)
	{
		if (!CallNode)
		{
			return false;
		}

		const UFunction* TargetFunction = GetCallNodeTargetFunction(CallNode);
		const FName CallFunctionName = GetCallNodeFunctionName(CallNode);
		if (!FunctionName.IsEmpty() && !CallFunctionName.ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (!FunctionPathFilter.IsEmpty())
		{
			const FString TargetPath = TargetFunction ? TargetFunction->GetPathName() : FString();
			const UClass* OwnerClass = GetCallNodeOwnerClass(CallNode);
			const FString ReferencePath = OwnerClass ? FString::Printf(TEXT("%s:%s"), *OwnerClass->GetPathName(), *CallFunctionName.ToString()) : FString();
			if (!TargetPath.Equals(FunctionPathFilter, ESearchCase::IgnoreCase) && !ReferencePath.Equals(FunctionPathFilter, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}

		if (OwnerClassFilter && !GeneratedClassMatches(GetCallNodeOwnerClass(CallNode), OwnerClassFilter))
		{
			return false;
		}

		return true;
	}

	TSharedRef<FJsonObject> CallNodeTargetToJson(const UK2Node_CallFunction* CallNode)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		const UFunction* TargetFunction = GetCallNodeTargetFunction(CallNode);
		const UClass* OwnerClass = GetCallNodeOwnerClass(CallNode);
		Object->SetStringField(TEXT("name"), GetCallNodeFunctionName(CallNode).ToString());
		Object->SetStringField(TEXT("path"), TargetFunction ? TargetFunction->GetPathName() : FString());
		Object->SetStringField(TEXT("owner_class"), OwnerClass ? OwnerClass->GetPathName() : FString());
		return Object;
	}

	TSharedRef<FJsonObject> FunctionReferenceToJson(const UBlueprint* Blueprint, const UEdGraph* Graph, const UK2Node_CallFunction* CallNode, const bool bIncludeNode)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("asset_path"), Blueprint ? Blueprint->GetPathName() : FString());
		Object->SetStringField(TEXT("package"), Blueprint && Blueprint->GetOutermost() ? Blueprint->GetOutermost()->GetName() : FString());
		Object->SetStringField(TEXT("graph"), Graph ? Graph->GetName() : FString());
		Object->SetStringField(TEXT("node_id"), CallNode ? GuidToString(CallNode->NodeGuid) : FString());
		Object->SetStringField(TEXT("node_name"), CallNode ? CallNode->GetName() : FString());
		Object->SetStringField(TEXT("title"), CallNode ? CallNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString());
		Object->SetObjectField(TEXT("function"), CallNodeTargetToJson(CallNode));
		if (bIncludeNode && CallNode)
		{
			Object->SetObjectField(TEXT("node"), NodeToJson(CallNode));
		}
		return Object;
	}

	void AppendFunctionReferencesForBlueprint(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		UClass* OwnerClassFilter,
		const FString& FunctionPathFilter,
		const bool bIncludeNodes,
		const int32 Limit,
		TArray<TSharedPtr<FJsonValue>>& OutReferences)
	{
		if (!Blueprint || OutReferences.Num() >= Limit)
		{
			return;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || OutReferences.Num() >= Limit)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (!CallNode || !CallNodeMatchesFunction(CallNode, FunctionName, OwnerClassFilter, FunctionPathFilter))
				{
					continue;
				}

				OutReferences.Add(MakeShared<FJsonValueObject>(FunctionReferenceToJson(Blueprint, Graph, CallNode, bIncludeNodes)));
				if (OutReferences.Num() >= Limit)
				{
					break;
				}
			}
		}
	}

	UClass* GetVariableNodeOwnerClass(const UBlueprint* Blueprint, const UK2Node_Variable* VariableNode)
	{
		if (!VariableNode)
		{
			return nullptr;
		}

		UClass* OwnerClass = VariableNode->VariableReference.GetMemberParentClass();
		if (!OwnerClass && VariableNode->VariableReference.IsSelfContext())
		{
			OwnerClass = Blueprint && Blueprint->GeneratedClass ? Blueprint->GeneratedClass : nullptr;
			if (!OwnerClass && Blueprint)
			{
				OwnerClass = Blueprint->SkeletonGeneratedClass;
			}
		}
		return OwnerClass;
	}

	bool VariableNodeMatchesVariable(const UBlueprint* Blueprint, const UK2Node_Variable* VariableNode, const FString& VariableName, UClass* OwnerClassFilter)
	{
		if (!VariableNode || VariableName.IsEmpty())
		{
			return false;
		}

		const FName MemberName = VariableNode->VariableReference.GetMemberName();
		if (MemberName.IsNone() || !MemberName.ToString().Equals(VariableName, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (OwnerClassFilter && !GeneratedClassMatches(GetVariableNodeOwnerClass(Blueprint, VariableNode), OwnerClassFilter))
		{
			return false;
		}

		return true;
	}

	TSharedRef<FJsonObject> VariableReferenceToJson(const UBlueprint* Blueprint, const UEdGraph* Graph, const UK2Node_Variable* VariableNode, const bool bIncludeNode)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("asset_path"), Blueprint ? Blueprint->GetPathName() : FString());
		Object->SetStringField(TEXT("package"), Blueprint && Blueprint->GetOutermost() ? Blueprint->GetOutermost()->GetName() : FString());
		Object->SetStringField(TEXT("graph"), Graph ? Graph->GetName() : FString());
		Object->SetStringField(TEXT("node_id"), VariableNode ? GuidToString(VariableNode->NodeGuid) : FString());
		Object->SetStringField(TEXT("node_name"), VariableNode ? VariableNode->GetName() : FString());
		Object->SetStringField(TEXT("title"), VariableNode ? VariableNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString());
		Object->SetStringField(TEXT("access"), Cast<UK2Node_VariableSet>(VariableNode) ? TEXT("set") : (Cast<UK2Node_VariableGet>(VariableNode) ? TEXT("get") : TEXT("variable")));

		TSharedRef<FJsonObject> VariableObject = MakeShared<FJsonObject>();
		FString ReferencedVariableName;
		if (VariableNode)
		{
			ReferencedVariableName = VariableNode->VariableReference.GetMemberName().ToString();
		}
		VariableObject->SetStringField(TEXT("name"), ReferencedVariableName);
		UClass* OwnerClass = GetVariableNodeOwnerClass(Blueprint, VariableNode);
		VariableObject->SetStringField(TEXT("owner_class"), OwnerClass ? OwnerClass->GetPathName() : FString());
		Object->SetObjectField(TEXT("variable"), VariableObject);

		if (bIncludeNode && VariableNode)
		{
			Object->SetObjectField(TEXT("node"), NodeToJson(VariableNode));
		}
		return Object;
	}

	void AppendVariableReferencesForBlueprint(
		UBlueprint* Blueprint,
		const FString& VariableName,
		UClass* OwnerClassFilter,
		const bool bIncludeNodes,
		const int32 Limit,
		TArray<TSharedPtr<FJsonValue>>& OutReferences)
	{
		if (!Blueprint || OutReferences.Num() >= Limit)
		{
			return;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || OutReferences.Num() >= Limit)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
				if (!VariableNode || !VariableNodeMatchesVariable(Blueprint, VariableNode, VariableName, OwnerClassFilter))
				{
					continue;
				}

				OutReferences.Add(MakeShared<FJsonValueObject>(VariableReferenceToJson(Blueprint, Graph, VariableNode, bIncludeNodes)));
				if (OutReferences.Num() >= Limit)
				{
					break;
				}
			}
		}
	}

	bool FindNewVariableDescription(UBlueprint* Blueprint, const FString& VariableName, FBPVariableDescription& OutVariable)
	{
		if (!Blueprint)
		{
			return false;
		}

		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName.ToString().Equals(VariableName, ESearchCase::IgnoreCase))
			{
				OutVariable = Variable;
				return true;
			}
		}
		return false;
	}

	TSharedRef<FJsonObject> VectorToJson(const FVector& Vector)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return Object;
	}

	TSharedRef<FJsonObject> RotatorToJson(const FRotator& Rotator)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
		Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
		Object->SetNumberField(TEXT("roll"), Rotator.Roll);
		return Object;
	}

	bool TryParseVectorValue(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (Object.IsValid() &&
				Object->TryGetNumberField(TEXT("x"), X) &&
				Object->TryGetNumberField(TEXT("y"), Y) &&
				Object->TryGetNumberField(TEXT("z"), Z))
			{
				OutVector = FVector(X, Y, Z);
				return true;
			}
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
			if (Array.Num() == 3)
			{
				OutVector = FVector(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber());
				return true;
			}
		}
		else if (Value->Type == EJson::String)
		{
			FVector ParsedVector;
			if (ParsedVector.InitFromString(Value->AsString()))
			{
				OutVector = ParsedVector;
				return true;
			}
		}
		return false;
	}

	bool TryParseRotatorValue(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			if (Object.IsValid() &&
				Object->TryGetNumberField(TEXT("pitch"), Pitch) &&
				Object->TryGetNumberField(TEXT("yaw"), Yaw) &&
				Object->TryGetNumberField(TEXT("roll"), Roll))
			{
				OutRotator = FRotator(Pitch, Yaw, Roll);
				return true;
			}
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
			if (Array.Num() == 3)
			{
				OutRotator = FRotator(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber());
				return true;
			}
		}
		else if (Value->Type == EJson::String)
		{
			FRotator ParsedRotator;
			if (ParsedRotator.InitFromString(Value->AsString()))
			{
				OutRotator = ParsedRotator;
				return true;
			}
		}
		return false;
	}

	TSharedRef<FJsonObject> TransformToJson(const FTransform& Transform)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetObjectField(TEXT("location"), VectorToJson(Transform.GetLocation()));
		Object->SetObjectField(TEXT("rotation"), RotatorToJson(Transform.Rotator()));
		Object->SetObjectField(TEXT("scale"), VectorToJson(Transform.GetScale3D()));
		return Object;
	}

	TSharedRef<FJsonObject> ColorToJson(const FColor& Color)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("r"), Color.R);
		Object->SetNumberField(TEXT("g"), Color.G);
		Object->SetNumberField(TEXT("b"), Color.B);
		Object->SetNumberField(TEXT("a"), Color.A);
		return Object;
	}

	TSharedRef<FJsonObject> LinearColorToJson(const FLinearColor& Color)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("r"), Color.R);
		Object->SetNumberField(TEXT("g"), Color.G);
		Object->SetNumberField(TEXT("b"), Color.B);
		Object->SetNumberField(TEXT("a"), Color.A);
		return Object;
	}

	bool TryParseTransformValue(const TSharedPtr<FJsonValue>& Value, FTransform& OutTransform)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (!Object.IsValid())
			{
				return false;
			}

			FVector Location = OutTransform.GetLocation();
			FRotator Rotation = OutTransform.Rotator();
			FVector Scale = OutTransform.GetScale3D();
			const TSharedPtr<FJsonValue> LocationValue = Object->TryGetField(TEXT("location"));
			const TSharedPtr<FJsonValue> RotationValue = Object->TryGetField(TEXT("rotation"));
			const TSharedPtr<FJsonValue> ScaleValue = Object->TryGetField(TEXT("scale"));
			if (LocationValue.IsValid() && !TryParseVectorValue(LocationValue, Location))
			{
				return false;
			}
			if (RotationValue.IsValid() && !TryParseRotatorValue(RotationValue, Rotation))
			{
				return false;
			}
			if (ScaleValue.IsValid() && !TryParseVectorValue(ScaleValue, Scale))
			{
				return false;
			}

			OutTransform = FTransform(Rotation, Location, Scale);
			return true;
		}
		if (Value->Type == EJson::String)
		{
			FTransform ParsedTransform;
			if (ParsedTransform.InitFromString(Value->AsString()))
			{
				OutTransform = ParsedTransform;
				return true;
			}
		}
		return false;
	}

	bool TryParseColorValue(const TSharedPtr<FJsonValue>& Value, FColor& OutColor)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			int32 R = 0;
			int32 G = 0;
			int32 B = 0;
			int32 A = 255;
			if (Object.IsValid() &&
				Object->TryGetNumberField(TEXT("r"), R) &&
				Object->TryGetNumberField(TEXT("g"), G) &&
				Object->TryGetNumberField(TEXT("b"), B))
			{
				Object->TryGetNumberField(TEXT("a"), A);
				OutColor = FColor(FMath::Clamp(R, 0, 255), FMath::Clamp(G, 0, 255), FMath::Clamp(B, 0, 255), FMath::Clamp(A, 0, 255));
				return true;
			}
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
			if (Array.Num() == 3 || Array.Num() == 4)
			{
				OutColor = FColor(
					FMath::Clamp(FMath::RoundToInt(Array[0]->AsNumber()), 0, 255),
					FMath::Clamp(FMath::RoundToInt(Array[1]->AsNumber()), 0, 255),
					FMath::Clamp(FMath::RoundToInt(Array[2]->AsNumber()), 0, 255),
					Array.Num() == 4 ? FMath::Clamp(FMath::RoundToInt(Array[3]->AsNumber()), 0, 255) : 255);
				return true;
			}
		}
		else if (Value->Type == EJson::String)
		{
			FColor ParsedColor;
			if (ParsedColor.InitFromString(Value->AsString()))
			{
				OutColor = ParsedColor;
				return true;
			}
		}
		return false;
	}

	bool TryParseLinearColorValue(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double R = 0.0;
			double G = 0.0;
			double B = 0.0;
			double A = 1.0;
			if (Object.IsValid() &&
				Object->TryGetNumberField(TEXT("r"), R) &&
				Object->TryGetNumberField(TEXT("g"), G) &&
				Object->TryGetNumberField(TEXT("b"), B))
			{
				Object->TryGetNumberField(TEXT("a"), A);
				OutColor = FLinearColor(R, G, B, A);
				return true;
			}
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
			if (Array.Num() == 3 || Array.Num() == 4)
			{
				OutColor = FLinearColor(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber(), Array.Num() == 4 ? Array[3]->AsNumber() : 1.0);
				return true;
			}
		}
		return false;
	}

	FString JsonValueToCompactString(const TSharedPtr<FJsonValue>& Value)
	{
		FString TextValue;
		if (!Value.IsValid())
		{
			return TextValue;
		}

		if (Value->Type == EJson::String)
		{
			return Value->AsString();
		}
		if (Value->Type == EJson::Number)
		{
			return FString::SanitizeFloat(Value->AsNumber());
		}
		if (Value->Type == EJson::Boolean)
		{
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		}

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TextValue);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return TextValue;
	}

	FString ExportPropertyText(const FProperty* Property, const void* PropertyData, UObject* Owner)
	{
		FString ExportedValue;
		if (Property && PropertyData)
		{
			Property->ExportText_Direct(ExportedValue, PropertyData, nullptr, Owner, PPF_None);
		}
		return ExportedValue;
	}

	FString GetPropertyTypeName(const FProperty* Property)
	{
		if (CastField<FBoolProperty>(Property))
		{
			return TEXT("bool");
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum() ? TEXT("enum") : TEXT("int");
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum ? TEXT("enum") : TEXT("int");
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				return CastField<FDoubleProperty>(Property) ? TEXT("double") : TEXT("float");
			}
			return Property && Property->GetSize() >= 8 ? TEXT("int64") : TEXT("int");
		}
		if (CastField<FStrProperty>(Property))
		{
			return TEXT("string");
		}
		if (CastField<FNameProperty>(Property))
		{
			return TEXT("name");
		}
		if (CastField<FTextProperty>(Property))
		{
			return TEXT("text");
		}
		if (CastField<FSoftClassProperty>(Property))
		{
			return TEXT("soft_class");
		}
		if (CastField<FSoftObjectProperty>(Property))
		{
			return TEXT("soft_object");
		}
		if (CastField<FClassProperty>(Property))
		{
			return TEXT("class");
		}
		if (CastField<FObjectPropertyBase>(Property))
		{
			return TEXT("object");
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("struct");
		}
		if (CastField<FArrayProperty>(Property))
		{
			return TEXT("array");
		}
		return TEXT("unsupported");
	}

	bool IsEditableDefaultProperty(const FProperty* Property)
	{
		return Property &&
			Property->HasAnyPropertyFlags(CPF_Edit) &&
			!Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate);
	}

	// Placed instances may also hold instance-only values, such as Blueprint actor-reference
	// variables, which carry CPF_DisableEditOnTemplate and are refused on defaults.
	bool IsEditableInstanceProperty(const FProperty* Property)
	{
		return Property &&
			Property->HasAnyPropertyFlags(CPF_Edit) &&
			!Property->HasAnyPropertyFlags(CPF_Transient);
	}

	TSharedPtr<FJsonValue> ExportPropertyValueToJson(const FProperty* Property, const void* PropertyData, UObject* Owner, bool& bOutSupported, FString& OutError);
	UObject* ResolveObjectReference(const FString& InPath, UClass* ExpectedClass);
	UClass* ResolveClassReference(const FString& InPath);

	int64 ResolveEnumValueByString(UEnum* Enum, const FString& ValueString)
	{
		if (!Enum)
		{
			return INDEX_NONE;
		}

		int64 EnumValue = Enum->GetValueByNameString(ValueString);
		if (EnumValue != INDEX_NONE)
		{
			return EnumValue;
		}

		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			const FString FullName = Enum->GetNameStringByIndex(Index);
			FString ShortName = FullName;
			int32 ScopeIndex = INDEX_NONE;
			if (ShortName.FindLastChar(TEXT(':'), ScopeIndex))
			{
				ShortName = ShortName.Mid(ScopeIndex + 1);
			}
			int32 PrefixIndex = INDEX_NONE;
			if (ShortName.FindLastChar(TEXT('_'), PrefixIndex))
			{
				const FString WithoutPrefix = ShortName.Mid(PrefixIndex + 1);
				if (WithoutPrefix.Equals(ValueString, ESearchCase::IgnoreCase))
				{
					return Enum->GetValueByIndex(Index);
				}
			}
			if (ShortName.Equals(ValueString, ESearchCase::IgnoreCase))
			{
				return Enum->GetValueByIndex(Index);
			}
		}

		return INDEX_NONE;
	}

	bool JsonToGameplayTagContainer(const TSharedPtr<FJsonValue>& Value, FGameplayTagContainer& OutContainer, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
		TSharedPtr<FJsonObject> ValueObject;
		if (Value.IsValid() && Value->Type == EJson::Array)
		{
			TagsArray = &Value->AsArray();
		}
		else if (Value.IsValid() && Value->Type == EJson::Object)
		{
			ValueObject = Value->AsObject();
			if (!TryGetArray(ValueObject, TEXT("tags"), TagsArray))
			{
				OutError = TEXT("Gameplay tag container object expects a 'tags' array.");
				return false;
			}
		}
		else
		{
			OutError = TEXT("Gameplay tag container expects a string array or {\"tags\": [...]}.");
			return false;
		}

		FGameplayTagContainer NewContainer;
		for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
		{
			if (!TagValue.IsValid() || TagValue->Type != EJson::String)
			{
				OutError = TEXT("Gameplay tag container entries must be strings.");
				return false;
			}
			const FString TagName = TagValue->AsString();
			const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagName), false);
			if (!Tag.IsValid())
			{
				OutError = FString::Printf(TEXT("Gameplay tag '%s' does not exist."), *TagName);
				return false;
			}
			NewContainer.AddTag(Tag);
		}

		OutContainer = NewContainer;
		return true;
	}

	TSharedPtr<FJsonValue> GameplayTagContainerToJsonValue(const FGameplayTagContainer& Container)
	{
		TArray<FGameplayTag> Tags;
		Container.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});

		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FGameplayTag& Tag : Tags)
		{
			Values.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		return MakeShared<FJsonValueArray>(Values);
	}

	TSharedRef<FJsonObject> PropertyToJson(UObject* Owner, const FProperty* Property, const void* PropertyData, const FString& PropertyPath, const FString& TargetKind, const FString& ComponentName)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Property ? Property->GetName() : FString());
		Object->SetStringField(TEXT("path"), PropertyPath);
		Object->SetStringField(TEXT("property_path"), PropertyPath);
		Object->SetStringField(TEXT("type"), GetPropertyTypeName(Property));
		Object->SetStringField(TEXT("cpp_type"), Property ? Property->GetCPPType() : FString());
		Object->SetStringField(TEXT("property_class"), Property ? Property->GetClass()->GetName() : FString());
		Object->SetStringField(TEXT("target"), TargetKind);
		Object->SetStringField(TEXT("object_path"), Owner ? Owner->GetPathName() : FString());
		Object->SetBoolField(TEXT("editable"), IsEditableDefaultProperty(Property));
		if (!ComponentName.IsEmpty())
		{
			Object->SetStringField(TEXT("component"), ComponentName);
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			if (UEnum* Enum = EnumProperty->GetEnum())
			{
				Object->SetStringField(TEXT("enum"), Enum->GetPathName());
			}
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (ByteProperty->Enum)
			{
				Object->SetStringField(TEXT("enum"), ByteProperty->Enum->GetPathName());
			}
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			Object->SetStringField(TEXT("value_class"), ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetPathName() : FString());
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			Object->SetStringField(TEXT("inner_type"), GetPropertyTypeName(ArrayProperty->Inner));
			Object->SetStringField(TEXT("inner_cpp_type"), ArrayProperty->Inner ? ArrayProperty->Inner->GetCPPType() : FString());
		}

		bool bSupported = false;
		FString ExportError;
		const TSharedPtr<FJsonValue> JsonValue = ExportPropertyValueToJson(Property, PropertyData, Owner, bSupported, ExportError);
		Object->SetBoolField(TEXT("supported"), bSupported);
		if (JsonValue.IsValid())
		{
			Object->SetField(TEXT("value"), JsonValue);
		}
		if (!ExportError.IsEmpty())
		{
			Object->SetStringField(TEXT("unsupported_reason"), ExportError);
		}
		Object->SetStringField(TEXT("value_text"), ExportPropertyText(Property, PropertyData, Owner));

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct && !bSupported)
			{
				TArray<TSharedPtr<FJsonValue>> Fields;
				for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); FieldIt; ++FieldIt)
				{
					const FProperty* InnerProperty = *FieldIt;
					if (!IsEditableDefaultProperty(InnerProperty))
					{
						continue;
					}
					const void* InnerData = InnerProperty->ContainerPtrToValuePtr<void>(PropertyData);
					Fields.Add(MakeShared<FJsonValueObject>(PropertyToJson(Owner, InnerProperty, InnerData, FString::Printf(TEXT("%s.%s"), *PropertyPath, *InnerProperty->GetName()), TargetKind, ComponentName)));
				}
				Object->SetArrayField(TEXT("fields"), Fields);
			}
		}

		return Object;
	}

	TSharedPtr<FJsonValue> ExportPropertyValueToJson(const FProperty* Property, const void* PropertyData, UObject* Owner, bool& bOutSupported, FString& OutError)
	{
		bOutSupported = true;
		OutError.Reset();
		if (!Property || !PropertyData)
		{
			bOutSupported = false;
			OutError = TEXT("Invalid property export target.");
			return nullptr;
		}

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(PropertyData));
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropertyData);
			return MakeShared<FJsonValueString>(EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetNameStringByValue(EnumValue) : LexToString(EnumValue));
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			const uint8 ByteValue = ByteProperty->GetPropertyValue(PropertyData);
			if (ByteProperty->Enum)
			{
				return MakeShared<FJsonValueString>(ByteProperty->Enum->GetNameStringByValue(ByteValue));
			}
			return MakeShared<FJsonValueNumber>(ByteValue);
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(PropertyData));
			}
			if (NumericProperty->CanHoldValue(-1))
			{
				return MakeShared<FJsonValueNumber>(static_cast<double>(NumericProperty->GetSignedIntPropertyValue(PropertyData)));
			}
			return MakeShared<FJsonValueNumber>(static_cast<double>(NumericProperty->GetUnsignedIntPropertyValue(PropertyData)));
		}
		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(PropertyData));
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(PropertyData).ToString());
		}
		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(PropertyData).ToString());
		}
		if (CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr& SoftObject = *static_cast<const FSoftObjectPtr*>(PropertyData);
			return MakeShared<FJsonValueString>(SoftObject.ToSoftObjectPath().ToString());
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(PropertyData);
			return MakeShared<FJsonValueString>(ObjectValue ? ObjectValue->GetPathName() : FString());
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct())
			{
				const FGameplayTag& Tag = *static_cast<const FGameplayTag*>(PropertyData);
				return MakeShared<FJsonValueString>(Tag.IsValid() ? Tag.ToString() : FString());
			}
			if (StructProperty->Struct == FGameplayTagContainer::StaticStruct())
			{
				return GameplayTagContainerToJsonValue(*static_cast<const FGameplayTagContainer*>(PropertyData));
			}
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				return MakeShared<FJsonValueObject>(VectorToJson(*static_cast<const FVector*>(PropertyData)));
			}
			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				return MakeShared<FJsonValueObject>(RotatorToJson(*static_cast<const FRotator*>(PropertyData)));
			}
			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				return MakeShared<FJsonValueObject>(TransformToJson(*static_cast<const FTransform*>(PropertyData)));
			}
			if (StructProperty->Struct == TBaseStructure<FColor>::Get())
			{
				return MakeShared<FJsonValueObject>(ColorToJson(*static_cast<const FColor*>(PropertyData)));
			}
			if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
			{
				return MakeShared<FJsonValueObject>(LinearColorToJson(*static_cast<const FLinearColor*>(PropertyData)));
			}

			bOutSupported = false;
			OutError = FString::Printf(TEXT("Struct type '%s' is not settable as a whole; use editable nested field paths when available."), StructProperty->Struct ? *StructProperty->Struct->GetName() : TEXT("<unknown>"));
			return nullptr;
		}
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			FScriptArrayHelper Helper(ArrayProperty, PropertyData);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				bool bElementSupported = false;
				FString ElementError;
				TSharedPtr<FJsonValue> ElementValue = ExportPropertyValueToJson(ArrayProperty->Inner, Helper.GetRawPtr(Index), Owner, bElementSupported, ElementError);
				if (!bElementSupported || !ElementValue.IsValid())
				{
					bOutSupported = false;
					OutError = FString::Printf(TEXT("Array property '%s' has unsupported element type '%s'."), *Property->GetName(), ArrayProperty->Inner ? *ArrayProperty->Inner->GetCPPType() : TEXT("<null>"));
					return nullptr;
				}
				Values.Add(ElementValue);
			}
			return MakeShared<FJsonValueArray>(Values);
		}

		bOutSupported = false;
		OutError = FString::Printf(TEXT("Property type '%s' is not supported."), *Property->GetCPPType());
		return nullptr;
	}

	/**
	 * Writes a value in Unreal's property text format (what the editor's copy/paste produces), e.g.
	 * `((BranchFilters=((BoneName="spine_01",BlendDepth=1))))` for an array of FInputBlendPose. Used for
	 * structs and arrays that have no dedicated JSON form, so they can still be set without a nested path.
	 */
	bool ImportPropertyText(FProperty* Property, void* PropertyData, const FString& Text, FString& OutError)
	{
		FStringOutputDevice ImportErrors;
		const TCHAR* End = Property->ImportText_Direct(*Text, PropertyData, nullptr, PPF_None, &ImportErrors);
		if (!End || !ImportErrors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Property '%s' could not import text '%s'%s%s"), *Property->GetName(), *Text,
				ImportErrors.IsEmpty() ? TEXT("") : TEXT(": "), *ImportErrors);
			return false;
		}
		return true;
	}

	bool SetPropertyValueFromJson(FProperty* Property, void* PropertyData, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Property || !PropertyData || !Value.IsValid())
		{
			OutError = TEXT("Invalid property set target.");
			return false;
		}

		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			bool bNewValue = false;
			if (Value->Type == EJson::Boolean)
			{
				bNewValue = Value->AsBool();
			}
			else if (Value->Type == EJson::String)
			{
				const FString StringValue = Value->AsString();
				bNewValue = StringValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || StringValue == TEXT("1");
			}
			else
			{
				OutError = FString::Printf(TEXT("Bool property '%s' expects a boolean or boolean string."), *Property->GetName());
				return false;
			}
			BoolProperty->SetPropertyValue(PropertyData, bNewValue);
			return true;
		}
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			int64 EnumValue = 0;
			if (Value->Type == EJson::String)
			{
				EnumValue = ResolveEnumValueByString(EnumProperty->GetEnum(), Value->AsString());
				if (EnumValue == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Enum property '%s' does not have value '%s'."), *Property->GetName(), *Value->AsString());
					return false;
				}
			}
			else if (Value->Type == EJson::Number)
			{
				EnumValue = static_cast<int64>(Value->AsNumber());
			}
			else
			{
				OutError = FString::Printf(TEXT("Enum property '%s' expects a string or number."), *Property->GetName());
				return false;
			}
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(PropertyData, EnumValue);
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			int64 ByteValue = 0;
			if (ByteProperty->Enum && Value->Type == EJson::String)
			{
				ByteValue = ResolveEnumValueByString(ByteProperty->Enum, Value->AsString());
				if (ByteValue == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Enum byte property '%s' does not have value '%s'."), *Property->GetName(), *Value->AsString());
					return false;
				}
			}
			else if (Value->Type == EJson::Number)
			{
				ByteValue = static_cast<int64>(Value->AsNumber());
			}
			else
			{
				OutError = FString::Printf(TEXT("Byte property '%s' expects a number%s."), *Property->GetName(), ByteProperty->Enum ? TEXT(" or enum string") : TEXT(""));
				return false;
			}
			ByteProperty->SetIntPropertyValue(PropertyData, ByteValue);
			return true;
		}
		if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				double NumberValue = 0.0;
				if (Value->Type == EJson::Number)
				{
					NumberValue = Value->AsNumber();
				}
				else if (Value->Type == EJson::String)
				{
					NumberValue = FCString::Atod(*Value->AsString());
				}
				else
				{
					OutError = FString::Printf(TEXT("Numeric property '%s' expects a number."), *Property->GetName());
					return false;
				}
				NumericProperty->SetFloatingPointPropertyValue(PropertyData, NumberValue);
				return true;
			}

			int64 IntValue = 0;
			if (Value->Type == EJson::Number)
			{
				IntValue = static_cast<int64>(Value->AsNumber());
			}
			else if (Value->Type == EJson::String)
			{
				IntValue = FCString::Atoi64(*Value->AsString());
			}
			else
			{
				OutError = FString::Printf(TEXT("Integer property '%s' expects a number or numeric string."), *Property->GetName());
				return false;
			}
			NumericProperty->SetIntPropertyValue(PropertyData, IntValue);
			return true;
		}
		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			StringProperty->SetPropertyValue(PropertyData, JsonValueToCompactString(Value));
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue(PropertyData, FName(*JsonValueToCompactString(Value)));
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			TextProperty->SetPropertyValue(PropertyData, FText::FromString(JsonValueToCompactString(Value)));
			return true;
		}
		if (CastField<FSoftObjectProperty>(Property))
		{
			if (Value->Type != EJson::String && Value->Type != EJson::Null)
			{
				OutError = FString::Printf(TEXT("Soft object/class property '%s' expects an asset path string or null."), *Property->GetName());
				return false;
			}
			const FString Path = Value->Type == EJson::Null ? FString() : StripObjectExportWrapper(Value->AsString());
			*static_cast<FSoftObjectPtr*>(PropertyData) = FSoftObjectPtr(FSoftObjectPath(Path));
			return true;
		}
		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			if (Value->Type != EJson::String && Value->Type != EJson::Null)
			{
				OutError = FString::Printf(TEXT("Class property '%s' expects a class or Blueprint asset path string or null."), *Property->GetName());
				return false;
			}

			UClass* ClassValue = nullptr;
			if (Value->Type != EJson::Null && !Value->AsString().IsEmpty())
			{
				ClassValue = ResolveClassReference(Value->AsString());
				if (!ClassValue)
				{
					OutError = FString::Printf(TEXT("Could not resolve class '%s' for property '%s'."), *Value->AsString(), *Property->GetName());
					return false;
				}
				if (ClassProperty->MetaClass && !ClassValue->IsChildOf(ClassProperty->MetaClass))
				{
					OutError = FString::Printf(TEXT("Class '%s' is not a child of required class '%s' for property '%s'."), *ClassValue->GetPathName(), *ClassProperty->MetaClass->GetPathName(), *Property->GetName());
					return false;
				}
			}

			ClassProperty->SetObjectPropertyValue(PropertyData, ClassValue);
			return true;
		}
		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (Value->Type != EJson::String && Value->Type != EJson::Null)
			{
				OutError = FString::Printf(TEXT("Object property '%s' expects an object path string or null."), *Property->GetName());
				return false;
			}

			UObject* ObjectValue = nullptr;
			if (Value->Type != EJson::Null && !Value->AsString().IsEmpty())
			{
				ObjectValue = ResolveObjectReference(Value->AsString(), ObjectProperty->PropertyClass);
				if (!ObjectValue)
				{
					OutError = FString::Printf(TEXT("Could not load object '%s' for property '%s'."), *Value->AsString(), *Property->GetName());
					return false;
				}
			}

			ObjectProperty->SetObjectPropertyValue(PropertyData, ObjectValue);
			return true;
		}
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct())
			{
				if (Value->Type != EJson::String)
				{
					OutError = FString::Printf(TEXT("Gameplay tag property '%s' expects a tag string."), *Property->GetName());
					return false;
				}
				const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Value->AsString()), false);
				if (!Tag.IsValid() && !Value->AsString().IsEmpty())
				{
					OutError = FString::Printf(TEXT("Gameplay tag '%s' does not exist."), *Value->AsString());
					return false;
				}
				*static_cast<FGameplayTag*>(PropertyData) = Tag;
				return true;
			}
			if (StructProperty->Struct == FGameplayTagContainer::StaticStruct())
			{
				FGameplayTagContainer Container;
				if (!JsonToGameplayTagContainer(Value, Container, OutError))
				{
					return false;
				}
				*static_cast<FGameplayTagContainer*>(PropertyData) = Container;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				FVector Vector = *static_cast<FVector*>(PropertyData);
				if (!TryParseVectorValue(Value, Vector))
				{
					OutError = FString::Printf(TEXT("Vector property '%s' expects {x,y,z}, [x,y,z], or an Unreal vector string."), *Property->GetName());
					return false;
				}
				*static_cast<FVector*>(PropertyData) = Vector;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator Rotator = *static_cast<FRotator*>(PropertyData);
				if (!TryParseRotatorValue(Value, Rotator))
				{
					OutError = FString::Printf(TEXT("Rotator property '%s' expects {pitch,yaw,roll}, [pitch,yaw,roll], or an Unreal rotator string."), *Property->GetName());
					return false;
				}
				*static_cast<FRotator*>(PropertyData) = Rotator;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				FTransform Transform = *static_cast<FTransform*>(PropertyData);
				if (!TryParseTransformValue(Value, Transform))
				{
					OutError = FString::Printf(TEXT("Transform property '%s' expects {location,rotation,scale} or an Unreal transform string."), *Property->GetName());
					return false;
				}
				*static_cast<FTransform*>(PropertyData) = Transform;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FColor>::Get())
			{
				FColor Color = *static_cast<FColor*>(PropertyData);
				if (!TryParseColorValue(Value, Color))
				{
					OutError = FString::Printf(TEXT("Color property '%s' expects {r,g,b,a}, [r,g,b,a], or an Unreal color string."), *Property->GetName());
					return false;
				}
				*static_cast<FColor*>(PropertyData) = Color;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
			{
				FLinearColor Color = *static_cast<FLinearColor*>(PropertyData);
				if (!TryParseLinearColorValue(Value, Color))
				{
					OutError = FString::Printf(TEXT("LinearColor property '%s' expects {r,g,b,a} or [r,g,b,a]."), *Property->GetName());
					return false;
				}
				*static_cast<FLinearColor*>(PropertyData) = Color;
				return true;
			}

			if (StructProperty->Struct == TBaseStructure<FGuid>::Get())
			{
				// Material attribute channels are identified by GUID, so an attribute-driven
				// graph cannot be wired at all without being able to write one of these.
				FString GuidText;
				FGuid ParsedGuid;
				if (!Value.IsValid() || !Value->TryGetString(GuidText) || !FGuid::Parse(GuidText, ParsedGuid))
				{
					OutError = FString::Printf(TEXT("Guid property '%s' expects a GUID string, with or without dashes."), *Property->GetName());
					return false;
				}
				*static_cast<FGuid*>(PropertyData) = ParsedGuid;
				return true;
			}

			if (Value->Type == EJson::String)
			{
				return ImportPropertyText(Property, PropertyData, Value->AsString(), OutError);
			}

			OutError = FString::Printf(TEXT("Struct property '%s' of type '%s' is not supported as a JSON object; pass the Unreal text form as a string, e.g. \"(Field=Value)\", or set an editable nested field path."), *Property->GetName(), StructProperty->Struct ? *StructProperty->Struct->GetName() : TEXT("<unknown>"));
			return false;
		}
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Value->Type == EJson::String)
			{
				return ImportPropertyText(Property, PropertyData, Value->AsString(), OutError);
			}
			if (Value->Type != EJson::Array)
			{
				OutError = FString::Printf(TEXT("Array property '%s' expects a JSON array or the Unreal text form as a string, e.g. \"((...),(...))\"."), *Property->GetName());
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
			FScriptArrayHelper Helper(ArrayProperty, PropertyData);
			Helper.EmptyAndAddValues(Array.Num());
			for (int32 Index = 0; Index < Array.Num(); ++Index)
			{
				if (!SetPropertyValueFromJson(ArrayProperty->Inner, Helper.GetRawPtr(Index), Array[Index], OutError))
				{
					OutError = FString::Printf(TEXT("Array property '%s' element %d failed: %s"), *Property->GetName(), Index, *OutError);
					return false;
				}
			}
			return true;
		}
		// Maps and sets have no JSON form here; the editor's copy/paste text is the only way in,
		// e.g. ((KeyPath, ValuePath),(KeyPath, ValuePath)) for a map. The whole container is replaced.
		if (CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property))
		{
			if (Value->Type == EJson::String)
			{
				return ImportPropertyText(Property, PropertyData, Value->AsString(), OutError);
			}
			OutError = FString::Printf(TEXT("Property '%s' of type '%s' expects the Unreal text form as a string, e.g. \"((Key, Value),(Key, Value))\"."), *Property->GetName(), *Property->GetCPPType());
			return false;
		}

		OutError = FString::Printf(TEXT("Property '%s' has unsupported type '%s'."), *Property->GetName(), *Property->GetCPPType());
		return false;
	}

	FProperty* FindPropertyByNameLoose(const UStruct* Struct, const FString& PropertyName)
	{
		if (!Struct || PropertyName.IsEmpty())
		{
			return nullptr;
		}
		if (FProperty* Property = FindFProperty<FProperty>(Struct, FName(*PropertyName)))
		{
			return Property;
		}
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			if ((*PropertyIt)->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				return *PropertyIt;
			}
		}
		return nullptr;
	}

	bool ResolvePropertyAddress(UObject* TargetObject, const FString& PropertyPath, FProperty*& OutProperty, void*& OutPropertyData, FString& OutError)
	{
		if (!TargetObject)
		{
			OutError = TEXT("Property target object is null.");
			return false;
		}

		TArray<FString> Segments;
		PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.Num() == 0)
		{
			OutError = TEXT("Missing required field 'property'.");
			return false;
		}

		const UStruct* CurrentStruct = TargetObject->GetClass();
		void* CurrentContainer = TargetObject;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			FProperty* Property = FindPropertyByNameLoose(CurrentStruct, Segments[SegmentIndex]);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Property path '%s' could not resolve segment '%s' on '%s'."), *PropertyPath, *Segments[SegmentIndex], CurrentStruct ? *CurrentStruct->GetName() : TEXT("<null>"));
				return false;
			}

			void* PropertyData = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
			if (SegmentIndex == Segments.Num() - 1)
			{
				OutProperty = Property;
				OutPropertyData = PropertyData;
				return true;
			}

			FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty || !StructProperty->Struct)
			{
				OutError = FString::Printf(TEXT("Property path '%s' segment '%s' is not a struct."), *PropertyPath, *Segments[SegmentIndex]);
				return false;
			}
			CurrentStruct = StructProperty->Struct;
			CurrentContainer = PropertyData;
		}

		OutError = FString::Printf(TEXT("Property path '%s' could not be resolved."), *PropertyPath);
		return false;
	}

	UObject* ResolveObjectPath(const FString& ObjectPath)
	{
		const FString CleanPath = StripObjectExportWrapper(ObjectPath);
		if (CleanPath.IsEmpty())
		{
			return nullptr;
		}
		if (UObject* FoundObject = StaticFindObject(UObject::StaticClass(), nullptr, *CleanPath))
		{
			return FoundObject;
		}
		return StaticLoadObject(UObject::StaticClass(), nullptr, *CleanPath);
	}

	UObject* LoadAssetObject(const FString& AssetPath, FString& OutError)
	{
		const FString ObjectPath = NormalizeAssetObjectPath(AssetPath);
		UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (!Object)
		{
			OutError = FString::Printf(TEXT("Could not load asset '%s'."), *ObjectPath);
		}
		return Object;
	}

	UObject* ResolveObjectReference(const FString& InPath, UClass* ExpectedClass)
	{
		const FString CleanPath = StripObjectExportWrapper(InPath);
		if (CleanPath.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* Object = StaticLoadObject(ExpectedClass ? ExpectedClass : UObject::StaticClass(), nullptr, *CleanPath))
		{
			return Object;
		}

		const FString ObjectPath = NormalizeAssetObjectPath(CleanPath);
		if (!ObjectPath.Equals(CleanPath, ESearchCase::CaseSensitive))
		{
			return StaticLoadObject(ExpectedClass ? ExpectedClass : UObject::StaticClass(), nullptr, *ObjectPath);
		}
		return nullptr;
	}

	UClass* ResolveClassReference(const FString& InPath)
	{
		const FString CleanPath = StripObjectExportWrapper(InPath);
		if (CleanPath.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* Class = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *CleanPath)))
		{
			return Class;
		}

		if (UObject* Object = ResolveObjectReference(CleanPath, UObject::StaticClass()))
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
			{
				return Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->ParentClass;
			}
			return Cast<UClass>(Object);
		}

		const FString ClassObjectPath = NormalizeAssetObjectPath(CleanPath) + TEXT("_C");
		return Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *ClassObjectPath));
	}

	void AddReferencersJson(const FString& PackageName, TArray<TSharedPtr<FJsonValue>>& OutReferencers)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FName> ReferencerNames;
		AssetRegistryModule.Get().GetReferencers(FName(*PackageName), ReferencerNames, UE::AssetRegistry::EDependencyCategory::Package);
		ReferencerNames.Sort([](const FName& A, const FName& B)
		{
			return A.ToString() < B.ToString();
		});
		for (const FName& Referencer : ReferencerNames)
		{
			if (!Referencer.IsNone())
			{
				OutReferencers.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
			}
		}
	}

	UObject* FindPackageAsset(UPackage* Package)
	{
		return Package ? Package->FindAssetInPackage(RF_Public | RF_Standalone) : nullptr;
	}

	bool SaveAssetPackage(UObject* Asset, FString& OutFilename, FString& OutError)
	{
		if (!Asset)
		{
			OutError = TEXT("Asset is null.");
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Asset '%s' does not have an outer package."), *Asset->GetPathName());
			return false;
		}

		const bool bIsMap = Asset->IsA<UWorld>() || Package->HasAnyPackageFlags(PKG_ContainsMap);
		if (bIsMap) Package->SetPackageFlags(PKG_ContainsMap);
		OutFilename = FPackageName::LongPackageNameToFilename(Package->GetName(),
			bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *OutFilename, SaveArgs);
		if (!bSaved)
		{
			OutError = FString::Printf(TEXT("Failed to save package '%s'."), *Package->GetName());
		}
		return bSaved;
	}

	UObject* GetBlueprintDefaultObject(UBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null.");
			return nullptr;
		}
		if (!Blueprint->GeneratedClass)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' does not have a generated class. Compile it before editing class defaults."), *Blueprint->GetPathName());
			return nullptr;
		}
		UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
		if (!DefaultObject)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' does not have a class default object."), *Blueprint->GetPathName());
			return nullptr;
		}
		return DefaultObject;
	}

	USimpleConstructionScript* GetBlueprintSCS(UBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint || !FBlueprintEditorUtils::DoesSupportComponents(Blueprint))
		{
			OutError = Blueprint ? FString::Printf(TEXT("Blueprint '%s' does not support components."), *Blueprint->GetPathName()) : TEXT("Blueprint is null.");
			return nullptr;
		}
		if (!Blueprint->SimpleConstructionScript)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript."), *Blueprint->GetPathName());
			return nullptr;
		}
		return Blueprint->SimpleConstructionScript;
	}

	FString NormalizeComponentName(FString ComponentName)
	{
		ComponentName.TrimStartAndEndInline();
		if (ComponentName.EndsWith(USimpleConstructionScript::ComponentTemplateNameSuffix))
		{
			ComponentName.LeftChopInline(USimpleConstructionScript::ComponentTemplateNameSuffix.Len());
		}
		return ComponentName;
	}

	USCS_Node* FindEditableSCSNode(UBlueprint* Blueprint, const FString& ComponentName, FString& OutError)
	{
		USimpleConstructionScript* SCS = GetBlueprintSCS(Blueprint, OutError);
		if (!SCS)
		{
			return nullptr;
		}

		const FString NormalizedName = NormalizeComponentName(ComponentName);
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}

			if (Node->GetVariableName().ToString().Equals(NormalizedName, ESearchCase::IgnoreCase) ||
				Node->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
				(Node->ComponentTemplate && Node->ComponentTemplate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
			{
				return Node;
			}
		}

		OutError = FString::Printf(TEXT("Blueprint-created component '%s' was not found in '%s'."), *ComponentName, *Blueprint->GetPathName());
		return nullptr;
	}

	UActorComponent* FindComponentTemplateByName(UBlueprint* Blueprint, const FString& ComponentName, bool& bOutEditable, FString& OutError)
	{
		bOutEditable = false;
		if (USCS_Node* Node = FindEditableSCSNode(Blueprint, ComponentName, OutError))
		{
			bOutEditable = true;
			return Node->ComponentTemplate;
		}

		OutError.Reset();
		const FString NormalizedName = NormalizeComponentName(ComponentName);
		if (UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass : nullptr)
		{
			if (AActor* CDO = Cast<AActor>(GeneratedClass->GetDefaultObject()))
			{
				for (UActorComponent* Component : CDO->GetComponents())
				{
					if (Component &&
						(Component->GetFName().ToString().Equals(NormalizedName, ESearchCase::IgnoreCase) ||
						Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
					{
						return Component;
					}
				}
			}
		}

		OutError = FString::Printf(TEXT("Component '%s' was not found in '%s'."), *ComponentName, Blueprint ? *Blueprint->GetPathName() : TEXT("<null>"));
		return nullptr;
	}

	void AddSCSNodeAndChildrenJson(const UBlueprint* Blueprint, const USCS_Node* Node, const FString& ParentName, TArray<TSharedPtr<FJsonValue>>& Components)
	{
		if (!Node || !Node->ComponentTemplate)
		{
			return;
		}

		const UActorComponent* Template = Node->ComponentTemplate;
		const USceneComponent* SceneTemplate = Cast<USceneComponent>(Template);
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
		Object->SetStringField(TEXT("template_name"), Template->GetName());
		Object->SetStringField(TEXT("component_class"), Template->GetClass()->GetPathName());
		Object->SetStringField(TEXT("parent"), ParentName);
		Object->SetStringField(TEXT("attach_socket"), Node->AttachToName.ToString());
		Object->SetBoolField(TEXT("inherited"), false);
		Object->SetBoolField(TEXT("editable"), true);
		Object->SetObjectField(TEXT("relative_location"), VectorToJson(SceneTemplate ? SceneTemplate->GetRelativeLocation() : FVector::ZeroVector));
		Object->SetObjectField(TEXT("relative_rotation"), RotatorToJson(SceneTemplate ? SceneTemplate->GetRelativeRotation() : FRotator::ZeroRotator));
		Object->SetObjectField(TEXT("relative_scale"), VectorToJson(SceneTemplate ? SceneTemplate->GetRelativeScale3D() : FVector::OneVector));
		Components.Add(MakeShared<FJsonValueObject>(Object));

		for (const USCS_Node* ChildNode : Node->GetChildNodes())
		{
			AddSCSNodeAndChildrenJson(Blueprint, ChildNode, Node->GetVariableName().ToString(), Components);
		}
	}

	void AddNativeComponentJson(const UActorComponent* Component, TArray<TSharedPtr<FJsonValue>>& Components)
	{
		if (!Component)
		{
			return;
		}

		const USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Component->GetName());
		Object->SetStringField(TEXT("template_name"), Component->GetName());
		Object->SetStringField(TEXT("component_class"), Component->GetClass()->GetPathName());
		Object->SetStringField(TEXT("parent"), SceneComponent && SceneComponent->GetAttachParent() ? SceneComponent->GetAttachParent()->GetName() : FString());
		Object->SetStringField(TEXT("attach_socket"), SceneComponent ? SceneComponent->GetAttachSocketName().ToString() : FString());
		Object->SetBoolField(TEXT("inherited"), true);
		Object->SetBoolField(TEXT("editable"), false);
		Object->SetObjectField(TEXT("relative_location"), VectorToJson(SceneComponent ? SceneComponent->GetRelativeLocation() : FVector::ZeroVector));
		Object->SetObjectField(TEXT("relative_rotation"), RotatorToJson(SceneComponent ? SceneComponent->GetRelativeRotation() : FRotator::ZeroRotator));
		Object->SetObjectField(TEXT("relative_scale"), VectorToJson(SceneComponent ? SceneComponent->GetRelativeScale3D() : FVector::OneVector));
		Components.Add(MakeShared<FJsonValueObject>(Object));
	}

	UClass* ResolveComponentClass(const FString& ClassPathOrName)
	{
		const TMap<FString, FString> Aliases = {
			{ TEXT("SceneComponent"), TEXT("/Script/Engine.SceneComponent") },
			{ TEXT("StaticMeshComponent"), TEXT("/Script/Engine.StaticMeshComponent") },
			{ TEXT("SkeletalMeshComponent"), TEXT("/Script/Engine.SkeletalMeshComponent") },
			{ TEXT("BoxComponent"), TEXT("/Script/Engine.BoxComponent") },
			{ TEXT("SphereComponent"), TEXT("/Script/Engine.SphereComponent") },
			{ TEXT("CapsuleComponent"), TEXT("/Script/Engine.CapsuleComponent") },
			{ TEXT("AudioComponent"), TEXT("/Script/Engine.AudioComponent") },
			{ TEXT("NiagaraComponent"), TEXT("/Script/Niagara.NiagaraComponent") }
		};

		if (const FString* AliasPath = Aliases.Find(ClassPathOrName))
		{
			return LoadObject<UClass>(nullptr, **AliasPath);
		}
		return ClassPathOrName.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *StripObjectExportWrapper(ClassPathOrName));
	}

	bool ImportJsonValueToProperty(UObject* TargetObject, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!TargetObject || !Property || !Value.IsValid())
		{
			OutError = TEXT("Invalid property import target.");
			return false;
		}

		void* PropertyData = Property->ContainerPtrToValuePtr<void>(TargetObject);
		return SetPropertyValueFromJson(Property, PropertyData, Value, OutError);
	}

	UClass* GetEffectiveAssetClass(UObject* Asset)
	{
		if (UClass* Class = Cast<UClass>(Asset))
		{
			return Class;
		}
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			if (Blueprint->GeneratedClass)
			{
				return Blueprint->GeneratedClass;
			}
			return Blueprint->ParentClass;
		}
		return Asset ? Asset->GetClass() : nullptr;
	}

	UObject* GetEffectiveDefaultObject(UObject* Asset)
	{
		if (UClass* Class = GetEffectiveAssetClass(Asset))
		{
			return Class->GetDefaultObject();
		}
		return Asset;
	}

	bool IsClassChildOf(UClass* Class, UClass* ParentClass)
	{
		return Class && ParentClass && Class->IsChildOf(ParentClass);
	}

	FString GetGasAssetKind(UObject* Asset)
	{
		UClass* Class = GetEffectiveAssetClass(Asset);
		if (IsClassChildOf(Class, UGameplayAbility::StaticClass()))
		{
			return TEXT("gameplay_ability");
		}
		if (IsClassChildOf(Class, UGameplayEffect::StaticClass()))
		{
			return TEXT("gameplay_effect");
		}
		if (IsClassChildOf(Class, AGameplayCueNotify_Actor::StaticClass()) || IsClassChildOf(Class, UGameplayCueNotify_Static::StaticClass()))
		{
			return TEXT("gameplay_cue");
		}
		if (IsClassChildOf(Class, UAttributeSet::StaticClass()))
		{
			return TEXT("attribute_set");
		}
		if (IsClassChildOf(Class, UGameplayModMagnitudeCalculation::StaticClass()))
		{
			return TEXT("mod_magnitude_calculation");
		}
		return FString();
	}

	bool IsPotentialGasAssetData(const FAssetData& AssetData)
	{
		const FString ClassPath = AssetData.AssetClassPath.ToString();
		return ClassPath.Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("GameplayAbility"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("GameplayEffect"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("GameplayCue"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("AttributeSet"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("GameplayModMagnitudeCalculation"), ESearchCase::IgnoreCase);
	}

	TSharedRef<FJsonObject> GasAssetToJson(UObject* Asset)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		UClass* Class = GetEffectiveAssetClass(Asset);
		Object->SetStringField(TEXT("asset_path"), Asset ? Asset->GetPathName() : FString());
		Object->SetStringField(TEXT("package"), Asset && Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : FString());
		Object->SetStringField(TEXT("asset_class"), Asset && Asset->GetClass() ? Asset->GetClass()->GetPathName() : FString());
		Object->SetStringField(TEXT("generated_class"), Class ? Class->GetPathName() : FString());
		Object->SetStringField(TEXT("parent_class"), Cast<UBlueprint>(Asset) && Cast<UBlueprint>(Asset)->ParentClass ? Cast<UBlueprint>(Asset)->ParentClass->GetPathName() : FString());
		Object->SetStringField(TEXT("kind"), GetGasAssetKind(Asset));
		return Object;
	}

	void AddPropertySnapshot(UObject* TargetObject, const TArray<FString>& PropertyNames, TSharedRef<FJsonObject> OutObject)
	{
		for (const FString& PropertyName : PropertyNames)
		{
			FString Error;
			FProperty* Property = nullptr;
			void* PropertyData = nullptr;
			if (ResolvePropertyAddress(TargetObject, PropertyName, Property, PropertyData, Error))
			{
				OutObject->SetObjectField(PropertyName, PropertyToJson(TargetObject, Property, PropertyData, PropertyName, TEXT("gas_default"), FString()));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> GetAbilityTaskNodesJson(UBlueprint* Blueprint)
	{
		TArray<TSharedPtr<FJsonValue>> Nodes;
		if (!Blueprint)
		{
			return Nodes;
		}

		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}
				const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
				const FString NodeClassName = Node->GetClass() ? Node->GetClass()->GetName() : FString();
				const bool bLooksLikeAbilityTask = NodeClassName.Contains(TEXT("AbilityTask"), ESearchCase::IgnoreCase) ||
					NodeTitle.Contains(TEXT("Wait Gameplay"), ESearchCase::IgnoreCase) ||
					NodeTitle.Contains(TEXT("Wait Delay"), ESearchCase::IgnoreCase) ||
					NodeTitle.Contains(TEXT("Play Montage"), ESearchCase::IgnoreCase);
				if (bLooksLikeAbilityTask)
				{
					TSharedRef<FJsonObject> NodeObject = NodeToJson(Node);
					NodeObject->SetStringField(TEXT("graph"), Graph->GetName());
					Nodes.Add(MakeShared<FJsonValueObject>(NodeObject));
				}
			}
		}
		return Nodes;
	}

	bool FindGameplayAttributeProperty(const FString& AttributeName, FProperty*& OutProperty, FString& OutError)
	{
		FString ClassPart;
		FString PropertyPart;
		if (AttributeName.Split(TEXT("."), &ClassPart, &PropertyPart))
		{
			UClass* AttributeClass = LoadObject<UClass>(nullptr, *StripObjectExportWrapper(ClassPart));
			if (!AttributeClass)
			{
				for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
				{
					if (ClassIt->GetName().Equals(ClassPart, ESearchCase::IgnoreCase) || ClassIt->GetPathName().Equals(ClassPart, ESearchCase::IgnoreCase))
					{
						AttributeClass = *ClassIt;
						break;
					}
				}
			}
			if (AttributeClass && AttributeClass->IsChildOf(UAttributeSet::StaticClass()))
			{
				OutProperty = FindPropertyByNameLoose(AttributeClass, PropertyPart);
				if (OutProperty)
				{
					return true;
				}
			}
		}

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (!Class || !Class->IsChildOf(UAttributeSet::StaticClass()))
			{
				continue;
			}
			if (FProperty* Property = FindPropertyByNameLoose(Class, AttributeName))
			{
				OutProperty = Property;
				return true;
			}
		}

		OutError = FString::Printf(TEXT("Could not resolve GameplayAttribute '%s'. Use AttributeSetClass.AttributeName or an unambiguous attribute property name."), *AttributeName);
		return false;
	}

	/**
	 * Tells an object a property was written, the way the editor would.
	 *
	 * Writing straight into the property memory skips whatever the class derives from that
	 * field, and the derived state then describes the value that used to be there. Callers that
	 * follow a write with a recompile need this in between or the compile reads stale state.
	 */
	void NotifyPropertyChanged(UObject* TargetObject, const FString& PropertyPath)
	{
		if (!TargetObject)
		{
			return;
		}

		FString RootPropertyName = PropertyPath;
		int32 SeparatorIndex = INDEX_NONE;
		if (RootPropertyName.FindChar(TEXT('.'), SeparatorIndex))
		{
			RootPropertyName.LeftInline(SeparatorIndex);
		}

		if (FProperty* ChangedProperty = TargetObject->GetClass()->FindPropertyByName(*RootPropertyName))
		{
			FPropertyChangedEvent PropertyChangedEvent(ChangedProperty, EPropertyChangeType::ValueSet);
			TargetObject->PostEditChangeProperty(PropertyChangedEvent);
		}
	}

	bool SetEditablePropertyValue(UObject* Asset, UObject* TargetObject, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError, TSharedPtr<FJsonObject>* OutPropertyJson = nullptr)
	{
		if (!TargetObject)
		{
			OutError = TEXT("Property target object is null.");
			return false;
		}

		FProperty* Property = nullptr;
		void* PropertyData = nullptr;
		if (!ResolvePropertyAddress(TargetObject, PropertyPath, Property, PropertyData, OutError))
		{
			return false;
		}

		if (!IsEditableDefaultProperty(Property))
		{
			OutError = FString::Printf(TEXT("Property '%s' is not editable on '%s'."), *PropertyPath, *TargetObject->GetPathName());
			return false;
		}

		TargetObject->Modify();
		TargetObject->PreEditChange(Property);
		if (!SetPropertyValueFromJson(Property, PropertyData, Value, OutError))
		{
			return false;
		}
		FPropertyChangedEvent ChangeEvent(Property);
		TargetObject->PostEditChangeProperty(ChangeEvent);
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		else if (Asset)
		{
			Asset->MarkPackageDirty();
		}

		if (OutPropertyJson)
		{
			*OutPropertyJson = PropertyToJson(TargetObject, Property, PropertyData, PropertyPath, TEXT("gas_default"), FString());
		}
		return true;
	}

	bool SyncGameplayEffectTagComponentAfterEdit(UGameplayEffect* GameplayEffect, const FString& PropertyPath)
	{
		if (!GameplayEffect)
		{
			return false;
		}

		if (PropertyPath.StartsWith(TEXT("InheritableOwnedTagsContainer"), ESearchCase::IgnoreCase))
		{
			GameplayEffect->Modify();
			UTargetTagsGameplayEffectComponent& TargetTagsComponent = GameplayEffect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
			TargetTagsComponent.Modify();
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			TargetTagsComponent.SetAndApplyTargetTagChanges(GameplayEffect->InheritableOwnedTagsContainer);
			GameplayEffect->InheritableOwnedTagsContainer = TargetTagsComponent.GetConfiguredTargetTagChanges();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			return true;
		}

		if (PropertyPath.StartsWith(TEXT("InheritableGameplayEffectTags"), ESearchCase::IgnoreCase))
		{
			GameplayEffect->Modify();
			UAssetTagsGameplayEffectComponent& AssetTagsComponent = GameplayEffect->FindOrAddComponent<UAssetTagsGameplayEffectComponent>();
			AssetTagsComponent.Modify();
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			AssetTagsComponent.SetAndApplyAssetTagChanges(GameplayEffect->InheritableGameplayEffectTags);
			GameplayEffect->InheritableGameplayEffectTags = AssetTagsComponent.GetConfiguredAssetTagChanges();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			return true;
		}

		return false;
	}

	FString ResolveKnownGasTagField(UObject* TargetObject, const FString& KnownField)
	{
		const TMap<FString, TArray<FString>> CandidatePaths = {
			{ TEXT("AbilityTags"), { TEXT("AbilityTags") } },
			{ TEXT("ActivationOwnedTags"), { TEXT("ActivationOwnedTags") } },
			{ TEXT("ActivationRequiredTags"), { TEXT("ActivationRequiredTags") } },
			{ TEXT("ActivationBlockedTags"), { TEXT("ActivationBlockedTags") } },
			{ TEXT("CancelAbilitiesWithTag"), { TEXT("CancelAbilitiesWithTag") } },
			{ TEXT("BlockAbilitiesWithTag"), { TEXT("BlockAbilitiesWithTag") } },
			{ TEXT("SourceRequiredTags"), { TEXT("SourceRequiredTags") } },
			{ TEXT("SourceBlockedTags"), { TEXT("SourceBlockedTags") } },
			{ TEXT("TargetRequiredTags"), { TEXT("TargetRequiredTags") } },
			{ TEXT("TargetBlockedTags"), { TEXT("TargetBlockedTags") } },
			{ TEXT("GrantedTags"), { TEXT("InheritableOwnedTagsContainer.Added"), TEXT("GrantedTags") } },
			{ TEXT("AssetTags"), { TEXT("InheritableGameplayEffectTags.Added"), TEXT("AssetTags") } },
			{ TEXT("ApplicationTagRequirements"), { TEXT("ApplicationTagRequirements.RequireTags"), TEXT("ApplicationTagRequirements") } },
			{ TEXT("OngoingTagRequirements"), { TEXT("OngoingTagRequirements.RequireTags"), TEXT("OngoingTagRequirements") } },
			{ TEXT("RemoveEffectsWithTags"), { TEXT("RemoveGameplayEffectsWithTags"), TEXT("RemoveEffectsWithTags") } }
		};

		if (const TArray<FString>* Paths = CandidatePaths.Find(KnownField))
		{
			for (const FString& Path : *Paths)
			{
				FString Error;
				FProperty* Property = nullptr;
				void* PropertyData = nullptr;
				if (ResolvePropertyAddress(TargetObject, Path, Property, PropertyData, Error))
				{
					return Path;
				}
			}
		}
		return KnownField;
	}

	TArray<TSharedPtr<FJsonValue>> FindGameplayTagsJson(const FString& Prefix, const FString& Query, int32 Limit)
	{
		FGameplayTagContainer AllTags;
		UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, true);
		TArray<FGameplayTag> Tags;
		AllTags.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});

		TArray<TSharedPtr<FJsonValue>> Results;
		for (const FGameplayTag& Tag : Tags)
		{
			const FString TagName = Tag.ToString();
			if (!Prefix.IsEmpty() && !TagName.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!Query.IsEmpty() && !TagName.Contains(Query, ESearchCase::IgnoreCase))
			{
				continue;
			}
			Results.Add(MakeShared<FJsonValueString>(TagName));
			if (Results.Num() >= Limit)
			{
				break;
			}
		}
		return Results;
	}

	FString EnumValueToShortString(const FString& Value)
	{
		FString Result = Value;
		int32 ScopeIndex = INDEX_NONE;
		if (Result.FindLastChar(TEXT(':'), ScopeIndex))
		{
			Result = Result.Mid(ScopeIndex + 1);
		}
		return Result;
	}

	template<typename EnumType>
	FString EnumToString(EnumType Value)
	{
		return EnumValueToShortString(UEnum::GetValueAsString(Value));
	}

	bool IsStateTreeAssetData(const FAssetData& AssetData)
	{
		const FString ClassPath = AssetData.AssetClassPath.ToString();
		return ClassPath.Contains(TEXT("StateTree"), ESearchCase::IgnoreCase) && !ClassPath.Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase);
	}

	bool LooksLikeStateTreeRelatedBlueprintName(const FString& Name)
	{
		return Name.StartsWith(TEXT("STT_"), ESearchCase::IgnoreCase) ||
			Name.StartsWith(TEXT("STC_"), ESearchCase::IgnoreCase) ||
			Name.StartsWith(TEXT("STE_"), ESearchCase::IgnoreCase);
	}

	FString GetStateTreeBlueprintRole(UClass* Class, const FString& AssetName = FString())
	{
		if (Class)
		{
			if (Class->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()))
			{
				return TEXT("task");
			}
			if (Class->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()))
			{
				return TEXT("condition");
			}
			if (Class->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()))
			{
				return TEXT("evaluator");
			}
		}
		if (AssetName.StartsWith(TEXT("STT_"), ESearchCase::IgnoreCase))
		{
			return TEXT("task");
		}
		if (AssetName.StartsWith(TEXT("STC_"), ESearchCase::IgnoreCase))
		{
			return TEXT("condition");
		}
		if (AssetName.StartsWith(TEXT("STE_"), ESearchCase::IgnoreCase) || AssetName.StartsWith(TEXT("ST_Evaluator"), ESearchCase::IgnoreCase))
		{
			return TEXT("evaluator");
		}
		return TEXT("utility");
	}

	FString BlueprintAssetPathFromClass(const UClass* Class)
	{
		if (!Class)
		{
			return FString();
		}
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
		{
			return Blueprint->GetPathName();
		}
		return Class->GetPathName();
	}

	void AddUniqueString(TArray<TSharedPtr<FJsonValue>>& Values, TSet<FString>& Seen, const FString& Value)
	{
		if (!Value.IsEmpty() && !Seen.Contains(Value))
		{
			Seen.Add(Value);
			Values.Add(MakeShared<FJsonValueString>(Value));
		}
	}

	void AddUniqueString(TSet<FString>& Values, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Values.Add(Value);
		}
	}

	TSharedRef<FJsonObject> StateTreeDependencySummary(const FAssetData& AssetData)
	{
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Dependencies;
		TArray<TSharedPtr<FJsonValue>> Referencers;
		TArray<TSharedPtr<FJsonValue>> RelatedBlueprints;
		TSet<FString> SeenRelated;

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FName> DependencyNames;
		AssetRegistryModule.Get().GetDependencies(AssetData.PackageName, DependencyNames, UE::AssetRegistry::EDependencyCategory::Package);
		DependencyNames.Sort([](const FName& A, const FName& B)
		{
			return A.ToString() < B.ToString();
		});
		for (const FName& DependencyName : DependencyNames)
		{
			const FString PackageName = DependencyName.ToString();
			Dependencies.Add(MakeShared<FJsonValueString>(PackageName));
			const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
			if (LooksLikeStateTreeRelatedBlueprintName(AssetName))
			{
				AddUniqueString(RelatedBlueprints, SeenRelated, ObjectPathFromPackagePath(PackageName));
			}
		}

		TArray<FName> ReferencerNames;
		AssetRegistryModule.Get().GetReferencers(AssetData.PackageName, ReferencerNames, UE::AssetRegistry::EDependencyCategory::Package);
		ReferencerNames.Sort([](const FName& A, const FName& B)
		{
			return A.ToString() < B.ToString();
		});
		for (const FName& ReferencerName : ReferencerNames)
		{
			Referencers.Add(MakeShared<FJsonValueString>(ReferencerName.ToString()));
		}

		Summary->SetArrayField(TEXT("dependencies"), Dependencies);
		Summary->SetNumberField(TEXT("dependency_count"), Dependencies.Num());
		Summary->SetArrayField(TEXT("referencers"), Referencers);
		Summary->SetNumberField(TEXT("referencer_count"), Referencers.Num());
		Summary->SetArrayField(TEXT("likely_state_tree_blueprints"), RelatedBlueprints);
		return Summary;
	}

	TSharedRef<FJsonObject> StateTreeAssetDataToJson(const FAssetData& AssetData, bool bIncludeDependencies)
	{
		TSharedRef<FJsonObject> Object = AssetDataToJson(AssetData);
		Object->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.ToString());
		if (bIncludeDependencies)
		{
			Object->SetObjectField(TEXT("dependency_summary"), StateTreeDependencySummary(AssetData));
		}
		return Object;
	}

	UStateTree* LoadStateTreeAsset(const FString& AssetPath, FString& OutError)
	{
		UObject* Asset = LoadAssetObject(AssetPath, OutError);
		UStateTree* StateTree = Cast<UStateTree>(Asset);
		if (!StateTree && Asset)
		{
			OutError = FString::Printf(TEXT("Asset '%s' is not a UStateTree."), *Asset->GetPathName());
		}
		return StateTree;
	}

	UStateTreeEditorData* GetStateTreeEditorData(UStateTree* StateTree)
	{
#if WITH_EDITORONLY_DATA
		return StateTree ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr;
#else
		return nullptr;
#endif
	}

	TSharedRef<FJsonObject> ExternalDataDescToJson(const FStateTreeExternalDataDesc& Desc)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Desc.Name.ToString());
		Object->SetStringField(TEXT("struct"), Desc.Struct ? Desc.Struct->GetPathName() : FString());
		Object->SetStringField(TEXT("requirement"), EnumToString(Desc.Requirement));
		Object->SetStringField(TEXT("handle"), Desc.Handle.IsValid() ? Desc.Handle.DataHandle.Describe() : FString());
#if WITH_EDITORONLY_DATA
		Object->SetStringField(TEXT("id"), GuidToString(Desc.ID));
#endif
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> PropertyBagToJson(const FInstancedPropertyBag& Bag, UObject* Owner, const FString& TargetKind)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const FConstStructView StructView = Bag.GetValue();
		const UScriptStruct* Struct = StructView.GetScriptStruct();
		const uint8* Memory = StructView.GetMemory();
		if (!Struct || !Memory)
		{
			return Values;
		}

		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const void* PropertyData = Property->ContainerPtrToValuePtr<void>(Memory);
			Values.Add(MakeShared<FJsonValueObject>(PropertyToJson(Owner, Property, PropertyData, Property->GetName(), TargetKind, FString())));
		}
		return Values;
	}

	TArray<TSharedPtr<FJsonValue>> PropertiesFromObject(UObject* Object, const FString& TargetKind)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Object)
		{
			return Values;
		}
		for (TFieldIterator<FProperty> PropertyIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!IsEditableDefaultProperty(Property))
			{
				continue;
			}
			const void* PropertyData = Property->ContainerPtrToValuePtr<void>(Object);
			Values.Add(MakeShared<FJsonValueObject>(PropertyToJson(Object, Property, PropertyData, Property->GetName(), TargetKind, FString())));
		}
		return Values;
	}

	TArray<TSharedPtr<FJsonValue>> PropertiesFromInstancedStruct(const FInstancedStruct& Instance, UObject* Owner, const FString& TargetKind)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const UScriptStruct* Struct = Instance.GetScriptStruct();
		const uint8* Memory = Instance.GetMemory();
		if (!Struct || !Memory)
		{
			return Values;
		}
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!IsEditableDefaultProperty(Property))
			{
				continue;
			}
			const void* PropertyData = Property->ContainerPtrToValuePtr<void>(Memory);
			Values.Add(MakeShared<FJsonValueObject>(PropertyToJson(Owner, Property, PropertyData, Property->GetName(), TargetKind, FString())));
		}
		return Values;
	}

	bool LoadMaterialAsset(const FString& AssetPath, UMaterial*& OutMaterial, FString& OutError)
	{
		UObject* Asset = LoadAssetObject(AssetPath, OutError);
		OutMaterial = Cast<UMaterial>(Asset);
		if (!OutMaterial)
		{
			OutError = Asset
				? FString::Printf(TEXT("Asset '%s' is not a UMaterial."), *Asset->GetPathName())
				: OutError;
			return false;
		}
		return true;
	}

	FString MaterialExpressionId(const UMaterialExpression* Expression)
	{
		return Expression ? Expression->MaterialExpressionGuid.ToString(EGuidFormats::Digits) : FString();
	}

	FString NormalizeMaterialExpressionLookupKey(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.RemoveFromStart(TEXT("{"));
		Value.RemoveFromEnd(TEXT("}"));
		Value.ReplaceInline(TEXT("-"), TEXT(""));
		return Value;
	}

	UMaterialExpression* FindMaterialExpression(UMaterial* Material, const FString& ExpressionIdOrName, FString& OutError)
	{
		if (!Material)
		{
			OutError = TEXT("Material is null.");
			return nullptr;
		}

		const FString LookupKey = NormalizeMaterialExpressionLookupKey(ExpressionIdOrName);
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (!Expression)
			{
				continue;
			}

			const FString GuidKey = NormalizeMaterialExpressionLookupKey(MaterialExpressionId(Expression));
			if (GuidKey.Equals(LookupKey, ESearchCase::IgnoreCase) ||
				Expression->GetName().Equals(ExpressionIdOrName, ESearchCase::IgnoreCase) ||
				Expression->GetPathName().Equals(ExpressionIdOrName, ESearchCase::IgnoreCase))
			{
				return Expression;
			}

			if (!Expression->Desc.IsEmpty() && Expression->Desc.Equals(ExpressionIdOrName, ESearchCase::IgnoreCase))
			{
				return Expression;
			}
		}

		OutError = FString::Printf(TEXT("Material expression '%s' was not found in '%s'."), *ExpressionIdOrName, *Material->GetPathName());
		return nullptr;
	}

	TSharedRef<FJsonObject> MaterialExpressionRefToJson(const UMaterialExpression* Expression)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), MaterialExpressionId(Expression));
		Object->SetStringField(TEXT("object_name"), Expression ? Expression->GetName() : FString());
		Object->SetStringField(TEXT("class"), Expression && Expression->GetClass() ? Expression->GetClass()->GetPathName() : FString());
		Object->SetStringField(TEXT("path"), Expression ? Expression->GetPathName() : FString());
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> MaterialExpressionInputsToJson(UMaterialExpression* Expression)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Expression)
		{
			return Values;
		}

		const int32 InputCount = Expression->CountInputs();
		for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
		{
			const FExpressionInput* Input = Expression->GetInput(InputIndex);
			if (!Input)
			{
				continue;
			}

			TSharedRef<FJsonObject> InputObject = MakeShared<FJsonObject>();
			InputObject->SetNumberField(TEXT("index"), InputIndex);
			InputObject->SetStringField(TEXT("name"), Expression->GetInputName(InputIndex).ToString());
			InputObject->SetBoolField(TEXT("connected"), Input->Expression != nullptr);
			InputObject->SetNumberField(TEXT("output_index"), Input->OutputIndex);
			if (Input->Expression)
			{
				InputObject->SetObjectField(TEXT("expression"), MaterialExpressionRefToJson(Input->Expression));
			}
			Values.Add(MakeShared<FJsonValueObject>(InputObject));
		}
		return Values;
	}

	TArray<TSharedPtr<FJsonValue>> MaterialExpressionOutputsToJson(UMaterialExpression* Expression)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Expression)
		{
			return Values;
		}

		TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
		for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
		{
			const FExpressionOutput& Output = Outputs[OutputIndex];
			TSharedRef<FJsonObject> OutputObject = MakeShared<FJsonObject>();
			OutputObject->SetNumberField(TEXT("index"), OutputIndex);
			OutputObject->SetStringField(TEXT("name"), Output.OutputName.ToString());
			OutputObject->SetNumberField(TEXT("mask"), Output.Mask);
			OutputObject->SetNumberField(TEXT("mask_r"), Output.MaskR);
			OutputObject->SetNumberField(TEXT("mask_g"), Output.MaskG);
			OutputObject->SetNumberField(TEXT("mask_b"), Output.MaskB);
			OutputObject->SetNumberField(TEXT("mask_a"), Output.MaskA);
			Values.Add(MakeShared<FJsonValueObject>(OutputObject));
		}
		return Values;
	}

	TSharedRef<FJsonObject> MaterialExpressionToJson(UMaterialExpression* Expression, const bool bIncludeProperties)
	{
		TSharedRef<FJsonObject> Object = MaterialExpressionRefToJson(Expression);
		if (!Expression)
		{
			return Object;
		}

		Object->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
		Object->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
		Object->SetStringField(TEXT("description"), Expression->Desc);
		Object->SetArrayField(TEXT("inputs"), MaterialExpressionInputsToJson(Expression));
		Object->SetArrayField(TEXT("outputs"), MaterialExpressionOutputsToJson(Expression));
		if (Expression->HasAParameterName())
		{
			Object->SetStringField(TEXT("parameter_name"), Expression->GetParameterName().ToString());
		}
		if (bIncludeProperties)
		{
			Object->SetArrayField(TEXT("properties"), PropertiesFromObject(Expression, TEXT("material_expression")));
		}
		return Object;
	}

	TArray<TPair<FString, EMaterialProperty>> KnownMaterialProperties()
	{
		return {
			TPair<FString, EMaterialProperty>(TEXT("BaseColor"), MP_BaseColor),
			TPair<FString, EMaterialProperty>(TEXT("Metallic"), MP_Metallic),
			TPair<FString, EMaterialProperty>(TEXT("Specular"), MP_Specular),
			TPair<FString, EMaterialProperty>(TEXT("Roughness"), MP_Roughness),
			TPair<FString, EMaterialProperty>(TEXT("EmissiveColor"), MP_EmissiveColor),
			TPair<FString, EMaterialProperty>(TEXT("Opacity"), MP_Opacity),
			TPair<FString, EMaterialProperty>(TEXT("OpacityMask"), MP_OpacityMask),
			TPair<FString, EMaterialProperty>(TEXT("Normal"), MP_Normal),
			TPair<FString, EMaterialProperty>(TEXT("WorldPositionOffset"), MP_WorldPositionOffset),
			TPair<FString, EMaterialProperty>(TEXT("AmbientOcclusion"), MP_AmbientOcclusion),
			// A material with bUseMaterialAttributes ignores every pin above and takes one packed
			// struct instead, so without this the root of such a graph cannot be reached at all.
			TPair<FString, EMaterialProperty>(TEXT("MaterialAttributes"), MP_MaterialAttributes)
		};
	}

	FString NormalizeMaterialPropertyName(FString Name)
	{
		Name.TrimStartAndEndInline();
		Name.RemoveFromStart(TEXT("MP_"));
		Name.ReplaceInline(TEXT("_"), TEXT(""));
		Name.ReplaceInline(TEXT(" "), TEXT(""));
		return Name;
	}

	bool ResolveMaterialPropertyName(const FString& Name, EMaterialProperty& OutProperty, FString& OutError)
	{
		const FString LookupName = NormalizeMaterialPropertyName(Name);
		for (const TPair<FString, EMaterialProperty>& Pair : KnownMaterialProperties())
		{
			if (NormalizeMaterialPropertyName(Pair.Key).Equals(LookupName, ESearchCase::IgnoreCase))
			{
				OutProperty = Pair.Value;
				return true;
			}
		}

		OutError = FString::Printf(TEXT("Unsupported material property '%s'."), *Name);
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> MaterialPropertyConnectionsToJson(UMaterial* Material)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Material)
		{
			return Values;
		}

		for (const TPair<FString, EMaterialProperty>& Pair : KnownMaterialProperties())
		{
			const FExpressionInput* Input = Material->GetExpressionInputForProperty(Pair.Value);
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("property"), Pair.Key);
			Object->SetBoolField(TEXT("connected"), Input && Input->Expression);
			if (Input)
			{
				Object->SetNumberField(TEXT("output_index"), Input->OutputIndex);
				if (Input->Expression)
				{
					Object->SetObjectField(TEXT("expression"), MaterialExpressionRefToJson(Input->Expression));
				}
			}
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	}

	TSharedRef<FJsonObject> MaterialToJson(UMaterial* Material,
	                                       const bool bIncludeExpressions,
	                                       const bool bIncludeExpressionProperties,
	                                       const bool bIncludeMaterialProperties)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (!Material)
		{
			return Object;
		}

		Object->SetStringField(TEXT("asset_path"), Material->GetPathName());
		Object->SetStringField(TEXT("package"), Material->GetOutermost() ? Material->GetOutermost()->GetName() : FString());
		Object->SetStringField(TEXT("name"), Material->GetName());
		Object->SetStringField(TEXT("class"), Material->GetClass() ? Material->GetClass()->GetPathName() : FString());
		Object->SetNumberField(TEXT("expression_count"), Material->GetExpressions().Num());
		Object->SetStringField(TEXT("blend_mode"), StaticEnum<EBlendMode>() ? StaticEnum<EBlendMode>()->GetNameStringByValue(Material->BlendMode) : FString());
		Object->SetNumberField(TEXT("opacity_mask_clip_value"), Material->OpacityMaskClipValue);
		Object->SetBoolField(TEXT("two_sided"), Material->TwoSided != 0);
		Object->SetBoolField(TEXT("dither_opacity_mask"), Material->DitherOpacityMask != 0);
		Object->SetArrayField(TEXT("material_inputs"), MaterialPropertyConnectionsToJson(Material));

		if (bIncludeExpressions)
		{
			TArray<TSharedPtr<FJsonValue>> Expressions;
			for (UMaterialExpression* Expression : Material->GetExpressions())
			{
				Expressions.Add(MakeShared<FJsonValueObject>(MaterialExpressionToJson(Expression, bIncludeExpressionProperties)));
			}
			Object->SetArrayField(TEXT("expressions"), Expressions);
		}
		if (bIncludeMaterialProperties)
		{
			Object->SetArrayField(TEXT("properties"), PropertiesFromObject(Material, TEXT("material")));
		}
		return Object;
	}

	TSharedRef<FJsonObject> MaterialFunctionToJson(UMaterialFunctionInterface* MaterialFunction,
	                                               const bool bIncludeExpressions,
	                                               const bool bIncludeExpressionProperties,
	                                               const bool bIncludeFunctionProperties)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (!MaterialFunction)
		{
			return Object;
		}

		Object->SetStringField(TEXT("asset_path"), MaterialFunction->GetPathName());
		Object->SetStringField(TEXT("package"), MaterialFunction->GetOutermost() ? MaterialFunction->GetOutermost()->GetName() : FString());
		Object->SetStringField(TEXT("name"), MaterialFunction->GetName());
		Object->SetStringField(TEXT("class"), MaterialFunction->GetClass() ? MaterialFunction->GetClass()->GetPathName() : FString());
		Object->SetNumberField(TEXT("expression_count"), MaterialFunction->GetExpressions().Num());

		if (bIncludeExpressions)
		{
			TArray<TSharedPtr<FJsonValue>> Expressions;
			for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
			{
				Expressions.Add(MakeShared<FJsonValueObject>(MaterialExpressionToJson(Expression, bIncludeExpressionProperties)));
			}
			Object->SetArrayField(TEXT("expressions"), Expressions);
		}
		if (bIncludeFunctionProperties)
		{
			Object->SetArrayField(TEXT("properties"), PropertiesFromObject(MaterialFunction, TEXT("material_function")));
		}
		return Object;
	}

	bool ApplyPropertyPatch(UObject* Asset, UObject* TargetObject, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			if (!SetEditablePropertyValue(Asset, TargetObject, Pair.Key, Pair.Value, OutError))
			{
				return false;
			}
		}
		return true;
	}

	UClass* ResolveMaterialExpressionClass(const FString& ClassPathOrName)
	{
		if (ClassPathOrName.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* Class = ResolveClassReference(ClassPathOrName))
		{
			return Class;
		}

		const FString ClassName = ClassPathOrName.StartsWith(TEXT("U")) ? ClassPathOrName.Mid(1) : ClassPathOrName;
		if (UClass* EngineClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName)))
		{
			return EngineClass;
		}
		if (!ClassName.StartsWith(TEXT("MaterialExpression")))
		{
			return LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.MaterialExpression%s"), *ClassName));
		}
		return nullptr;
	}

	void CollectReferencedObjectsFromProperty(const FProperty* Property, const void* PropertyData, TSet<FString>& OutPaths);

	void CollectObjectReference(UObject* Object, TSet<FString>& OutPaths)
	{
		if (!Object)
		{
			return;
		}
		OutPaths.Add(Object->GetPathName());
		if (const UClass* Class = Cast<UClass>(Object))
		{
			const FString BlueprintPath = BlueprintAssetPathFromClass(Class);
			if (!BlueprintPath.IsEmpty())
			{
				OutPaths.Add(BlueprintPath);
			}
		}
	}

	void CollectReferencedObjectsFromStruct(const UStruct* Struct, const void* StructData, TSet<FString>& OutPaths)
	{
		if (!Struct || !StructData)
		{
			return;
		}
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			CollectReferencedObjectsFromProperty(Property, Property->ContainerPtrToValuePtr<void>(StructData), OutPaths);
		}
	}

	void CollectReferencedObjectsFromProperty(const FProperty* Property, const void* PropertyData, TSet<FString>& OutPaths)
	{
		if (!Property || !PropertyData)
		{
			return;
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			CollectObjectReference(ObjectProperty->GetObjectPropertyValue(PropertyData), OutPaths);
			return;
		}
		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftPtr = SoftObjectProperty->GetPropertyValue(PropertyData);
			if (SoftPtr.ToSoftObjectPath().IsValid())
			{
				OutPaths.Add(SoftPtr.ToSoftObjectPath().ToString());
			}
			return;
		}
		if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr SoftPtr = SoftClassProperty->GetPropertyValue(PropertyData);
			if (SoftPtr.ToSoftObjectPath().IsValid())
			{
				OutPaths.Add(SoftPtr.ToSoftObjectPath().ToString());
			}
			return;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			CollectReferencedObjectsFromStruct(StructProperty->Struct, PropertyData, OutPaths);
			return;
		}
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, PropertyData);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				CollectReferencedObjectsFromProperty(ArrayProperty->Inner, Helper.GetRawPtr(Index), OutPaths);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> PathsToJsonArray(const TSet<FString>& Paths)
	{
		TArray<FString> SortedPaths = Paths.Array();
		SortedPaths.Sort();
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Path : SortedPaths)
		{
			Values.Add(MakeShared<FJsonValueString>(Path));
		}
		return Values;
	}

	UClass* GetBlueprintWrapperClass(const FStateTreeEditorNode& Node, const FString& Role)
	{
		if (Role == TEXT("task"))
		{
			if (const FStateTreeBlueprintTaskWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintTaskWrapper>())
			{
				return Wrapper->TaskClass.Get();
			}
		}
		if (Role == TEXT("condition") || Role == TEXT("transition_condition") || Role == TEXT("enter_condition"))
		{
			if (const FStateTreeBlueprintConditionWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintConditionWrapper>())
			{
				return Wrapper->ConditionClass.Get();
			}
		}
		if (Role == TEXT("evaluator"))
		{
			if (const FStateTreeBlueprintEvaluatorWrapper* Wrapper = Node.Node.GetPtr<FStateTreeBlueprintEvaluatorWrapper>())
			{
				return Wrapper->EvaluatorClass.Get();
			}
		}
		return nullptr;
	}

	TSharedRef<FJsonObject> StateTreeNodeToJson(UStateTreeEditorData* EditorData, UObject* Owner, const FStateTreeEditorNode& Node, const FString& Role, int32 Index = INDEX_NONE)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), GuidToString(Node.ID));
		Object->SetStringField(TEXT("node_id"), GuidToString(Node.GetNodeID()));
		Object->SetStringField(TEXT("name"), Node.GetName().ToString());
		Object->SetStringField(TEXT("role"), Role);
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetNumberField(TEXT("expression_indent"), Node.ExpressionIndent);
		Object->SetStringField(TEXT("expression_operand"), EnumToString(Node.ExpressionOperand));
		Object->SetStringField(TEXT("node_struct"), Node.Node.GetScriptStruct() ? Node.Node.GetScriptStruct()->GetPathName() : FString());
		Object->SetStringField(TEXT("instance_struct"), Node.Instance.GetScriptStruct() ? Node.Instance.GetScriptStruct()->GetPathName() : FString());
		Object->SetStringField(TEXT("instance_object"), Node.InstanceObject ? Node.InstanceObject->GetPathName() : FString());
		Object->SetStringField(TEXT("instance_class"), Node.InstanceObject ? Node.InstanceObject->GetClass()->GetPathName() : FString());
		if (EditorData)
		{
			Object->SetStringField(TEXT("description"), EditorData->GetNodeDescription(Node, EStateTreeNodeFormatting::Text).ToString());
		}

		UClass* WrapperClass = GetBlueprintWrapperClass(Node, Role);
		if (!WrapperClass && Node.InstanceObject)
		{
			WrapperClass = Node.InstanceObject->GetClass();
		}
		if (WrapperClass)
		{
			Object->SetStringField(TEXT("blueprint_class"), WrapperClass->GetPathName());
			Object->SetStringField(TEXT("blueprint_asset"), BlueprintAssetPathFromClass(WrapperClass));
			Object->SetStringField(TEXT("blueprint_role"), GetStateTreeBlueprintRole(WrapperClass));
		}

		TArray<TSharedPtr<FJsonValue>> Properties = Node.InstanceObject ?
			PropertiesFromObject(Node.InstanceObject, TEXT("statetree_node_instance")) :
			PropertiesFromInstancedStruct(Node.Instance, Owner, TEXT("statetree_node_instance"));
		Object->SetArrayField(TEXT("properties"), Properties);

		TSet<FString> References;
		if (WrapperClass)
		{
			CollectObjectReference(WrapperClass, References);
		}
		CollectReferencedObjectsFromStruct(Node.Node.GetScriptStruct(), Node.Node.GetMemory(), References);
		if (Node.InstanceObject)
		{
			CollectReferencedObjectsFromStruct(Node.InstanceObject->GetClass(), Node.InstanceObject, References);
		}
		else
		{
			CollectReferencedObjectsFromStruct(Node.Instance.GetScriptStruct(), Node.Instance.GetMemory(), References);
		}
		Object->SetArrayField(TEXT("referenced_assets"), PathsToJsonArray(References));
		return Object;
	}

	FString MakeStatePath(const UStateTreeState* State)
	{
		return State ? State->GetPath() : FString();
	}

	void GatherStateTreeStates(const UStateTreeEditorData* EditorData, TArray<const UStateTreeState*>& OutStates)
	{
		if (!EditorData)
		{
			return;
		}
		TFunction<void(const UStateTreeState*)> Visit = [&Visit, &OutStates](const UStateTreeState* State)
		{
			if (!State)
			{
				return;
			}
			OutStates.Add(State);
			for (const UStateTreeState* Child : State->Children)
			{
				Visit(Child);
			}
		};
		for (const UStateTreeState* Root : EditorData->SubTrees)
		{
			Visit(Root);
		}
	}

	void GatherMutableStateTreeStates(UStateTreeEditorData* EditorData, TArray<UStateTreeState*>& OutStates)
	{
		if (!EditorData)
		{
			return;
		}
		TFunction<void(UStateTreeState*)> Visit = [&Visit, &OutStates](UStateTreeState* State)
		{
			if (!State)
			{
				return;
			}
			OutStates.Add(State);
			for (UStateTreeState* Child : State->Children)
			{
				Visit(Child);
			}
		};
		for (UStateTreeState* Root : EditorData->SubTrees)
		{
			Visit(Root);
		}
	}

	UStateTreeState* ResolveMutableState(UStateTreeEditorData* EditorData, const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
	{
		if (!EditorData)
		{
			OutError = TEXT("StateTree has no editor data.");
			return nullptr;
		}

		const FString StateId = GetOptionalString(Arguments, TEXT("state_id"), GetOptionalString(Arguments, TEXT("id")));
		const FString StatePath = GetOptionalString(Arguments, TEXT("state_path"), GetOptionalString(Arguments, TEXT("state")));
		const FString StateName = GetOptionalString(Arguments, TEXT("state_name"), GetOptionalString(Arguments, TEXT("name")));

		TArray<UStateTreeState*> States;
		GatherMutableStateTreeStates(EditorData, States);
		TArray<UStateTreeState*> Matches;
		for (UStateTreeState* State : States)
		{
			if (!State)
			{
				continue;
			}
			if (!StateId.IsEmpty() && GuidToString(State->ID).Equals(StateId, ESearchCase::IgnoreCase))
			{
				return State;
			}
			if (!StatePath.IsEmpty() && MakeStatePath(State).Equals(StatePath, ESearchCase::IgnoreCase))
			{
				Matches.Add(State);
			}
			else if (!StateName.IsEmpty() && State->Name.ToString().Equals(StateName, ESearchCase::IgnoreCase))
			{
				Matches.Add(State);
			}
		}

		if (Matches.Num() == 1)
		{
			return Matches[0];
		}
		if (Matches.Num() > 1)
		{
			OutError = FString::Printf(TEXT("State name/path is ambiguous; provide state_id or full state_path. %d matches found."), Matches.Num());
			return nullptr;
		}
		OutError = TEXT("State was not found.");
		return nullptr;
	}

	const UStateTreeState* ResolveState(const UStateTreeEditorData* EditorData, const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
	{
		return ResolveMutableState(const_cast<UStateTreeEditorData*>(EditorData), Arguments, OutError);
	}

	FString StatePathFromRuntimeHandle(const UStateTree* StateTree, FStateTreeStateHandle Handle)
	{
		if (!StateTree || !Handle.IsValid())
		{
			return Handle.Describe();
		}
		const FGuid StateId = StateTree->GetStateIdFromHandle(Handle);
		if (StateId.IsValid())
		{
			if (const UStateTreeEditorData* EditorData = GetStateTreeEditorData(const_cast<UStateTree*>(StateTree)))
			{
				if (const UStateTreeState* State = EditorData->GetStateByID(StateId))
				{
					return MakeStatePath(State);
				}
			}
		}
		if (const FCompactStateTreeState* CompactState = StateTree->GetStateFromHandle(Handle))
		{
			TArray<FString> Parts;
			FStateTreeStateHandle Current = Handle;
			while (Current.IsValid())
			{
				if (const FCompactStateTreeState* State = StateTree->GetStateFromHandle(Current))
				{
					Parts.Insert(State->Name.ToString(), 0);
					Current = State->Parent;
				}
				else
				{
					break;
				}
			}
			return FString::Join(Parts, TEXT("/"));
		}
		return Handle.Describe();
	}

	TSharedRef<FJsonObject> StateLinkToJson(const FStateTreeStateLink& Link, const UStateTreeEditorData* EditorData)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
		Object->SetStringField(TEXT("name"), Link.Name.ToString());
		Object->SetStringField(TEXT("id"), GuidToString(Link.ID));
		Object->SetStringField(TEXT("type"), EnumToString(Link.LinkType));
		if (EditorData && Link.ID.IsValid())
		{
			if (const UStateTreeState* Target = EditorData->GetStateByID(Link.ID))
			{
				Object->SetStringField(TEXT("path"), MakeStatePath(Target));
			}
		}
#endif
		Object->SetStringField(TEXT("handle"), Link.StateHandle.Describe());
		Object->SetStringField(TEXT("fallback"), EnumToString(Link.Fallback));
		return Object;
	}

	TSharedRef<FJsonObject> EventDescToJson(const FStateTreeEventDesc& EventDesc)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("tag"), EventDesc.Tag.ToString());
		Object->SetStringField(TEXT("payload_struct"), EventDesc.PayloadStruct ? EventDesc.PayloadStruct->GetPathName() : FString());
		Object->SetBoolField(TEXT("consume_on_select"), EventDesc.bConsumeEventOnSelect);
		Object->SetBoolField(TEXT("valid"), EventDesc.IsValid());
		return Object;
	}

	TSharedRef<FJsonObject> TransitionToJson(const UStateTreeEditorData* EditorData, UObject* Owner, const FStateTreeTransition& Transition, const FString& StatePath, int32 Index)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), GuidToString(Transition.ID));
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("source_state_path"), StatePath);
		Object->SetStringField(TEXT("trigger"), EnumToString(Transition.Trigger));
		Object->SetObjectField(TEXT("required_event"), EventDescToJson(Transition.RequiredEvent));
		Object->SetObjectField(TEXT("target"), StateLinkToJson(Transition.State, EditorData));
		Object->SetStringField(TEXT("priority"), EnumToString(Transition.Priority));
		Object->SetBoolField(TEXT("delay_enabled"), Transition.bDelayTransition);
		Object->SetNumberField(TEXT("delay_duration"), Transition.DelayDuration);
		Object->SetNumberField(TEXT("delay_random_variance"), Transition.DelayRandomVariance);
		Object->SetBoolField(TEXT("enabled"), Transition.bTransitionEnabled);

		TArray<TSharedPtr<FJsonValue>> Conditions;
		for (int32 ConditionIndex = 0; ConditionIndex < Transition.Conditions.Num(); ++ConditionIndex)
		{
			Conditions.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(const_cast<UStateTreeEditorData*>(EditorData), Owner, Transition.Conditions[ConditionIndex], TEXT("transition_condition"), ConditionIndex)));
		}
		Object->SetArrayField(TEXT("conditions"), Conditions);
		Object->SetNumberField(TEXT("condition_count"), Conditions.Num());
		return Object;
	}

	TSharedRef<FJsonObject> StateToJson(UStateTree* StateTree, UStateTreeEditorData* EditorData, const UStateTreeState* State, bool bRecursive)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		if (!State)
		{
			return Object;
		}
		Object->SetStringField(TEXT("id"), GuidToString(State->ID));
		Object->SetStringField(TEXT("handle"), StateTree ? StateTree->GetStateHandleFromId(State->ID).Describe() : FString());
		Object->SetStringField(TEXT("name"), State->Name.ToString());
		Object->SetStringField(TEXT("path"), MakeStatePath(State));
		Object->SetStringField(TEXT("description"), State->Description);
		Object->SetStringField(TEXT("tag"), State->Tag.ToString());
		Object->SetStringField(TEXT("type"), EnumToString(State->Type));
		Object->SetStringField(TEXT("selection_behavior"), EnumToString(State->SelectionBehavior));
		Object->SetStringField(TEXT("tasks_completion"), EnumToString(State->TasksCompletion));
		Object->SetBoolField(TEXT("enabled"), State->bEnabled);
		Object->SetStringField(TEXT("parent_id"), State->Parent ? GuidToString(State->Parent->ID) : FString());
		Object->SetStringField(TEXT("parent_path"), State->Parent ? MakeStatePath(State->Parent) : FString());
		Object->SetNumberField(TEXT("weight"), State->Weight);
		Object->SetBoolField(TEXT("has_custom_tick_rate"), State->bHasCustomTickRate);
		Object->SetNumberField(TEXT("custom_tick_rate"), State->CustomTickRate);
		Object->SetBoolField(TEXT("check_prerequisites_when_activating_child_directly"), State->bCheckPrerequisitesWhenActivatingChildDirectly);
		Object->SetBoolField(TEXT("has_required_event_to_enter"), State->bHasRequiredEventToEnter);
		Object->SetObjectField(TEXT("required_event_to_enter"), EventDescToJson(State->RequiredEventToEnter));
		Object->SetObjectField(TEXT("linked_subtree"), StateLinkToJson(State->LinkedSubtree, EditorData));
		Object->SetStringField(TEXT("linked_asset"), State->LinkedAsset ? State->LinkedAsset->GetPathName() : FString());
		Object->SetArrayField(TEXT("parameters"), PropertyBagToJson(State->Parameters.Parameters, const_cast<UStateTreeState*>(State), TEXT("statetree_state_parameters")));

		TArray<TSharedPtr<FJsonValue>> Children;
		for (const UStateTreeState* Child : State->Children)
		{
			if (bRecursive)
			{
				Children.Add(MakeShared<FJsonValueObject>(StateToJson(StateTree, EditorData, Child, true)));
			}
			else if (Child)
			{
				TSharedRef<FJsonObject> ChildObject = MakeShared<FJsonObject>();
				ChildObject->SetStringField(TEXT("id"), GuidToString(Child->ID));
				ChildObject->SetStringField(TEXT("name"), Child->Name.ToString());
				ChildObject->SetStringField(TEXT("path"), MakeStatePath(Child));
				Children.Add(MakeShared<FJsonValueObject>(ChildObject));
			}
		}
		Object->SetArrayField(TEXT("children"), Children);
		Object->SetNumberField(TEXT("child_count"), Children.Num());

		TArray<TSharedPtr<FJsonValue>> EnterConditions;
		for (int32 Index = 0; Index < State->EnterConditions.Num(); ++Index)
		{
			EnterConditions.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->EnterConditions[Index], TEXT("enter_condition"), Index)));
		}
		Object->SetArrayField(TEXT("conditions"), EnterConditions);

		TArray<TSharedPtr<FJsonValue>> Tasks;
		for (int32 Index = 0; Index < State->Tasks.Num(); ++Index)
		{
			Tasks.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->Tasks[Index], TEXT("task"), Index)));
		}
		if (State->SingleTask.Node.IsValid())
		{
			Tasks.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->SingleTask, TEXT("task"), State->Tasks.Num())));
		}
		Object->SetArrayField(TEXT("tasks"), Tasks);

		TArray<TSharedPtr<FJsonValue>> Considerations;
		for (int32 Index = 0; Index < State->Considerations.Num(); ++Index)
		{
			Considerations.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->Considerations[Index], TEXT("consideration"), Index)));
		}
		Object->SetArrayField(TEXT("considerations"), Considerations);

		TArray<TSharedPtr<FJsonValue>> Transitions;
		for (int32 Index = 0; Index < State->Transitions.Num(); ++Index)
		{
			Transitions.Add(MakeShared<FJsonValueObject>(TransitionToJson(EditorData, const_cast<UStateTreeState*>(State), State->Transitions[Index], MakeStatePath(State), Index)));
		}
		Object->SetArrayField(TEXT("transitions"), Transitions);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		if (!State->bEnabled)
		{
			TSharedRef<FJsonObject> Warning = MakeShared<FJsonObject>();
			Warning->SetStringField(TEXT("severity"), TEXT("info"));
			Warning->SetStringField(TEXT("category"), TEXT("state_setup"));
			Warning->SetStringField(TEXT("explanation"), TEXT("State is disabled and will not be selectable at runtime."));
			Warnings.Add(MakeShared<FJsonValueObject>(Warning));
		}
		if (State->Children.Num() == 0 && State->Tasks.Num() == 0 && !State->SingleTask.Node.IsValid() && State->Transitions.Num() == 0)
		{
			TSharedRef<FJsonObject> Warning = MakeShared<FJsonObject>();
			Warning->SetStringField(TEXT("severity"), TEXT("info"));
			Warning->SetStringField(TEXT("category"), TEXT("state_setup"));
			Warning->SetStringField(TEXT("explanation"), TEXT("State has no children, tasks, or outgoing transitions. This may be intentional for a terminal or placeholder state."));
			Warnings.Add(MakeShared<FJsonValueObject>(Warning));
		}
		Object->SetArrayField(TEXT("warnings"), Warnings);
		return Object;
	}

	TSharedRef<FJsonObject> BuildStateTreeInspection(UStateTree* StateTree, bool bIncludeHierarchy = true)
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
		Data->SetStringField(TEXT("asset_path"), StateTree ? StateTree->GetPathName() : FString());
		Data->SetStringField(TEXT("package"), StateTree && StateTree->GetOutermost() ? StateTree->GetOutermost()->GetName() : FString());
		Data->SetBoolField(TEXT("ready_to_run"), StateTree ? StateTree->IsReadyToRun() : false);
		Data->SetStringField(TEXT("schema_class"), StateTree && StateTree->GetSchema() ? StateTree->GetSchema()->GetClass()->GetPathName() : FString());
		Data->SetStringField(TEXT("editor_schema_class"), EditorData && EditorData->EditorSchema ? EditorData->EditorSchema->GetClass()->GetPathName() : FString());
		Data->SetBoolField(TEXT("has_editor_data"), EditorData != nullptr);
		Data->SetNumberField(TEXT("last_compiled_editor_data_hash"), StateTree ? StateTree->LastCompiledEditorDataHash : 0);

		TArray<TSharedPtr<FJsonValue>> ContextData;
		if (StateTree)
		{
			for (const FStateTreeExternalDataDesc& Desc : StateTree->GetContextDataDescs())
			{
				ContextData.Add(MakeShared<FJsonValueObject>(ExternalDataDescToJson(Desc)));
			}
		}
		Data->SetArrayField(TEXT("context_data"), ContextData);

		TArray<TSharedPtr<FJsonValue>> ExternalData;
		if (StateTree)
		{
			for (const FStateTreeExternalDataDesc& Desc : StateTree->GetExternalDataDescs())
			{
				ExternalData.Add(MakeShared<FJsonValueObject>(ExternalDataDescToJson(Desc)));
			}
		}
		Data->SetArrayField(TEXT("external_data_requirements"), ExternalData);

		if (StateTree)
		{
			Data->SetArrayField(TEXT("parameters"), PropertyBagToJson(StateTree->GetDefaultParameters(), StateTree, TEXT("statetree_parameters")));
		}
		if (EditorData)
		{
			Data->SetArrayField(TEXT("editor_parameters"), PropertyBagToJson(EditorData->GetRootParametersPropertyBag(), StateTree, TEXT("statetree_editor_parameters")));

			TArray<TSharedPtr<FJsonValue>> Evaluators;
			for (int32 Index = 0; Index < EditorData->Evaluators.Num(); ++Index)
			{
				Evaluators.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, StateTree, EditorData->Evaluators[Index], TEXT("evaluator"), Index)));
			}
			Data->SetArrayField(TEXT("evaluators"), Evaluators);

			TArray<TSharedPtr<FJsonValue>> GlobalTasks;
			for (int32 Index = 0; Index < EditorData->GlobalTasks.Num(); ++Index)
			{
				GlobalTasks.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, StateTree, EditorData->GlobalTasks[Index], TEXT("global_task"), Index)));
			}
			Data->SetArrayField(TEXT("global_tasks"), GlobalTasks);
			Data->SetStringField(TEXT("global_tasks_completion"), EnumToString(EditorData->GlobalTasksCompletion));

			TArray<TSharedPtr<FJsonValue>> States;
			for (const UStateTreeState* Root : EditorData->SubTrees)
			{
				States.Add(MakeShared<FJsonValueObject>(StateToJson(StateTree, EditorData, Root, bIncludeHierarchy)));
			}
			Data->SetArrayField(TEXT("states"), States);
			TSharedPtr<FJsonObject> RootStateObject = States.Num() > 0 ? States[0]->AsObject() : nullptr;
			Data->SetObjectField(TEXT("root_state"), RootStateObject.IsValid() ? RootStateObject.ToSharedRef() : MakeShared<FJsonObject>());

			TArray<const UStateTreeState*> FlatStates;
			GatherStateTreeStates(EditorData, FlatStates);
			TArray<TSharedPtr<FJsonValue>> FlatStateValues;
			for (const UStateTreeState* State : FlatStates)
			{
				TSharedRef<FJsonObject> Flat = MakeShared<FJsonObject>();
				Flat->SetStringField(TEXT("id"), GuidToString(State->ID));
				Flat->SetStringField(TEXT("handle"), StateTree ? StateTree->GetStateHandleFromId(State->ID).Describe() : FString());
				Flat->SetStringField(TEXT("name"), State->Name.ToString());
				Flat->SetStringField(TEXT("path"), MakeStatePath(State));
				Flat->SetStringField(TEXT("parent_id"), State->Parent ? GuidToString(State->Parent->ID) : FString());
				FlatStateValues.Add(MakeShared<FJsonValueObject>(Flat));
			}
			Data->SetArrayField(TEXT("state_index"), FlatStateValues);

			FString BindingsText;
			if (FProperty* BindingsProperty = FindPropertyByNameLoose(EditorData->GetClass(), TEXT("EditorBindings")))
			{
				BindingsText = ExportPropertyText(BindingsProperty, BindingsProperty->ContainerPtrToValuePtr<void>(EditorData), EditorData);
			}
			Data->SetStringField(TEXT("property_bindings_text"), BindingsText);
		}
		return Data;
	}

	TSharedRef<FJsonObject> MakeStateTreeIssue(const FString& Severity, const FString& Category, const FString& Explanation, const FString& SuggestedFix, float Confidence, const UStateTreeState* State = nullptr, const FString& ReferencedAsset = FString())
	{
		TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("severity"), Severity);
		Issue->SetStringField(TEXT("category"), Category);
		if (State)
		{
			Issue->SetStringField(TEXT("state_id"), GuidToString(State->ID));
			Issue->SetStringField(TEXT("state_path"), MakeStatePath(State));
		}
		if (!ReferencedAsset.IsEmpty())
		{
			Issue->SetStringField(TEXT("referenced_asset"), ReferencedAsset);
		}
		Issue->SetStringField(TEXT("explanation"), Explanation);
		Issue->SetStringField(TEXT("suggested_fix"), SuggestedFix);
		Issue->SetNumberField(TEXT("confidence"), Confidence);
		return Issue;
	}

	void AddNodeValidationIssues(const FStateTreeEditorNode& Node, const FString& Role, const UStateTreeState* State, TArray<TSharedPtr<FJsonValue>>& Issues)
	{
		const bool bBlueprintWrapper = Role.Contains(TEXT("task")) || Role.Contains(TEXT("condition")) || Role == TEXT("evaluator");
		if (bBlueprintWrapper && Node.Node.IsValid())
		{
			const UScriptStruct* Struct = Node.Node.GetScriptStruct();
			const bool bIsBlueprintWrapper = Struct == FStateTreeBlueprintTaskWrapper::StaticStruct() ||
				Struct == FStateTreeBlueprintConditionWrapper::StaticStruct() ||
				Struct == FStateTreeBlueprintEvaluatorWrapper::StaticStruct();
			if (bIsBlueprintWrapper && !GetBlueprintWrapperClass(Node, Role))
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("missing_blueprint_class"), FString::Printf(TEXT("%s node '%s' has no valid Blueprint class."), *Role, *Node.GetName().ToString()), TEXT("Reassign the task/condition/evaluator Blueprint class in the StateTree editor."), 0.9f, State)));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ValidateStateTreeStatic(UStateTree* StateTree, bool bCompile, bool& bOutCompileSucceeded)
	{
		TArray<TSharedPtr<FJsonValue>> Issues;
		bOutCompileSucceeded = true;
		UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
		if (!StateTree)
		{
			Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("load"), TEXT("StateTree asset is null."), TEXT("Check the asset path."), 1.0f)));
			return Issues;
		}
		if (!EditorData)
		{
			Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("editor_data"), TEXT("StateTree has no editor data, so structure cannot be inspected or safely edited."), TEXT("Open and resave/recompile the StateTree in the editor."), 0.9f)));
			return Issues;
		}
		if (EditorData->SubTrees.Num() == 0)
		{
			Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("structure"), TEXT("StateTree has no root/subtree states."), TEXT("Add at least one root state."), 0.95f)));
		}

		TArray<const UStateTreeState*> States;
		GatherStateTreeStates(EditorData, States);
		TMap<FString, int32> PathCounts;
		TMap<const UStateTreeState*, TSet<FString>> SiblingNames;
		for (const UStateTreeState* State : States)
		{
			if (!State)
			{
				continue;
			}
			PathCounts.FindOrAdd(MakeStatePath(State))++;
			if (State->Parent)
			{
				TSet<FString>& Names = SiblingNames.FindOrAdd(State->Parent);
				const FString Name = State->Name.ToString().ToLower();
				if (Names.Contains(Name))
				{
					Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("warning"), TEXT("duplicate_state_name"), FString::Printf(TEXT("Sibling state name '%s' is duplicated."), *State->Name.ToString()), TEXT("Rename one of the sibling states or use full state paths/ids for bridge edits."), 0.85f, State)));
				}
				Names.Add(Name);
			}

			if (!State->bEnabled)
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("info"), TEXT("disabled_state"), TEXT("State is disabled and will not be selected at runtime."), TEXT("Enable it if this state should participate in selection."), 0.9f, State)));
			}
			if (State->Children.Num() == 0 &&
				(State->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder ||
				State->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom ||
				State->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility ||
				State->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility))
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("info"), TEXT("selector_without_children"), TEXT("State selection behavior tries children, but the state has no children."), TEXT("Use Try Enter for a leaf state or add selectable children."), 0.75f, State)));
			}
			if (State->Transitions.Num() == 0 && State->Children.Num() == 0 && (State->Tasks.Num() > 0 || State->SingleTask.Node.IsValid()))
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("info"), TEXT("leaf_without_transitions"), TEXT("Leaf state has tasks but no outgoing transitions. It may stay active until a parent/global transition fires."), TEXT("Add a completion/tick/event transition if this state should leave on its own."), 0.65f, State)));
			}

			for (const FStateTreeEditorNode& Node : State->Tasks)
			{
				AddNodeValidationIssues(Node, TEXT("task"), State, Issues);
			}
			if (State->SingleTask.Node.IsValid())
			{
				AddNodeValidationIssues(State->SingleTask, TEXT("task"), State, Issues);
			}
			for (const FStateTreeEditorNode& Node : State->EnterConditions)
			{
				AddNodeValidationIssues(Node, TEXT("enter_condition"), State, Issues);
			}
			for (const FStateTreeTransition& Transition : State->Transitions)
			{
				if (!Transition.bTransitionEnabled)
				{
					Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("info"), TEXT("disabled_transition"), TEXT("Transition is disabled and will not fire."), TEXT("Enable the transition if it is expected at runtime."), 0.9f, State)));
				}
#if WITH_EDITORONLY_DATA
				if (Transition.State.LinkType == EStateTreeTransitionType::GotoState && Transition.State.ID.IsValid() && !EditorData->GetStateByID(Transition.State.ID))
				{
					Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("invalid_transition_target"), FString::Printf(TEXT("Transition target '%s' could not be resolved."), *Transition.State.Name.ToString()), TEXT("Re-select the transition target state."), 0.95f, State)));
				}
#endif
				for (const FStateTreeEditorNode& Node : Transition.Conditions)
				{
					AddNodeValidationIssues(Node, TEXT("transition_condition"), State, Issues);
				}
			}
			if (State->Type == EStateTreeStateType::LinkedAsset && !State->LinkedAsset)
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("missing_linked_asset"), TEXT("LinkedAsset state has no linked StateTree asset."), TEXT("Assign a linked StateTree asset or change the state type."), 0.95f, State)));
			}
		}

		for (const TPair<FString, int32>& Pair : PathCounts)
		{
			if (Pair.Value > 1)
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("warning"), TEXT("duplicate_state_path"), FString::Printf(TEXT("State path '%s' appears %d times."), *Pair.Key, Pair.Value), TEXT("Rename states so human-readable paths are unique."), 0.8f)));
			}
		}

		for (const FStateTreeEditorNode& Node : EditorData->Evaluators)
		{
			AddNodeValidationIssues(Node, TEXT("evaluator"), nullptr, Issues);
		}
		for (const FStateTreeEditorNode& Node : EditorData->GlobalTasks)
		{
			AddNodeValidationIssues(Node, TEXT("task"), nullptr, Issues);
		}

		if (bCompile)
		{
			FStateTreeCompilerLog CompileLog;
			bOutCompileSucceeded = UE::StateTree::Compiler::FCompilerManager::CompileSynchronously(StateTree, CompileLog);
			const TArray<TSharedRef<FTokenizedMessage>> CompileMessages = CompileLog.ToTokenizedMessages();
			for (const TSharedRef<FTokenizedMessage>& Message : CompileMessages)
			{
				const FString Severity = Message->GetSeverity() == EMessageSeverity::Error ? TEXT("error") :
					Message->GetSeverity() == EMessageSeverity::Warning ? TEXT("warning") : TEXT("info");
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(Severity, TEXT("compiler"), Message->ToText().ToString(), TEXT("Open the StateTree compiler message and repair the referenced state or node."), 0.95f)));
			}
			if (!bOutCompileSucceeded && CompileMessages.Num() == 0)
			{
				Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("compiler"), TEXT("StateTree compiler returned failure without detailed messages."), TEXT("Open the StateTree in the editor and compile to see full diagnostics."), 0.8f)));
			}
		}

		return Issues;
	}

	UStateTree* GetStateTreeFromComponent(UStateTreeComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}
		if (FStructProperty* RefProperty = FindFProperty<FStructProperty>(Component->GetClass(), TEXT("StateTreeRef")))
		{
			if (RefProperty->Struct == FStateTreeReference::StaticStruct())
			{
				FStateTreeReference* Reference = RefProperty->ContainerPtrToValuePtr<FStateTreeReference>(Component);
				return Reference ? Reference->GetMutableStateTree() : nullptr;
			}
		}
		return nullptr;
	}

	FStateTreeInstanceData* GetInstanceDataFromComponent(UStateTreeComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}
		if (FStructProperty* InstanceProperty = FindFProperty<FStructProperty>(Component->GetClass(), TEXT("InstanceData")))
		{
			if (InstanceProperty->Struct == FStateTreeInstanceData::StaticStruct())
			{
				return InstanceProperty->ContainerPtrToValuePtr<FStateTreeInstanceData>(Component);
			}
		}
		return nullptr;
	}

	TArray<UWorld*> GetCandidateWorlds()
	{
		TArray<UWorld*> Worlds;
		if (!GEngine)
		{
			return Worlds;
		}
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game || World->WorldType == EWorldType::Editor))
			{
				Worlds.Add(World);
			}
		}
		return Worlds;
	}

	AActor* ResolveStateTreeActor(const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
	{
		const bool bSelected = GetOptionalBool(Arguments, TEXT("selected_actor"), false) || GetOptionalString(Arguments, TEXT("actor")).Equals(TEXT("selected"), ESearchCase::IgnoreCase);
		if (bSelected && GEditor)
		{
			if (USelection* Selection = GEditor->GetSelectedActors())
			{
				if (AActor* Actor = Selection->GetTop<AActor>())
				{
					return Actor;
				}
			}
		}

		const FString ActorPath = GetOptionalString(Arguments, TEXT("actor_path"));
		if (!ActorPath.IsEmpty())
		{
			if (AActor* Actor = Cast<AActor>(ResolveObjectPath(ActorPath)))
			{
				return Actor;
			}
		}

		const FString ActorName = GetOptionalString(Arguments, TEXT("actor_name"), GetOptionalString(Arguments, TEXT("actor")));
		const FString ClassFilter = GetOptionalString(Arguments, TEXT("actor_class"));
		UClass* ActorClass = ClassFilter.IsEmpty() ? nullptr : ResolveClassReference(ClassFilter);
		for (UWorld* World : GetCandidateWorlds())
		{
			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				AActor* Actor = *ActorIt;
				if (!Actor)
				{
					continue;
				}
				if (ActorClass && !Actor->IsA(ActorClass))
				{
					continue;
				}
				if (!ActorName.IsEmpty() &&
					!Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase) &&
					!Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase) &&
					!Actor->GetPathName().Contains(ActorName, ESearchCase::IgnoreCase))
				{
					continue;
				}
				return Actor;
			}
		}

		OutError = TEXT("Could not find a matching actor. Provide actor_path, actor_name, actor_class, or selected_actor=true.");
		return nullptr;
	}

	TSharedRef<FJsonObject> StateTreeEventToJson(const FStateTreeEvent& Event)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("tag"), Event.Tag.ToString());
		Object->SetStringField(TEXT("origin"), Event.Origin.ToString());
		Object->SetStringField(TEXT("payload_struct"), Event.Payload.GetScriptStruct() ? Event.Payload.GetScriptStruct()->GetPathName() : FString());
		return Object;
	}

	TSharedRef<FJsonObject> RuntimeTaskToJson(const UStateTree* StateTree, int32 NodeIndex)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("node_index"), NodeIndex);
		if (!StateTree)
		{
			return Object;
		}
		const FConstStructView Node = StateTree->GetNode(NodeIndex);
		Object->SetStringField(TEXT("node_struct"), Node.GetScriptStruct() ? Node.GetScriptStruct()->GetPathName() : FString());
		const UScriptStruct* Struct = Node.GetScriptStruct();
		const uint8* Memory = Node.GetMemory();
		if (Struct && Memory)
		{
			TSet<FString> References;
			CollectReferencedObjectsFromStruct(Struct, Memory, References);
			Object->SetArrayField(TEXT("referenced_assets"), PathsToJsonArray(References));
		}
		return Object;
	}

	TSharedRef<FJsonObject> BuildComponentRuntimeSnapshot(UStateTreeComponent* Component)
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("component"), Component ? Component->GetPathName() : FString());
		Data->SetStringField(TEXT("component_name"), Component ? Component->GetName() : FString());
		Data->SetStringField(TEXT("owner"), Component && Component->GetOwner() ? Component->GetOwner()->GetPathName() : FString());
		Data->SetStringField(TEXT("owner_label"), Component && Component->GetOwner() ? Component->GetOwner()->GetActorLabel() : FString());
		Data->SetBoolField(TEXT("running"), Component ? Component->IsRunning() : false);
		Data->SetBoolField(TEXT("paused"), Component ? Component->IsPaused() : false);
		Data->SetStringField(TEXT("run_status"), Component ? EnumToString(Component->GetStateTreeRunStatus()) : FString());

		UStateTree* StateTree = GetStateTreeFromComponent(Component);
		FStateTreeInstanceData* InstanceData = GetInstanceDataFromComponent(Component);
		Data->SetStringField(TEXT("state_tree"), StateTree ? StateTree->GetPathName() : FString());
		Data->SetBoolField(TEXT("has_instance_data"), InstanceData != nullptr);

		TArray<TSharedPtr<FJsonValue>> ActiveFrames;
		TArray<TSharedPtr<FJsonValue>> ActiveStatePaths;
		TArray<TSharedPtr<FJsonValue>> RunningTasks;
		TArray<TSharedPtr<FJsonValue>> Events;
		if (Component && StateTree && InstanceData)
		{
			FStateTreeReadOnlyExecutionContext Context(Component, StateTree, *InstanceData);
			Data->SetStringField(TEXT("context_run_status"), EnumToString(Context.GetStateTreeRunStatus()));
			Data->SetStringField(TEXT("active_state_name"), Context.GetActiveStateName());
			for (const FName& ActiveName : Context.GetActiveStateNames())
			{
				ActiveStatePaths.Add(MakeShared<FJsonValueString>(ActiveName.ToString()));
			}
			for (const FStateTreeExecutionFrame& Frame : Context.GetActiveFrames())
			{
				TSharedRef<FJsonObject> FrameObject = MakeShared<FJsonObject>();
				const UStateTree* FrameTree = Frame.StateTree.Get();
				FrameObject->SetStringField(TEXT("state_tree"), FrameTree ? FrameTree->GetPathName() : FString());
				FrameObject->SetStringField(TEXT("root_state"), Frame.RootState.Describe());
				FrameObject->SetBoolField(TEXT("global_frame"), Frame.bIsGlobalFrame);
				TArray<TSharedPtr<FJsonValue>> FrameStates;
				for (int32 Index = 0; Index < Frame.ActiveStates.Num(); ++Index)
				{
					const FStateTreeStateHandle Handle = Frame.ActiveStates[Index];
					TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
					StateObject->SetStringField(TEXT("handle"), Handle.Describe());
					StateObject->SetStringField(TEXT("path"), StatePathFromRuntimeHandle(FrameTree, Handle));
					if (FrameTree)
					{
						if (const FCompactStateTreeState* CompactState = FrameTree->GetStateFromHandle(Handle))
						{
							StateObject->SetStringField(TEXT("name"), CompactState->Name.ToString());
							for (int32 TaskOffset = 0; TaskOffset < CompactState->TasksNum; ++TaskOffset)
							{
								RunningTasks.Add(MakeShared<FJsonValueObject>(RuntimeTaskToJson(FrameTree, CompactState->TasksBegin + TaskOffset)));
							}
						}
					}
					FrameStates.Add(MakeShared<FJsonValueObject>(StateObject));
				}
				FrameObject->SetArrayField(TEXT("active_states"), FrameStates);
				ActiveFrames.Add(MakeShared<FJsonValueObject>(FrameObject));
			}
			for (const FStateTreeSharedEvent& SharedEvent : InstanceData->GetEventQueue().GetEventsView())
			{
				if (SharedEvent.IsValid())
				{
					Events.Add(MakeShared<FJsonValueObject>(StateTreeEventToJson(*SharedEvent.Get())));
				}
			}
		}
		Data->SetArrayField(TEXT("active_frames"), ActiveFrames);
		Data->SetArrayField(TEXT("active_state_paths"), ActiveStatePaths);
		Data->SetArrayField(TEXT("running_tasks"), RunningTasks);
		Data->SetArrayField(TEXT("event_queue"), Events);
		Data->SetStringField(TEXT("limitations"), TEXT("UE runtime exposes current active frames, active states, run status, running task node structs, and event queue. Per-condition pass/fail and transition candidate rejection details are not exposed through public UE 5.7 component APIs, so blocked-transition explanations combine runtime state with static transition/condition inspection."));
		return Data;
	}

	struct FStateTreeTraceBuffer
	{
		FString TraceId;
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStateTreeComponent> Component;
		FString LastActivePath;
		double StartedAt = 0.0;
		int32 MaxEvents = 256;
		bool bRunning = false;
		TArray<TSharedPtr<FJsonValue>> Events;
	};

	TMap<FString, FStateTreeTraceBuffer> GStateTreeTraceBuffers;
	int32 GStateTreeTraceSerial = 0;

	FString SnapshotActivePath(const TSharedPtr<FJsonObject>& Snapshot)
	{
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (!TryGetArray(Snapshot, TEXT("active_state_paths"), Paths))
		{
			return FString();
		}
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Value : *Paths)
		{
			Parts.Add(Value->AsString());
		}
		return FString::Join(Parts, TEXT(" > "));
	}

	void AddTraceEvent(FStateTreeTraceBuffer& Trace, const FString& Type, const TSharedPtr<FJsonObject>& Payload)
	{
		TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetStringField(TEXT("type"), Type);
		Event->SetNumberField(TEXT("time_seconds"), FPlatformTime::Seconds());
		if (Payload.IsValid())
		{
			Event->SetObjectField(TEXT("payload"), Payload);
		}
		Trace.Events.Add(MakeShared<FJsonValueObject>(Event));
		while (Trace.Events.Num() > Trace.MaxEvents)
		{
			Trace.Events.RemoveAt(0);
		}
	}

	void PollTrace(FStateTreeTraceBuffer& Trace)
	{
		if (!Trace.bRunning || !Trace.Component.IsValid())
		{
			return;
		}
		TSharedRef<FJsonObject> Snapshot = BuildComponentRuntimeSnapshot(Trace.Component.Get());
		const FString ActivePath = SnapshotActivePath(Snapshot);
		if (!ActivePath.Equals(Trace.LastActivePath, ESearchCase::CaseSensitive))
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("from"), Trace.LastActivePath);
			Payload->SetStringField(TEXT("to"), ActivePath);
			Payload->SetObjectField(TEXT("snapshot"), Snapshot);
			AddTraceEvent(Trace, TEXT("state_change"), Payload);
			Trace.LastActivePath = ActivePath;
		}
	}

	TSharedPtr<FJsonObject> GetEnvelopeData(const TSharedRef<FJsonObject>& Envelope)
	{
		TSharedPtr<FJsonObject> Data;
		TryGetObject(Envelope, TEXT("data"), Data);
		return Data;
	}

	TSharedPtr<FJsonObject> GetFirstComponentSnapshot(const TSharedPtr<FJsonObject>& SnapshotData)
	{
		if (!SnapshotData.IsValid())
		{
			return nullptr;
		}
		if (!GetOptionalString(SnapshotData, TEXT("state_tree")).IsEmpty())
		{
			return SnapshotData;
		}
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
		if (TryGetArray(SnapshotData, TEXT("components"), Components) && Components->Num() > 0)
		{
			return (*Components)[0]->AsObject();
		}
		return nullptr;
	}

	void CollectJsonStringSignals(const TSharedPtr<FJsonValue>& Value, TSet<FString>& OutStrings)
	{
		if (!Value.IsValid())
		{
			return;
		}
		if (Value->Type == EJson::String)
		{
			const FString String = Value->AsString();
			if (String.Contains(TEXT("/Game/")) || String.Contains(TEXT("/Script/")) ||
				String.Contains(TEXT("GameplayTag")) || String.Contains(TEXT("GameplayAbility")) ||
				String.Contains(TEXT("EQS"), ESearchCase::IgnoreCase) || String.Contains(TEXT("EnvQuery"), ESearchCase::IgnoreCase))
			{
				AddUniqueString(OutStrings, String);
			}
			return;
		}
		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Child : Value->AsArray())
			{
				CollectJsonStringSignals(Child, OutStrings);
			}
			return;
		}
		if (Value->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Value->AsObject()->Values)
			{
				CollectJsonStringSignals(Pair.Value, OutStrings);
			}
		}
	}

	void CollectJsonStringSignals(const TSharedPtr<FJsonObject>& Object, TSet<FString>& OutStrings)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			CollectJsonStringSignals(Pair.Value, OutStrings);
		}
	}

	FString MermaidIdForState(const UStateTreeState* State)
	{
		FString Id = State ? GuidToString(State->ID) : TEXT("State");
		Id.ReplaceInline(TEXT("-"), TEXT("_"));
		return FString::Printf(TEXT("S_%s"), *Id);
	}

	FString StateLabelForMermaid(const UStateTreeState* State)
	{
		FString Label = State ? MakeStatePath(State) : TEXT("Unknown");
		Label.ReplaceInline(TEXT("\""), TEXT("'"));
		return Label;
	}

	void AppendStateTreeMarkdownNode(FString& Out, UStateTreeEditorData* EditorData, const UStateTreeState* State, int32 Depth)
	{
		if (!State)
		{
			return;
		}
		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		Out += FString::Printf(TEXT("%s- `%s` (`%s`, %s)\n"), *Indent, *MakeStatePath(State), *GuidToString(State->ID), *EnumToString(State->Type));
		if (State->Tasks.Num() > 0 || State->SingleTask.Node.IsValid())
		{
			Out += FString::Printf(TEXT("%s  - Tasks:\n"), *Indent);
			for (int32 Index = 0; Index < State->Tasks.Num(); ++Index)
			{
				Out += FString::Printf(TEXT("%s    - %s\n"), *Indent, *StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->Tasks[Index], TEXT("task"), Index)->GetStringField(TEXT("description")));
			}
			if (State->SingleTask.Node.IsValid())
			{
				Out += FString::Printf(TEXT("%s    - %s\n"), *Indent, *StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->SingleTask, TEXT("task"), State->Tasks.Num())->GetStringField(TEXT("description")));
			}
		}
		if (State->EnterConditions.Num() > 0)
		{
			Out += FString::Printf(TEXT("%s  - Enter conditions:\n"), *Indent);
			for (int32 Index = 0; Index < State->EnterConditions.Num(); ++Index)
			{
				Out += FString::Printf(TEXT("%s    - %s\n"), *Indent, *StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->EnterConditions[Index], TEXT("enter_condition"), Index)->GetStringField(TEXT("description")));
			}
		}
		if (State->Transitions.Num() > 0)
		{
			Out += FString::Printf(TEXT("%s  - Transitions:\n"), *Indent);
			for (const FStateTreeTransition& Transition : State->Transitions)
			{
				const FString Target = Transition.State.Name.IsNone() ? Transition.State.StateHandle.Describe() : Transition.State.Name.ToString();
				Out += FString::Printf(TEXT("%s    - %s -> %s (%s, %d condition%s)\n"), *Indent, *EnumToString(Transition.Trigger), *Target, *EnumToString(Transition.Priority), Transition.Conditions.Num(), Transition.Conditions.Num() == 1 ? TEXT("") : TEXT("s"));
			}
		}
		for (const UStateTreeState* Child : State->Children)
		{
			AppendStateTreeMarkdownNode(Out, EditorData, Child, Depth + 1);
		}
	}

	void AppendMermaidStateDeclarations(FString& Out, const UStateTreeState* State)
	{
		if (!State)
		{
			return;
		}
		Out += FString::Printf(TEXT("    state \"%s\" as %s\n"), *StateLabelForMermaid(State), *MermaidIdForState(State));
		for (const UStateTreeState* Child : State->Children)
		{
			AppendMermaidStateDeclarations(Out, Child);
		}
	}

	void AppendMermaidStateEdges(FString& Out, const UStateTreeEditorData* EditorData, const UStateTreeState* State)
	{
		if (!State)
		{
			return;
		}
		for (const UStateTreeState* Child : State->Children)
		{
			if (Child)
			{
				Out += FString::Printf(TEXT("    %s --> %s : child\n"), *MermaidIdForState(State), *MermaidIdForState(Child));
			}
		}
		for (const FStateTreeTransition& Transition : State->Transitions)
		{
			const UStateTreeState* Target = nullptr;
#if WITH_EDITORONLY_DATA
			if (EditorData && Transition.State.ID.IsValid())
			{
				Target = EditorData->GetStateByID(Transition.State.ID);
			}
#endif
			if (Target)
			{
				FString Label = EnumToString(Transition.Trigger);
				if (Transition.Conditions.Num() > 0)
				{
					Label += FString::Printf(TEXT(" / %d condition%s"), Transition.Conditions.Num(), Transition.Conditions.Num() == 1 ? TEXT("") : TEXT("s"));
				}
				Out += FString::Printf(TEXT("    %s --> %s : %s\n"), *MermaidIdForState(State), *MermaidIdForState(Target), *Label);
			}
		}
		for (const UStateTreeState* Child : State->Children)
		{
			AppendMermaidStateEdges(Out, EditorData, Child);
		}
	}

	FStateTreeTransition* FindMutableTransition(UStateTreeEditorData* EditorData, const FString& TransitionId, UStateTreeState** OutState = nullptr)
	{
		if (OutState)
		{
			*OutState = nullptr;
		}
		if (!EditorData || TransitionId.IsEmpty())
		{
			return nullptr;
		}
		TArray<UStateTreeState*> States;
		GatherMutableStateTreeStates(EditorData, States);
		for (UStateTreeState* State : States)
		{
			if (!State)
			{
				continue;
			}
			for (FStateTreeTransition& Transition : State->Transitions)
			{
				if (GuidToString(Transition.ID).Equals(TransitionId, ESearchCase::IgnoreCase))
				{
					if (OutState)
					{
						*OutState = State;
					}
					return &Transition;
				}
			}
		}
		return nullptr;
	}

	bool InitializeStateTreeBlueprintEditorNode(FStateTreeEditorNode& Node, UObject* Outer, UClass* Class, const FString& Role, FString& OutError)
	{
		if (!Class)
		{
			OutError = TEXT("Could not resolve node Blueprint class.");
			return false;
		}
		if (!Outer)
		{
			OutError = TEXT("Node outer is null.");
			return false;
		}
		Node.ID = FGuid::NewGuid();
		Node.Node.Reset();
		Node.Instance.Reset();
		Node.InstanceObject = nullptr;
		Node.ExecutionRuntimeData.Reset();
		Node.ExecutionRuntimeDataObject = nullptr;
		if (Role == TEXT("task") || Role == TEXT("global_task"))
		{
			if (!Class->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()))
			{
				OutError = FString::Printf(TEXT("Class '%s' is not a StateTree task Blueprint class."), *Class->GetPathName());
				return false;
			}
			Node.Node.InitializeAs(FStateTreeBlueprintTaskWrapper::StaticStruct());
			FStateTreeBlueprintTaskWrapper& Wrapper = Node.Node.GetMutable<FStateTreeBlueprintTaskWrapper>();
			Wrapper.TaskClass = Class;
		}
		else if (Role == TEXT("condition") || Role == TEXT("enter_condition") || Role == TEXT("transition_condition"))
		{
			if (!Class->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()))
			{
				OutError = FString::Printf(TEXT("Class '%s' is not a StateTree condition Blueprint class."), *Class->GetPathName());
				return false;
			}
			Node.Node.InitializeAs(FStateTreeBlueprintConditionWrapper::StaticStruct());
			FStateTreeBlueprintConditionWrapper& Wrapper = Node.Node.GetMutable<FStateTreeBlueprintConditionWrapper>();
			Wrapper.ConditionClass = Class;
		}
		else if (Role == TEXT("evaluator"))
		{
			if (!Class->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()))
			{
				OutError = FString::Printf(TEXT("Class '%s' is not a StateTree evaluator Blueprint class."), *Class->GetPathName());
				return false;
			}
			Node.Node.InitializeAs(FStateTreeBlueprintEvaluatorWrapper::StaticStruct());
			FStateTreeBlueprintEvaluatorWrapper& Wrapper = Node.Node.GetMutable<FStateTreeBlueprintEvaluatorWrapper>();
			Wrapper.EvaluatorClass = Class;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported StateTree node role '%s'."), *Role);
			return false;
		}
		Node.InstanceObject = NewObject<UObject>(Outer, Class, NAME_None, RF_Transactional);
		return true;
	}

	FStateTreeEditorNode* FindMutableStateTreeNodeById(UStateTreeEditorData* EditorData, const FString& NodeId, UStateTreeState** OutState = nullptr, FStateTreeTransition** OutTransition = nullptr)
	{
		if (OutState)
		{
			*OutState = nullptr;
		}
		if (OutTransition)
		{
			*OutTransition = nullptr;
		}
		if (!EditorData || NodeId.IsEmpty())
		{
			return nullptr;
		}
		auto MatchesNode = [&NodeId](const FStateTreeEditorNode& Node)
		{
			return GuidToString(Node.ID).Equals(NodeId, ESearchCase::IgnoreCase) || GuidToString(Node.GetNodeID()).Equals(NodeId, ESearchCase::IgnoreCase);
		};
		for (FStateTreeEditorNode& Node : EditorData->Evaluators)
		{
			if (MatchesNode(Node))
			{
				return &Node;
			}
		}
		for (FStateTreeEditorNode& Node : EditorData->GlobalTasks)
		{
			if (MatchesNode(Node))
			{
				return &Node;
			}
		}
		TArray<UStateTreeState*> States;
		GatherMutableStateTreeStates(EditorData, States);
		for (UStateTreeState* State : States)
		{
			for (FStateTreeEditorNode& Node : State->Tasks)
			{
				if (MatchesNode(Node))
				{
					if (OutState)
					{
						*OutState = State;
					}
					return &Node;
				}
			}
			if (State->SingleTask.Node.IsValid() && MatchesNode(State->SingleTask))
			{
				if (OutState)
				{
					*OutState = State;
				}
				return &State->SingleTask;
			}
			for (FStateTreeEditorNode& Node : State->EnterConditions)
			{
				if (MatchesNode(Node))
				{
					if (OutState)
					{
						*OutState = State;
					}
					return &Node;
				}
			}
			for (FStateTreeTransition& Transition : State->Transitions)
			{
				for (FStateTreeEditorNode& Node : Transition.Conditions)
				{
					if (MatchesNode(Node))
					{
						if (OutState)
						{
							*OutState = State;
						}
						if (OutTransition)
						{
							*OutTransition = &Transition;
						}
						return &Node;
					}
				}
			}
		}
		return nullptr;
	}

	bool RemoveStateTreeEditorNodeById(TArray<FStateTreeEditorNode>& Nodes, const FString& NodeId)
	{
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (GuidToString(Nodes[Index].ID).Equals(NodeId, ESearchCase::IgnoreCase) || GuidToString(Nodes[Index].GetNodeID()).Equals(NodeId, ESearchCase::IgnoreCase))
			{
				Nodes.RemoveAt(Index);
				return true;
			}
		}
		return false;
	}

	void CompileStateTreeIfRequested(UStateTree* StateTree, bool bCompile)
	{
		if (StateTree && bCompile)
		{
			FStateTreeCompilerLog CompileLog;
			UE::StateTree::Compiler::FCompilerManager::CompileSynchronously(StateTree, CompileLog);
		}
	}

	void SetCommonNodeFields(UEdGraph* Graph, UEdGraphNode* Node, int32 X, int32 Y)
	{
		Node->SetFlags(RF_Transactional);
		Graph->AddNode(Node, true, false);
		Node->CreateNewGuid();
		Node->NodePosX = X;
		Node->NodePosY = Y;
	}

	TSharedPtr<FJsonObject> StatusData(uint32 Port)
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("project"), FApp::GetProjectName());
		Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
		Data->SetNumberField(TEXT("port"), Port);
		Data->SetStringField(TEXT("base_url"), FString::Printf(TEXT("http://127.0.0.1:%u/llm-blueprint/v1"), Port));
		Data->SetStringField(TEXT("invoke_url"), FString::Printf(TEXT("http://127.0.0.1:%u/llm-blueprint/v1/invoke"), Port));
		return Data;
	}

	bool IsLocalPeer(const FHttpServerRequest& Request)
	{
		if (!Request.PeerAddress.IsValid())
		{
			return true;
		}

		const FString Peer = Request.PeerAddress->ToString(false);
		return Peer.StartsWith(TEXT("127.")) || Peer.Equals(TEXT("::1")) || Peer.Equals(TEXT("0:0:0:0:0:0:0:1"));
	}

	void MarkGraphChanged(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		if (Graph)
		{
			Graph->NotifyGraphChanged();
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	bool IsNodeInGraph(const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		return Graph && Node && Node->GetGraph() == Graph;
	}

	bool NodeHasExecPin(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return true;
			}
		}
		return false;
	}

	TSharedRef<FJsonObject> NodeMoveToJson(const UEdGraphNode* Node, const int32 OldX, const int32 OldY)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), GuidToString(Node->NodeGuid));
		Object->SetStringField(TEXT("object_name"), Node->GetName());
		Object->SetNumberField(TEXT("old_x"), OldX);
		Object->SetNumberField(TEXT("old_y"), OldY);
		Object->SetNumberField(TEXT("x"), Node->NodePosX);
		Object->SetNumberField(TEXT("y"), Node->NodePosY);
		return Object;
	}
}

void FLLMBlueprintBridgeServer::Start()
{
	const ULLMBlueprintBridgeSettings* Settings = GetDefault<ULLMBlueprintBridgeSettings>();
	Port = Settings ? static_cast<uint32>(Settings->Port) : DefaultPort;
	if (Port == 0)
	{
		Port = DefaultPort;
	}

	Router = FHttpServerModule::Get().GetHttpRouter(Port, true);
	if (!Router.IsValid())
	{
		UE_LOG(LogLLMBlueprintBridge, Error, TEXT("Unable to bind HTTP router on port %u."), Port);
		return;
	}

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/llm-blueprint/v1/status")), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateRaw(this, &FLLMBlueprintBridgeServer::HandleStatus)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/llm-blueprint/v1/tools")), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateRaw(this, &FLLMBlueprintBridgeServer::HandleTools)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/llm-blueprint/v1/invoke")), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateRaw(this, &FLLMBlueprintBridgeServer::HandleInvoke)));

	// MCP(JSON-RPC 2.0) 엔드포인트. 위의 REST 경로와 나란히 열린다.
	if (Settings == nullptr || Settings->bEnableMcpEndpoint)
	{
		FString McpPath = Settings ? Settings->McpUrlPath : FString(TEXT("/mcp"));
		McpPath.TrimStartAndEndInline();
		if (McpPath.IsEmpty())
		{
			McpPath = TEXT("/mcp");
		}
		if (!McpPath.StartsWith(TEXT("/")))
		{
			McpPath = TEXT("/") + McpPath;
		}

		McpEndpoint = MakeUnique<FLLMBlueprintBridgeMcpEndpoint>(
			*this,
			[this](const FHttpServerRequest& Request) { return IsAuthorized(Request); });

		const EHttpServerRequestVerbs McpVerbs =
			EHttpServerRequestVerbs::VERB_POST
			| EHttpServerRequestVerbs::VERB_GET
			| EHttpServerRequestVerbs::VERB_OPTIONS;

		const FHttpRouteHandle McpRoute = Router->BindRoute(
			FHttpPath(McpPath),
			McpVerbs,
			FHttpRequestHandler::CreateRaw(McpEndpoint.Get(), &FLLMBlueprintBridgeMcpEndpoint::HandleRequest));

		if (McpRoute.IsValid())
		{
			RouteHandles.Add(McpRoute);
			UE_LOG(LogLLMBlueprintBridge, Display, TEXT("MCP endpoint at http://127.0.0.1:%u%s"), Port, *McpPath);
		}
		else
		{
			McpEndpoint.Reset();
			UE_LOG(LogLLMBlueprintBridge, Error, TEXT("Unable to bind MCP route '%s'."), *McpPath);
		}
	}

	FHttpServerModule::Get().StartAllListeners();
	UE_LOG(LogLLMBlueprintBridge, Display, TEXT("LLM Blueprint Bridge listening at http://127.0.0.1:%u/llm-blueprint/v1"), Port);
}

void FLLMBlueprintBridgeServer::Stop()
{
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& RouteHandle : RouteHandles)
		{
			if (RouteHandle.IsValid())
			{
				Router->UnbindRoute(RouteHandle);
			}
		}
	}

	RouteHandles.Reset();
	Router.Reset();
	McpEndpoint.Reset();
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::InvokeToolByName(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
	check(IsInGameThread());
	return InvokeTool(ToolName, Arguments);
}

bool FLLMBlueprintBridgeServer::HandleStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeJsonResponse(MakeErrorObject(TEXT("unauthorized"), TEXT("Request is not authorized.")), EHttpServerResponseCodes::Denied));
		return true;
	}

	OnComplete(MakeJsonResponse(MakeOk(StatusData(Port))));
	return true;
}

bool FLLMBlueprintBridgeServer::HandleTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeJsonResponse(MakeErrorObject(TEXT("unauthorized"), TEXT("Request is not authorized.")), EHttpServerResponseCodes::Denied));
		return true;
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Tools;
	const TArray<FString> ToolNames = {
		TEXT("status"),
		TEXT("list_blueprints"),
		TEXT("find_blueprint_assets"),
		TEXT("inspect_blueprint"),
		TEXT("inspect_blueprint_git_revision"),
		TEXT("diff_blueprint_graph_against_git_revision"),
		TEXT("find_functions"),
		TEXT("create_blueprint"),
		TEXT("add_widget"),
		TEXT("remove_widget"),
		TEXT("move_widget"),
		TEXT("copy_nodes"),
		TEXT("add_blueprint_interface"),
		TEXT("add_variable"),
		TEXT("delete_variable"),
		TEXT("find_variable_references"),
		TEXT("add_function"),
		TEXT("rename_function"),
		TEXT("delete_function_graph"),
		TEXT("find_function_references"),
		TEXT("retarget_function_call"),
		TEXT("list_native_events"),
		TEXT("spawn_native_event"),
		TEXT("find_blueprint_actions"),
		TEXT("spawn_blueprint_action"),
		TEXT("list_components"),
		TEXT("add_component"),
		TEXT("remove_component"),
		TEXT("attach_component"),
		TEXT("set_component_transform"),
		TEXT("set_component_property"),
		TEXT("get_class_defaults"),
		TEXT("set_class_default"),
		TEXT("get_property"),
		TEXT("set_property"),
		TEXT("find_assets"),
		TEXT("create_folder"),
		TEXT("duplicate_asset"),
		TEXT("rename_asset"),
		TEXT("move_asset"),
		TEXT("delete_asset"),
		TEXT("save_asset"),
		TEXT("save_all_dirty"),
		TEXT("diff_asset_against_git_revision"),
		TEXT("create_datatable"),
		TEXT("inspect_datatable"),
		TEXT("get_datatable_rows"),
		TEXT("set_datatable_row"),
		TEXT("add_datatable_row"),
		TEXT("remove_datatable_row"),
		TEXT("rename_datatable_row"),
		TEXT("inspect_material"),
		TEXT("inspect_material_function"),
		TEXT("setup_occlusion_mask_function"),
		TEXT("setup_textured_occlusion_mask_function"),
		TEXT("set_material_property"),
		TEXT("create_material_expression"),
		TEXT("set_material_expression_property"),
		TEXT("connect_material_expressions"),
		TEXT("connect_material_property"),
		TEXT("clear_material_expressions"),
		TEXT("delete_material_expression"),
		TEXT("recompile_material"),
		TEXT("setup_camera_cutout_material"),
		TEXT("list_gas_assets"),
		TEXT("inspect_gameplay_ability"),
		TEXT("inspect_gameplay_effect"),
		TEXT("inspect_attribute_set"),
		TEXT("inspect_gameplay_cue"),
		TEXT("inspect_gameplay_tags"),
		TEXT("create_gameplay_ability"),
		TEXT("create_gameplay_effect"),
		TEXT("create_gameplay_cue"),
		TEXT("create_gameplay_mod_magnitude_calculation"),
		TEXT("add_gameplay_tag"),
		TEXT("remove_gameplay_tag"),
		TEXT("find_gameplay_tags"),
		TEXT("set_gameplay_tag_container"),
		TEXT("set_ability_policy"),
		TEXT("set_ability_tags"),
		TEXT("add_ability_task_node"),
		TEXT("configure_ability_commit_flow"),
		TEXT("configure_ability_cooldown"),
		TEXT("configure_ability_cost"),
		TEXT("set_effect_duration"),
		TEXT("add_effect_modifier"),
		TEXT("remove_effect_modifier"),
		TEXT("set_effect_modifier"),
		TEXT("add_granted_ability"),
		TEXT("set_effect_stacking"),
		TEXT("add_effect_execution"),
		TEXT("inspect_attribute_references"),
		TEXT("generate_attribute_set_cpp"),
		TEXT("validate_attribute_replication"),
		TEXT("validate_gas_asset"),
		TEXT("validate_gas_project"),
		TEXT("gas_dependency_report"),
		TEXT("create_basic_ability_recipe"),
		TEXT("create_cooldown_effect_recipe"),
		TEXT("create_cost_effect_recipe"),
		TEXT("create_damage_effect_recipe"),
		TEXT("create_buff_effect_recipe"),
		TEXT("list_statetree_assets"),
		TEXT("list_statetree_related_blueprints"),
		TEXT("find_statetree_assets"),
		TEXT("inspect_statetree"),
		TEXT("inspect_statetree_state"),
		TEXT("inspect_statetree_transitions"),
		TEXT("inspect_statetree_tasks"),
		TEXT("inspect_statetree_conditions"),
		TEXT("inspect_statetree_evaluators"),
		TEXT("inspect_statetree_parameters"),
		TEXT("inspect_statetree_blueprint_dependency"),
		TEXT("inspect_statetree_task_blueprint"),
		TEXT("inspect_statetree_condition_blueprint"),
		TEXT("inspect_statetree_evaluator_blueprint"),
		TEXT("validate_statetree"),
		TEXT("validate_statetree_project"),
		TEXT("validate_statetree_blueprint_dependency"),
		TEXT("statetree_debug_status"),
		TEXT("statetree_debug_snapshot"),
		TEXT("statetree_debug_actor"),
		TEXT("statetree_debug_trace_start"),
		TEXT("statetree_debug_trace_stop"),
		TEXT("statetree_debug_trace_read"),
		TEXT("statetree_explain_last_transition"),
		TEXT("statetree_explain_blocked_transitions"),
		TEXT("statetree_debug_report"),
		TEXT("statetree_failure_report"),
		TEXT("statetree_compare_runtime_to_asset"),
		TEXT("create_statetree"),
		TEXT("duplicate_statetree"),
		TEXT("add_statetree_state"),
		TEXT("rename_statetree_state"),
		TEXT("move_statetree_state"),
		TEXT("remove_statetree_state"),
		TEXT("add_statetree_task"),
		TEXT("remove_statetree_task"),
		TEXT("add_statetree_condition"),
		TEXT("remove_statetree_condition"),
		TEXT("add_statetree_transition"),
		TEXT("remove_statetree_transition"),
		TEXT("set_statetree_property"),
		TEXT("set_statetree_binding"),
		TEXT("compile_or_validate_statetree"),
		TEXT("save_statetree"),
		TEXT("create_basic_statetree_recipe"),
		TEXT("create_patrol_chase_attack_statetree_recipe"),
		TEXT("create_gas_skill_statetree_recipe"),
		TEXT("create_eqs_positioning_statetree_recipe"),
		TEXT("statetree_dependency_report"),
		TEXT("statetree_gas_report"),
		TEXT("statetree_eqs_report"),
		TEXT("statetree_blueprint_report"),
		TEXT("export_statetree_markdown"),
		TEXT("export_statetree_mermaid"),
		TEXT("export_statetree_debug_markdown"),
		TEXT("spawn_node"),
		TEXT("connect_pins"),
		TEXT("set_pin_default"),
		TEXT("format_blueprint_graph"),
		TEXT("delete_node"),
		TEXT("add_anim_state"),
		TEXT("add_anim_transition"),
		TEXT("compile_blueprint"),
		TEXT("save_blueprint"),
		TEXT("inspect_anim_sequence"),
		TEXT("get_root_motion_track"),
		TEXT("set_root_motion_keys"),
		TEXT("flatten_root_motion_axis"),
		TEXT("get_bone_component_transform"),
		TEXT("list_skeleton_bones"),
		TEXT("inspect_anim_notifies"),
		TEXT("add_anim_notify"),
		TEXT("remove_anim_notify"),
		TEXT("inspect_ik_retargeter"),
		TEXT("create_retarget_pose"),
		TEXT("set_retarget_pose_bone_rotation"),
		TEXT("auto_align_retarget_pose"),
		TEXT("reset_retarget_pose"),
		TEXT("retarget_animations"),
		TEXT("create_level_sequence"),
		TEXT("inspect_level_sequence"),
		TEXT("set_level_sequence_channel_keys"),
		TEXT("open_level_sequence"),
		TEXT("bake_level_sequence_animation"),
		TEXT("set_level_sequence_animation"),
		TEXT("remove_level_sequence_track"),
		TEXT("copy_level_sequence_bindings"),
		TEXT("spawn_level_actor"),
		TEXT("pie_control"),
		TEXT("pie_call_actor_function"),
		TEXT("pie_get_actor_state"),
		TEXT("exec_console_command"),
		TEXT("get_frame_stats"),
		TEXT("read_log"),
		TEXT("list_dirty_packages"),
		TEXT("create_asset"),
		TEXT("inspect_niagara_system"),
		TEXT("set_niagara_user_parameter"),
		TEXT("set_niagara_module_input"),
		TEXT("add_niagara_emitter"),
		TEXT("set_niagara_emitter_property"),
		TEXT("compile_niagara_system")
	};

	for (const FString& ToolName : ToolNames)
	{
		Tools.Add(MakeShared<FJsonValueString>(ToolName));
	}
	Data->SetArrayField(TEXT("tools"), Tools);
	TArray<TSharedPtr<FJsonValue>> DeprecatedTools;
	DeprecatedTools.Add(MakeShared<FJsonValueString>(TEXT("format_blueprint_graph")));
	Data->SetArrayField(TEXT("deprecated_tools"), DeprecatedTools);
	Data->SetStringField(TEXT("layout_guidance"), TEXT("Prefer manual NodePosX/NodePosY placement with set_property. Both automatic layout strategies are deprecated because their placements are unreadable for humans."));
	Data->SetStringField(TEXT("schema_doc"), TEXT("Plugins/LLMBlueprintBridge/Docs/API.md"));
	OnComplete(MakeJsonResponse(MakeOk(Data)));
	return true;
}

bool FLLMBlueprintBridgeServer::HandleInvoke(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!IsAuthorized(Request))
	{
		OnComplete(MakeJsonResponse(MakeErrorObject(TEXT("unauthorized"), TEXT("Request is not authorized.")), EHttpServerResponseCodes::Denied));
		return true;
	}

	const FString Body = ReadRequestBody(Request);
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		OnComplete(MakeJsonResponse(MakeErrorObject(TEXT("bad_json"), TEXT("Expected a JSON object body.")), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	FString ToolName;
	if (!RootObject->TryGetStringField(TEXT("tool"), ToolName) || ToolName.IsEmpty())
	{
		OnComplete(MakeJsonResponse(MakeErrorObject(TEXT("missing_tool"), TEXT("Expected a non-empty 'tool' string.")), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	TSharedPtr<FJsonObject> Arguments;
	if (!TryGetObject(RootObject, TEXT("arguments"), Arguments))
	{
		Arguments = MakeShared<FJsonObject>();
	}

	AsyncTask(ENamedThreads::GameThread, [this, ToolName, Arguments, OnComplete]()
	{
		const TSharedRef<FJsonObject> Result = InvokeTool(ToolName, Arguments);
		const bool bOk = Result->GetBoolField(TEXT("ok"));
		OnComplete(MakeJsonResponse(Result, bOk ? EHttpServerResponseCodes::Ok : EHttpServerResponseCodes::BadRequest));
	});

	return true;
}

bool FLLMBlueprintBridgeServer::IsAuthorized(const FHttpServerRequest& Request) const
{
	const ULLMBlueprintBridgeSettings* Settings = GetDefault<ULLMBlueprintBridgeSettings>();
	if (Settings && Settings->bLocalRequestsOnly && !IsLocalPeer(Request))
	{
		return false;
	}

	if (!Settings || Settings->AuthToken.IsEmpty())
	{
		return true;
	}

	for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
	{
		if (!Header.Key.Equals(HeaderToken, ESearchCase::IgnoreCase))
		{
			continue;
		}

		for (const FString& HeaderValue : Header.Value)
		{
			if (HeaderValue == Settings->AuthToken)
			{
				return true;
			}
		}
	}
	return false;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::InvokeTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
{
	if (ToolName == TEXT("status"))
	{
		return ToolStatus(Arguments);
	}
	if (ToolName == TEXT("inspect_anim_sequence"))
	{
		return ToolInspectAnimSequence(Arguments);
	}
	if (ToolName == TEXT("get_root_motion_track"))
	{
		return ToolGetRootMotionTrack(Arguments);
	}
	if (ToolName == TEXT("set_root_motion_keys"))
	{
		return ToolSetRootMotionKeys(Arguments);
	}
	if (ToolName == TEXT("flatten_root_motion_axis"))
	{
		return ToolFlattenRootMotionAxis(Arguments);
	}
	if (ToolName == TEXT("get_bone_component_transform"))
	{
		return ToolGetBoneComponentTransform(Arguments);
	}
	if (ToolName == TEXT("list_skeleton_bones"))
	{
		return ToolListSkeletonBones(Arguments);
	}
	if (ToolName == TEXT("inspect_anim_notifies"))
	{
		return ToolInspectAnimNotifies(Arguments);
	}
	if (ToolName == TEXT("add_anim_notify"))
	{
		return ToolAddAnimNotify(Arguments);
	}
	if (ToolName == TEXT("remove_anim_notify"))
	{
		return ToolRemoveAnimNotify(Arguments);
	}
	if (ToolName == TEXT("inspect_ik_retargeter"))
	{
		return ToolInspectIKRetargeter(Arguments);
	}
	if (ToolName == TEXT("create_retarget_pose"))
	{
		return ToolCreateRetargetPose(Arguments);
	}
	if (ToolName == TEXT("set_retarget_pose_bone_rotation"))
	{
		return ToolSetRetargetPoseBoneRotation(Arguments);
	}
	if (ToolName == TEXT("auto_align_retarget_pose"))
	{
		return ToolAutoAlignRetargetPose(Arguments);
	}
	if (ToolName == TEXT("reset_retarget_pose"))
	{
		return ToolResetRetargetPose(Arguments);
	}
	if (ToolName == TEXT("retarget_animations"))
	{
		return ToolRetargetAnimations(Arguments);
	}
	if (ToolName == TEXT("create_level_sequence"))
	{
		return ToolCreateLevelSequence(Arguments);
	}
	if (ToolName == TEXT("inspect_level_sequence"))
	{
		return ToolInspectLevelSequence(Arguments);
	}
	if (ToolName == TEXT("set_level_sequence_channel_keys"))
	{
		return ToolSetLevelSequenceChannelKeys(Arguments);
	}
	if (ToolName == TEXT("open_level_sequence"))
	{
		return ToolOpenLevelSequence(Arguments);
	}
	if (ToolName == TEXT("bake_level_sequence_animation"))
	{
		return ToolBakeLevelSequenceAnimation(Arguments);
	}
	if (ToolName == TEXT("set_level_sequence_animation"))
	{
		return ToolSetLevelSequenceAnimation(Arguments);
	}
	if (ToolName == TEXT("remove_level_sequence_track"))
	{
		return ToolRemoveLevelSequenceTrack(Arguments);
	}
	if (ToolName == TEXT("copy_level_sequence_bindings"))
	{
		return ToolCopyLevelSequenceBindings(Arguments);
	}
	if (ToolName == TEXT("spawn_level_actor"))
	{
		return ToolSpawnLevelActor(Arguments);
	}
	if (ToolName == TEXT("pie_control"))
	{
		return ToolPieControl(Arguments);
	}
	if (ToolName == TEXT("pie_call_actor_function"))
	{
		return ToolPieCallActorFunction(Arguments);
	}
	if (ToolName == TEXT("pie_get_actor_state"))
	{
		return ToolPieGetActorState(Arguments);
	}
	if (ToolName == TEXT("exec_console_command"))
	{
		return ToolExecConsoleCommand(Arguments);
	}
	if (ToolName == TEXT("get_frame_stats"))
	{
		return ToolGetFrameStats(Arguments);
	}
	if (ToolName == TEXT("read_log"))
	{
		return ToolReadLog(Arguments);
	}
	if (ToolName == TEXT("list_dirty_packages"))
	{
		return ToolListDirtyPackages(Arguments);
	}
	if (ToolName == TEXT("create_asset"))
	{
		return ToolCreateAsset(Arguments);
	}
	if (ToolName == TEXT("inspect_niagara_system"))
	{
		return ToolInspectNiagaraSystem(Arguments);
	}
	if (ToolName == TEXT("set_niagara_user_parameter"))
	{
		return ToolSetNiagaraUserParameter(Arguments);
	}
	if (ToolName == TEXT("set_niagara_module_input"))
	{
		return ToolSetNiagaraModuleInput(Arguments);
	}
	if (ToolName == TEXT("add_niagara_emitter"))
	{
		return ToolAddNiagaraEmitter(Arguments);
	}
	if (ToolName == TEXT("set_niagara_emitter_property"))
	{
		return ToolSetNiagaraEmitterProperty(Arguments);
	}
	if (ToolName == TEXT("compile_niagara_system"))
	{
		return ToolCompileNiagaraSystem(Arguments);
	}
	if (ToolName == TEXT("list_blueprints"))
	{
		return ToolListBlueprints(Arguments);
	}
	if (ToolName == TEXT("find_blueprint_assets"))
	{
		return ToolFindBlueprintAssets(Arguments);
	}
	if (ToolName == TEXT("inspect_blueprint"))
	{
		return ToolInspectBlueprint(Arguments);
	}
	if (ToolName == TEXT("inspect_blueprint_git_revision"))
	{
		return ToolInspectBlueprintGitRevision(Arguments);
	}
	if (ToolName == TEXT("diff_blueprint_graph_against_git_revision"))
	{
		return ToolDiffBlueprintGraphAgainstGitRevision(Arguments);
	}
	if (ToolName == TEXT("find_functions"))
	{
		return ToolFindFunctions(Arguments);
	}
	if (ToolName == TEXT("create_blueprint"))
	{
		return ToolCreateBlueprint(Arguments);
	}
	if (ToolName == TEXT("add_widget"))
	{
		return ToolAddWidget(Arguments);
	}
	if (ToolName == TEXT("remove_widget"))
	{
		return ToolRemoveWidget(Arguments);
	}
	if (ToolName == TEXT("move_widget"))
	{
		return ToolMoveWidget(Arguments);
	}
	if (ToolName == TEXT("copy_nodes"))
	{
		return ToolCopyNodes(Arguments);
	}
	if (ToolName == TEXT("add_blueprint_interface"))
	{
		return ToolAddBlueprintInterface(Arguments);
	}
	if (ToolName == TEXT("add_variable"))
	{
		return ToolAddVariable(Arguments);
	}
	if (ToolName == TEXT("delete_variable"))
	{
		return ToolDeleteVariable(Arguments);
	}
	if (ToolName == TEXT("find_variable_references"))
	{
		return ToolFindVariableReferences(Arguments);
	}
	if (ToolName == TEXT("add_function"))
	{
		return ToolAddFunction(Arguments);
	}
	if (ToolName == TEXT("rename_function"))
	{
		return ToolRenameFunction(Arguments);
	}
	if (ToolName == TEXT("delete_function_graph"))
	{
		return ToolDeleteFunctionGraph(Arguments);
	}
	if (ToolName == TEXT("find_function_references"))
	{
		return ToolFindFunctionReferences(Arguments);
	}
	if (ToolName == TEXT("retarget_function_call"))
	{
		return ToolRetargetFunctionCall(Arguments);
	}
	if (ToolName == TEXT("list_native_events"))
	{
		return ToolListNativeEvents(Arguments);
	}
	if (ToolName == TEXT("spawn_native_event"))
	{
		return ToolSpawnNativeEvent(Arguments);
	}
	if (ToolName == TEXT("find_blueprint_actions"))
	{
		return ToolFindBlueprintActions(Arguments);
	}
	if (ToolName == TEXT("spawn_blueprint_action"))
	{
		return ToolSpawnBlueprintAction(Arguments);
	}
	if (ToolName == TEXT("list_components"))
	{
		return ToolListComponents(Arguments);
	}
	if (ToolName == TEXT("add_component"))
	{
		return ToolAddComponent(Arguments);
	}
	if (ToolName == TEXT("remove_component"))
	{
		return ToolRemoveComponent(Arguments);
	}
	if (ToolName == TEXT("attach_component"))
	{
		return ToolAttachComponent(Arguments);
	}
	if (ToolName == TEXT("set_component_transform"))
	{
		return ToolSetComponentTransform(Arguments);
	}
	if (ToolName == TEXT("set_component_property"))
	{
		return ToolSetComponentProperty(Arguments);
	}
	if (ToolName == TEXT("get_class_defaults"))
	{
		return ToolGetClassDefaults(Arguments);
	}
	if (ToolName == TEXT("set_class_default"))
	{
		return ToolSetClassDefault(Arguments);
	}
	if (ToolName == TEXT("get_property"))
	{
		return ToolGetProperty(Arguments);
	}
	if (ToolName == TEXT("set_property"))
	{
		return ToolSetProperty(Arguments);
	}
	if (ToolName == TEXT("find_assets"))
	{
		return ToolFindAssets(Arguments);
	}
	if (ToolName == TEXT("create_folder"))
	{
		return ToolCreateFolder(Arguments);
	}
	if (ToolName == TEXT("duplicate_asset"))
	{
		return ToolDuplicateAsset(Arguments);
	}
	if (ToolName == TEXT("rename_asset"))
	{
		return ToolRenameAsset(Arguments);
	}
	if (ToolName == TEXT("move_asset"))
	{
		return ToolMoveAsset(Arguments);
	}
	if (ToolName == TEXT("delete_asset"))
	{
		return ToolDeleteAsset(Arguments);
	}
	if (ToolName == TEXT("save_asset"))
	{
		return ToolSaveAsset(Arguments);
	}
	if (ToolName == TEXT("save_all_dirty"))
	{
		return ToolSaveAllDirty(Arguments);
	}
	if (ToolName == TEXT("diff_asset_against_git_revision"))
	{
		return ToolDiffAssetAgainstGitRevision(Arguments);
	}
	if (ToolName == TEXT("create_datatable"))
	{
		return ToolCreateDataTable(Arguments);
	}
	if (ToolName == TEXT("inspect_datatable"))
	{
		return ToolInspectDataTable(Arguments);
	}
	if (ToolName == TEXT("get_datatable_rows"))
	{
		return ToolGetDataTableRows(Arguments);
	}
	if (ToolName == TEXT("set_datatable_row"))
	{
		return ToolSetDataTableRow(Arguments);
	}
	if (ToolName == TEXT("add_datatable_row"))
	{
		return ToolAddDataTableRow(Arguments);
	}
	if (ToolName == TEXT("remove_datatable_row"))
	{
		return ToolRemoveDataTableRow(Arguments);
	}
	if (ToolName == TEXT("rename_datatable_row"))
	{
		return ToolRenameDataTableRow(Arguments);
	}
	if (ToolName == TEXT("inspect_material"))
	{
		return ToolInspectMaterial(Arguments);
	}
	if (ToolName == TEXT("inspect_material_function"))
	{
		return ToolInspectMaterialFunction(Arguments);
	}
	if (ToolName == TEXT("setup_occlusion_mask_function"))
	{
		return ToolSetupOcclusionMaskFunction(Arguments);
	}
	if (ToolName == TEXT("setup_textured_occlusion_mask_function"))
	{
		return ToolSetupTexturedOcclusionMaskFunction(Arguments);
	}
	if (ToolName == TEXT("set_material_property"))
	{
		return ToolSetMaterialProperty(Arguments);
	}
	if (ToolName == TEXT("create_material_expression"))
	{
		return ToolCreateMaterialExpression(Arguments);
	}
	if (ToolName == TEXT("set_material_expression_property"))
	{
		return ToolSetMaterialExpressionProperty(Arguments);
	}
	if (ToolName == TEXT("connect_material_expressions"))
	{
		return ToolConnectMaterialExpressions(Arguments);
	}
	if (ToolName == TEXT("connect_material_property"))
	{
		return ToolConnectMaterialProperty(Arguments);
	}
	if (ToolName == TEXT("clear_material_expressions"))
	{
		return ToolClearMaterialExpressions(Arguments);
	}
	if (ToolName == TEXT("delete_material_expression"))
	{
		return ToolDeleteMaterialExpression(Arguments);
	}
	if (ToolName == TEXT("recompile_material"))
	{
		return ToolRecompileMaterial(Arguments);
	}
	if (ToolName == TEXT("setup_camera_cutout_material"))
	{
		return ToolSetupCameraCutoutMaterial(Arguments);
	}
	if (ToolName == TEXT("list_gas_assets"))
	{
		return ToolListGasAssets(Arguments);
	}
	if (ToolName == TEXT("inspect_gameplay_ability"))
	{
		return ToolInspectGameplayAbility(Arguments);
	}
	if (ToolName == TEXT("inspect_gameplay_effect"))
	{
		return ToolInspectGameplayEffect(Arguments);
	}
	if (ToolName == TEXT("inspect_attribute_set"))
	{
		return ToolInspectAttributeSet(Arguments);
	}
	if (ToolName == TEXT("inspect_gameplay_cue"))
	{
		return ToolInspectGameplayCue(Arguments);
	}
	if (ToolName == TEXT("inspect_gameplay_tags"))
	{
		return ToolInspectGameplayTags(Arguments);
	}
	if (ToolName == TEXT("create_gameplay_ability"))
	{
		return ToolCreateGameplayAbility(Arguments);
	}
	if (ToolName == TEXT("create_gameplay_effect"))
	{
		return ToolCreateGameplayEffect(Arguments);
	}
	if (ToolName == TEXT("create_gameplay_cue"))
	{
		return ToolCreateGameplayCue(Arguments);
	}
	if (ToolName == TEXT("create_gameplay_mod_magnitude_calculation"))
	{
		return ToolCreateGameplayModMagnitudeCalculation(Arguments);
	}
	if (ToolName == TEXT("add_gameplay_tag"))
	{
		return ToolAddGameplayTag(Arguments);
	}
	if (ToolName == TEXT("remove_gameplay_tag"))
	{
		return ToolRemoveGameplayTag(Arguments);
	}
	if (ToolName == TEXT("find_gameplay_tags"))
	{
		return ToolFindGameplayTags(Arguments);
	}
	if (ToolName == TEXT("set_gameplay_tag_container"))
	{
		return ToolSetGameplayTagContainer(Arguments);
	}
	if (ToolName == TEXT("set_ability_policy"))
	{
		return ToolSetAbilityPolicy(Arguments);
	}
	if (ToolName == TEXT("set_ability_tags"))
	{
		return ToolSetAbilityTags(Arguments);
	}
	if (ToolName == TEXT("add_ability_task_node"))
	{
		return ToolAddAbilityTaskNode(Arguments);
	}
	if (ToolName == TEXT("configure_ability_commit_flow"))
	{
		return ToolConfigureAbilityCommitFlow(Arguments);
	}
	if (ToolName == TEXT("configure_ability_cooldown"))
	{
		return ToolConfigureAbilityCooldown(Arguments);
	}
	if (ToolName == TEXT("configure_ability_cost"))
	{
		return ToolConfigureAbilityCost(Arguments);
	}
	if (ToolName == TEXT("set_effect_duration"))
	{
		return ToolSetEffectDuration(Arguments);
	}
	if (ToolName == TEXT("add_effect_modifier"))
	{
		return ToolAddEffectModifier(Arguments);
	}
	if (ToolName == TEXT("remove_effect_modifier"))
	{
		return ToolRemoveEffectModifier(Arguments);
	}
	if (ToolName == TEXT("set_effect_modifier"))
	{
		return ToolSetEffectModifier(Arguments);
	}
	if (ToolName == TEXT("add_granted_ability"))
	{
		return ToolAddGrantedAbility(Arguments);
	}
	if (ToolName == TEXT("set_effect_stacking"))
	{
		return ToolSetEffectStacking(Arguments);
	}
	if (ToolName == TEXT("add_effect_execution"))
	{
		return ToolAddEffectExecution(Arguments);
	}
	if (ToolName == TEXT("inspect_attribute_references"))
	{
		return ToolInspectAttributeReferences(Arguments);
	}
	if (ToolName == TEXT("generate_attribute_set_cpp"))
	{
		return ToolGenerateAttributeSetCpp(Arguments);
	}
	if (ToolName == TEXT("validate_attribute_replication"))
	{
		return ToolValidateAttributeReplication(Arguments);
	}
	if (ToolName == TEXT("validate_gas_asset"))
	{
		return ToolValidateGasAsset(Arguments);
	}
	if (ToolName == TEXT("validate_gas_project"))
	{
		return ToolValidateGasProject(Arguments);
	}
	if (ToolName == TEXT("gas_dependency_report"))
	{
		return ToolGasDependencyReport(Arguments);
	}
	if (ToolName == TEXT("create_basic_ability_recipe"))
	{
		return ToolCreateBasicAbilityRecipe(Arguments);
	}
	if (ToolName == TEXT("create_cooldown_effect_recipe"))
	{
		return ToolCreateCooldownEffectRecipe(Arguments);
	}
	if (ToolName == TEXT("create_cost_effect_recipe"))
	{
		return ToolCreateCostEffectRecipe(Arguments);
	}
	if (ToolName == TEXT("create_damage_effect_recipe"))
	{
		return ToolCreateDamageEffectRecipe(Arguments);
	}
	if (ToolName == TEXT("create_buff_effect_recipe"))
	{
		return ToolCreateBuffEffectRecipe(Arguments);
	}
	if (ToolName == TEXT("list_statetree_assets"))
	{
		return ToolListStateTreeAssets(Arguments);
	}
	if (ToolName == TEXT("list_statetree_related_blueprints"))
	{
		return ToolListStateTreeRelatedBlueprints(Arguments);
	}
	if (ToolName == TEXT("find_statetree_assets"))
	{
		return ToolFindStateTreeAssets(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree"))
	{
		return ToolInspectStateTree(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_state"))
	{
		return ToolInspectStateTreeState(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_transitions"))
	{
		return ToolInspectStateTreeTransitions(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_tasks"))
	{
		return ToolInspectStateTreeTasks(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_conditions"))
	{
		return ToolInspectStateTreeConditions(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_evaluators"))
	{
		return ToolInspectStateTreeEvaluators(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_parameters"))
	{
		return ToolInspectStateTreeParameters(Arguments);
	}
	if (ToolName == TEXT("inspect_statetree_blueprint_dependency") ||
		ToolName == TEXT("inspect_statetree_task_blueprint") ||
		ToolName == TEXT("inspect_statetree_condition_blueprint") ||
		ToolName == TEXT("inspect_statetree_evaluator_blueprint"))
	{
		return ToolInspectStateTreeBlueprintDependency(Arguments);
	}
	if (ToolName == TEXT("validate_statetree"))
	{
		return ToolValidateStateTree(Arguments);
	}
	if (ToolName == TEXT("validate_statetree_project"))
	{
		return ToolValidateStateTreeProject(Arguments);
	}
	if (ToolName == TEXT("validate_statetree_blueprint_dependency"))
	{
		return ToolValidateStateTreeBlueprintDependency(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_status"))
	{
		return ToolStateTreeDebugStatus(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_snapshot"))
	{
		return ToolStateTreeDebugSnapshot(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_actor"))
	{
		return ToolStateTreeDebugActor(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_trace_start"))
	{
		return ToolStateTreeDebugTraceStart(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_trace_stop"))
	{
		return ToolStateTreeDebugTraceStop(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_trace_read"))
	{
		return ToolStateTreeDebugTraceRead(Arguments);
	}
	if (ToolName == TEXT("statetree_explain_last_transition"))
	{
		return ToolStateTreeExplainLastTransition(Arguments);
	}
	if (ToolName == TEXT("statetree_explain_blocked_transitions"))
	{
		return ToolStateTreeExplainBlockedTransitions(Arguments);
	}
	if (ToolName == TEXT("statetree_debug_report"))
	{
		return ToolStateTreeDebugReport(Arguments);
	}
	if (ToolName == TEXT("statetree_failure_report"))
	{
		return ToolStateTreeFailureReport(Arguments);
	}
	if (ToolName == TEXT("statetree_compare_runtime_to_asset"))
	{
		return ToolStateTreeCompareRuntimeToAsset(Arguments);
	}
	if (ToolName == TEXT("create_statetree"))
	{
		return ToolCreateStateTree(Arguments);
	}
	if (ToolName == TEXT("duplicate_statetree"))
	{
		return ToolDuplicateStateTree(Arguments);
	}
	if (ToolName == TEXT("add_statetree_state"))
	{
		return ToolAddStateTreeState(Arguments);
	}
	if (ToolName == TEXT("rename_statetree_state"))
	{
		return ToolRenameStateTreeState(Arguments);
	}
	if (ToolName == TEXT("move_statetree_state"))
	{
		return ToolMoveStateTreeState(Arguments);
	}
	if (ToolName == TEXT("remove_statetree_state"))
	{
		return ToolRemoveStateTreeState(Arguments);
	}
	if (ToolName == TEXT("add_statetree_task"))
	{
		return ToolAddStateTreeTask(Arguments);
	}
	if (ToolName == TEXT("remove_statetree_task"))
	{
		return ToolRemoveStateTreeTask(Arguments);
	}
	if (ToolName == TEXT("add_statetree_condition"))
	{
		return ToolAddStateTreeCondition(Arguments);
	}
	if (ToolName == TEXT("remove_statetree_condition"))
	{
		return ToolRemoveStateTreeCondition(Arguments);
	}
	if (ToolName == TEXT("add_statetree_transition"))
	{
		return ToolAddStateTreeTransition(Arguments);
	}
	if (ToolName == TEXT("remove_statetree_transition"))
	{
		return ToolRemoveStateTreeTransition(Arguments);
	}
	if (ToolName == TEXT("set_statetree_property"))
	{
		return ToolSetStateTreeProperty(Arguments);
	}
	if (ToolName == TEXT("set_statetree_binding"))
	{
		return ToolSetStateTreeBinding(Arguments);
	}
	if (ToolName == TEXT("compile_or_validate_statetree"))
	{
		return ToolCompileOrValidateStateTree(Arguments);
	}
	if (ToolName == TEXT("save_statetree"))
	{
		return ToolSaveStateTree(Arguments);
	}
	if (ToolName == TEXT("create_basic_statetree_recipe"))
	{
		return ToolCreateBasicStateTreeRecipe(Arguments);
	}
	if (ToolName == TEXT("create_patrol_chase_attack_statetree_recipe"))
	{
		return ToolCreatePatrolChaseAttackStateTreeRecipe(Arguments);
	}
	if (ToolName == TEXT("create_gas_skill_statetree_recipe"))
	{
		return ToolCreateGasSkillStateTreeRecipe(Arguments);
	}
	if (ToolName == TEXT("create_eqs_positioning_statetree_recipe"))
	{
		return ToolCreateEqsPositioningStateTreeRecipe(Arguments);
	}
	if (ToolName == TEXT("statetree_dependency_report"))
	{
		return ToolStateTreeDependencyReport(Arguments);
	}
	if (ToolName == TEXT("statetree_gas_report"))
	{
		return ToolStateTreeGasReport(Arguments);
	}
	if (ToolName == TEXT("statetree_eqs_report"))
	{
		return ToolStateTreeEqsReport(Arguments);
	}
	if (ToolName == TEXT("statetree_blueprint_report"))
	{
		return ToolStateTreeBlueprintReport(Arguments);
	}
	if (ToolName == TEXT("export_statetree_markdown"))
	{
		return ToolExportStateTreeMarkdown(Arguments);
	}
	if (ToolName == TEXT("export_statetree_mermaid"))
	{
		return ToolExportStateTreeMermaid(Arguments);
	}
	if (ToolName == TEXT("export_statetree_debug_markdown"))
	{
		return ToolExportStateTreeDebugMarkdown(Arguments);
	}
	if (ToolName == TEXT("spawn_node"))
	{
		return ToolSpawnNode(Arguments);
	}
	if (ToolName == TEXT("connect_pins"))
	{
		return ToolConnectPins(Arguments);
	}
	if (ToolName == TEXT("set_pin_default"))
	{
		return ToolSetPinDefault(Arguments);
	}
	if (ToolName == TEXT("format_blueprint_graph"))
	{
		return ToolFormatBlueprintGraph(Arguments);
	}
	if (ToolName == TEXT("delete_node"))
	{
		return ToolDeleteNode(Arguments);
	}
	if (ToolName == TEXT("add_anim_state"))
	{
		return ToolAddAnimState(Arguments);
	}
	if (ToolName == TEXT("add_anim_transition"))
	{
		return ToolAddAnimTransition(Arguments);
	}
	if (ToolName == TEXT("compile_blueprint"))
	{
		return ToolCompileBlueprint(Arguments);
	}
	if (ToolName == TEXT("save_blueprint"))
	{
		return ToolSaveBlueprint(Arguments);
	}

	return MakeErrorObject(TEXT("unknown_tool"), FString::Printf(TEXT("Unknown tool '%s'."), *ToolName));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStatus(const TSharedPtr<FJsonObject>& Arguments)
{
	return MakeOk(StatusData(Port));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListBlueprints(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("path"), TEXT("/Game"));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 200), 1, 5000);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), Assets, true);

	TArray<TSharedPtr<FJsonValue>> Blueprints;
	int32 SkippedExcludedCount = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (Blueprints.Num() >= Limit)
		{
			break;
		}
		if (IsAssetDataExcluded(Asset, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}

		const FString ClassPath = Asset.AssetClassPath.ToString();
		if (!ClassPath.Contains(TEXT("Blueprint")))
		{
			continue;
		}

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
		Item->SetStringField(TEXT("package"), Asset.PackageName.ToString());
		Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Item->SetStringField(TEXT("class"), ClassPath);
		Blueprints.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("content_path"), Path);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("blueprints"), Blueprints);
	Data->SetNumberField(TEXT("count"), Blueprints.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindBlueprintAssets(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("content_path"), GetOptionalString(Arguments, TEXT("path"), TEXT("/Game")));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const FString Query = GetOptionalString(Arguments, TEXT("query")).ToLower();
	const FString ParentClassFilter = GetOptionalString(Arguments, TEXT("parent_class"), GetOptionalString(Arguments, TEXT("exact_parent_class")));
	const FString DerivedFromFilter = GetOptionalString(Arguments, TEXT("derived_from"), GetOptionalString(Arguments, TEXT("derives_from"), GetOptionalString(Arguments, TEXT("base_class"))));
	const FString BlueprintAssetClassFilter = GetOptionalString(Arguments, TEXT("blueprint_asset_class"), GetOptionalString(Arguments, TEXT("asset_class")));
	const bool bRecursive = GetOptionalBool(Arguments, TEXT("recursive"), true);
	const bool bLoadAssets = GetOptionalBool(Arguments, TEXT("load_assets"), true);
	const bool bIncludeOnlyOnDiskAssets = GetOptionalBool(Arguments, TEXT("include_only_on_disk_assets"), false);
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 200), 1, 5000);
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UClass* ParentClass = nullptr;
	if (!ParentClassFilter.IsEmpty())
	{
		ParentClass = ResolveClass(ParentClassFilter);
		if (!ParentClass)
		{
			return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve parent_class '%s'."), *ParentClassFilter));
		}
	}

	UClass* DerivedFromClass = nullptr;
	if (!DerivedFromFilter.IsEmpty())
	{
		DerivedFromClass = ResolveClass(DerivedFromFilter);
		if (!DerivedFromClass)
		{
			return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve derived_from/base_class '%s'."), *DerivedFromFilter));
		}
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = bRecursive;
	Filter.bIncludeOnlyOnDiskAssets = bIncludeOnlyOnDiskAssets;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 SkippedExcludedCount = 0;
	int32 ScannedBlueprintCount = 0;
	int32 SkippedLoadFailedCount = 0;
	for (const FAssetData& AssetData : Assets)
	{
		if (Results.Num() >= Limit)
		{
			break;
		}
		if (IsAssetDataExcluded(AssetData, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}
		if (!IsBlueprintAssetData(AssetData) || !BlueprintAssetClassMatches(AssetData, BlueprintAssetClassFilter))
		{
			continue;
		}
		++ScannedBlueprintCount;

		const FString GeneratedClassTag = CleanAssetClassTagPath(GetAssetTagString(AssetData, FBlueprintTags::GeneratedClassPath));
		const FString ParentClassTag = CleanAssetClassTagPath(GetAssetTagString(AssetData, FBlueprintTags::ParentClassPath));
		const FString NativeParentClassTag = CleanAssetClassTagPath(GetAssetTagString(AssetData, FBlueprintTags::NativeParentClassPath));
		const FString AssetSearchText = (AssetData.AssetName.ToString() + TEXT(" ") +
			AssetData.PackageName.ToString() + TEXT(" ") +
			AssetData.GetObjectPathString() + TEXT(" ") +
			AssetData.AssetClassPath.ToString() + TEXT(" ") +
			GeneratedClassTag + TEXT(" ") +
			ParentClassTag + TEXT(" ") +
			NativeParentClassTag).ToLower();
		if (!Query.IsEmpty() && !AssetSearchText.Contains(Query))
		{
			continue;
		}

		UBlueprint* Blueprint = nullptr;
		if (bLoadAssets)
		{
			Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
			if (!Blueprint)
			{
				++SkippedLoadFailedCount;
				if (ParentClass || DerivedFromClass)
				{
					continue;
				}
			}
		}

		bool bParentMatches = ParentClass == nullptr;
		if (ParentClass)
		{
			if (Blueprint && Blueprint->ParentClass)
			{
				bParentMatches = Blueprint->ParentClass == ParentClass;
			}
			else
			{
				bParentMatches = ClassPathTextMatches(ParentClassTag, ParentClassFilter) || ClassPathTextMatches(NativeParentClassTag, ParentClassFilter);
			}
		}
		if (!bParentMatches)
		{
			continue;
		}

		bool bDerivedMatches = DerivedFromClass == nullptr;
		if (DerivedFromClass)
		{
			if (Blueprint && Blueprint->GeneratedClass)
			{
				bDerivedMatches = Blueprint->GeneratedClass->IsChildOf(DerivedFromClass);
			}
			else
			{
				bDerivedMatches = ClassPathTextMatches(GeneratedClassTag, DerivedFromFilter) ||
					ClassPathTextMatches(ParentClassTag, DerivedFromFilter) ||
					ClassPathTextMatches(NativeParentClassTag, DerivedFromFilter);
			}
		}
		if (!bDerivedMatches)
		{
			continue;
		}

		TSharedRef<FJsonObject> Item = BlueprintAssetDataToJson(AssetData, Blueprint);
		Item->SetBoolField(TEXT("parent_class_match"), bParentMatches);
		Item->SetBoolField(TEXT("derived_from_match"), bDerivedMatches);
		Item->SetStringField(TEXT("type_filter_mode"), bLoadAssets ? TEXT("loaded_class") : TEXT("asset_registry_tags"));
		Results.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("content_path"), Path);
	Data->SetStringField(TEXT("query"), Query);
	Data->SetStringField(TEXT("parent_class_filter"), ParentClassFilter);
	Data->SetStringField(TEXT("derived_from_filter"), DerivedFromFilter);
	Data->SetStringField(TEXT("blueprint_asset_class_filter"), BlueprintAssetClassFilter);
	Data->SetBoolField(TEXT("recursive"), bRecursive);
	Data->SetBoolField(TEXT("load_assets"), bLoadAssets);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("blueprints"), Results);
	Data->SetNumberField(TEXT("count"), Results.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	Data->SetNumberField(TEXT("scanned_blueprint_count"), ScannedBlueprintCount);
	Data->SetNumberField(TEXT("skipped_load_failed_count"), SkippedLoadFailedCount);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectBlueprint(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	return MakeOk(BlueprintToJson(Blueprint, GetBlueprintJsonOptions(Arguments, false)));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectBlueprintGitRevision(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Revision;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("revision"), Revision, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* CurrentAsset = LoadAssetObject(AssetPath, Error);
	if (!CurrentAsset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FText DiffError;
	FString RepoRoot;
	FString RelativeFilename;
	FString TempAssetFilename;
	FGitAssetDiffModule& GitDiffModule = FGitAssetDiffModule::Get();
	UObject* RevisionAsset = GitDiffModule.LoadAssetFromGitRevision(
		FAssetData(CurrentAsset),
		Revision,
		DiffError,
		&RepoRoot,
		&RelativeFilename,
		&TempAssetFilename);
	if (!RevisionAsset)
	{
		return MakeErrorObject(TEXT("git_revision_load_failed"), DiffError.ToString());
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(RevisionAsset);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("not_blueprint"), FString::Printf(TEXT("Revision asset '%s' is not a Blueprint."), *RevisionAsset->GetPathName()));
	}

	TSharedRef<FJsonObject> Data = BlueprintToJson(Blueprint, GetBlueprintJsonOptions(Arguments, false));
	Data->SetStringField(TEXT("source_asset_path"), CurrentAsset->GetPathName());
	Data->SetStringField(TEXT("revision"), Revision);
	Data->SetStringField(TEXT("repo_root"), RepoRoot);
	Data->SetStringField(TEXT("relative_filename"), RelativeFilename);
	Data->SetStringField(TEXT("temp_asset_filename"), TempAssetFilename);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDiffBlueprintGraphAgainstGitRevision(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Revision;
	FString GraphName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("revision"), Revision, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* CurrentBlueprint = LoadBlueprint(AssetPath, Error);
	if (!CurrentBlueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FText DiffError;
	FString RepoRoot;
	FString RelativeFilename;
	FString TempAssetFilename;
	FGitAssetDiffModule& GitDiffModule = FGitAssetDiffModule::Get();
	UObject* RevisionAsset = GitDiffModule.LoadAssetFromGitRevision(
		FAssetData(CurrentBlueprint),
		Revision,
		DiffError,
		&RepoRoot,
		&RelativeFilename,
		&TempAssetFilename);
	if (!RevisionAsset)
	{
		return MakeErrorObject(TEXT("git_revision_load_failed"), DiffError.ToString());
	}

	UBlueprint* RevisionBlueprint = Cast<UBlueprint>(RevisionAsset);
	if (!RevisionBlueprint)
	{
		return MakeErrorObject(TEXT("not_blueprint"), FString::Printf(TEXT("Revision asset '%s' is not a Blueprint."), *RevisionAsset->GetPathName()));
	}

	UEdGraph* CurrentGraph = FindBlueprintGraphByName(CurrentBlueprint, GraphName);
	UEdGraph* RevisionGraph = FindBlueprintGraphByName(RevisionBlueprint, GraphName);
	if (!CurrentGraph || !RevisionGraph)
	{
		return MakeErrorObject(
			TEXT("graph_not_found"),
			FString::Printf(
				TEXT("Graph '%s' was not found. current=%s revision=%s"),
				*GraphName,
				CurrentGraph ? TEXT("yes") : TEXT("no"),
				RevisionGraph ? TEXT("yes") : TEXT("no")));
	}

	const FBlueprintJsonOptions BlueprintOptions = GetBlueprintJsonOptions(Arguments, false);
	const bool bIncludeUnchanged = GetOptionalBool(Arguments, TEXT("include_unchanged"), false);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), CurrentBlueprint->GetPathName());
	Data->SetStringField(TEXT("revision_asset_path"), RevisionBlueprint->GetPathName());
	Data->SetStringField(TEXT("revision"), Revision);
	Data->SetStringField(TEXT("repo_root"), RepoRoot);
	Data->SetStringField(TEXT("relative_filename"), RelativeFilename);
	Data->SetStringField(TEXT("temp_asset_filename"), TempAssetFilename);
	Data->SetStringField(TEXT("graph"), CurrentGraph->GetName());
	Data->SetObjectField(TEXT("diff"), BlueprintGraphDiffToJson(RevisionGraph, CurrentGraph, BlueprintOptions.bIncludeNodes, bIncludeUnchanged));
	Data->SetObjectField(TEXT("revision_graph"), GraphToJson(RevisionGraph, BlueprintOptions));
	Data->SetObjectField(TEXT("current_graph"), GraphToJson(CurrentGraph, BlueprintOptions));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindFunctions(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString Query = GetOptionalString(Arguments, TEXT("query"));
	const FString ClassPath = GetOptionalString(Arguments, TEXT("class_path"));
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 50), 1, 500);
	if (Query.IsEmpty() && ClassPath.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'query', 'class_path', or both."));
	}

	TArray<UClass*> Classes;
	if (!ClassPath.IsEmpty())
	{
		if (UClass* Class = ResolveClass(ClassPath))
		{
			Classes.Add(Class);
		}
		else
		{
			return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve class '%s'."), *ClassPath));
		}
	}
	else
	{
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			Classes.Add(*ClassIt);
		}
	}

	TArray<TSharedPtr<FJsonValue>> Functions;
	const FString QueryLower = Query.ToLower();
	TSet<FString> Seen;
	for (UClass* Class : Classes)
	{
		for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
		{
			UFunction* Function = *FunctionIt;
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent))
			{
				continue;
			}

			const FString SignatureKey = FString::Printf(TEXT("%s::%s"), Function->GetOwnerClass() ? *Function->GetOwnerClass()->GetPathName() : TEXT(""), *Function->GetName());
			if (Seen.Contains(SignatureKey))
			{
				continue;
			}

			const FString SearchText = (Function->GetName() + TEXT(" ") + SignatureKey + TEXT(" ") + Function->GetMetaData(TEXT("Keywords"))).ToLower();
			if (!QueryLower.IsEmpty() && !SearchText.Contains(QueryLower))
			{
				continue;
			}

			Functions.Add(MakeShared<FJsonValueObject>(FunctionToJson(Function)));
			Seen.Add(SignatureKey);
			if (Functions.Num() >= Limit)
			{
				break;
			}
		}

		if (Functions.Num() >= Limit)
		{
			break;
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("functions"), Functions);
	Data->SetNumberField(TEXT("count"), Functions.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateBlueprint(const TSharedPtr<FJsonObject>& Arguments)
{
	FString PackagePath;
	FString Error;
	if (!RequireString(Arguments, TEXT("package_path"), PackagePath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const FString ParentClassPath = GetOptionalString(Arguments, TEXT("parent_class"), TEXT("/Script/Engine.Actor"));
	UClass* ParentClass = ResolveClass(ParentClassPath);
	if (!ParentClass)
	{
		return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve parent class '%s'."), *ParentClassPath));
	}
	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		return MakeErrorObject(TEXT("invalid_parent_class"), FString::Printf(TEXT("Class '%s' cannot be used as a Blueprint parent."), *ParentClass->GetPathName()));
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
	if (AssetName.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_package_path"), TEXT("'package_path' must look like /Game/Folder/BP_Name."));
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return MakeErrorObject(TEXT("package_failed"), FString::Printf(TEXT("Could not create package '%s'."), *PackagePath));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "CreateBlueprint", "LLM Create Blueprint"));
	Package->Modify();
	UClass* blueprintAssetClass = UBlueprint::StaticClass();
	UClass* blueprintGeneratedClass = UBlueprintGeneratedClass::StaticClass();
	if (ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		blueprintAssetClass = UWidgetBlueprint::StaticClass();
		blueprintGeneratedClass = UWidgetBlueprintGeneratedClass::StaticClass();
	}
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		blueprintAssetClass,
		blueprintGeneratedClass,
		TEXT("LLMBlueprintBridge"));
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("create_failed"), TEXT("FKismetEditorUtilities::CreateBlueprint returned null."));
	}

	FAssetRegistryModule::AssetCreated(Blueprint);
	Blueprint->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("package"), Blueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddWidget(const TSharedPtr<FJsonObject>& Arguments)
{
	FString assetPath;
	FString widgetClassPath;
	FString widgetName;
	FString error;
	if (!RequireString(Arguments, TEXT("asset_path"), assetPath, error) ||
		!RequireString(Arguments, TEXT("widget_class"), widgetClassPath, error) ||
		!RequireString(Arguments, TEXT("name"), widgetName, error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), error);
	}

	UWidgetBlueprint* widgetBlueprint = Cast<UWidgetBlueprint>(LoadBlueprint(assetPath, error));
	if (!widgetBlueprint || !widgetBlueprint->WidgetTree)
	{
		return MakeErrorObject(TEXT("invalid_blueprint"),
			FString::Printf(TEXT("Asset '%s' is not a Widget Blueprint."), *assetPath));
	}

	UClass* widgetClass = ResolveClass(widgetClassPath);
	if (!widgetClass || !widgetClass->IsChildOf(UWidget::StaticClass()))
	{
		return MakeErrorObject(TEXT("class_not_found"),
			FString::Printf(TEXT("Could not resolve widget class '%s'."), *widgetClassPath));
	}
	if (widgetBlueprint->WidgetTree->FindWidget(FName(*widgetName)))
	{
		return MakeErrorObject(TEXT("widget_exists"),
			FString::Printf(TEXT("Widget '%s' already exists."), *widgetName));
	}

	UWidget* widget = widgetBlueprint->WidgetTree->ConstructWidget<UWidget>(widgetClass, FName(*widgetName));
	if (!widget)
		return MakeErrorObject(TEXT("create_failed"), TEXT("WidgetTree failed to construct the widget."));

	const FString parentName = GetOptionalString(Arguments, TEXT("parent"));
	if (parentName.IsEmpty())
	{
		if (widgetBlueprint->WidgetTree->RootWidget)
			return MakeErrorObject(TEXT("root_exists"), TEXT("The Widget Blueprint already has a root widget."));
		widgetBlueprint->WidgetTree->RootWidget = widget;
	}
	else
	{
		UPanelWidget* parent = Cast<UPanelWidget>(widgetBlueprint->WidgetTree->FindWidget(FName(*parentName)));
		if (!parent)
			return MakeErrorObject(TEXT("parent_not_found"), FString::Printf(TEXT("Panel widget '%s' was not found."), *parentName));
		const int32 index = GetOptionalInt(Arguments, TEXT("index"), -1);
		UPanelSlot* slot = index >= 0 && index <= parent->GetChildrenCount() ? parent->InsertChildAt(index, widget) : parent->AddChild(widget);
		if (!slot)
			return MakeErrorObject(TEXT("attach_failed"), FString::Printf(TEXT("Could not attach '%s' to '%s'."), *widgetName, *parentName));
	}
	// The designer registers every new widget variable; without it the compiler logs "did not get a GUID".
	widgetBlueprint->OnVariableAdded(widget->GetFName());

	double numberValue = 0.0;
	if (USizeBox* sizeBox = Cast<USizeBox>(widget))
	{
		if (Arguments->TryGetNumberField(TEXT("width"), numberValue))
			sizeBox->SetWidthOverride(static_cast<float>(numberValue));
		if (Arguments->TryGetNumberField(TEXT("height"), numberValue))
			sizeBox->SetHeightOverride(static_cast<float>(numberValue));
	}
	if (UImage* image = Cast<UImage>(widget))
	{
		const FString texturePath = GetOptionalString(Arguments, TEXT("texture"));
		if (!texturePath.IsEmpty())
		{
			if (UTexture2D* texture = LoadObject<UTexture2D>(nullptr, *texturePath))
				image->SetBrushFromTexture(texture, true);
			else
				return MakeErrorObject(TEXT("texture_not_found"), FString::Printf(TEXT("Texture '%s' was not found."), *texturePath));
		}
	}
	if (UTextBlock* textBlock = Cast<UTextBlock>(widget))
	{
		const FString textValue = GetOptionalString(Arguments, TEXT("text"));
		textBlock->SetText(FText::FromString(textValue));
		if (Arguments->TryGetNumberField(TEXT("font_size"), numberValue))
		{
			FSlateFontInfo font = textBlock->GetFont();
			font.Size = FMath::Max(1, FMath::RoundToInt(numberValue));
			textBlock->SetFont(font);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(widgetBlueprint);
	TSharedRef<FJsonObject> data = MakeShared<FJsonObject>();
	data->SetStringField(TEXT("asset_path"), assetPath);
	data->SetStringField(TEXT("name"), widgetName);
	data->SetStringField(TEXT("widget_class"), widgetClass->GetPathName());
	data->SetStringField(TEXT("object_path"), widget->GetPathName());
	data->SetStringField(TEXT("parent"), parentName);
	return MakeOk(data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddBlueprintInterface(const TSharedPtr<FJsonObject>& Arguments)
{
	FString assetPath;
	FString interfaceClassPath;
	FString error;
	if (!RequireString(Arguments, TEXT("asset_path"), assetPath, error)
		|| !RequireString(Arguments, TEXT("interface_class"), interfaceClassPath, error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), error);
	}

	UBlueprint* blueprint = LoadBlueprint(assetPath, error);
	if (!blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), error);
	}

	UClass* interfaceClass = ResolveClass(interfaceClassPath);
	if (!interfaceClass)
	{
		return MakeErrorObject(
			TEXT("class_not_found"),
			FString::Printf(TEXT("Could not resolve interface class '%s'."), *interfaceClassPath));
	}
	if (!interfaceClass->IsChildOf(UInterface::StaticClass()))
	{
		return MakeErrorObject(
			TEXT("invalid_interface_class"),
			FString::Printf(TEXT("Class '%s' is not a UInterface."), *interfaceClass->GetPathName()));
	}

	if (FBlueprintEditorUtils::ImplementsInterface(blueprint, true, interfaceClass))
	{
		TSharedRef<FJsonObject> data = MakeShared<FJsonObject>();
		data->SetStringField(TEXT("asset_path"), blueprint->GetPathName());
		data->SetStringField(TEXT("interface_class"), interfaceClass->GetPathName());
		data->SetBoolField(TEXT("added"), false);
		data->SetBoolField(TEXT("already_implemented"), true);
		return MakeOk(data);
	}

	const FScopedTransaction transaction(
		NSLOCTEXT("LLMBlueprintBridge", "AddBlueprintInterface", "LLM Add Blueprint Interface"));
	blueprint->Modify();
	if (!FBlueprintEditorUtils::ImplementNewInterface(blueprint, interfaceClass->GetClassPathName()))
	{
		return MakeErrorObject(
			TEXT("add_blueprint_interface_failed"),
			FString::Printf(
				TEXT("Could not add interface '%s' to Blueprint '%s'."),
				*interfaceClass->GetPathName(),
				*blueprint->GetPathName()));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(blueprint);

	TSharedRef<FJsonObject> data = MakeShared<FJsonObject>();
	data->SetStringField(TEXT("asset_path"), blueprint->GetPathName());
	data->SetStringField(TEXT("interface_class"), interfaceClass->GetPathName());
	data->SetBoolField(TEXT("added"), true);
	data->SetBoolField(TEXT("already_implemented"), false);
	return MakeOk(data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddVariable(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString VariableName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) || !RequireString(Arguments, TEXT("name"), VariableName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	TSharedPtr<FJsonObject> TypeObject;
	if (!TryGetObject(Arguments, TEXT("type"), TypeObject))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required object field 'type'."));
	}

	FEdGraphPinType PinType;
	if (!BuildPinType(TypeObject, PinType, Error))
	{
		return MakeErrorObject(TEXT("invalid_pin_type"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const FString DefaultValue = GetOptionalString(Arguments, TEXT("default"));
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddVariable", "LLM Add Blueprint Variable"));
	Blueprint->Modify();
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType, DefaultValue))
	{
		return MakeErrorObject(TEXT("add_variable_failed"), FString::Printf(TEXT("Could not add variable '%s'."), *VariableName));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("name"), VariableName);
	Data->SetObjectField(TEXT("type"), PinTypeToJson(PinType));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddFunction(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString FunctionName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) || !RequireString(Arguments, TEXT("name"), FunctionName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddFunction", "LLM Add Blueprint Function"));
	Blueprint->Modify();

	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!Graph)
	{
		return MakeErrorObject(TEXT("create_graph_failed"), FString::Printf(TEXT("Could not create function graph '%s'."), *FunctionName));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);

	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		EntryNode = EntryNode ? EntryNode : Cast<UK2Node_FunctionEntry>(Node);
		ResultNode = ResultNode ? ResultNode : Cast<UK2Node_FunctionResult>(Node);
	}

	auto AddUserPins = [this, &Error](UK2Node_EditablePinBase* Node, const TArray<TSharedPtr<FJsonValue>>* PinArray, EEdGraphPinDirection Direction) -> bool
	{
		if (!Node || !PinArray)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& PinValue : *PinArray)
		{
			const TSharedPtr<FJsonObject> PinObject = PinValue.IsValid() ? PinValue->AsObject() : nullptr;
			FString PinName;
			if (!RequireString(PinObject, TEXT("name"), PinName, Error))
			{
				return false;
			}

			TSharedPtr<FJsonObject> TypeObject;
			if (!TryGetObject(PinObject, TEXT("type"), TypeObject))
			{
				Error = FString::Printf(TEXT("Pin '%s' is missing object field 'type'."), *PinName);
				return false;
			}

			FEdGraphPinType PinType;
			if (!BuildPinType(TypeObject, PinType, Error))
			{
				return false;
			}

			FText PinError;
			if (!Node->CanCreateUserDefinedPin(PinType, Direction, PinError))
			{
				Error = PinError.ToString();
				return false;
			}
			Node->CreateUserDefinedPin(FName(*PinName), PinType, Direction);
		}
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
	TryGetArray(Arguments, TEXT("inputs"), Inputs);
	TryGetArray(Arguments, TEXT("outputs"), Outputs);

	// A new function graph has only an entry node, and outputs live on the return node, so
	// without one every requested output was silently dropped.
	if (!ResultNode && Outputs && Outputs->Num() > 0)
	{
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
		ResultNode = ResultCreator.CreateNode();
		ResultNode->FunctionReference = EntryNode ? EntryNode->FunctionReference : ResultNode->FunctionReference;
		ResultNode->NodePosX = EntryNode ? EntryNode->NodePosX + 400 : 400;
		ResultNode->NodePosY = EntryNode ? EntryNode->NodePosY : 0;
		ResultCreator.Finalize();
	}

	if (!AddUserPins(EntryNode, Inputs, EGPD_Output) || !AddUserPins(ResultNode, Outputs, EGPD_Input))
	{
		return MakeErrorObject(TEXT("add_pin_failed"), Error);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("graph"), Graph->GetName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRenameFunction(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString FunctionName;
	FString NewName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("new_name"), NewName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FunctionName = GetOptionalString(Arguments, TEXT("function"), GetOptionalString(Arguments, TEXT("graph")));
	if (FunctionName.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'function' or 'graph' for rename_function."));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, FunctionName, Error) : nullptr;
	if (!Graph)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error);
	}
	if (!Blueprint->FunctionGraphs.Contains(Graph))
	{
		return MakeErrorObject(TEXT("not_function_graph"), FString::Printf(TEXT("Graph '%s' is not a user function graph on '%s'."), *FunctionName, *Blueprint->GetPathName()));
	}
	if (!NewName.Equals(FunctionName, ESearchCase::IgnoreCase) && !FBlueprintEditorUtils::IsGraphNameUnique(Blueprint, FName(*NewName)))
	{
		return MakeErrorObject(TEXT("name_conflict"), FString::Printf(TEXT("Graph/function name '%s' already exists in '%s'."), *NewName, *Blueprint->GetPathName()));
	}

	const FString OldName = Graph->GetName();
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RenameFunction", "LLM Rename Blueprint Function"));
	Blueprint->Modify();
	Graph->Modify();
	FBlueprintEditorUtils::RenameGraph(Graph, NewName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("old_name"), OldName);
	Data->SetStringField(TEXT("new_name"), Graph->GetName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindFunctionReferences(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString AssetPath = GetOptionalString(Arguments, TEXT("asset_path"));
	FString Path = GetOptionalString(Arguments, TEXT("path"), TEXT("/Game"));
	const FString FunctionName = GetOptionalString(Arguments, TEXT("function"), GetOptionalString(Arguments, TEXT("function_name")));
	const FString FunctionPath = GetOptionalString(Arguments, TEXT("function_path"));
	const FString OwnerClassPath = GetOptionalString(Arguments, TEXT("owner_class"), GetOptionalString(Arguments, TEXT("class_path")));
	const bool bRecursive = GetOptionalBool(Arguments, TEXT("recursive"), true);
	const bool bIncludeNodes = GetOptionalBool(Arguments, TEXT("include_nodes"), false);
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 500), 1, 10000);
	FString Error;
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	if (FunctionName.IsEmpty() && FunctionPath.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'function'/'function_name' or 'function_path'."));
	}

	UClass* OwnerClass = nullptr;
	if (!OwnerClassPath.IsEmpty())
	{
		OwnerClass = ResolveClass(OwnerClassPath);
		if (!OwnerClass)
		{
			return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve owner class '%s'."), *OwnerClassPath));
		}
	}

	TArray<TSharedPtr<FJsonValue>> References;
	int32 ScannedBlueprintCount = 0;
	int32 SkippedExcludedCount = 0;
	int32 SkippedLoadFailedCount = 0;

	if (!AssetPath.IsEmpty())
	{
		if (UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error))
		{
			ScannedBlueprintCount++;
			AppendFunctionReferencesForBlueprint(Blueprint, FunctionName, OwnerClass, FunctionPath, bIncludeNodes, Limit, References);
		}
		else
		{
			return MakeErrorObject(TEXT("load_failed"), Error);
		}
	}
	else
	{
		if (!NormalizeFolderPath(Path, Path, Error))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), Error);
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), Assets, bRecursive);
		for (const FAssetData& Asset : Assets)
		{
			if (References.Num() >= Limit)
			{
				break;
			}
			if (IsAssetDataExcluded(Asset, ExcludedPaths))
			{
				++SkippedExcludedCount;
				continue;
			}
			if (!Asset.AssetClassPath.ToString().Contains(TEXT("Blueprint")))
			{
				continue;
			}

			UObject* Object = Asset.GetAsset();
			UBlueprint* Blueprint = Cast<UBlueprint>(Object);
			if (!Blueprint)
			{
				SkippedLoadFailedCount++;
				continue;
			}

			ScannedBlueprintCount++;
			AppendFunctionReferencesForBlueprint(Blueprint, FunctionName, OwnerClass, FunctionPath, bIncludeNodes, Limit, References);
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("path"), AssetPath.IsEmpty() ? Path : FString());
	Data->SetStringField(TEXT("function"), FunctionName);
	Data->SetStringField(TEXT("function_path"), FunctionPath);
	Data->SetStringField(TEXT("owner_class"), OwnerClass ? OwnerClass->GetPathName() : OwnerClassPath);
	Data->SetBoolField(TEXT("recursive"), bRecursive);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetNumberField(TEXT("scanned_blueprint_count"), ScannedBlueprintCount);
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	Data->SetNumberField(TEXT("skipped_load_failed_count"), SkippedLoadFailedCount);
	Data->SetArrayField(TEXT("references"), References);
	Data->SetNumberField(TEXT("count"), References.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindVariableReferences(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString AssetPath = GetOptionalString(Arguments, TEXT("asset_path"));
	FString Path = GetOptionalString(Arguments, TEXT("path"), TEXT("/Game"));
	const FString VariableName = GetOptionalString(Arguments, TEXT("variable"), GetOptionalString(Arguments, TEXT("name")));
	const FString OwnerClassPath = GetOptionalString(Arguments, TEXT("owner_class"), GetOptionalString(Arguments, TEXT("class_path")));
	const bool bRecursive = GetOptionalBool(Arguments, TEXT("recursive"), true);
	const bool bIncludeNodes = GetOptionalBool(Arguments, TEXT("include_nodes"), false);
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 500), 1, 10000);
	FString Error;
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	if (VariableName.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'variable' or 'name'."));
	}

	UClass* OwnerClass = nullptr;
	if (!OwnerClassPath.IsEmpty())
	{
		OwnerClass = ResolveClass(OwnerClassPath);
		if (!OwnerClass)
		{
			return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve owner class '%s'."), *OwnerClassPath));
		}
	}

	TArray<TSharedPtr<FJsonValue>> References;
	int32 ScannedBlueprintCount = 0;
	int32 SkippedExcludedCount = 0;
	int32 SkippedLoadFailedCount = 0;

	if (!AssetPath.IsEmpty())
	{
		if (UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error))
		{
			ScannedBlueprintCount++;
			AppendVariableReferencesForBlueprint(Blueprint, VariableName, OwnerClass, bIncludeNodes, Limit, References);
		}
		else
		{
			return MakeErrorObject(TEXT("load_failed"), Error);
		}
	}
	else
	{
		if (!NormalizeFolderPath(Path, Path, Error))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), Error);
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), Assets, bRecursive);
		for (const FAssetData& Asset : Assets)
		{
			if (References.Num() >= Limit)
			{
				break;
			}
			if (IsAssetDataExcluded(Asset, ExcludedPaths))
			{
				++SkippedExcludedCount;
				continue;
			}
			if (!Asset.AssetClassPath.ToString().Contains(TEXT("Blueprint")))
			{
				continue;
			}

			UObject* Object = Asset.GetAsset();
			UBlueprint* Blueprint = Cast<UBlueprint>(Object);
			if (!Blueprint)
			{
				SkippedLoadFailedCount++;
				continue;
			}

			ScannedBlueprintCount++;
			AppendVariableReferencesForBlueprint(Blueprint, VariableName, OwnerClass, bIncludeNodes, Limit, References);
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("path"), AssetPath.IsEmpty() ? Path : FString());
	Data->SetStringField(TEXT("variable"), VariableName);
	Data->SetStringField(TEXT("owner_class"), OwnerClass ? OwnerClass->GetPathName() : OwnerClassPath);
	Data->SetBoolField(TEXT("recursive"), bRecursive);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetNumberField(TEXT("scanned_blueprint_count"), ScannedBlueprintCount);
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	Data->SetNumberField(TEXT("skipped_load_failed_count"), SkippedLoadFailedCount);
	Data->SetArrayField(TEXT("references"), References);
	Data->SetNumberField(TEXT("count"), References.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDeleteVariable(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString VariableName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	VariableName = GetOptionalString(Arguments, TEXT("variable"), GetOptionalString(Arguments, TEXT("name")));
	if (VariableName.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'variable' or 'name' for delete_variable."));
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("delete_variable requires confirm=true."));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FBPVariableDescription Variable;
	if (!FindNewVariableDescription(Blueprint, VariableName, Variable))
	{
		return MakeErrorObject(TEXT("variable_not_found"), FString::Printf(TEXT("Blueprint '%s' has no user-defined member variable named '%s'. Inherited or native variables cannot be deleted from this Blueprint."), *Blueprint->GetPathName(), *VariableName));
	}

	TArray<TSharedPtr<FJsonValue>> References;
	if (!GetOptionalBool(Arguments, TEXT("allow_referenced"), false))
	{
		FString ReferencePath = GetOptionalString(Arguments, TEXT("reference_path"), TEXT("/Game"));
		if (!NormalizeFolderPath(ReferencePath, ReferencePath, Error))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), Error);
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*ReferencePath), Assets, true);
		UClass* OwnerClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.AssetClassPath.ToString().Contains(TEXT("Blueprint")))
			{
				continue;
			}
			UBlueprint* Candidate = Cast<UBlueprint>(Asset.GetAsset());
			if (!Candidate)
			{
				continue;
			}
			AppendVariableReferencesForBlueprint(Candidate, Variable.VarName.ToString(), OwnerClass, false, 1000, References);
		}

		if (!References.IsEmpty())
		{
			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
			Data->SetStringField(TEXT("variable"), Variable.VarName.ToString());
			Data->SetArrayField(TEXT("referenced_by"), References);
			Data->SetNumberField(TEXT("referenced_by_count"), References.Num());
			TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("variable_referenced"), FString::Printf(TEXT("Variable '%s' still has Blueprint get/set references. Pass allow_referenced=true only after intentionally clearing or accepting them."), *Variable.VarName.ToString()));
			ErrorObject->SetObjectField(TEXT("data"), Data);
			return ErrorObject;
		}
	}

	const FString RemovedName = Variable.VarName.ToString();
	const FEdGraphPinType RemovedType = Variable.VarType;
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "DeleteVariable", "LLM Delete Blueprint Variable"));
	Blueprint->Modify();
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Variable.VarName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("deleted_variable"), RemovedName);
	Data->SetObjectField(TEXT("type"), PinTypeToJson(RemovedType));
	Data->SetArrayField(TEXT("referenced_by"), References);
	Data->SetNumberField(TEXT("referenced_by_count"), References.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDeleteFunctionGraph(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("delete_function_graph requires confirm=true."));
	}

	const FString FunctionName = GetOptionalString(Arguments, TEXT("function"), GetOptionalString(Arguments, TEXT("graph")));
	if (FunctionName.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'function' or 'graph' for delete_function_graph."));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, FunctionName, Error) : nullptr;
	if (!Graph)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error);
	}
	if (!Blueprint->FunctionGraphs.Contains(Graph))
	{
		return MakeErrorObject(TEXT("not_function_graph"), FString::Printf(TEXT("Graph '%s' is not a user function graph on '%s'."), *FunctionName, *Blueprint->GetPathName()));
	}

	TArray<TSharedPtr<FJsonValue>> References;
	if (!GetOptionalBool(Arguments, TEXT("allow_referenced"), false))
	{
		FString ReferencePath = GetOptionalString(Arguments, TEXT("reference_path"), TEXT("/Game"));
		if (!NormalizeFolderPath(ReferencePath, ReferencePath, Error))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), Error);
		}
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*ReferencePath), Assets, true);
		UClass* OwnerClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.AssetClassPath.ToString().Contains(TEXT("Blueprint")))
			{
				continue;
			}
			UBlueprint* Candidate = Cast<UBlueprint>(Asset.GetAsset());
			if (!Candidate)
			{
				continue;
			}
			AppendFunctionReferencesForBlueprint(Candidate, Graph->GetName(), OwnerClass, FString(), false, 1000, References);
		}

		if (!References.IsEmpty())
		{
			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
			Data->SetStringField(TEXT("function"), Graph->GetName());
			Data->SetArrayField(TEXT("referenced_by"), References);
			Data->SetNumberField(TEXT("referenced_by_count"), References.Num());
			TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("function_referenced"), FString::Printf(TEXT("Function '%s' still has Blueprint call references. Pass allow_referenced=true only after intentionally clearing or accepting them."), *Graph->GetName()));
			ErrorObject->SetObjectField(TEXT("data"), Data);
			return ErrorObject;
		}
	}

	const FString RemovedName = Graph->GetName();
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "DeleteFunctionGraph", "LLM Delete Blueprint Function Graph"));
	Blueprint->Modify();
	Graph->Modify();
	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Default);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("deleted_function"), RemovedName);
	Data->SetArrayField(TEXT("referenced_by"), References);
	Data->SetNumberField(TEXT("referenced_by_count"), References.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRetargetFunctionCall(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString NodeId;
	FString TargetFunctionName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("node"), NodeId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	TargetFunctionName = GetOptionalString(Arguments, TEXT("target_function"), GetOptionalString(Arguments, TEXT("function")));
	const FString TargetClassPath = GetOptionalString(Arguments, TEXT("target_class"), GetOptionalString(Arguments, TEXT("class_path")));
	if (TargetFunctionName.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide 'target_function' or 'function'."));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName, Error) : nullptr;
	UEdGraphNode* OldNode = Graph ? FindNode(Graph, NodeId, Error) : nullptr;
	UK2Node_CallFunction* OldCallNode = Cast<UK2Node_CallFunction>(OldNode);
	if (!OldCallNode)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error.IsEmpty() ? FString::Printf(TEXT("Node '%s' is not a call-function node."), *NodeId) : Error);
	}

	UFunction* TargetFunction = ResolveFunction(Blueprint, TargetClassPath, TargetFunctionName, Error);
	if (!TargetFunction)
	{
		return MakeErrorObject(TEXT("function_not_found"), Error);
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RetargetFunctionCall", "LLM Retarget Blueprint Function Call"));
	Blueprint->Modify();
	Graph->Modify();
	OldCallNode->Modify();

	UK2Node_CallFunction* NewCallNode = NewObject<UK2Node_CallFunction>(Graph);
	SetCommonNodeFields(Graph, NewCallNode, OldCallNode->NodePosX, OldCallNode->NodePosY);
	NewCallNode->SetFromFunction(TargetFunction);
	NewCallNode->PostPlacedNewNode();
	NewCallNode->AllocateDefaultPins();

	const UEdGraphSchema* Schema = Graph->GetSchema();
	int32 ReconnectedLinkCount = 0;
	int32 CopiedDefaultCount = 0;
	TArray<TSharedPtr<FJsonValue>> FailedPins;

	for (UEdGraphPin* OldPin : OldCallNode->Pins)
	{
		if (!OldPin || OldPin->WasTrashed())
		{
			continue;
		}

		UEdGraphPin* NewPin = NewCallNode->FindPin(OldPin->PinName, OldPin->Direction);
		if (!NewPin)
		{
			TSharedRef<FJsonObject> FailedPin = MakeShared<FJsonObject>();
			FailedPin->SetStringField(TEXT("pin"), OldPin->PinName.ToString());
			FailedPin->SetStringField(TEXT("reason"), TEXT("No matching pin on target function."));
			FailedPins.Add(MakeShared<FJsonValueObject>(FailedPin));
			continue;
		}

		for (UEdGraphPin* LinkedPin : OldPin->LinkedTo)
		{
			if (!LinkedPin || !LinkedPin->GetOwningNode() || LinkedPin->GetOwningNode() == OldCallNode)
			{
				continue;
			}

			const bool bConnected = OldPin->Direction == EGPD_Input
				? (Schema && Schema->TryCreateConnection(LinkedPin, NewPin))
				: (Schema && Schema->TryCreateConnection(NewPin, LinkedPin));
			if (bConnected)
			{
				ReconnectedLinkCount++;
			}
			else
			{
				TSharedRef<FJsonObject> FailedPin = MakeShared<FJsonObject>();
				FailedPin->SetStringField(TEXT("pin"), OldPin->PinName.ToString());
				FailedPin->SetStringField(TEXT("linked_node"), LinkedPin->GetOwningNode()->GetName());
				FailedPin->SetStringField(TEXT("linked_pin"), LinkedPin->PinName.ToString());
				FailedPin->SetStringField(TEXT("reason"), TEXT("Schema refused restored connection."));
				FailedPins.Add(MakeShared<FJsonValueObject>(FailedPin));
			}
		}

		if (OldPin->Direction == EGPD_Input && OldPin->LinkedTo.IsEmpty() && !OldPin->GetDefaultAsString().IsEmpty() && Schema)
		{
			Schema->TrySetDefaultValue(*NewPin, OldPin->GetDefaultAsString());
			CopiedDefaultCount++;
		}
	}

	const FString OldNodeId = GuidToString(OldCallNode->NodeGuid);
	OldCallNode->BreakAllNodeLinks();
	OldCallNode->DestroyNode();
	MarkGraphChanged(Blueprint, Graph);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("graph"), Graph->GetName());
	Data->SetStringField(TEXT("old_node_id"), OldNodeId);
	Data->SetObjectField(TEXT("new_node"), NodeToJson(NewCallNode));
	Data->SetObjectField(TEXT("target_function"), FunctionToJson(TargetFunction));
	Data->SetNumberField(TEXT("reconnected_link_count"), ReconnectedLinkCount);
	Data->SetNumberField(TEXT("copied_default_count"), CopiedDefaultCount);
	Data->SetArrayField(TEXT("failed_pins"), FailedPins);
	Data->SetNumberField(TEXT("failed_pin_count"), FailedPins.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListNativeEvents(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	if (!Blueprint->ParentClass)
	{
		return MakeErrorObject(TEXT("invalid_blueprint"), FString::Printf(TEXT("Blueprint '%s' has no parent class."), *Blueprint->GetPathName()));
	}

	const FString Query = GetOptionalString(Arguments, TEXT("query"));
	const FString QueryLookup = NormalizeEventLookupName(Query);
	const bool bIncludeImplemented = GetOptionalBool(Arguments, TEXT("include_implemented"), true);

	TArray<UFunction*> Functions;
	GetNativeEventFunctionsForBlueprint(Blueprint, Functions);

	TArray<TSharedPtr<FJsonValue>> Events;
	for (UFunction* Function : Functions)
	{
		if (!Function)
		{
			continue;
		}

		UClass* SignatureClass = GetNativeEventSignatureClass(Function);
		const bool bImplemented = SignatureClass && FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, SignatureClass, Function->GetFName()) != nullptr;
		if (!bIncludeImplemented && bImplemented)
		{
			continue;
		}

		if (!QueryLookup.IsEmpty())
		{
			const FString FunctionLookup = NormalizeEventLookupName(Function->GetName());
			const FString DisplayLookup = NormalizeEventLookupName(GetNativeEventDisplayName(Function));
			if (!FunctionLookup.Contains(QueryLookup) && !DisplayLookup.Contains(QueryLookup))
			{
				continue;
			}
		}

		Events.Add(MakeShared<FJsonValueObject>(NativeEventToJson(Blueprint, Function)));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetPathName());
	Data->SetArrayField(TEXT("events"), Events);
	Data->SetNumberField(TEXT("count"), Events.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSpawnNativeEvent(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString EventName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("event_name"), EventName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UEdGraph* Graph = FindGraph(Blueprint, GraphName, Error);
	if (!Graph)
	{
		return MakeErrorObject(TEXT("graph_not_found"), Error);
	}

	if (!FBlueprintEditorUtils::IsEventGraph(Graph))
	{
		return MakeErrorObject(TEXT("invalid_graph"), FString::Printf(TEXT("Native event nodes must be spawned in an event graph; '%s' is not an event graph."), *GraphName));
	}

	UFunction* Function = ResolveNativeEventFunction(Blueprint, EventName, GetOptionalString(Arguments, TEXT("owner_class")), Error);
	if (!Function)
	{
		return MakeErrorObject(TEXT("event_not_found"), Error);
	}

	UClass* SignatureClass = GetNativeEventSignatureClass(Function);
	if (!SignatureClass)
	{
		return MakeErrorObject(TEXT("event_not_found"), FString::Printf(TEXT("Native event '%s' has no signature class."), *Function->GetName()));
	}

	if (UK2Node_Event* ExistingNode = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, SignatureClass, Function->GetFName()))
	{
		return MakeErrorObject(TEXT("duplicate_native_event"), FString::Printf(TEXT("Native event '%s' is already implemented by node '%s' (%s)."), *GetNativeEventDisplayName(Function), *ExistingNode->GetName(), *GuidToString(ExistingNode->NodeGuid)));
	}

	const int32 X = GetOptionalInt(Arguments, TEXT("x"), 0);
	const int32 Y = GetOptionalInt(Arguments, TEXT("y"), 0);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SpawnNativeEvent", "LLM Spawn Native Blueprint Event"));
	Blueprint->Modify();
	Graph->Modify();

	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
	SetCommonNodeFields(Graph, EventNode, X, Y);
	EventNode->EventReference.SetExternalMember(Function->GetFName(), SignatureClass);
	EventNode->bOverrideFunction = true;
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	MarkGraphChanged(Blueprint, Graph);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("event"), NativeEventToJson(Blueprint, Function));
	Data->SetObjectField(TEXT("node"), NodeToJson(EventNode));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindBlueprintActions(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UEdGraph* Graph = FindGraph(Blueprint, GraphName, Error);
	if (!Graph)
	{
		return MakeErrorObject(TEXT("graph_not_found"), Error);
	}

	TArray<UEdGraphPin*> ContextPins;
	const FString ContextNodeId = GetOptionalString(Arguments, TEXT("context_node"));
	const FString ContextPinName = GetOptionalString(Arguments, TEXT("context_pin"));
	if (!ContextPinName.IsEmpty())
	{
		if (ContextNodeId.IsEmpty())
		{
			return MakeErrorObject(TEXT("invalid_arguments"), TEXT("'context_pin' requires 'context_node'."));
		}

		UEdGraphNode* ContextNode = FindNode(Graph, ContextNodeId, Error);
		UEdGraphPin* ContextPin = ContextNode ? FindPin(ContextNode, ContextPinName, EGPD_MAX, Error) : nullptr;
		if (!ContextPin)
		{
			return MakeErrorObject(TEXT("resolve_failed"), Error);
		}
		ContextPins.Add(ContextPin);
	}

	const FString Query = GetOptionalString(Arguments, TEXT("query"));
	const FString NodeClassFilter = GetOptionalString(Arguments, TEXT("node_class"));
	const FString FunctionPathFilter = GetOptionalString(Arguments, TEXT("function_path"));
	const FString MemberPathFilter = GetOptionalString(Arguments, TEXT("member_path"));
	const FString DisplayNameFilter = GetOptionalString(Arguments, TEXT("display_name"));
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 50), 1, 500);

	FBlueprintActionMenuBuilder MenuBuilder;
	BuildBlueprintActionMenu(Blueprint, Graph, ContextPins, MenuBuilder);

	TArray<TSharedPtr<FJsonValue>> Actions;
	TSet<FString> SeenActionIds;
	for (int32 ActionIndex = 0; ActionIndex < MenuBuilder.GetNumActions() && Actions.Num() < Limit; ++ActionIndex)
	{
		TSharedPtr<FEdGraphSchemaAction> Action = MenuBuilder.GetSchemaAction(ActionIndex);
		if (!Action.IsValid() ||
			!ActionMatchesQuery(*Action, Query) ||
			!BlueprintActionMatchesMetadataFilters(Action, NodeClassFilter, FunctionPathFilter, MemberPathFilter, DisplayNameFilter))
		{
			continue;
		}

		const FString ActionId = GetBlueprintActionId(*Action);
		if (SeenActionIds.Contains(ActionId))
		{
			continue;
		}

		SeenActionIds.Add(ActionId);
		Actions.Add(MakeShared<FJsonValueObject>(BlueprintActionToJson(Action)));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("graph"), Graph->GetName());
	Data->SetStringField(TEXT("query"), Query);
	Data->SetStringField(TEXT("node_class_filter"), NodeClassFilter);
	Data->SetStringField(TEXT("function_path_filter"), FunctionPathFilter);
	Data->SetStringField(TEXT("member_path_filter"), MemberPathFilter);
	Data->SetStringField(TEXT("display_name_filter"), DisplayNameFilter);
	Data->SetArrayField(TEXT("actions"), Actions);
	Data->SetNumberField(TEXT("count"), Actions.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSpawnBlueprintAction(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString ActionId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("action_id"), ActionId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UEdGraph* Graph = FindGraph(Blueprint, GraphName, Error);
	if (!Graph)
	{
		return MakeErrorObject(TEXT("graph_not_found"), Error);
	}

	TArray<UEdGraphPin*> ContextPins;
	const FString ContextNodeId = GetOptionalString(Arguments, TEXT("context_node"));
	const FString ContextPinName = GetOptionalString(Arguments, TEXT("context_pin"));
	if (!ContextPinName.IsEmpty())
	{
		if (ContextNodeId.IsEmpty())
		{
			return MakeErrorObject(TEXT("invalid_arguments"), TEXT("'context_pin' requires 'context_node'."));
		}

		UEdGraphNode* ContextNode = FindNode(Graph, ContextNodeId, Error);
		UEdGraphPin* ContextPin = ContextNode ? FindPin(ContextNode, ContextPinName, EGPD_MAX, Error) : nullptr;
		if (!ContextPin)
		{
			return MakeErrorObject(TEXT("resolve_failed"), Error);
		}
		ContextPins.Add(ContextPin);
	}

	FBlueprintActionMenuBuilder MenuBuilder;
	BuildBlueprintActionMenu(Blueprint, Graph, ContextPins, MenuBuilder);

	TSharedPtr<FEdGraphSchemaAction> SelectedAction;
	for (int32 ActionIndex = 0; ActionIndex < MenuBuilder.GetNumActions(); ++ActionIndex)
	{
		TSharedPtr<FEdGraphSchemaAction> Action = MenuBuilder.GetSchemaAction(ActionIndex);
		if (Action.IsValid() && GetBlueprintActionId(*Action).Equals(ActionId, ESearchCase::IgnoreCase))
		{
			SelectedAction = Action;
			break;
		}
	}

	if (!SelectedAction.IsValid())
	{
		return MakeErrorObject(TEXT("action_not_found"), FString::Printf(TEXT("Blueprint action '%s' was not found for graph '%s'. Re-run find_blueprint_actions for this graph and use one of the returned action_id values."), *ActionId, *GraphName));
	}

	const int32 X = GetOptionalInt(Arguments, TEXT("x"), 0);
	const int32 Y = GetOptionalInt(Arguments, TEXT("y"), 0);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SpawnBlueprintAction", "LLM Spawn Blueprint Action"));
	Blueprint->Modify();
	Graph->Modify();

	UEdGraphNode* NewNode = SelectedAction->PerformAction(Graph, ContextPins, FVector2f(static_cast<float>(X), static_cast<float>(Y)), false);
	if (!NewNode)
	{
		return MakeErrorObject(TEXT("spawn_failed"), FString::Printf(TEXT("Blueprint action '%s' did not create a node."), *ActionId));
	}

	MarkGraphChanged(Blueprint, Graph);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("action"), BlueprintActionToJson(SelectedAction));
	Data->SetObjectField(TEXT("node"), NodeToJson(NewNode));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListComponents(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	USimpleConstructionScript* SCS = GetBlueprintSCS(Blueprint, Error);
	if (!SCS)
	{
		return MakeErrorObject(TEXT("invalid_blueprint"), Error);
	}

	TArray<TSharedPtr<FJsonValue>> Components;
	TSet<FString> EditableNames;
	for (const USCS_Node* RootNode : SCS->GetRootNodes())
	{
		AddSCSNodeAndChildrenJson(Blueprint, RootNode, FString(), Components);
	}
	for (const USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node)
		{
			EditableNames.Add(Node->GetVariableName().ToString());
		}
	}

	if (UClass* ParentClass = Blueprint->ParentClass)
	{
		if (AActor* ParentCDO = Cast<AActor>(ParentClass->GetDefaultObject()))
		{
			for (UActorComponent* Component : ParentCDO->GetComponents())
			{
				if (Component && !EditableNames.Contains(Component->GetName()))
				{
					AddNativeComponentJson(Component, Components);
				}
			}
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetArrayField(TEXT("components"), Components);
	Data->SetNumberField(TEXT("count"), Components.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddComponent(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ClassName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("component_class"), ClassName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	USimpleConstructionScript* SCS = GetBlueprintSCS(Blueprint, Error);
	if (!SCS)
	{
		return MakeErrorObject(TEXT("invalid_blueprint"), Error);
	}

	UClass* ComponentClass = ResolveComponentClass(ClassName);
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve component class '%s'."), *ClassName));
	}

	const FString RequestedName = GetOptionalString(Arguments, TEXT("name"));
	const FString ParentName = GetOptionalString(Arguments, TEXT("parent"));
	const FString AttachSocket = GetOptionalString(Arguments, TEXT("attach_socket"));

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddComponent", "LLM Add Blueprint Component"));
	Blueprint->Modify();
	SCS->Modify();

	USCS_Node* NewNode = SCS->CreateNode(ComponentClass, RequestedName.IsEmpty() ? NAME_None : FName(*RequestedName));
	if (!NewNode || !NewNode->ComponentTemplate)
	{
		return MakeErrorObject(TEXT("add_component_failed"), TEXT("Could not create SCS component node."));
	}
	NewNode->SetFlags(RF_Transactional);
	NewNode->ComponentTemplate->SetFlags(RF_Transactional);
	NewNode->AttachToName = AttachSocket.IsEmpty() ? NAME_None : FName(*AttachSocket);

	if (!ParentName.IsEmpty())
	{
		if (USCS_Node* ParentNode = FindEditableSCSNode(Blueprint, ParentName, Error))
		{
			if (!NewNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()) || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
			{
				return MakeErrorObject(TEXT("invalid_attachment"), TEXT("Only SceneComponent-derived components can be attached in the SCS hierarchy."));
			}
			// Local SCS parents are represented by ChildNodes; the named parent fields
			// are reserved for inherited/native components.
			ParentNode->AddChildNode(NewNode);
		}
		else
		{
			Error.Reset();
			bool bParentEditable = false;
			UActorComponent* ParentTemplate = FindComponentTemplateByName(Blueprint, ParentName, bParentEditable, Error);
			USceneComponent* ParentSceneTemplate = Cast<USceneComponent>(ParentTemplate);
			if (!ParentSceneTemplate || !NewNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
			{
				return MakeErrorObject(TEXT("invalid_attachment"), FString::Printf(TEXT("Parent '%s' was not found as a scene component."), *ParentName));
			}
			NewNode->SetParent(ParentSceneTemplate);
			SCS->AddNode(NewNode);
		}
	}
	else
	{
		SCS->AddNode(NewNode);
	}

	SCS->ValidateSceneRootNodes();
	FBlueprintEditorUtils::UpdateRootComponentReference(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Components;
	AddSCSNodeAndChildrenJson(Blueprint, NewNode, ParentName, Components);
	TSharedPtr<FJsonObject> ComponentObject = Components.Num() > 0 ? Components[0]->AsObject() : MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("component"), ComponentObject);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveComponent(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ComponentName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("component"), ComponentName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	USimpleConstructionScript* SCS = Blueprint ? GetBlueprintSCS(Blueprint, Error) : nullptr;
	USCS_Node* Node = SCS ? FindEditableSCSNode(Blueprint, ComponentName, Error) : nullptr;
	if (!Node)
	{
		return MakeErrorObject(TEXT("component_not_editable"), Error.IsEmpty() ? FString::Printf(TEXT("Component '%s' is inherited/native or was not found."), *ComponentName) : Error);
	}

	const FString RemovedName = Node->GetVariableName().ToString();
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveComponent", "LLM Remove Blueprint Component"));
	Blueprint->Modify();
	SCS->Modify();
	Node->Modify();
	SCS->RemoveNodeAndPromoteChildren(Node);
	SCS->ValidateSceneRootNodes();
	FBlueprintEditorUtils::UpdateRootComponentReference(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_component"), RemovedName);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAttachComponent(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ComponentName;
	FString ParentName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("component"), ComponentName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	ParentName = GetOptionalString(Arguments, TEXT("parent"));
	const FString AttachSocket = GetOptionalString(Arguments, TEXT("attach_socket"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	USimpleConstructionScript* SCS = Blueprint ? GetBlueprintSCS(Blueprint, Error) : nullptr;
	USCS_Node* Node = SCS ? FindEditableSCSNode(Blueprint, ComponentName, Error) : nullptr;
	if (!Node)
	{
		return MakeErrorObject(TEXT("component_not_editable"), Error.IsEmpty() ? FString::Printf(TEXT("Component '%s' is inherited/native or was not found."), *ComponentName) : Error);
	}
	if (!Node->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
	{
		return MakeErrorObject(TEXT("invalid_attachment"), FString::Printf(TEXT("Component '%s' is not a SceneComponent."), *ComponentName));
	}
	USCS_Node* localParentNode = ParentName.IsEmpty()
		? nullptr : FindEditableSCSNode(Blueprint, ParentName, Error);
	if (localParentNode && (localParentNode == Node || localParentNode->IsChildOf(Node)))
	{
		return MakeErrorObject(TEXT("invalid_attachment"), TEXT("A component cannot be attached to itself or one of its descendants."));
	}
	if (localParentNode && !localParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
	{
		return MakeErrorObject(TEXT("invalid_attachment"), FString::Printf(TEXT("Parent '%s' is not a SceneComponent."), *ParentName));
	}
	USceneComponent* inheritedParentTemplate = nullptr;
	if (!ParentName.IsEmpty() && !localParentNode)
	{
		Error.Reset();
		bool parentEditable = false;
		inheritedParentTemplate = Cast<USceneComponent>(
			FindComponentTemplateByName(Blueprint, ParentName, parentEditable, Error));
		if (!inheritedParentTemplate)
		{
			return MakeErrorObject(TEXT("invalid_attachment"), FString::Printf(TEXT("Parent '%s' was not found as a scene component."), *ParentName));
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AttachComponent", "LLM Attach Blueprint Component"));
	Blueprint->Modify();
	SCS->Modify();
	Node->Modify();
	Node->AttachToName = AttachSocket.IsEmpty() ? NAME_None : FName(*AttachSocket);
	Node->ParentComponentOrVariableName = NAME_None;
	Node->ParentComponentOwnerClassName = NAME_None;
	Node->bIsParentComponentNative = false;

	if (USCS_Node* OldParentNode = SCS->FindParentNode(Node))
	{
		OldParentNode->RemoveChildNode(Node, false);
	}
	else
	{
		SCS->RemoveNode(Node, false);
	}

	if (ParentName.IsEmpty())
	{
		SCS->AddNode(Node);
	}
	else if (localParentNode)
	{
		localParentNode->AddChildNode(Node, false);
	}
	else
	{
		Node->SetParent(inheritedParentTemplate);
		SCS->AddNode(Node);
	}

	SCS->ValidateSceneRootNodes();
	FBlueprintEditorUtils::UpdateRootComponentReference(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Components;
	AddSCSNodeAndChildrenJson(Blueprint, Node, ParentName, Components);
	TSharedPtr<FJsonObject> ComponentObject = Components.Num() > 0 ? Components[0]->AsObject() : MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("component"), ComponentObject);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetComponentTransform(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ComponentName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("component"), ComponentName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	USCS_Node* Node = Blueprint ? FindEditableSCSNode(Blueprint, ComponentName, Error) : nullptr;
	if (!Node)
	{
		return MakeErrorObject(TEXT("component_not_editable"), Error.IsEmpty() ? FString::Printf(TEXT("Component '%s' is inherited/native or was not found."), *ComponentName) : Error);
	}

	USceneComponent* SceneTemplate = Cast<USceneComponent>(Node->ComponentTemplate);
	if (!SceneTemplate)
	{
		return MakeErrorObject(TEXT("not_scene_component"), FString::Printf(TEXT("Component '%s' is not a SceneComponent."), *ComponentName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetComponentTransform", "LLM Set Component Transform"));
	Blueprint->Modify();
	Node->Modify();
	SceneTemplate->Modify();

	const TSharedPtr<FJsonValue> LocationValue = Arguments->TryGetField(TEXT("location"));
	const TSharedPtr<FJsonValue> RotationValue = Arguments->TryGetField(TEXT("rotation"));
	const TSharedPtr<FJsonValue> ScaleValue = Arguments->TryGetField(TEXT("scale"));
	FVector Location = SceneTemplate->GetRelativeLocation();
	FRotator Rotation = SceneTemplate->GetRelativeRotation();
	FVector Scale = SceneTemplate->GetRelativeScale3D();
	if (LocationValue.IsValid() && !TryParseVectorValue(LocationValue, Location))
	{
		return MakeErrorObject(TEXT("invalid_transform"), TEXT("'location' must be {x,y,z}, [x,y,z], or an Unreal vector string."));
	}
	if (RotationValue.IsValid() && !TryParseRotatorValue(RotationValue, Rotation))
	{
		return MakeErrorObject(TEXT("invalid_transform"), TEXT("'rotation' must be {pitch,yaw,roll}, [pitch,yaw,roll], or an Unreal rotator string."));
	}
	if (ScaleValue.IsValid() && !TryParseVectorValue(ScaleValue, Scale))
	{
		return MakeErrorObject(TEXT("invalid_transform"), TEXT("'scale' must be {x,y,z}, [x,y,z], or an Unreal vector string."));
	}

	SceneTemplate->SetRelativeLocation_Direct(Location);
	SceneTemplate->SetRelativeRotation_Direct(Rotation);
	SceneTemplate->SetRelativeScale3D_Direct(Scale);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Components;
	AddSCSNodeAndChildrenJson(Blueprint, Node, Node->ParentComponentOrVariableName.ToString(), Components);
	TSharedPtr<FJsonObject> ComponentObject = Components.Num() > 0 ? Components[0]->AsObject() : MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("component"), ComponentObject);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetComponentProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ComponentName;
	FString PropertyName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("component"), ComponentName, Error) ||
		!RequireString(Arguments, TEXT("property"), PropertyName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'value'."));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	bool bEditable = false;
	UActorComponent* ComponentTemplate = Blueprint ? FindComponentTemplateByName(Blueprint, ComponentName, bEditable, Error) : nullptr;
	if (!ComponentTemplate || !bEditable)
	{
		return MakeErrorObject(TEXT("component_not_editable"), Error.IsEmpty() ? FString::Printf(TEXT("Component '%s' is inherited/native or was not found."), *ComponentName) : Error);
	}

	FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), FName(*PropertyName));
	if (!Property)
	{
		return MakeErrorObject(TEXT("property_not_found"), FString::Printf(TEXT("Property '%s' was not found on component class '%s'."), *PropertyName, *ComponentTemplate->GetClass()->GetPathName()));
	}
	if (!Property->HasAnyPropertyFlags(CPF_Edit))
	{
		return MakeErrorObject(TEXT("property_not_editable"), FString::Printf(TEXT("Property '%s' is not editable."), *PropertyName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetComponentProperty", "LLM Set Component Property"));
	Blueprint->Modify();
	ComponentTemplate->Modify();
	ComponentTemplate->PreEditChange(Property);
	if (!ImportJsonValueToProperty(ComponentTemplate, Property, Value, Error))
	{
		return MakeErrorObject(TEXT("property_import_failed"), Error);
	}
	FPropertyChangedEvent ChangeEvent(Property);
	ComponentTemplate->PostEditChangeProperty(ChangeEvent);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("component"), ComponentName);
	Data->SetStringField(TEXT("property"), PropertyName);
	Data->SetStringField(TEXT("value"), ExportPropertyText(Property, Property->ContainerPtrToValuePtr<void>(ComponentTemplate), ComponentTemplate));
	Data->SetObjectField(TEXT("property_data"), PropertyToJson(ComponentTemplate, Property, Property->ContainerPtrToValuePtr<void>(ComponentTemplate), PropertyName, TEXT("component"), ComponentName));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGetClassDefaults(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UObject* DefaultObject = Blueprint ? GetBlueprintDefaultObject(Blueprint, Error) : nullptr;
	if (!DefaultObject)
	{
		return MakeErrorObject(Blueprint ? TEXT("invalid_blueprint") : TEXT("load_failed"), Error);
	}

	const bool bIncludeInherited = GetOptionalBool(Arguments, TEXT("include_inherited"), true);
	const bool bIncludeUnsupported = GetOptionalBool(Arguments, TEXT("include_unsupported"), true);
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 500), 1, 5000);

	TArray<TSharedPtr<FJsonValue>> Properties;
	const EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;
	for (TFieldIterator<FProperty> PropertyIt(DefaultObject->GetClass(), SuperFlags); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (!IsEditableDefaultProperty(Property))
		{
			continue;
		}

		const void* PropertyData = Property->ContainerPtrToValuePtr<void>(DefaultObject);
		TSharedRef<FJsonObject> PropertyObject = PropertyToJson(DefaultObject, Property, PropertyData, Property->GetName(), TEXT("class_default"), FString());
		if (!bIncludeUnsupported && !PropertyObject->GetBoolField(TEXT("supported")) && !PropertyObject->HasField(TEXT("fields")))
		{
			continue;
		}

		Properties.Add(MakeShared<FJsonValueObject>(PropertyObject));
		if (Properties.Num() >= Limit)
		{
			break;
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("class"), DefaultObject->GetClass()->GetPathName());
	Data->SetStringField(TEXT("object_path"), DefaultObject->GetPathName());
	Data->SetArrayField(TEXT("properties"), Properties);
	Data->SetNumberField(TEXT("count"), Properties.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetClassDefault(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString PropertyPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("property"), PropertyPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'value'."));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UObject* DefaultObject = Blueprint ? GetBlueprintDefaultObject(Blueprint, Error) : nullptr;
	if (!DefaultObject)
	{
		return MakeErrorObject(Blueprint ? TEXT("invalid_blueprint") : TEXT("load_failed"), Error);
	}

	FProperty* Property = nullptr;
	void* PropertyData = nullptr;
	if (!ResolvePropertyAddress(DefaultObject, PropertyPath, Property, PropertyData, Error))
	{
		return MakeErrorObject(TEXT("property_not_found"), Error);
	}
	if (!IsEditableDefaultProperty(Property))
	{
		return MakeErrorObject(TEXT("property_not_editable"), FString::Printf(TEXT("Property '%s' is not editable on class defaults."), *PropertyPath));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetClassDefault", "LLM Set Blueprint Class Default"));
	Blueprint->Modify();
	DefaultObject->Modify();
	DefaultObject->PreEditChange(Property);
	if (!SetPropertyValueFromJson(Property, PropertyData, Value, Error))
	{
		return MakeErrorObject(TEXT("property_set_failed"), Error);
	}
	FPropertyChangedEvent ChangeEvent(Property);
	DefaultObject->PostEditChangeProperty(ChangeEvent);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetObjectField(TEXT("property"), PropertyToJson(DefaultObject, Property, PropertyData, PropertyPath, TEXT("class_default"), FString()));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGetProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString PropertyPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("property"), PropertyPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FString TargetKind = GetOptionalString(Arguments, TEXT("target"), TEXT("class_default"));
	FString ComponentName = GetOptionalString(Arguments, TEXT("component"));
	if (!ComponentName.IsEmpty())
	{
		TargetKind = TEXT("component");
	}

	UBlueprint* Blueprint = nullptr;
	UObject* TargetObject = nullptr;
	bool bComponentEditable = false;
	const FString ObjectPath = GetOptionalString(Arguments, TEXT("object_path"));
	if (!ObjectPath.IsEmpty())
	{
		TargetObject = ResolveObjectPath(ObjectPath);
		if (!TargetObject)
		{
			return MakeErrorObject(TEXT("object_not_found"), FString::Printf(TEXT("Object '%s' was not found."), *ObjectPath));
		}
		TargetKind = TargetKind.IsEmpty() ? TEXT("object") : TargetKind;
	}
	else
	{
		FString AssetPath;
		if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), Error);
		}
		Blueprint = LoadBlueprint(AssetPath, Error);
		if (!Blueprint)
		{
			return MakeErrorObject(TEXT("load_failed"), Error);
		}

		if (TargetKind.Equals(TEXT("component"), ESearchCase::IgnoreCase))
		{
			if (ComponentName.IsEmpty())
			{
				return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Component target requires a 'component' field."));
			}
			TargetObject = FindComponentTemplateByName(Blueprint, ComponentName, bComponentEditable, Error);
			if (!TargetObject)
			{
				return MakeErrorObject(TEXT("component_not_found"), Error);
			}
		}
		else
		{
			TargetKind = TEXT("class_default");
			TargetObject = GetBlueprintDefaultObject(Blueprint, Error);
			if (!TargetObject)
			{
				return MakeErrorObject(TEXT("invalid_blueprint"), Error);
			}
		}
	}

	FProperty* Property = nullptr;
	void* PropertyData = nullptr;
	if (!ResolvePropertyAddress(TargetObject, PropertyPath, Property, PropertyData, Error))
	{
		return MakeErrorObject(TEXT("property_not_found"), Error);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	if (Blueprint)
	{
		Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	}
	Data->SetObjectField(TEXT("property"), PropertyToJson(TargetObject, Property, PropertyData, PropertyPath, TargetKind, ComponentName));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString PropertyPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("property"), PropertyPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'value'."));
	}

	FString TargetKind = GetOptionalString(Arguments, TEXT("target"), TEXT("class_default"));
	FString ComponentName = GetOptionalString(Arguments, TEXT("component"));
	if (!ComponentName.IsEmpty())
	{
		TargetKind = TEXT("component");
	}

	UBlueprint* Blueprint = nullptr;
	UObject* TargetObject = nullptr;
	bool bComponentEditable = false;
	const FString ObjectPath = GetOptionalString(Arguments, TEXT("object_path"));
	if (!ObjectPath.IsEmpty())
	{
		TargetObject = ResolveObjectPath(ObjectPath);
		if (!TargetObject)
		{
			return MakeErrorObject(TEXT("object_not_found"), FString::Printf(TEXT("Object '%s' was not found."), *ObjectPath));
		}

		const FString AssetPath = GetOptionalString(Arguments, TEXT("asset_path"));
		if (!AssetPath.IsEmpty())
		{
			Blueprint = LoadBlueprint(AssetPath, Error);
			if (!Blueprint)
			{
				return MakeErrorObject(TEXT("load_failed"), Error);
			}
		}
		TargetKind = TargetKind.IsEmpty() ? TEXT("object") : TargetKind;

		// The outliner label and folder are not editable properties; route them through the actor API.
		AActor* TargetActor = Cast<AActor>(TargetObject);
		if (TargetActor && !TargetActor->IsTemplate() &&
			(PropertyPath == TEXT("ActorLabel") || PropertyPath == TEXT("FolderPath")))
		{
			if (Value->Type != EJson::String)
			{
				return MakeErrorObject(TEXT("invalid_arguments"), FString::Printf(TEXT("'%s' expects a string."), *PropertyPath));
			}
			const FString NewValue = Value->AsString();
			const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetActorOutliner", "LLM Set Actor Label/Folder"));
			TargetActor->Modify();
			FString Applied;
			if (PropertyPath == TEXT("ActorLabel"))
			{
				TargetActor->SetActorLabel(NewValue);
				Applied = TargetActor->GetActorLabel();
			}
			else
			{
				TargetActor->SetFolderPath(FName(*NewValue));
				Applied = TargetActor->GetFolderPath().ToString();
			}
			TargetActor->MarkPackageDirty();

			TSharedRef<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
			PropertyObject->SetStringField(TEXT("name"), PropertyPath);
			PropertyObject->SetStringField(TEXT("object_path"), TargetActor->GetPathName());
			PropertyObject->SetStringField(TEXT("value"), Applied);
			PropertyObject->SetStringField(TEXT("value_text"), Applied);
			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetObjectField(TEXT("property"), PropertyObject);
			return MakeOk(Data);
		}
	}
	else
	{
		FString AssetPath;
		if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), Error);
		}
		Blueprint = LoadBlueprint(AssetPath, Error);
		if (!Blueprint)
		{
			return MakeErrorObject(TEXT("load_failed"), Error);
		}

		if (TargetKind.Equals(TEXT("component"), ESearchCase::IgnoreCase))
		{
			if (ComponentName.IsEmpty())
			{
				return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Component target requires a 'component' field."));
			}
			TargetObject = FindComponentTemplateByName(Blueprint, ComponentName, bComponentEditable, Error);
			if (!TargetObject)
			{
				return MakeErrorObject(TEXT("component_not_found"), Error);
			}
			if (!bComponentEditable)
			{
				return MakeErrorObject(TEXT("component_not_editable"), FString::Printf(TEXT("Component '%s' is inherited/native and cannot be edited through Blueprint defaults."), *ComponentName));
			}
		}
		else
		{
			TargetKind = TEXT("class_default");
			TargetObject = GetBlueprintDefaultObject(Blueprint, Error);
			if (!TargetObject)
			{
				return MakeErrorObject(TEXT("invalid_blueprint"), Error);
			}
		}
	}

	FProperty* Property = nullptr;
	void* PropertyData = nullptr;
	if (!ResolvePropertyAddress(TargetObject, PropertyPath, Property, PropertyData, Error))
	{
		return MakeErrorObject(TEXT("property_not_found"), Error);
	}
	const bool bPlacedInstance = !ObjectPath.IsEmpty() && !TargetObject->IsTemplate();
	// Node coordinates are editor layout data, although Unreal does not flag them CPF_Edit.
	// Permit only the two coordinate fields on a node belonging to the supplied Blueprint.
	UEdGraphNode* LayoutNode = Cast<UEdGraphNode>(TargetObject);
	const bool bNodeLayoutProperty = Blueprint && LayoutNode &&
		LayoutNode->GetTypedOuter<UBlueprint>() == Blueprint &&
		(PropertyPath == TEXT("NodePosX") || PropertyPath == TEXT("NodePosY"));
	if (!bNodeLayoutProperty && (bPlacedInstance ? !IsEditableInstanceProperty(Property) : !IsEditableDefaultProperty(Property)))
	{
		return MakeErrorObject(TEXT("property_not_editable"), FString::Printf(TEXT("Property '%s' is not editable on '%s'."), *PropertyPath, TargetObject ? *TargetObject->GetPathName() : TEXT("<null>")));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetProperty", "LLM Set Blueprint Property"));
	if (Blueprint)
	{
		Blueprint->Modify();
	}
	TargetObject->Modify();
	TargetObject->PreEditChange(Property);
	if (!SetPropertyValueFromJson(Property, PropertyData, Value, Error))
	{
		return MakeErrorObject(TEXT("property_set_failed"), Error);
	}
	FPropertyChangedEvent ChangeEvent(Property);
	TargetObject->PostEditChangeProperty(ChangeEvent);
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (bNodeLayoutProperty)
		{
			LayoutNode->GetGraph()->NotifyGraphChanged();
		}
		else
		{
			FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);
		}
	}
	else
	{
		TargetObject->MarkPackageDirty();
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	if (Blueprint)
	{
		Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	}
	Data->SetObjectField(TEXT("property"), PropertyToJson(TargetObject, Property, PropertyData, PropertyPath, TargetKind, ComponentName));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindAssets(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("path"), TEXT("/Game"));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const FString Query = GetOptionalString(Arguments, TEXT("query")).ToLower();
	const FString ClassPath = GetOptionalString(Arguments, TEXT("class_path"));
	const bool bRecursive = GetOptionalBool(Arguments, TEXT("recursive"), true);
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 200), 1, 5000);
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = bRecursive;
	Filter.bIncludeOnlyOnDiskAssets = false;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> AssetObjects;
	int32 SkippedExcludedCount = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (IsAssetDataExcluded(Asset, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}
		if (!Query.IsEmpty())
		{
			const FString SearchText = FString::Printf(TEXT("%s %s %s"), *Asset.AssetName.ToString(), *Asset.GetObjectPathString(), *Asset.PackageName.ToString()).ToLower();
			if (!SearchText.Contains(Query))
			{
				continue;
			}
		}

		if (!ClassPath.IsEmpty())
		{
			const FString AssetClassPath = Asset.AssetClassPath.ToString();
			const FString AssetClassName = Asset.AssetClassPath.GetAssetName().ToString();
			if (!AssetClassPath.Equals(ClassPath, ESearchCase::IgnoreCase) && !AssetClassName.Equals(ClassPath, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		AssetObjects.Add(MakeShared<FJsonValueObject>(AssetDataToJson(Asset)));
		if (AssetObjects.Num() >= Limit)
		{
			break;
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Path);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("assets"), AssetObjects);
	Data->SetNumberField(TEXT("count"), AssetObjects.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateFolder(const TSharedPtr<FJsonObject>& Arguments)
{
	FString FolderPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("path"), FolderPath, Error) ||
		!NormalizeFolderPath(FolderPath, FolderPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!IsGameContentPath(FolderPath))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("create_folder is limited to /Game content paths."));
	}

	const FString DiskPath = FPackageName::LongPackageNameToFilename(FolderPath);
	const bool bCreated = IFileManager::Get().MakeDirectory(*DiskPath, true);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().ScanPathsSynchronous({ FolderPath }, true);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), FolderPath);
	Data->SetStringField(TEXT("disk_path"), DiskPath);
	Data->SetBoolField(TEXT("created"), bCreated || IFileManager::Get().DirectoryExists(*DiskPath));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDuplicateAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	FString SourcePath;
	FString DestinationPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), SourcePath, Error) ||
		!RequireString(Arguments, TEXT("destination_path"), DestinationPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* SourceAsset = LoadAssetObject(SourcePath, Error);
	if (!SourceAsset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FString DestinationPackagePath;
	FString DestinationAssetName;
	if (!SplitAssetPackagePath(DestinationPath, DestinationPackagePath, DestinationAssetName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!IsGameContentPath(DestinationPackagePath))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("duplicate_asset destination must be under /Game."));
	}
	if (ResolveObjectPath(ObjectPathFromPackagePath(FString::Printf(TEXT("%s/%s"), *DestinationPackagePath, *DestinationAssetName))))
	{
		return MakeErrorObject(TEXT("asset_exists"), FString::Printf(TEXT("Destination asset '%s/%s' already exists."), *DestinationPackagePath, *DestinationAssetName));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* NewAsset = AssetTools.DuplicateAsset(DestinationAssetName, DestinationPackagePath, SourceAsset);
	if (!NewAsset)
	{
		return MakeErrorObject(TEXT("duplicate_failed"), FString::Printf(TEXT("Failed to duplicate '%s' to '%s/%s'."), *SourceAsset->GetPathName(), *DestinationPackagePath, *DestinationAssetName));
	}
	NewAsset->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("source_asset_path"), SourceAsset->GetPathName());
	Data->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
	Data->SetStringField(TEXT("package"), NewAsset->GetOutermost()->GetName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRenameAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString NewName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("new_name"), NewName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!IsGameContentPath(Asset->GetOutermost()->GetName()))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("rename_asset is limited to /Game assets."));
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TArray<FAssetRenameData> RenameData;
	RenameData.Add(FAssetRenameData(Asset, PackagePath, NewName));
	if (!AssetTools.RenameAssets(RenameData))
	{
		return MakeErrorObject(TEXT("rename_failed"), FString::Printf(TEXT("Failed to rename '%s' to '%s'."), *AssetPath, *NewName));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolMoveAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString DestinationFolder;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("destination_path"), DestinationFolder, Error) ||
		!NormalizeFolderPath(DestinationFolder, DestinationFolder, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!IsGameContentPath(Asset->GetOutermost()->GetName()) || !IsGameContentPath(DestinationFolder))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("move_asset is limited to /Game assets and destinations."));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TArray<FAssetRenameData> RenameData;
	RenameData.Add(FAssetRenameData(Asset, DestinationFolder, Asset->GetName()));
	if (!AssetTools.RenameAssets(RenameData))
	{
		return MakeErrorObject(TEXT("move_failed"), FString::Printf(TEXT("Failed to move '%s' to '%s'."), *AssetPath, *DestinationFolder));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDeleteAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const FString PackageName = Asset->GetOutermost()->GetName();
	TArray<TSharedPtr<FJsonValue>> Referencers;
	AddReferencersJson(PackageName, Referencers);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("package"), PackageName);
	Data->SetArrayField(TEXT("referenced_by"), Referencers);
	Data->SetNumberField(TEXT("referenced_by_count"), Referencers.Num());

	if (!IsGameContentPath(PackageName))
	{
		TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("unsafe_path"), TEXT("delete_asset is limited to /Game assets; engine and plugin assets are rejected."));
		ErrorObject->SetObjectField(TEXT("data"), Data);
		return ErrorObject;
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("confirmation_required"), TEXT("delete_asset requires confirm=true."));
		ErrorObject->SetObjectField(TEXT("data"), Data);
		return ErrorObject;
	}
	if (Referencers.Num() > 0 && !GetOptionalBool(Arguments, TEXT("allow_referenced"), false))
	{
		TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("asset_referenced"), TEXT("Asset has on-disk referencers. Pass allow_referenced=true only if deletion is intentional."));
		ErrorObject->SetObjectField(TEXT("data"), Data);
		return ErrorObject;
	}

	const int32 DeletedCount = ObjectTools::DeleteObjects({ Asset }, false);
	Data->SetNumberField(TEXT("deleted_count"), DeletedCount);
	Data->SetBoolField(TEXT("deleted"), DeletedCount > 0);
	if (DeletedCount <= 0)
	{
		TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("delete_failed"), FString::Printf(TEXT("Unreal rejected deletion of '%s'."), *AssetPath));
		ErrorObject->SetObjectField(TEXT("data"), Data);
		return ErrorObject;
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSaveAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FString Filename;
	const bool bSaved = SaveAssetPackage(Asset, Filename, Error);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
	Data->SetStringField(TEXT("filename"), Filename);
	Data->SetBoolField(TEXT("saved"), bSaved);
	if (!bSaved)
	{
		return MakeErrorObject(TEXT("save_failed"), Error);
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSaveAllDirty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString PathPrefix = GetOptionalString(Arguments, TEXT("path_prefix"), GetOptionalString(Arguments, TEXT("path")));
	FString Error;
	if (PathPrefix.IsEmpty())
	{
		PathPrefix = TEXT("/Game");
	}
	if (!NormalizeFolderPath(PathPrefix, PathPrefix, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	TArray<TSharedPtr<FJsonValue>> SavedPackages;
	TArray<TSharedPtr<FJsonValue>> FailedPackages;
	int32 AttemptedCount = 0;
	for (TObjectIterator<UPackage> PackageIt; PackageIt; ++PackageIt)
	{
		UPackage* Package = *PackageIt;
		if (!Package || !Package->IsDirty())
		{
			continue;
		}

		const FString PackageName = Package->GetName();
		if (!PackageName.Equals(PathPrefix, ESearchCase::IgnoreCase) && !PackageName.StartsWith(PathPrefix + TEXT("/"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			continue;
		}

		UObject* Asset = FindPackageAsset(Package);
		if (!Asset)
		{
			continue;
		}

		++AttemptedCount;
		FString Filename;
		FString SaveError;
		TSharedRef<FJsonObject> PackageObject = MakeShared<FJsonObject>();
		PackageObject->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		PackageObject->SetStringField(TEXT("package"), PackageName);
		if (SaveAssetPackage(Asset, Filename, SaveError))
		{
			PackageObject->SetStringField(TEXT("filename"), Filename);
			SavedPackages.Add(MakeShared<FJsonValueObject>(PackageObject));
		}
		else
		{
			PackageObject->SetStringField(TEXT("message"), SaveError);
			FailedPackages.Add(MakeShared<FJsonValueObject>(PackageObject));
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path_prefix"), PathPrefix);
	Data->SetNumberField(TEXT("attempted_count"), AttemptedCount);
	Data->SetNumberField(TEXT("saved_count"), SavedPackages.Num());
	Data->SetNumberField(TEXT("failed_count"), FailedPackages.Num());
	Data->SetArrayField(TEXT("saved"), SavedPackages);
	Data->SetArrayField(TEXT("failed"), FailedPackages);
	if (FailedPackages.Num() > 0)
	{
		TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("save_failed"), TEXT("One or more dirty packages failed to save."));
		ErrorObject->SetObjectField(TEXT("data"), Data);
		return ErrorObject;
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDiffAssetAgainstGitRevision(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Revision;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("revision"), Revision, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FText DiffError;
	FString RepoRoot;
	FString RelativeFilename;
	FString TempAssetFilename;
	FGitAssetDiffModule& GitDiffModule = FGitAssetDiffModule::Get();
	const bool bOpened = GitDiffModule.DiffAssetAgainstGitRevision(
		FAssetData(Asset),
		Revision,
		DiffError,
		&RepoRoot,
		&RelativeFilename,
		&TempAssetFilename);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("package"), Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : FString());
	Data->SetStringField(TEXT("revision"), Revision);
	Data->SetStringField(TEXT("repo_root"), RepoRoot);
	Data->SetStringField(TEXT("relative_filename"), RelativeFilename);
	Data->SetStringField(TEXT("temp_asset_filename"), TempAssetFilename);
	Data->SetBoolField(TEXT("opened_diff"), bOpened);
	if (!bOpened)
	{
		TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("git_asset_diff_failed"), DiffError.ToString());
		ErrorObject->SetObjectField(TEXT("data"), Data);
		return ErrorObject;
	}

	return MakeOk(Data);
}

namespace
{
	UDataTable* LoadDataTableAsset(const FString& AssetPath, FString& OutError)
	{
		UObject* Asset = LoadAssetObject(AssetPath, OutError);
		if (!Asset)
		{
			return nullptr;
		}
		UDataTable* DataTable = Cast<UDataTable>(Asset);
		if (!DataTable)
		{
			OutError = FString::Printf(TEXT("Asset '%s' is not a DataTable (class: %s)."), *Asset->GetPathName(), *Asset->GetClass()->GetName());
		}
		return DataTable;
	}

	FProperty* FindDataTableColumn(const UScriptStruct* RowStruct, const FString& ColumnName)
	{
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			FProperty* Property = *It;
			if (Property->GetName().Equals(ColumnName, ESearchCase::IgnoreCase) ||
				Property->GetAuthoredName().Equals(ColumnName, ESearchCase::IgnoreCase))
			{
				return Property;
			}
		}
		return nullptr;
	}

	TSharedRef<FJsonObject> DataTableRowToJson(const UScriptStruct* RowStruct, const uint8* RowData)
	{
		TSharedRef<FJsonObject> RowObject = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			FProperty* Property = *It;
			const TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(Property, Property->ContainerPtrToValuePtr<void>(RowData), 0, 0);
			RowObject->SetField(Property->GetAuthoredName(), Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
		}
		return RowObject;
	}

	bool ApplyJsonValuesToDataTableRow(const UScriptStruct* RowStruct, uint8* RowData, const TSharedPtr<FJsonObject>& Values, TArray<FString>& OutAppliedColumns, FString& OutError)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
		{
			FProperty* Property = FindDataTableColumn(RowStruct, Pair.Key);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("Row struct '%s' has no column '%s'."), *RowStruct->GetName(), *Pair.Key);
				return false;
			}
			if (!FJsonObjectConverter::JsonValueToUProperty(Pair.Value, Property, Property->ContainerPtrToValuePtr<void>(RowData), 0, 0))
			{
				OutError = FString::Printf(TEXT("Could not convert value for column '%s' (type: %s)."), *Property->GetAuthoredName(), *Property->GetCPPType());
				return false;
			}
			OutAppliedColumns.Add(Property->GetAuthoredName());
		}
		return true;
	}

	uint8* FindDataTableRowData(const UDataTable* DataTable, FName RowName)
	{
		uint8* const* Found = DataTable->GetRowMap().Find(RowName);
		return Found ? *Found : nullptr;
	}
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectDataTable(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UDataTable* DataTable = LoadDataTableAsset(AssetPath, Error);
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	TArray<TSharedPtr<FJsonValue>> Columns;
	if (RowStruct)
	{
		for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
		{
			FProperty* Property = *It;
			TSharedRef<FJsonObject> Column = MakeShared<FJsonObject>();
			Column->SetStringField(TEXT("name"), Property->GetAuthoredName());
			if (Property->GetName() != Property->GetAuthoredName())
			{
				Column->SetStringField(TEXT("internal_name"), Property->GetName());
			}
			Column->SetStringField(TEXT("type"), Property->GetCPPType());
			Columns.Add(MakeShared<FJsonValueObject>(Column));
		}
	}

	TArray<TSharedPtr<FJsonValue>> RowNames;
	for (const FName RowName : DataTable->GetRowNames())
	{
		RowNames.Add(MakeShared<FJsonValueString>(RowName.ToString()));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_struct"), RowStruct ? RowStruct->GetName() : FString());
	Data->SetStringField(TEXT("row_struct_path"), RowStruct ? RowStruct->GetPathName() : FString());
	Data->SetArrayField(TEXT("columns"), Columns);
	Data->SetArrayField(TEXT("row_names"), RowNames);
	Data->SetNumberField(TEXT("row_count"), RowNames.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateDataTable(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString RowStructPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("row_struct"), RowStructPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FString PackagePath;
	FString AssetName;
	if (!SplitAssetPackagePath(AssetPath, PackagePath, AssetName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!IsGameContentPath(PackagePath))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("create_datatable is limited to /Game assets."));
	}
	if (ResolveObjectPath(ObjectPathFromPackagePath(FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName))))
	{
		return MakeErrorObject(TEXT("asset_exists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *PackagePath, *AssetName));
	}

	// A full path (/Script/Module.Struct) is exact; a bare name is looked up, for structs whose module is obvious.
	const FString StructName = StripObjectExportWrapper(RowStructPath);
	UScriptStruct* RowStruct = StructName.StartsWith(TEXT("/"))
		? LoadObject<UScriptStruct>(nullptr, *StructName)
		: FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::NativeFirst);
	if (!RowStruct)
	{
		return MakeErrorObject(TEXT("struct_not_found"), FString::Printf(TEXT("Row struct '%s' was not found."), *RowStructPath));
	}
	if (!RowStruct->IsChildOf(FTableRowBase::StaticStruct()))
	{
		return MakeErrorObject(TEXT("invalid_row_struct"), FString::Printf(TEXT("'%s' does not derive from FTableRowBase."), *RowStruct->GetPathName()));
	}

	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = RowStruct;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UDataTable* DataTable = Cast<UDataTable>(AssetTools.CreateAsset(AssetName, PackagePath, UDataTable::StaticClass(), Factory));
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("create_failed"), FString::Printf(TEXT("Failed to create DataTable '%s/%s'."), *PackagePath, *AssetName));
	}
	DataTable->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_struct_path"), RowStruct->GetPathName());
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		const bool bSaved = SaveAssetPackage(DataTable, Filename, Error);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			return MakeErrorObject(TEXT("save_failed"), Error);
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGetDataTableRows(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UDataTable* DataTable = LoadDataTableAsset(AssetPath, Error);
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MakeErrorObject(TEXT("invalid_datatable"), FString::Printf(TEXT("DataTable '%s' has no row struct."), *DataTable->GetPathName()));
	}

	TArray<FName> RequestedRows;
	const TArray<TSharedPtr<FJsonValue>>* RowNamesArray = nullptr;
	if (TryGetArray(Arguments, TEXT("row_names"), RowNamesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RowNamesArray)
		{
			FString RowNameString;
			if (Value.IsValid() && Value->TryGetString(RowNameString) && !RowNameString.IsEmpty())
			{
				RequestedRows.Add(FName(*RowNameString));
			}
		}
	}
	else if (const FString SingleRowName = GetOptionalString(Arguments, TEXT("row_name")); !SingleRowName.IsEmpty())
	{
		RequestedRows.Add(FName(*SingleRowName));
	}

	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 100), 1, 5000);
	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<TSharedPtr<FJsonValue>> MissingRows;
	if (RequestedRows.Num() > 0)
	{
		for (const FName RowName : RequestedRows)
		{
			const uint8* RowData = FindDataTableRowData(DataTable, RowName);
			if (!RowData)
			{
				MissingRows.Add(MakeShared<FJsonValueString>(RowName.ToString()));
				continue;
			}
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("row_name"), RowName.ToString());
			Row->SetObjectField(TEXT("values"), DataTableRowToJson(RowStruct, RowData));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	else
	{
		for (const TPair<FName, uint8*>& Pair : DataTable->GetRowMap())
		{
			if (Rows.Num() >= Limit)
			{
				break;
			}
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("row_name"), Pair.Key.ToString());
			Row->SetObjectField(TEXT("values"), DataTableRowToJson(RowStruct, Pair.Value));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_struct"), RowStruct->GetName());
	Data->SetArrayField(TEXT("rows"), Rows);
	Data->SetNumberField(TEXT("count"), Rows.Num());
	Data->SetNumberField(TEXT("total_row_count"), DataTable->GetRowMap().Num());
	if (MissingRows.Num() > 0)
	{
		Data->SetArrayField(TEXT("missing_rows"), MissingRows);
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetDataTableRow(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString RowNameString;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("row_name"), RowNameString, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	TSharedPtr<FJsonObject> Values;
	if (!TryGetObject(Arguments, TEXT("values"), Values) || Values->Values.Num() == 0)
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Expected a non-empty 'values' object."));
	}

	UDataTable* DataTable = LoadDataTableAsset(AssetPath, Error);
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!IsGameContentPath(DataTable->GetOutermost()->GetName()))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("set_datatable_row is limited to /Game assets."));
	}
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MakeErrorObject(TEXT("invalid_datatable"), FString::Printf(TEXT("DataTable '%s' has no row struct."), *DataTable->GetPathName()));
	}

	const FName RowName(*RowNameString);
	uint8* RowData = FindDataTableRowData(DataTable, RowName);
	bool bCreated = false;
	if (!RowData)
	{
		if (!GetOptionalBool(Arguments, TEXT("create_if_missing"), false))
		{
			return MakeErrorObject(TEXT("row_not_found"), FString::Printf(TEXT("Row '%s' does not exist in '%s'. Pass create_if_missing=true to add it."), *RowNameString, *DataTable->GetPathName()));
		}
		RowData = FDataTableEditorUtils::AddRow(DataTable, RowName);
		if (!RowData)
		{
			return MakeErrorObject(TEXT("add_failed"), FString::Printf(TEXT("Failed to add row '%s' to '%s'."), *RowNameString, *DataTable->GetPathName()));
		}
		bCreated = true;
	}

	FDataTableEditorUtils::BroadcastPreChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
	DataTable->Modify();
	TArray<FString> AppliedColumns;
	const bool bApplied = ApplyJsonValuesToDataTableRow(RowStruct, RowData, Values, AppliedColumns, Error);
	DataTable->HandleDataTableChanged(RowName);
	DataTable->MarkPackageDirty();
	FDataTableEditorUtils::BroadcastPostChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
	if (!bApplied)
	{
		return MakeErrorObject(TEXT("invalid_value"), Error);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_name"), RowNameString);
	Data->SetBoolField(TEXT("created"), bCreated);
	Data->SetArrayField(TEXT("applied_columns"), StringArrayToJsonArray(AppliedColumns));
	Data->SetObjectField(TEXT("values"), DataTableRowToJson(RowStruct, RowData));
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		const bool bSaved = SaveAssetPackage(DataTable, Filename, Error);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			return MakeErrorObject(TEXT("save_failed"), Error);
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddDataTableRow(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString RowNameString;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("row_name"), RowNameString, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UDataTable* DataTable = LoadDataTableAsset(AssetPath, Error);
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!IsGameContentPath(DataTable->GetOutermost()->GetName()))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("add_datatable_row is limited to /Game assets."));
	}
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return MakeErrorObject(TEXT("invalid_datatable"), FString::Printf(TEXT("DataTable '%s' has no row struct."), *DataTable->GetPathName()));
	}

	const FName RowName(*RowNameString);
	if (FindDataTableRowData(DataTable, RowName))
	{
		return MakeErrorObject(TEXT("row_exists"), FString::Printf(TEXT("Row '%s' already exists in '%s'. Use set_datatable_row to modify it."), *RowNameString, *DataTable->GetPathName()));
	}

	uint8* RowData = FDataTableEditorUtils::AddRow(DataTable, RowName);
	if (!RowData)
	{
		return MakeErrorObject(TEXT("add_failed"), FString::Printf(TEXT("Failed to add row '%s' to '%s'."), *RowNameString, *DataTable->GetPathName()));
	}

	TArray<FString> AppliedColumns;
	TSharedPtr<FJsonObject> Values;
	if (TryGetObject(Arguments, TEXT("values"), Values) && Values->Values.Num() > 0)
	{
		FDataTableEditorUtils::BroadcastPreChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
		DataTable->Modify();
		const bool bApplied = ApplyJsonValuesToDataTableRow(RowStruct, RowData, Values, AppliedColumns, Error);
		DataTable->HandleDataTableChanged(RowName);
		FDataTableEditorUtils::BroadcastPostChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
		if (!bApplied)
		{
			return MakeErrorObject(TEXT("invalid_value"), Error);
		}
	}
	DataTable->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_name"), RowNameString);
	Data->SetArrayField(TEXT("applied_columns"), StringArrayToJsonArray(AppliedColumns));
	Data->SetObjectField(TEXT("values"), DataTableRowToJson(RowStruct, RowData));
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		const bool bSaved = SaveAssetPackage(DataTable, Filename, Error);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			return MakeErrorObject(TEXT("save_failed"), Error);
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveDataTableRow(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString RowNameString;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("row_name"), RowNameString, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UDataTable* DataTable = LoadDataTableAsset(AssetPath, Error);
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!IsGameContentPath(DataTable->GetOutermost()->GetName()))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("remove_datatable_row is limited to /Game assets."));
	}

	const FName RowName(*RowNameString);
	if (!FindDataTableRowData(DataTable, RowName))
	{
		return MakeErrorObject(TEXT("row_not_found"), FString::Printf(TEXT("Row '%s' does not exist in '%s'."), *RowNameString, *DataTable->GetPathName()));
	}
	if (!FDataTableEditorUtils::RemoveRow(DataTable, RowName))
	{
		return MakeErrorObject(TEXT("remove_failed"), FString::Printf(TEXT("Failed to remove row '%s' from '%s'."), *RowNameString, *DataTable->GetPathName()));
	}
	DataTable->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_name"), RowNameString);
	Data->SetBoolField(TEXT("removed"), true);
	Data->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		const bool bSaved = SaveAssetPackage(DataTable, Filename, Error);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			return MakeErrorObject(TEXT("save_failed"), Error);
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRenameDataTableRow(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString RowNameString;
	FString NewRowNameString;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("row_name"), RowNameString, Error) ||
		!RequireString(Arguments, TEXT("new_row_name"), NewRowNameString, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UDataTable* DataTable = LoadDataTableAsset(AssetPath, Error);
	if (!DataTable)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!IsGameContentPath(DataTable->GetOutermost()->GetName()))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("rename_datatable_row is limited to /Game assets."));
	}

	const FName RowName(*RowNameString);
	const FName NewRowName(*NewRowNameString);
	if (!FindDataTableRowData(DataTable, RowName))
	{
		return MakeErrorObject(TEXT("row_not_found"), FString::Printf(TEXT("Row '%s' does not exist in '%s'."), *RowNameString, *DataTable->GetPathName()));
	}
	if (FindDataTableRowData(DataTable, NewRowName))
	{
		return MakeErrorObject(TEXT("row_exists"), FString::Printf(TEXT("Row '%s' already exists in '%s'."), *NewRowNameString, *DataTable->GetPathName()));
	}
	if (!FDataTableEditorUtils::RenameRow(DataTable, RowName, NewRowName))
	{
		return MakeErrorObject(TEXT("rename_failed"), FString::Printf(TEXT("Failed to rename row '%s' to '%s' in '%s'."), *RowNameString, *NewRowNameString, *DataTable->GetPathName()));
	}
	DataTable->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("old_row_name"), RowNameString);
	Data->SetStringField(TEXT("row_name"), NewRowNameString);
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		const bool bSaved = SaveAssetPackage(DataTable, Filename, Error);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			return MakeErrorObject(TEXT("save_failed"), Error);
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectMaterial(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const bool bIncludeExpressions = GetOptionalBool(Arguments, TEXT("include_expressions"), true);
	const bool bIncludeExpressionProperties = GetOptionalBool(Arguments, TEXT("include_expression_properties"), false);
	const bool bIncludeMaterialProperties = GetOptionalBool(Arguments, TEXT("include_material_properties"), false);
	return MakeOk(MaterialToJson(Material, bIncludeExpressions, bIncludeExpressionProperties, bIncludeMaterialProperties));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectMaterialFunction(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UMaterialFunctionInterface* MaterialFunction = Cast<UMaterialFunctionInterface>(Asset);
	if (!MaterialFunction)
	{
		return MakeErrorObject(TEXT("load_failed"), FString::Printf(TEXT("Asset '%s' is not a UMaterialFunctionInterface."), *AssetPath));
	}

	const bool bIncludeExpressions = GetOptionalBool(Arguments, TEXT("include_expressions"), true);
	const bool bIncludeExpressionProperties = GetOptionalBool(Arguments, TEXT("include_expression_properties"), false);
	const bool bIncludeFunctionProperties = GetOptionalBool(Arguments, TEXT("include_function_properties"), false);
	return MakeOk(MaterialFunctionToJson(MaterialFunction, bIncludeExpressions, bIncludeExpressionProperties, bIncludeFunctionProperties));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetupOcclusionMaskFunction(const TSharedPtr<FJsonObject>& Arguments)
{
	FString assetPath;
	FString error;
	if (!RequireString(Arguments, TEXT("asset_path"), assetPath, error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), error);
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("setup_occlusion_mask_function clears and rebuilds the material function; pass confirm=true."));
	}

	UObject* asset = LoadAssetObject(assetPath, error);
	if (!asset)
	{
		return MakeErrorObject(TEXT("load_failed"), error);
	}

	UMaterialFunction* materialFunction = Cast<UMaterialFunction>(asset);
	if (!materialFunction)
	{
		return MakeErrorObject(TEXT("load_failed"), FString::Printf(TEXT("Asset '%s' is not a UMaterialFunction."), *assetPath));
	}

	const FString worldPositionInputName = GetOptionalString(Arguments, TEXT("world_position_input"), TEXT("WorldPosition"));
	const FString playerLocationInputName = GetOptionalString(Arguments, TEXT("player_location_input"), TEXT("PlayerLocation"));
	const FString radiusInputName = GetOptionalString(Arguments, TEXT("radius_input"), TEXT("CutoutRadius"));
	const FString hardnessInputName = GetOptionalString(Arguments, TEXT("hardness_input"), TEXT("CutoutHardness"));
	const FString outputName = GetOptionalString(Arguments, TEXT("output_name"), TEXT("OpacityMask"));
	const float radius = static_cast<float>(GetOptionalInt(Arguments, TEXT("radius"), 220));
	const float hardness = static_cast<float>(GetOptionalInt(Arguments, TEXT("hardness"), 35));

	const FScopedTransaction transaction(NSLOCTEXT("LLMBlueprintBridge", "SetupOcclusionMaskFunction", "LLM Setup Occlusion Mask Function"));
	materialFunction->Modify();
	while (materialFunction->GetExpressions().Num() > 0)
	{
		TArray<UMaterialExpression*> expressionsToDelete;
		for (UMaterialExpression* expression : materialFunction->GetExpressions())
		{
			expressionsToDelete.Add(expression);
		}

		const int32 expressionCountBeforeDelete = materialFunction->GetExpressions().Num();
		for (UMaterialExpression* expression : expressionsToDelete)
		{
			UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(materialFunction, expression);
		}

		if (materialFunction->GetExpressions().Num() >= expressionCountBeforeDelete)
		{
			return MakeErrorObject(TEXT("clear_failed"), TEXT("Could not clear existing material function expressions."));
		}
	}

	UMaterialExpressionFunctionInput* worldPositionInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -980, -180));
	UMaterialExpressionFunctionInput* playerLocationInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -980, 20));
	UMaterialExpressionFunctionInput* radiusInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -980, 220));
	UMaterialExpressionFunctionInput* hardnessInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -980, 420));
	UMaterialExpressionSphereMask* sphereMask = Cast<UMaterialExpressionSphereMask>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionSphereMask::StaticClass(), -560, 70));
	UMaterialExpressionOneMinus* oneMinus = Cast<UMaterialExpressionOneMinus>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionOneMinus::StaticClass(), -300, 70));
	UMaterialExpressionSaturate* saturate = Cast<UMaterialExpressionSaturate>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionSaturate::StaticClass(), -80, 70));
	UMaterialExpressionFunctionOutput* functionOutput = Cast<UMaterialExpressionFunctionOutput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionOutput::StaticClass(), 180, 70));

	if (!worldPositionInput || !playerLocationInput || !radiusInput || !hardnessInput || !sphereMask || !oneMinus || !saturate || !functionOutput)
	{
		return MakeErrorObject(TEXT("create_failed"), TEXT("Failed to create one or more occlusion mask function expressions."));
	}

	auto configureInput = [](UMaterialExpressionFunctionInput* input,
	                         const FString& inputName,
	                         const EFunctionInputType inputType,
	                         const FVector4f& previewValue,
	                         const int32 sortPriority)
	{
		input->InputName = FName(*inputName);
		input->InputType = inputType;
		input->PreviewValue = previewValue;
		input->bUsePreviewValueAsDefault = true;
		input->SortPriority = sortPriority;
		input->ConditionallyGenerateId(true);
		input->ValidateName();
	};

	configureInput(worldPositionInput, worldPositionInputName, FunctionInput_Vector3, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 0);
	configureInput(playerLocationInput, playerLocationInputName, FunctionInput_Vector3, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 1);
	configureInput(radiusInput, radiusInputName, FunctionInput_Scalar, FVector4f(radius, 0.0f, 0.0f, 0.0f), 2);
	configureInput(hardnessInput, hardnessInputName, FunctionInput_Scalar, FVector4f(hardness, 0.0f, 0.0f, 0.0f), 3);

	functionOutput->OutputName = FName(*outputName);
	functionOutput->SortPriority = 0;
	functionOutput->ConditionallyGenerateId(true);
	functionOutput->ValidateName();

	auto connectExpressionInput = [](UMaterialExpression* fromExpression,
	                                 UMaterialExpression* toExpression,
	                                 const int32 toInputIndex,
	                                 FString& outError) -> bool
	{
		FExpressionInput* input = toExpression ? toExpression->GetInput(toInputIndex) : nullptr;
		if (fromExpression && input)
		{
			input->Connect(0, fromExpression);
			return true;
		}

		outError = FString::Printf(TEXT("Could not connect %s to %s input %d."),
		                           *GetNameSafe(fromExpression),
		                           *GetNameSafe(toExpression),
		                           toInputIndex);
		return false;
	};

	if (!connectExpressionInput(worldPositionInput, sphereMask, 0, error) ||
	    !connectExpressionInput(playerLocationInput, sphereMask, 1, error) ||
	    !connectExpressionInput(radiusInput, sphereMask, 2, error) ||
	    !connectExpressionInput(hardnessInput, sphereMask, 3, error) ||
	    !connectExpressionInput(sphereMask, oneMinus, 0, error) ||
	    !connectExpressionInput(oneMinus, saturate, 0, error) ||
	    !connectExpressionInput(saturate, functionOutput, 0, error))
	{
		return MakeErrorObject(TEXT("connection_failed"), error);
	}

	UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(materialFunction);
	UMaterialEditingLibrary::UpdateMaterialFunction(materialFunction);

	TSharedRef<FJsonObject> data = MaterialFunctionToJson(materialFunction, true, true, false);
	data->SetBoolField(TEXT("configured"), true);
	data->SetStringField(TEXT("world_position_input"), worldPositionInputName);
	data->SetStringField(TEXT("player_location_input"), playerLocationInputName);
	data->SetStringField(TEXT("radius_input"), radiusInputName);
	data->SetStringField(TEXT("hardness_input"), hardnessInputName);
	data->SetStringField(TEXT("output_name"), outputName);

	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString filename;
		FString saveError;
		const bool bSaved = SaveAssetPackage(materialFunction, filename, saveError);
		data->SetStringField(TEXT("filename"), filename);
		data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			TSharedRef<FJsonObject> errorObject = MakeErrorObject(TEXT("save_failed"), saveError);
			errorObject->SetObjectField(TEXT("data"), data);
			return errorObject;
		}
	}

	return MakeOk(data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetupTexturedOcclusionMaskFunction(const TSharedPtr<FJsonObject>& Arguments)
{
	FString assetPath;
	FString error;
	if (!RequireString(Arguments, TEXT("asset_path"), assetPath, error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), error);
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("setup_textured_occlusion_mask_function clears and rebuilds the material function; pass confirm=true."));
	}

	UObject* asset = LoadAssetObject(assetPath, error);
	if (!asset)
	{
		return MakeErrorObject(TEXT("load_failed"), error);
	}

	UMaterialFunction* materialFunction = Cast<UMaterialFunction>(asset);
	if (!materialFunction)
	{
		return MakeErrorObject(TEXT("load_failed"), FString::Printf(TEXT("Asset '%s' is not a UMaterialFunction."), *assetPath));
	}

	const FString worldPositionInputName = GetOptionalString(Arguments, TEXT("world_position_input"), TEXT("WorldPosition"));
	const FString playerLocationInputName = GetOptionalString(Arguments, TEXT("player_location_input"), TEXT("PlayerLocation"));
	const FString radiusInputName = GetOptionalString(Arguments, TEXT("radius_input"), TEXT("CutoutRadius"));
	const FString hardnessInputName = GetOptionalString(Arguments, TEXT("hardness_input"), TEXT("CutoutHardness"));
	const FString noiseStrengthInputName = GetOptionalString(Arguments, TEXT("noise_strength_input"), TEXT("NoiseStrength"));
	const FString noiseSizeInputName = GetOptionalString(Arguments, TEXT("noise_size_input"), TEXT("NoiseSize"));
	const FString speedInputName = GetOptionalString(Arguments, TEXT("speed_input"), TEXT("Speed"));
	const FString outputName = GetOptionalString(Arguments, TEXT("output_name"), TEXT("OpacityMask"));
	const FString noiseTexturePath = GetOptionalString(Arguments, TEXT("noise_texture"), TEXT("/DatasmithContent/Materials/Water/Textures/T_Perlin_Mask.T_Perlin_Mask"));

	const float radius = static_cast<float>(GetOptionalInt(Arguments, TEXT("radius"), 220));
	const float hardness = static_cast<float>(GetOptionalInt(Arguments, TEXT("hardness"), 35));
	const float noiseStrength = static_cast<float>(GetOptionalInt(Arguments, TEXT("noise_strength_percent"), 25)) / 100.0f;
	const float noiseSize = static_cast<float>(GetOptionalInt(Arguments, TEXT("noise_size"), 1));
	const float speed = static_cast<float>(GetOptionalInt(Arguments, TEXT("speed"), 1));

	const FScopedTransaction transaction(NSLOCTEXT("LLMBlueprintBridge", "SetupTexturedOcclusionMaskFunction", "LLM Setup Textured Occlusion Mask Function"));
	materialFunction->Modify();
	while (materialFunction->GetExpressions().Num() > 0)
	{
		TArray<UMaterialExpression*> expressionsToDelete;
		for (UMaterialExpression* expression : materialFunction->GetExpressions())
		{
			expressionsToDelete.Add(expression);
		}

		const int32 expressionCountBeforeDelete = materialFunction->GetExpressions().Num();
		for (UMaterialExpression* expression : expressionsToDelete)
		{
			UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(materialFunction, expression);
		}

		if (materialFunction->GetExpressions().Num() >= expressionCountBeforeDelete)
		{
			return MakeErrorObject(TEXT("clear_failed"), TEXT("Could not clear existing material function expressions."));
		}
	}

	UMaterialExpressionFunctionInput* worldPositionInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, -260));
	UMaterialExpressionFunctionInput* playerLocationInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, -80));
	UMaterialExpressionFunctionInput* radiusInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, 100));
	UMaterialExpressionFunctionInput* hardnessInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, 280));
	UMaterialExpressionFunctionInput* noiseStrengthInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, 520));
	UMaterialExpressionFunctionInput* noiseSizeInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, 700));
	UMaterialExpressionFunctionInput* speedInput = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionInput::StaticClass(), -1280, 880));

	UMaterialExpressionSphereMask* sphereMask = Cast<UMaterialExpressionSphereMask>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionSphereMask::StaticClass(), -840, -80));
	UMaterialExpressionOneMinus* baseVisibleMask = Cast<UMaterialExpressionOneMinus>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionOneMinus::StaticClass(), -560, -80));
	UMaterialExpressionTextureCoordinate* textureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionTextureCoordinate::StaticClass(), -1040, 620));
	UMaterialExpressionMultiply* scaledUv = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionMultiply::StaticClass(), -840, 660));
	UMaterialExpressionPanner* panner = Cast<UMaterialExpressionPanner>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionPanner::StaticClass(), -620, 660));
	UMaterialExpressionTextureObjectParameter* noiseTexture = Cast<UMaterialExpressionTextureObjectParameter>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionTextureObjectParameter::StaticClass(), -620, 860));
	UMaterialExpressionTextureSample* textureSample = Cast<UMaterialExpressionTextureSample>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionTextureSample::StaticClass(), -360, 680));
	UMaterialExpressionSubtract* centeredNoise = Cast<UMaterialExpressionSubtract>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionSubtract::StaticClass(), -120, 680));
	UMaterialExpressionMultiply* weightedNoise = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionMultiply::StaticClass(), 100, 620));
	UMaterialExpressionAdd* noisyVisibleMask = Cast<UMaterialExpressionAdd>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionAdd::StaticClass(), 340, 240));
	UMaterialExpressionSaturate* saturate = Cast<UMaterialExpressionSaturate>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionSaturate::StaticClass(), 580, 240));
	UMaterialExpressionFunctionOutput* functionOutput = Cast<UMaterialExpressionFunctionOutput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(materialFunction, UMaterialExpressionFunctionOutput::StaticClass(), 820, 240));

	if (!worldPositionInput || !playerLocationInput || !radiusInput || !hardnessInput ||
	    !noiseStrengthInput || !noiseSizeInput || !speedInput || !sphereMask || !baseVisibleMask ||
	    !textureCoordinate || !scaledUv || !panner || !noiseTexture || !textureSample || !centeredNoise ||
	    !weightedNoise || !noisyVisibleMask || !saturate || !functionOutput)
	{
		return MakeErrorObject(TEXT("create_failed"), TEXT("Failed to create one or more textured occlusion mask function expressions."));
	}

	auto configureInput = [](UMaterialExpressionFunctionInput* input,
	                         const FString& inputName,
	                         const EFunctionInputType inputType,
	                         const FVector4f& previewValue,
	                         const int32 sortPriority)
	{
		input->InputName = FName(*inputName);
		input->InputType = inputType;
		input->PreviewValue = previewValue;
		input->bUsePreviewValueAsDefault = true;
		input->SortPriority = sortPriority;
		input->ConditionallyGenerateId(true);
		input->ValidateName();
	};

	configureInput(worldPositionInput, worldPositionInputName, FunctionInput_Vector3, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 0);
	configureInput(playerLocationInput, playerLocationInputName, FunctionInput_Vector3, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 1);
	configureInput(radiusInput, radiusInputName, FunctionInput_Scalar, FVector4f(radius, 0.0f, 0.0f, 0.0f), 2);
	configureInput(hardnessInput, hardnessInputName, FunctionInput_Scalar, FVector4f(hardness, 0.0f, 0.0f, 0.0f), 3);
	configureInput(noiseStrengthInput, noiseStrengthInputName, FunctionInput_Scalar, FVector4f(noiseStrength, 0.0f, 0.0f, 0.0f), 4);
	configureInput(noiseSizeInput, noiseSizeInputName, FunctionInput_Scalar, FVector4f(noiseSize, 0.0f, 0.0f, 0.0f), 5);
	configureInput(speedInput, speedInputName, FunctionInput_Scalar, FVector4f(speed, 0.0f, 0.0f, 0.0f), 6);

	auto setNoiseSamplerType = [noiseTexture, textureSample]()
	{
		noiseTexture->SamplerType = SAMPLERTYPE_LinearGrayscale;
		textureSample->SamplerType = SAMPLERTYPE_LinearGrayscale;
	};

	noiseTexture->ParameterName = TEXT("Noise");
	noiseTexture->UpdateParameterGuid(true, true);
	noiseTexture->ValidateParameterName(false);
	if (UTexture* texture = Cast<UTexture>(ResolveObjectReference(noiseTexturePath, UTexture::StaticClass())))
	{
		noiseTexture->Texture = texture;
		textureSample->Texture = texture;
	}
	setNoiseSamplerType();

	centeredNoise->ConstB = 0.5f;
	functionOutput->OutputName = FName(*outputName);
	functionOutput->SortPriority = 0;
	functionOutput->ConditionallyGenerateId(true);
	functionOutput->ValidateName();

	auto connectExpressionInput = [](UMaterialExpression* fromExpression,
	                                 UMaterialExpression* toExpression,
	                                 const int32 toInputIndex,
	                                 FString& outError,
	                                 const int32 fromOutputIndex = 0) -> bool
	{
		FExpressionInput* input = toExpression ? toExpression->GetInput(toInputIndex) : nullptr;
		if (fromExpression && input)
		{
			input->Connect(fromOutputIndex, fromExpression);
			return true;
		}

		outError = FString::Printf(TEXT("Could not connect %s output %d to %s input %d."),
		                           *GetNameSafe(fromExpression),
		                           fromOutputIndex,
		                           *GetNameSafe(toExpression),
		                           toInputIndex);
		return false;
	};

	if (!connectExpressionInput(worldPositionInput, sphereMask, 0, error) ||
	    !connectExpressionInput(playerLocationInput, sphereMask, 1, error) ||
	    !connectExpressionInput(radiusInput, sphereMask, 2, error) ||
	    !connectExpressionInput(hardnessInput, sphereMask, 3, error) ||
	    !connectExpressionInput(sphereMask, baseVisibleMask, 0, error) ||
	    !connectExpressionInput(textureCoordinate, scaledUv, 0, error) ||
	    !connectExpressionInput(noiseSizeInput, scaledUv, 1, error) ||
	    !connectExpressionInput(scaledUv, panner, 0, error) ||
	    !connectExpressionInput(speedInput, panner, 2, error) ||
	    !connectExpressionInput(panner, textureSample, 0, error) ||
	    !connectExpressionInput(noiseTexture, textureSample, 1, error) ||
	    !connectExpressionInput(textureSample, centeredNoise, 0, error, 1) ||
	    !connectExpressionInput(centeredNoise, weightedNoise, 0, error) ||
	    !connectExpressionInput(noiseStrengthInput, weightedNoise, 1, error) ||
	    !connectExpressionInput(baseVisibleMask, noisyVisibleMask, 0, error) ||
	    !connectExpressionInput(weightedNoise, noisyVisibleMask, 1, error) ||
	    !connectExpressionInput(noisyVisibleMask, saturate, 0, error) ||
	    !connectExpressionInput(saturate, functionOutput, 0, error))
	{
		return MakeErrorObject(TEXT("connection_failed"), error);
	}

	UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(materialFunction);
	UMaterialEditingLibrary::UpdateMaterialFunction(materialFunction);
	setNoiseSamplerType();
	materialFunction->MarkPackageDirty();

	TSharedRef<FJsonObject> data = MaterialFunctionToJson(materialFunction, true, true, false);
	data->SetBoolField(TEXT("configured"), true);
	data->SetStringField(TEXT("world_position_input"), worldPositionInputName);
	data->SetStringField(TEXT("player_location_input"), playerLocationInputName);
	data->SetStringField(TEXT("radius_input"), radiusInputName);
	data->SetStringField(TEXT("hardness_input"), hardnessInputName);
	data->SetStringField(TEXT("noise_strength_input"), noiseStrengthInputName);
	data->SetStringField(TEXT("noise_size_input"), noiseSizeInputName);
	data->SetStringField(TEXT("speed_input"), speedInputName);
	data->SetStringField(TEXT("noise_texture"), noiseTexturePath);
	data->SetStringField(TEXT("output_name"), outputName);

	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString filename;
		FString saveError;
		const bool bSaved = SaveAssetPackage(materialFunction, filename, saveError);
		data->SetStringField(TEXT("filename"), filename);
		data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			TSharedRef<FJsonObject> errorObject = MakeErrorObject(TEXT("save_failed"), saveError);
			errorObject->SetObjectField(TEXT("data"), data);
			return errorObject;
		}
	}

	return MakeOk(data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetMaterialProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString PropertyPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	PropertyPath = GetOptionalString(Arguments, TEXT("property_path"), GetOptionalString(Arguments, TEXT("property")));
	if (PropertyPath.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required string field 'property' or 'property_path'."));
	}

	const TSharedPtr<FJsonValue> Value = Arguments.IsValid() ? Arguments->TryGetField(TEXT("value")) : nullptr;
	if (!Value.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'value'."));
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	TSharedPtr<FJsonObject> PropertyJson;
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetMaterialProperty", "LLM Set Material Property"));
	if (!SetEditablePropertyValue(Material, Material, PropertyPath, Value, Error, &PropertyJson))
	{
		return MakeErrorObject(TEXT("unsupported_property_type"), Error);
	}

	// UMaterial derives cached state from these fields in PostEditChangeProperty - the shading
	// model bitfield from ShadingModel, for one. Writing the field and going straight to a
	// recompile leaves the derived state describing the old material.
	NotifyPropertyChanged(Material, PropertyPath);

	const bool bRecompile = GetOptionalBool(Arguments, TEXT("recompile"), true);
	if (bRecompile)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
	}

	TSharedRef<FJsonObject> Data = MaterialToJson(Material, false, false, false);
	Data->SetObjectField(TEXT("property"), PropertyJson.IsValid() ? PropertyJson.ToSharedRef() : MakeShared<FJsonObject>());
	Data->SetBoolField(TEXT("recompiled"), bRecompile);

	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		FString SaveError;
		const bool bSaved = SaveAssetPackage(Material, Filename, SaveError);
		Data->SetStringField(TEXT("filename"), Filename);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("save_failed"), SaveError);
			ErrorObject->SetObjectField(TEXT("data"), Data);
			return ErrorObject;
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateMaterialExpression(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ClassPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("class_path"), ClassPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UClass* ExpressionClass = ResolveMaterialExpressionClass(ClassPath);
	if (!ExpressionClass || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
	{
		return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve material expression class '%s'."), *ClassPath));
	}

	UObject* SelectedAsset = nullptr;
	const FString SelectedAssetPath = GetOptionalString(Arguments, TEXT("selected_asset"));
	if (!SelectedAssetPath.IsEmpty())
	{
		SelectedAsset = ResolveObjectReference(SelectedAssetPath, UObject::StaticClass());
		if (!SelectedAsset)
		{
			return MakeErrorObject(TEXT("load_failed"), FString::Printf(TEXT("Could not load selected_asset '%s'."), *SelectedAssetPath));
		}
	}

	const int32 X = GetOptionalInt(Arguments, TEXT("x"), 0);
	const int32 Y = GetOptionalInt(Arguments, TEXT("y"), 0);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "CreateMaterialExpression", "LLM Create Material Expression"));
	Material->Modify();
	UMaterialExpression* Expression = UMaterialEditingLibrary::CreateMaterialExpressionEx(Material, nullptr, ExpressionClass, SelectedAsset, X, Y);
	if (!Expression)
	{
		return MakeErrorObject(TEXT("create_failed"), FString::Printf(TEXT("Failed to create material expression '%s'."), *ClassPath));
	}

	TSharedPtr<FJsonObject> Properties;
	if (TryGetObject(Arguments, TEXT("properties"), Properties) && !ApplyPropertyPatch(Material, Expression, Properties, Error))
	{
		return MakeErrorObject(TEXT("unsupported_property_type"), Error);
	}

	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
	Data->SetObjectField(TEXT("expression"), MaterialExpressionToJson(Expression, true));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ExpressionId;
	FString PropertyPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("expression"), ExpressionId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	PropertyPath = GetOptionalString(Arguments, TEXT("property_path"), GetOptionalString(Arguments, TEXT("property")));
	if (PropertyPath.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required string field 'property' or 'property_path'."));
	}

	const TSharedPtr<FJsonValue> Value = Arguments.IsValid() ? Arguments->TryGetField(TEXT("value")) : nullptr;
	if (!Value.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'value'."));
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	UMaterialExpression* Expression = FindMaterialExpression(Material, ExpressionId, Error);
	if (!Expression)
	{
		return MakeErrorObject(TEXT("expression_not_found"), Error);
	}

	TSharedPtr<FJsonObject> PropertyJson;
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetMaterialExpressionProperty", "LLM Set Material Expression Property"));
	if (!SetEditablePropertyValue(Material, Expression, PropertyPath, Value, Error, &PropertyJson))
	{
		return MakeErrorObject(TEXT("unsupported_property_type"), Error);
	}

	NotifyPropertyChanged(Expression, PropertyPath);

	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
	Data->SetObjectField(TEXT("expression"), MaterialExpressionToJson(Expression, true));
	Data->SetObjectField(TEXT("property"), PropertyJson.IsValid() ? PropertyJson.ToSharedRef() : MakeShared<FJsonObject>());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString FromExpressionId;
	FString ToExpressionId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("from_expression"), FromExpressionId, Error) ||
		!RequireString(Arguments, TEXT("to_expression"), ToExpressionId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UMaterialExpression* FromExpression = FindMaterialExpression(Material, FromExpressionId, Error);
	if (!FromExpression)
	{
		return MakeErrorObject(TEXT("expression_not_found"), Error);
	}
	UMaterialExpression* ToExpression = FindMaterialExpression(Material, ToExpressionId, Error);
	if (!ToExpression)
	{
		return MakeErrorObject(TEXT("expression_not_found"), Error);
	}

	const FString FromOutput = GetOptionalString(Arguments, TEXT("from_output"));
	const FString ToInput = GetOptionalString(Arguments, TEXT("to_input"));
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "ConnectMaterialExpressions", "LLM Connect Material Expressions"));
	Material->Modify();
	FromExpression->Modify();
	ToExpression->Modify();

	// SetMaterialAttributes grows a pin and an entry in AttributeSetTypes together, and only its
	// own ConnectInputAttribute keeps the two in step. Writing the array directly leaves the pin
	// list one short and the compiler reads past the end of it, so the attribute name is routed
	// here instead of through the generic by-name connect.
	bool bConnected = false;
	bool bHandledAsAttribute = false;
	if (UMaterialExpressionSetMaterialAttributes* SetAttributes = Cast<UMaterialExpressionSetMaterialAttributes>(ToExpression))
	{
		if (!ToInput.IsEmpty())
		{
			for (const TPair<FString, EMaterialProperty>& KnownProperty : KnownMaterialProperties())
			{
				if (ToInput.Equals(KnownProperty.Key, ESearchCase::IgnoreCase))
				{
					const int32 OutputIndex = FMath::Max(0, FromExpression->GetOutputs().IndexOfByPredicate(
						[&FromOutput](const FExpressionOutput& Output)
						{
							return FromOutput.IsEmpty() || Output.OutputName.ToString().Equals(FromOutput, ESearchCase::IgnoreCase);
						}));

					bConnected = SetAttributes->ConnectInputAttribute(KnownProperty.Value, FromExpression, OutputIndex);
					bHandledAsAttribute = true;
					break;
				}
			}
		}
	}

	if (!bHandledAsAttribute)
	{
		bConnected = UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutput, ToExpression, ToInput);
	}
	if (!bConnected)
	{
		return MakeErrorObject(TEXT("connection_failed"), FString::Printf(TEXT("Could not connect '%s.%s' to '%s.%s'."), *FromExpressionId, *FromOutput, *ToExpressionId, *ToInput));
	}
	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
	Data->SetObjectField(TEXT("from_expression"), MaterialExpressionRefToJson(FromExpression));
	Data->SetStringField(TEXT("from_output"), FromOutput);
	Data->SetObjectField(TEXT("to_expression"), MaterialExpressionToJson(ToExpression, false));
	Data->SetStringField(TEXT("to_input"), ToInput);
	Data->SetBoolField(TEXT("connected"), true);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolConnectMaterialProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString FromExpressionId;
	FString PropertyName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("from_expression"), FromExpressionId, Error) ||
		!RequireString(Arguments, TEXT("property"), PropertyName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	EMaterialProperty MaterialProperty = MP_MAX;
	if (!ResolveMaterialPropertyName(PropertyName, MaterialProperty, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	UMaterialExpression* FromExpression = FindMaterialExpression(Material, FromExpressionId, Error);
	if (!FromExpression)
	{
		return MakeErrorObject(TEXT("expression_not_found"), Error);
	}

	const FString FromOutput = GetOptionalString(Arguments, TEXT("from_output"));
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "ConnectMaterialProperty", "LLM Connect Material Property"));
	Material->Modify();
	FromExpression->Modify();
	const bool bConnected = UMaterialEditingLibrary::ConnectMaterialProperty(FromExpression, FromOutput, MaterialProperty);
	if (!bConnected)
	{
		return MakeErrorObject(TEXT("connection_failed"), FString::Printf(TEXT("Could not connect '%s.%s' to material property '%s'."), *FromExpressionId, *FromOutput, *PropertyName));
	}
	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MaterialToJson(Material, false, false, false);
	Data->SetObjectField(TEXT("from_expression"), MaterialExpressionRefToJson(FromExpression));
	Data->SetStringField(TEXT("from_output"), FromOutput);
	Data->SetStringField(TEXT("property"), PropertyName);
	Data->SetBoolField(TEXT("connected"), true);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolClearMaterialExpressions(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("clear_material_expressions requires confirm=true."));
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const int32 OldExpressionCount = Material->GetExpressions().Num();
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "ClearMaterialExpressions", "LLM Clear Material Expressions"));
	Material->Modify();
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MaterialToJson(Material, true, false, false);
	Data->SetNumberField(TEXT("old_expression_count"), OldExpressionCount);
	Data->SetBoolField(TEXT("cleared"), true);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDeleteMaterialExpression(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ExpressionId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("expression"), ExpressionId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("delete_material_expression requires confirm=true."));
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	UMaterialExpression* Expression = FindMaterialExpression(Material, ExpressionId, Error);
	if (!Expression)
	{
		return MakeErrorObject(TEXT("expression_not_found"), Error);
	}

	const FString DeletedId = MaterialExpressionId(Expression);
	const FString DeletedName = Expression->GetName();
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "DeleteMaterialExpression", "LLM Delete Material Expression"));
	Material->Modify();
	UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MaterialToJson(Material, true, false, false);
	Data->SetStringField(TEXT("deleted_expression_id"), DeletedId);
	Data->SetStringField(TEXT("deleted_expression_name"), DeletedName);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRecompileMaterial(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UMaterialEditingLibrary::RecompileMaterial(Material);
	TSharedRef<FJsonObject> Data = MaterialToJson(Material, false, false, false);
	Data->SetBoolField(TEXT("recompiled"), true);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetupCameraCutoutMaterial(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UMaterial* Material = nullptr;
	if (!LoadMaterialAsset(AssetPath, Material, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const bool bReplaceExisting = GetOptionalBool(Arguments, TEXT("replace_existing"), true);
	if (bReplaceExisting && Material->GetExpressions().Num() > 0 && !GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("confirmation_required"), TEXT("setup_camera_cutout_material would clear existing expressions; pass confirm=true."));
	}

	const FString PlayerLocationName = GetOptionalString(Arguments, TEXT("player_location_parameter"), TEXT("PlayerLocation"));
	const FString RadiusName = GetOptionalString(Arguments, TEXT("radius_parameter"), TEXT("CutoutRadius"));
	const FString HardnessName = GetOptionalString(Arguments, TEXT("hardness_parameter"), TEXT("CutoutHardness"));
	const double Radius = GetOptionalInt(Arguments, TEXT("radius"), 220);
	const double Hardness = GetOptionalInt(Arguments, TEXT("hardness"), 35);

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetupCameraCutoutMaterial", "LLM Setup Camera Cutout Material"));
	Material->Modify();
	if (bReplaceExisting)
	{
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
	}

	{
		TSharedPtr<FJsonValue> BlendValue = MakeShared<FJsonValueString>(TEXT("BLEND_Masked"));
		if (!SetEditablePropertyValue(Material, Material, TEXT("BlendMode"), BlendValue, Error))
		{
			return MakeErrorObject(TEXT("unsupported_property_type"), Error);
		}
		TSharedPtr<FJsonValue> ShadingValue = MakeShared<FJsonValueString>(TEXT("MSM_DefaultLit"));
		SetEditablePropertyValue(Material, Material, TEXT("ShadingModel"), ShadingValue, Error);
		TSharedPtr<FJsonValue> TwoSidedValue = MakeShared<FJsonValueBoolean>(true);
		SetEditablePropertyValue(Material, Material, TEXT("TwoSided"), TwoSidedValue, Error);
		TSharedPtr<FJsonValue> DitherValue = MakeShared<FJsonValueBoolean>(true);
		SetEditablePropertyValue(Material, Material, TEXT("DitherOpacityMask"), DitherValue, Error);
		TSharedPtr<FJsonValue> ClipValue = MakeShared<FJsonValueNumber>(0.333);
		SetEditablePropertyValue(Material, Material, TEXT("OpacityMaskClipValue"), ClipValue, Error);
	}

	UMaterialExpressionWorldPosition* WorldPosition = Cast<UMaterialExpressionWorldPosition>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionWorldPosition::StaticClass(), -900, -180));
	UMaterialExpressionVectorParameter* PlayerLocation = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -900, 40));
	UMaterialExpressionScalarParameter* CutoutRadius = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -900, 260));
	UMaterialExpressionScalarParameter* CutoutHardness = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -900, 460));
	UMaterialExpressionSphereMask* SphereMask = Cast<UMaterialExpressionSphereMask>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionSphereMask::StaticClass(), -520, 80));
	UMaterialExpressionOneMinus* OneMinus = Cast<UMaterialExpressionOneMinus>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionOneMinus::StaticClass(), -230, 80));
	UMaterialExpressionConstant3Vector* BaseColor = Cast<UMaterialExpressionConstant3Vector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -230, -180));

	if (!WorldPosition || !PlayerLocation || !CutoutRadius || !CutoutHardness || !SphereMask || !OneMinus || !BaseColor)
	{
		return MakeErrorObject(TEXT("create_failed"), TEXT("Failed to create one or more camera cutout material expressions."));
	}

	PlayerLocation->ParameterName = FName(*PlayerLocationName);
	PlayerLocation->DefaultValue = FLinearColor::Black;
	PlayerLocation->UpdateParameterGuid(true, true);
	PlayerLocation->ValidateParameterName(false);
	CutoutRadius->ParameterName = FName(*RadiusName);
	CutoutRadius->DefaultValue = static_cast<float>(Radius);
	CutoutRadius->UpdateParameterGuid(true, true);
	CutoutRadius->ValidateParameterName(false);
	CutoutHardness->ParameterName = FName(*HardnessName);
	CutoutHardness->DefaultValue = static_cast<float>(Hardness);
	CutoutHardness->UpdateParameterGuid(true, true);
	CutoutHardness->ValidateParameterName(false);
	BaseColor->Constant = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);
	Material->BuildEditorParameterList();

	UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPosition, FString(), SphereMask, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(PlayerLocation, FString(), SphereMask, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CutoutRadius, FString(), SphereMask, TEXT("Radius"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CutoutHardness, FString(), SphereMask, TEXT("Hardness"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SphereMask, FString(), OneMinus, TEXT("Input"));
	UMaterialEditingLibrary::ConnectMaterialProperty(OneMinus, FString(), MP_OpacityMask);
	UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, FString(), MP_BaseColor);
	UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);

	TSharedRef<FJsonObject> Data = MaterialToJson(Material, true, true, false);
	Data->SetStringField(TEXT("player_location_parameter"), PlayerLocationName);
	Data->SetStringField(TEXT("radius_parameter"), RadiusName);
	Data->SetStringField(TEXT("hardness_parameter"), HardnessName);
	Data->SetBoolField(TEXT("configured"), true);

	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		FString SaveError;
		const bool bSaved = SaveAssetPackage(Material, Filename, SaveError);
		Data->SetStringField(TEXT("filename"), Filename);
		Data->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			TSharedRef<FJsonObject> ErrorObject = MakeErrorObject(TEXT("save_failed"), SaveError);
			ErrorObject->SetObjectField(TEXT("data"), Data);
			return ErrorObject;
		}
	}

	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListGasAssets(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("content_path"), GetOptionalString(Arguments, TEXT("path"), TEXT("/Game")));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const FString Query = GetOptionalString(Arguments, TEXT("query")).ToLower();
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 500), 1, 5000);
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = GetOptionalBool(Arguments, TEXT("recursive"), true);
	Filter.bIncludeOnlyOnDiskAssets = false;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> AssetObjects;
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> ByKind;
	int32 SkippedExcludedCount = 0;
	int32 SkippedNonCandidateCount = 0;
	int32 LoadedCandidateCount = 0;
	for (const FAssetData& AssetData : Assets)
	{
		if (AssetObjects.Num() >= Limit)
		{
			break;
		}
		if (IsAssetDataExcluded(AssetData, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}
		if (!Query.IsEmpty() && !AssetData.AssetName.ToString().ToLower().Contains(Query))
		{
			continue;
		}
		if (!IsPotentialGasAssetData(AssetData))
		{
			++SkippedNonCandidateCount;
			continue;
		}

		UObject* Asset = AssetData.GetAsset();
		++LoadedCandidateCount;
		const FString Kind = GetGasAssetKind(Asset);
		if (Kind.IsEmpty())
		{
			continue;
		}

		TSharedRef<FJsonObject> AssetObject = GasAssetToJson(Asset);
		AssetObject->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObject->SetStringField(TEXT("object_path"), AssetData.GetObjectPathString());
		const TSharedPtr<FJsonValue> JsonValue = MakeShared<FJsonValueObject>(AssetObject);
		AssetObjects.Add(JsonValue);
		ByKind.FindOrAdd(Kind).Add(JsonValue);
	}

	if (GetOptionalBool(Arguments, TEXT("include_native_attribute_sets"), true))
	{
		for (TObjectIterator<UClass> ClassIt; ClassIt && AssetObjects.Num() < Limit; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (!Class || !Class->IsChildOf(UAttributeSet::StaticClass()) || Class == UAttributeSet::StaticClass() || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			if (!Query.IsEmpty() && !Class->GetName().ToLower().Contains(Query))
			{
				continue;
			}
			TSharedRef<FJsonObject> ClassObject = GasAssetToJson(Class);
			ClassObject->SetStringField(TEXT("name"), Class->GetName());
			ClassObject->SetStringField(TEXT("object_path"), Class->GetPathName());
			ClassObject->SetBoolField(TEXT("native_class"), true);
			const TSharedPtr<FJsonValue> JsonValue = MakeShared<FJsonValueObject>(ClassObject);
			AssetObjects.Add(JsonValue);
			ByKind.FindOrAdd(TEXT("attribute_set")).Add(JsonValue);
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("content_path"), Path);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("assets"), AssetObjects);
	Data->SetNumberField(TEXT("count"), AssetObjects.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	Data->SetNumberField(TEXT("skipped_non_candidate_count"), SkippedNonCandidateCount);
	Data->SetNumberField(TEXT("loaded_candidate_count"), LoadedCandidateCount);
	TSharedRef<FJsonObject> KindObject = MakeShared<FJsonObject>();
	for (TPair<FString, TArray<TSharedPtr<FJsonValue>>>& Pair : ByKind)
	{
		KindObject->SetArrayField(Pair.Key, Pair.Value);
	}
	Data->SetObjectField(TEXT("by_kind"), KindObject);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectGameplayAbility(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* DefaultObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!Asset || !Cast<UGameplayAbility>(DefaultObject))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayAbility Blueprint/class default.") : Error);
	}

	TSharedRef<FJsonObject> Data = GasAssetToJson(Asset);
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	AddPropertySnapshot(DefaultObject, {
		TEXT("AbilityTags"),
		TEXT("ActivationOwnedTags"),
		TEXT("ActivationRequiredTags"),
		TEXT("ActivationBlockedTags"),
		TEXT("CancelAbilitiesWithTag"),
		TEXT("BlockAbilitiesWithTag"),
		TEXT("SourceRequiredTags"),
		TEXT("SourceBlockedTags"),
		TEXT("TargetRequiredTags"),
		TEXT("TargetBlockedTags"),
		TEXT("InstancingPolicy"),
		TEXT("NetExecutionPolicy"),
		TEXT("ReplicationPolicy"),
		TEXT("CooldownGameplayEffectClass"),
		TEXT("CostGameplayEffectClass")
	}, Properties);
	Data->SetObjectField(TEXT("properties"), Properties);

	if (UGameplayAbility* Ability = Cast<UGameplayAbility>(DefaultObject))
	{
		Data->SetStringField(TEXT("cooldown_ge"), Ability->GetCooldownGameplayEffect() ? Ability->GetCooldownGameplayEffect()->GetPathName() : FString());
		Data->SetStringField(TEXT("cost_ge"), Ability->GetCostGameplayEffect() ? Ability->GetCostGameplayEffect()->GetPathName() : FString());
	}
	Data->SetArrayField(TEXT("ability_task_nodes"), GetAbilityTaskNodesJson(Cast<UBlueprint>(Asset)));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectGameplayEffect(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* DefaultObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!Asset || !Cast<UGameplayEffect>(DefaultObject))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect Blueprint/class default.") : Error);
	}

	TSharedRef<FJsonObject> Data = GasAssetToJson(Asset);
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	AddPropertySnapshot(DefaultObject, {
		TEXT("DurationPolicy"),
		TEXT("DurationMagnitude"),
		TEXT("MaxDurationMagnitude"),
		TEXT("Period"),
		TEXT("Modifiers"),
		TEXT("Executions"),
		TEXT("GrantedAbilities"),
		TEXT("InheritableGameplayEffectTags"),
		TEXT("InheritableOwnedTagsContainer"),
		TEXT("OngoingTagRequirements"),
		TEXT("ApplicationTagRequirements"),
		TEXT("RemoveGameplayEffectsWithTags"),
		TEXT("StackingType"),
		TEXT("StackLimitCount"),
		TEXT("StackDurationRefreshPolicy"),
		TEXT("StackPeriodResetPolicy"),
		TEXT("StackExpirationPolicy"),
		TEXT("ImmunityTagRequirements")
	}, Properties);
	Data->SetObjectField(TEXT("properties"), Properties);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectAttributeSet(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UClass* Class = GetEffectiveAssetClass(Asset);
	if (!Asset || !IsClassChildOf(Class, UAttributeSet::StaticClass()))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not an AttributeSet.") : Error);
	}

	TArray<TSharedPtr<FJsonValue>> Attributes;
	for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		const bool bGameplayAttributeData = Property->GetCPPType().Contains(TEXT("FGameplayAttributeData"));
		const bool bNumeric = CastField<FNumericProperty>(Property) != nullptr;
		if (!bGameplayAttributeData && !bNumeric)
		{
			continue;
		}
		TSharedRef<FJsonObject> Attribute = MakeShared<FJsonObject>();
		Attribute->SetStringField(TEXT("name"), Property->GetName());
		Attribute->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
		Attribute->SetBoolField(TEXT("replicated"), Property->HasAnyPropertyFlags(CPF_Net));
		Attribute->SetStringField(TEXT("rep_notify"), Property->RepNotifyFunc.ToString());
		Attribute->SetBoolField(TEXT("has_on_rep"), !Property->RepNotifyFunc.IsNone() && Class->FindFunctionByName(Property->RepNotifyFunc) != nullptr);
		Attributes.Add(MakeShared<FJsonValueObject>(Attribute));
	}

	TSharedRef<FJsonObject> Data = GasAssetToJson(Asset);
	Data->SetArrayField(TEXT("attributes"), Attributes);
	Data->SetNumberField(TEXT("count"), Attributes.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectGameplayCue(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* DefaultObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!Asset || !(Cast<AGameplayCueNotify_Actor>(DefaultObject) || Cast<UGameplayCueNotify_Static>(DefaultObject)))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayCueNotify.") : Error);
	}

	TSharedRef<FJsonObject> Data = GasAssetToJson(Asset);
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	AddPropertySnapshot(DefaultObject, { TEXT("GameplayCueTag"), TEXT("GameplayCueName") }, Properties);
	Data->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Graphs;
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		TArray<UEdGraph*> BlueprintGraphs;
		Blueprint->GetAllGraphs(BlueprintGraphs);
		for (UEdGraph* Graph : BlueprintGraphs)
		{
			if (Graph)
			{
				TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
				GraphObject->SetStringField(TEXT("name"), Graph->GetName());
				GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
				Graphs.Add(MakeShared<FJsonValueObject>(GraphObject));
			}
		}
	}
	Data->SetArrayField(TEXT("graphs"), Graphs);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectGameplayTags(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString Prefix = GetOptionalString(Arguments, TEXT("prefix"));
	const FString Query = GetOptionalString(Arguments, TEXT("query"));
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 500), 1, 10000);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("prefix"), Prefix);
	Data->SetStringField(TEXT("query"), Query);
	Data->SetArrayField(TEXT("tags"), FindGameplayTagsJson(Prefix, Query, Limit));
	return MakeOk(Data);
}

static TSharedRef<FJsonObject> CreateGasBlueprintAsset(const TSharedPtr<FJsonObject>& Arguments, UClass* DefaultParentClass, const FString& ToolLabel)
{
	FString RequestedPackagePath;
	FString Error;
	if (!RequireString(Arguments, TEXT("package_path"), RequestedPackagePath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FString PackageFolderPath;
	FString AssetName;
	if (!SplitAssetPackagePath(RequestedPackagePath, PackageFolderPath, AssetName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!IsGameContentPath(PackageFolderPath))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("GAS asset creation is limited to /Game content paths."));
	}
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *PackageFolderPath, *AssetName);
	if (ResolveObjectPath(ObjectPathFromPackagePath(PackagePath)))
	{
		return MakeErrorObject(TEXT("asset_exists"), FString::Printf(TEXT("Asset '%s' already exists."), *PackagePath));
	}

	UClass* ParentClass = DefaultParentClass;
	const FString ParentClassPath = GetOptionalString(Arguments, TEXT("parent_class"));
	if (!ParentClassPath.IsEmpty())
	{
		UObject* ParentObject = StaticLoadObject(UObject::StaticClass(), nullptr, *StripObjectExportWrapper(ParentClassPath));
		if (UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentObject))
		{
			ParentClass = ParentBlueprint->GeneratedClass;
		}
		else
		{
			ParentClass = Cast<UClass>(ParentObject);
		}
		if (!ParentClass)
		{
			for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
			{
				if (ClassIt->GetName().Equals(ParentClassPath, ESearchCase::IgnoreCase) || ClassIt->GetPathName().Equals(ParentClassPath, ESearchCase::IgnoreCase))
				{
					ParentClass = *ClassIt;
					break;
				}
			}
		}
	}

	if (!ParentClass)
	{
		return MakeErrorObject(TEXT("class_not_found"), TEXT("Could not resolve GAS parent class."));
	}
	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		return MakeErrorObject(TEXT("unsupported_gas_asset"), FString::Printf(TEXT("Class '%s' cannot be used as a Blueprint parent."), *ParentClass->GetPathName()));
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return MakeErrorObject(TEXT("package_failed"), FString::Printf(TEXT("Could not create package '%s'."), *PackagePath));
	}
	const FScopedTransaction Transaction(FText::FromString(ToolLabel));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*AssetName), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("LLMBlueprintBridge")));
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("create_failed"), FString::Printf(TEXT("Failed to create Blueprint '%s'."), *PackagePath));
	}

	FAssetRegistryModule::AssetCreated(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TArray<TSharedPtr<FJsonValue>> InitialChanges;
	UObject* DefaultObject = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	if (DefaultObject)
	{
		TSharedPtr<FJsonObject> TagsObject;
		if (TryGetObject(Arguments, TEXT("tags"), TagsObject))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : TagsObject->Values)
			{
				FString SetError;
				const FString PropertyPath = ResolveKnownGasTagField(DefaultObject, Pair.Key);
				TSharedPtr<FJsonObject> PropertyJson;
				if (SetEditablePropertyValue(Blueprint, DefaultObject, PropertyPath, Pair.Value, SetError, &PropertyJson))
				{
					InitialChanges.Add(MakeShared<FJsonValueObject>(PropertyJson.ToSharedRef()));
				}
			}
		}
		TSharedPtr<FJsonObject> DefaultsObject;
		if (TryGetObject(Arguments, TEXT("defaults"), DefaultsObject))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DefaultsObject->Values)
			{
				FString SetError;
				TSharedPtr<FJsonObject> PropertyJson;
				if (SetEditablePropertyValue(Blueprint, DefaultObject, Pair.Key, Pair.Value, SetError, &PropertyJson))
				{
					InitialChanges.Add(MakeShared<FJsonValueObject>(PropertyJson.ToSharedRef()));
				}
			}
		}
		if (InitialChanges.Num() > 0)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
	}
	Blueprint->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("package"), Blueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	Data->SetStringField(TEXT("kind"), GetGasAssetKind(Blueprint));
	Data->SetArrayField(TEXT("initial_changes"), InitialChanges);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateGameplayAbility(const TSharedPtr<FJsonObject>& Arguments)
{
	return CreateGasBlueprintAsset(Arguments, UGameplayAbility::StaticClass(), TEXT("LLM Create GameplayAbility"));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateGameplayEffect(const TSharedPtr<FJsonObject>& Arguments)
{
	return CreateGasBlueprintAsset(Arguments, UGameplayEffect::StaticClass(), TEXT("LLM Create GameplayEffect"));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateGameplayCue(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString CueType = GetOptionalString(Arguments, TEXT("cue_type"), GetOptionalString(Arguments, TEXT("type"), TEXT("static")));
	UClass* ParentClass = CueType.Equals(TEXT("actor"), ESearchCase::IgnoreCase) ? AGameplayCueNotify_Actor::StaticClass() : UGameplayCueNotify_Static::StaticClass();
	return CreateGasBlueprintAsset(Arguments, ParentClass, TEXT("LLM Create GameplayCue"));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateGameplayModMagnitudeCalculation(const TSharedPtr<FJsonObject>& Arguments)
{
	return CreateGasBlueprintAsset(Arguments, UGameplayModMagnitudeCalculation::StaticClass(), TEXT("LLM Create GameplayModMagnitudeCalculation"));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddGameplayTag(const TSharedPtr<FJsonObject>& Arguments)
{
	FString TagName;
	FString Error;
	if (!RequireString(Arguments, TEXT("tag"), TagName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	IGameplayTagsEditorModule& TagsEditor = FModuleManager::LoadModuleChecked<IGameplayTagsEditorModule>(TEXT("GameplayTagsEditor"));
	const bool bAdded = TagsEditor.AddNewGameplayTagToINI(TagName, GetOptionalString(Arguments, TEXT("comment")), NAME_None, false, true);
	UGameplayTagsManager::Get().EditorRefreshGameplayTagTree();

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("tag"), TagName);
	Data->SetBoolField(TEXT("added"), bAdded);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveGameplayTag(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("remove_gameplay_tag is guarded; pass confirm=true after checking project references."));
	}
	return MakeErrorObject(TEXT("unsupported_gas_asset"), TEXT("Safe gameplay tag deletion is intentionally not automated by this bridge; use the Gameplay Tags editor so redirects and references can be reviewed."));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindGameplayTags(const TSharedPtr<FJsonObject>& Arguments)
{
	return ToolInspectGameplayTags(Arguments);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetGameplayTagContainer(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	const TSharedPtr<FJsonValue> TagsValue = Arguments->TryGetField(TEXT("tags"));
	if (!TagsValue.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'tags'."));
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* TargetObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!TargetObject)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	const FString KnownField = GetOptionalString(Arguments, TEXT("known_field"));
	FString PropertyPath = GetOptionalString(Arguments, TEXT("property_path"), GetOptionalString(Arguments, TEXT("property")));
	if (PropertyPath.IsEmpty() && !KnownField.IsEmpty())
	{
		PropertyPath = ResolveKnownGasTagField(TargetObject, KnownField);
	}
	if (PropertyPath.IsEmpty())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Provide property_path/property or known_field."));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetGameplayTagContainer", "LLM Set Gameplay Tag Container"));
	if (Asset)
	{
		Asset->Modify();
	}
	TSharedPtr<FJsonObject> PropertyJson;
	if (!SetEditablePropertyValue(Asset, TargetObject, PropertyPath, TagsValue, Error, &PropertyJson))
	{
		return MakeErrorObject(Error.Contains(TEXT("Gameplay tag")) ? TEXT("invalid_gameplay_tag") : TEXT("unsupported_property_type"), Error);
	}
	if (SyncGameplayEffectTagComponentAfterEdit(Cast<UGameplayEffect>(TargetObject), PropertyPath))
	{
		FProperty* Property = nullptr;
		void* PropertyData = nullptr;
		if (ResolvePropertyAddress(TargetObject, PropertyPath, Property, PropertyData, Error))
		{
			PropertyJson = PropertyToJson(TargetObject, Property, PropertyData, PropertyPath, TEXT("gas_default"), FString());
		}
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		else if (Asset)
		{
			Asset->MarkPackageDirty();
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("known_field"), KnownField);
	Data->SetStringField(TEXT("property_path"), PropertyPath);
	Data->SetObjectField(TEXT("property"), PropertyJson.ToSharedRef());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetAbilityPolicy(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* TargetObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!Cast<UGameplayAbility>(TargetObject))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayAbility.") : Error);
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetAbilityPolicy", "LLM Set GameplayAbility Policy"));
	if (Asset)
	{
		Asset->Modify();
	}
	TArray<TSharedPtr<FJsonValue>> Changed;
	const TMap<FString, FString> Fields = {
		{ TEXT("instancing_policy"), TEXT("InstancingPolicy") },
		{ TEXT("net_execution_policy"), TEXT("NetExecutionPolicy") },
		{ TEXT("replication_policy"), TEXT("ReplicationPolicy") }
	};
	for (const TPair<FString, FString>& Field : Fields)
	{
		const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Field.Key);
		if (!Value.IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> PropertyJson;
		if (!SetEditablePropertyValue(Asset, TargetObject, Field.Value, Value, Error, &PropertyJson))
		{
			return MakeErrorObject(TEXT("unsupported_property_type"), Error);
		}
		Changed.Add(MakeShared<FJsonValueObject>(PropertyJson.ToSharedRef()));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetArrayField(TEXT("changed"), Changed);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetAbilityTags(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* TargetObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!Cast<UGameplayAbility>(TargetObject))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayAbility.") : Error);
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetAbilityTags", "LLM Set GameplayAbility Tags"));
	if (Asset)
	{
		Asset->Modify();
	}
	TArray<TSharedPtr<FJsonValue>> Changed;
	const TArray<FString> Fields = {
		TEXT("AbilityTags"), TEXT("ActivationOwnedTags"), TEXT("ActivationRequiredTags"), TEXT("ActivationBlockedTags"),
		TEXT("CancelAbilitiesWithTag"), TEXT("BlockAbilitiesWithTag"), TEXT("SourceRequiredTags"), TEXT("SourceBlockedTags"),
		TEXT("TargetRequiredTags"), TEXT("TargetBlockedTags")
	};
	for (const FString& Field : Fields)
	{
		const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Field);
		if (!Value.IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> PropertyJson;
		if (!SetEditablePropertyValue(Asset, TargetObject, Field, Value, Error, &PropertyJson))
		{
			return MakeErrorObject(Error.Contains(TEXT("Gameplay tag")) ? TEXT("invalid_gameplay_tag") : TEXT("unsupported_property_type"), Error);
		}
		Changed.Add(MakeShared<FJsonValueObject>(PropertyJson.ToSharedRef()));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetArrayField(TEXT("changed"), Changed);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddAbilityTaskNode(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedPtr<FJsonObject> FindArgs = MakeShared<FJsonObject>();
	FindArgs->SetStringField(TEXT("asset_path"), GetOptionalString(Arguments, TEXT("asset_path")));
	FindArgs->SetStringField(TEXT("graph"), GetOptionalString(Arguments, TEXT("graph"), TEXT("EventGraph")));
	FindArgs->SetStringField(TEXT("query"), GetOptionalString(Arguments, TEXT("task"), GetOptionalString(Arguments, TEXT("query"), TEXT("Wait Gameplay Event"))));
	FindArgs->SetNumberField(TEXT("limit"), 1);

	const TSharedRef<FJsonObject> FindResult = ToolFindBlueprintActions(FindArgs);
	if (!FindResult->GetBoolField(TEXT("ok")))
	{
		return FindResult;
	}
	const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
	TSharedPtr<FJsonObject> FindData;
	if (!TryGetObject(FindResult, TEXT("data"), FindData) || !TryGetArray(FindData, TEXT("actions"), Actions) || Actions->Num() == 0)
	{
		return MakeErrorObject(TEXT("action_not_found"), TEXT("No matching AbilityTask Blueprint action was found."));
	}
	const FString ActionId = (*Actions)[0]->AsObject()->GetStringField(TEXT("action_id"));

	TSharedPtr<FJsonObject> SpawnArgs = MakeShared<FJsonObject>();
	SpawnArgs->SetStringField(TEXT("asset_path"), GetOptionalString(Arguments, TEXT("asset_path")));
	SpawnArgs->SetStringField(TEXT("graph"), GetOptionalString(Arguments, TEXT("graph"), TEXT("EventGraph")));
	SpawnArgs->SetStringField(TEXT("action_id"), ActionId);
	SpawnArgs->SetNumberField(TEXT("x"), GetOptionalInt(Arguments, TEXT("x"), 0));
	SpawnArgs->SetNumberField(TEXT("y"), GetOptionalInt(Arguments, TEXT("y"), 0));
	return ToolSpawnBlueprintAction(SpawnArgs);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolConfigureAbilityCommitFlow(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	auto SpawnActionByQuery = [this, &Arguments, &Results](const FString& Query, int32 X, int32 Y)
	{
		TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
		AddArgs->SetStringField(TEXT("asset_path"), GetOptionalString(Arguments, TEXT("asset_path")));
		AddArgs->SetStringField(TEXT("graph"), GetOptionalString(Arguments, TEXT("graph"), TEXT("EventGraph")));
		AddArgs->SetStringField(TEXT("query"), Query);
		AddArgs->SetNumberField(TEXT("x"), X);
		AddArgs->SetNumberField(TEXT("y"), Y);
		const TSharedRef<FJsonObject> Result = ToolAddAbilityTaskNode(AddArgs);
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("query"), Query);
		Entry->SetBoolField(TEXT("ok"), Result->GetBoolField(TEXT("ok")));
		if (TSharedPtr<FJsonObject> Data; TryGetObject(Result, TEXT("data"), Data))
		{
			Entry->SetObjectField(TEXT("data"), Data);
		}
		else
		{
			Entry->SetStringField(TEXT("message"), Result->GetStringField(TEXT("message")));
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	};

	SpawnActionByQuery(TEXT("CommitAbility"), 240, 0);
	if (GetOptionalBool(Arguments, TEXT("branch"), true))
	{
		SpawnActionByQuery(TEXT("Branch"), 480, 0);
	}
	SpawnActionByQuery(TEXT("EndAbility"), 720, 0);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetArrayField(TEXT("steps"), Results);
	Data->SetStringField(TEXT("note"), TEXT("Nodes are spawned for a conventional CommitAbility/EndAbility flow; agents should inspect and connect pins to match project style."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolConfigureAbilityCooldown(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedPtr<FJsonObject> SetArgs = MakeShared<FJsonObject>();
	SetArgs->SetStringField(TEXT("asset_path"), GetOptionalString(Arguments, TEXT("asset_path")));
	SetArgs->SetStringField(TEXT("property"), TEXT("CooldownGameplayEffectClass"));
	SetArgs->SetField(TEXT("value"), MakeShared<FJsonValueString>(GetOptionalString(Arguments, TEXT("cooldown_effect"))));
	return ToolSetClassDefault(SetArgs);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolConfigureAbilityCost(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedPtr<FJsonObject> SetArgs = MakeShared<FJsonObject>();
	SetArgs->SetStringField(TEXT("asset_path"), GetOptionalString(Arguments, TEXT("asset_path")));
	SetArgs->SetStringField(TEXT("property"), TEXT("CostGameplayEffectClass"));
	SetArgs->SetField(TEXT("value"), MakeShared<FJsonValueString>(GetOptionalString(Arguments, TEXT("cost_effect"))));
	return ToolSetClassDefault(SetArgs);
}

static bool ResolveGameplayModOp(const FString& InValue, EGameplayModOp::Type& OutOp)
{
	if (InValue.Equals(TEXT("Additive"), ESearchCase::IgnoreCase) || InValue.Equals(TEXT("Add"), ESearchCase::IgnoreCase))
	{
		OutOp = EGameplayModOp::Additive;
		return true;
	}
	if (InValue.Equals(TEXT("Multiplicitive"), ESearchCase::IgnoreCase) || InValue.Equals(TEXT("Multiply"), ESearchCase::IgnoreCase) || InValue.Equals(TEXT("Multiplicative"), ESearchCase::IgnoreCase))
	{
		OutOp = EGameplayModOp::Multiplicitive;
		return true;
	}
	if (InValue.Equals(TEXT("Division"), ESearchCase::IgnoreCase) || InValue.Equals(TEXT("Divide"), ESearchCase::IgnoreCase))
	{
		OutOp = EGameplayModOp::Division;
		return true;
	}
	if (InValue.Equals(TEXT("Override"), ESearchCase::IgnoreCase))
	{
		OutOp = EGameplayModOp::Override;
		return true;
	}
	return false;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetEffectDuration(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UGameplayEffect* Effect = Asset ? Cast<UGameplayEffect>(GetEffectiveDefaultObject(Asset)) : nullptr;
	if (!Effect)
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}

	const FString Policy = GetOptionalString(Arguments, TEXT("duration_policy"), GetOptionalString(Arguments, TEXT("policy"), TEXT("Instant")));
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetEffectDuration", "LLM Set GameplayEffect Duration"));
	if (Asset)
	{
		Asset->Modify();
	}
	Effect->Modify();
	if (Policy.Equals(TEXT("Instant"), ESearchCase::IgnoreCase))
	{
		Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
	}
	else if (Policy.Equals(TEXT("Infinite"), ESearchCase::IgnoreCase))
	{
		Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
	}
	else if (Policy.Equals(TEXT("HasDuration"), ESearchCase::IgnoreCase) || Policy.Equals(TEXT("Duration"), ESearchCase::IgnoreCase))
	{
		Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	}
	else
	{
		return MakeErrorObject(TEXT("invalid_arguments"), FString::Printf(TEXT("Unknown duration policy '%s'."), *Policy));
	}

	double Duration = 0.0;
	if (Arguments->TryGetNumberField(TEXT("duration"), Duration) || Arguments->TryGetNumberField(TEXT("duration_magnitude"), Duration))
	{
		Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(static_cast<float>(Duration)));
	}
	double Period = 0.0;
	if (Arguments->TryGetNumberField(TEXT("period"), Period))
	{
		Effect->Period.SetValue(static_cast<float>(Period));
	}
	Effect->PostEditChange();
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	else
	{
		Effect->MarkPackageDirty();
	}
	return ToolInspectGameplayEffect(Arguments);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddEffectModifier(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString AttributeName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("attribute"), AttributeName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UGameplayEffect* Effect = Asset ? Cast<UGameplayEffect>(GetEffectiveDefaultObject(Asset)) : nullptr;
	if (!Effect)
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}

	FProperty* AttributeProperty = nullptr;
	if (!FindGameplayAttributeProperty(AttributeName, AttributeProperty, Error))
	{
		return MakeErrorObject(TEXT("invalid_attribute"), Error);
	}

	EGameplayModOp::Type Op = EGameplayModOp::Additive;
	if (!ResolveGameplayModOp(GetOptionalString(Arguments, TEXT("modifier_operation"), GetOptionalString(Arguments, TEXT("operation"), TEXT("Additive"))), Op))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Unsupported modifier operation."));
	}

	double Magnitude = 0.0;
	Arguments->TryGetNumberField(TEXT("magnitude"), Magnitude);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddEffectModifier", "LLM Add GameplayEffect Modifier"));
	if (Asset)
	{
		Asset->Modify();
	}
	Effect->Modify();
	FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = FGameplayAttribute(AttributeProperty);
	Modifier.ModifierOp = Op;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(static_cast<float>(Magnitude)));
	Effect->PostEditChange();
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	else
	{
		Effect->MarkPackageDirty();
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetNumberField(TEXT("index"), Effect->Modifiers.Num() - 1);
	Data->SetStringField(TEXT("attribute"), Modifier.Attribute.GetName());
	Data->SetStringField(TEXT("operation"), UEnum::GetValueAsString(Modifier.ModifierOp.GetValue()));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveEffectModifier(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UGameplayEffect* Effect = Asset ? Cast<UGameplayEffect>(GetEffectiveDefaultObject(Asset)) : nullptr;
	if (!Effect)
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}
	const int32 Index = GetOptionalInt(Arguments, TEXT("index"), -1);
	if (!Effect->Modifiers.IsValidIndex(Index))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Modifier index is out of range."));
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveEffectModifier", "LLM Remove GameplayEffect Modifier"));
	if (Asset)
	{
		Asset->Modify();
	}
	Effect->Modify();
	Effect->Modifiers.RemoveAt(Index);
	Effect->PostEditChange();
	Effect->MarkPackageDirty();
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetNumberField(TEXT("removed_index"), Index);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetEffectModifier(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UGameplayEffect* Effect = Asset ? Cast<UGameplayEffect>(GetEffectiveDefaultObject(Asset)) : nullptr;
	if (!Effect)
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}
	const int32 Index = GetOptionalInt(Arguments, TEXT("index"), -1);
	if (!Effect->Modifiers.IsValidIndex(Index))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Modifier index is out of range."));
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetEffectModifier", "LLM Set GameplayEffect Modifier"));
	if (Asset)
	{
		Asset->Modify();
	}
	Effect->Modify();
	FGameplayModifierInfo& Modifier = Effect->Modifiers[Index];
	const FString AttributeName = GetOptionalString(Arguments, TEXT("attribute"));
	if (!AttributeName.IsEmpty())
	{
		FProperty* AttributeProperty = nullptr;
		if (!FindGameplayAttributeProperty(AttributeName, AttributeProperty, Error))
		{
			return MakeErrorObject(TEXT("invalid_attribute"), Error);
		}
		Modifier.Attribute = FGameplayAttribute(AttributeProperty);
	}
	const FString Operation = GetOptionalString(Arguments, TEXT("modifier_operation"), GetOptionalString(Arguments, TEXT("operation")));
	if (!Operation.IsEmpty())
	{
		EGameplayModOp::Type Op = EGameplayModOp::Additive;
		if (!ResolveGameplayModOp(Operation, Op))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Unsupported modifier operation."));
		}
		Modifier.ModifierOp = Op;
	}
	double Magnitude = 0.0;
	if (Arguments->TryGetNumberField(TEXT("magnitude"), Magnitude) || Arguments->TryGetNumberField(TEXT("scalable_float_value"), Magnitude))
	{
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(static_cast<float>(Magnitude)));
	}
	Effect->PostEditChange();
	Effect->MarkPackageDirty();
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("attribute"), Modifier.Attribute.GetName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddGrantedAbility(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString AbilityPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("ability"), AbilityPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UGameplayEffect* Effect = Asset ? Cast<UGameplayEffect>(GetEffectiveDefaultObject(Asset)) : nullptr;
	if (!Effect)
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}
	UObject* AbilityAsset = LoadAssetObject(AbilityPath, Error);
	UClass* AbilityClass = GetEffectiveAssetClass(AbilityAsset);
	if (!IsClassChildOf(AbilityClass, UGameplayAbility::StaticClass()))
	{
		return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Could not resolve GameplayAbility class from '%s'."), *AbilityPath));
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddGrantedAbility", "LLM Add Granted Ability"));
	if (Asset)
	{
		Asset->Modify();
	}
	Effect->Modify();
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FGameplayAbilitySpecDef& SpecDef = Effect->GrantedAbilities.AddDefaulted_GetRef();
	const int32 GrantedAbilityIndex = Effect->GrantedAbilities.Num() - 1;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	SpecDef.Ability = AbilityClass;
	SpecDef.LevelScalableFloat.SetValue(static_cast<float>(GetOptionalInt(Arguments, TEXT("level"), 1)));
	SpecDef.InputID = GetOptionalInt(Arguments, TEXT("input_id"), INDEX_NONE);
	Effect->PostEditChange();
	Effect->MarkPackageDirty();
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetNumberField(TEXT("index"), GrantedAbilityIndex);
	Data->SetStringField(TEXT("ability_class"), AbilityClass->GetPathName());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetEffectStacking(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UObject* TargetObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
	if (!Cast<UGameplayEffect>(TargetObject))
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetEffectStacking", "LLM Set GameplayEffect Stacking"));
	TArray<TSharedPtr<FJsonValue>> Changed;
	const TMap<FString, FString> Fields = {
		{ TEXT("stacking_type"), TEXT("StackingType") },
		{ TEXT("stack_limit_count"), TEXT("StackLimitCount") },
		{ TEXT("duration_refresh_policy"), TEXT("StackDurationRefreshPolicy") },
		{ TEXT("period_reset_policy"), TEXT("StackPeriodResetPolicy") },
		{ TEXT("expiration_policy"), TEXT("StackExpirationPolicy") }
	};
	for (const TPair<FString, FString>& Field : Fields)
	{
		const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Field.Key);
		if (!Value.IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> PropertyJson;
		if (!SetEditablePropertyValue(Asset, TargetObject, Field.Value, Value, Error, &PropertyJson))
		{
			return MakeErrorObject(TEXT("unsupported_property_type"), Error);
		}
		Changed.Add(MakeShared<FJsonValueObject>(PropertyJson.ToSharedRef()));
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetArrayField(TEXT("changed"), Changed);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddEffectExecution(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ClassPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("calculation_class"), ClassPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	UGameplayEffect* Effect = Asset ? Cast<UGameplayEffect>(GetEffectiveDefaultObject(Asset)) : nullptr;
	if (!Effect)
	{
		return MakeErrorObject(Asset ? TEXT("unsupported_gas_asset") : TEXT("load_failed"), Asset ? TEXT("Asset is not a GameplayEffect.") : Error);
	}
	UClass* CalculationClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *StripObjectExportWrapper(ClassPath)));
	if (!CalculationClass || !CalculationClass->IsChildOf(UGameplayEffectExecutionCalculation::StaticClass()))
	{
		return MakeErrorObject(TEXT("class_not_found"), FString::Printf(TEXT("Execution calculation class '%s' was not found."), *ClassPath));
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddEffectExecution", "LLM Add GameplayEffect Execution"));
	Effect->Modify();
	FGameplayEffectExecutionDefinition& Execution = Effect->Executions.AddDefaulted_GetRef();
	Execution.CalculationClass = CalculationClass;
	Effect->PostEditChange();
	Effect->MarkPackageDirty();
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Data->SetStringField(TEXT("calculation_class"), CalculationClass->GetPathName());
	Data->SetNumberField(TEXT("index"), Effect->Executions.Num() - 1);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectAttributeReferences(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString Attribute = GetOptionalString(Arguments, TEXT("attribute"));
	const FString Path = GetOptionalString(Arguments, TEXT("path"), GetOptionalString(Arguments, TEXT("content_path"), TEXT("/Game")));
	TSharedPtr<FJsonObject> ListArgs = MakeShared<FJsonObject>();
	ListArgs->SetStringField(TEXT("path"), Path);
	ListArgs->SetStringField(TEXT("query"), GetOptionalString(Arguments, TEXT("query")));
	ListArgs->SetNumberField(TEXT("limit"), GetOptionalInt(Arguments, TEXT("limit"), 1000));
	const TSharedRef<FJsonObject> Listed = ToolListGasAssets(ListArgs);
	TArray<TSharedPtr<FJsonValue>> References;
	if (TSharedPtr<FJsonObject> Data; TryGetObject(Listed, TEXT("data"), Data))
	{
		const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
		if (TryGetArray(Data, TEXT("assets"), Assets))
		{
			for (const TSharedPtr<FJsonValue>& AssetValue : *Assets)
			{
				const TSharedPtr<FJsonObject> AssetObject = AssetValue->AsObject();
				if (!AssetObject.IsValid())
				{
					continue;
				}
				FString LoadError;
				UObject* Asset = LoadAssetObject(AssetObject->GetStringField(TEXT("asset_path")), LoadError);
				UObject* DefaultObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
				const FString Exported = DefaultObject ? DefaultObject->GetFullName() + TEXT(" ") + ExportPropertyText(FindPropertyByNameLoose(DefaultObject->GetClass(), TEXT("Modifiers")), FindPropertyByNameLoose(DefaultObject->GetClass(), TEXT("Modifiers")) ? FindPropertyByNameLoose(DefaultObject->GetClass(), TEXT("Modifiers"))->ContainerPtrToValuePtr<void>(DefaultObject) : nullptr, DefaultObject) : FString();
				if (Attribute.IsEmpty() || Exported.Contains(Attribute, ESearchCase::IgnoreCase))
				{
					References.Add(AssetValue);
				}
			}
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("attribute"), Attribute);
	Data->SetArrayField(TEXT("references"), References);
	Data->SetNumberField(TEXT("count"), References.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGenerateAttributeSetCpp(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GetOptionalBool(Arguments, TEXT("confirm"), false))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("generate_attribute_set_cpp can create C++ files; pass confirm=true and explicit class/attributes when you want generation."));
	}
	return MakeErrorObject(TEXT("unsupported_gas_asset"), TEXT("C++ AttributeSet generation is intentionally left as a guarded future extension in this bridge build."));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolValidateAttributeReplication(const TSharedPtr<FJsonObject>& Arguments)
{
	const TSharedRef<FJsonObject> Inspection = ToolInspectAttributeSet(Arguments);
	if (!Inspection->GetBoolField(TEXT("ok")))
	{
		return Inspection;
	}
	TArray<TSharedPtr<FJsonValue>> Issues;
	TSharedPtr<FJsonObject> Data;
	if (TryGetObject(Inspection, TEXT("data"), Data))
	{
		const TArray<TSharedPtr<FJsonValue>>* Attributes = nullptr;
		if (TryGetArray(Data, TEXT("attributes"), Attributes))
		{
			for (const TSharedPtr<FJsonValue>& AttributeValue : *Attributes)
			{
				const TSharedPtr<FJsonObject> Attribute = AttributeValue->AsObject();
				if (Attribute.IsValid() && Attribute->GetBoolField(TEXT("replicated")) && !Attribute->GetBoolField(TEXT("has_on_rep")))
				{
					TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("severity"), TEXT("warning"));
					Issue->SetStringField(TEXT("code"), TEXT("missing_on_rep"));
					Issue->SetStringField(TEXT("attribute"), Attribute->GetStringField(TEXT("name")));
					Issues.Add(MakeShared<FJsonValueObject>(Issue));
				}
			}
		}
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("issues"), Issues);
	Result->SetBoolField(TEXT("valid"), Issues.Num() == 0);
	return MakeOk(Result);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolValidateGasAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UObject* Asset = LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	UObject* DefaultObject = GetEffectiveDefaultObject(Asset);
	const FString Kind = GetGasAssetKind(Asset);
	TArray<TSharedPtr<FJsonValue>> Issues;
	if (Kind.IsEmpty())
	{
		return MakeErrorObject(TEXT("unsupported_gas_asset"), TEXT("Asset is not a supported GAS asset."));
	}
	if (Kind == TEXT("gameplay_ability"))
	{
		if (GetAbilityTaskNodesJson(Cast<UBlueprint>(Asset)).Num() > 0)
		{
			// Detailed exec-pin graph validation is intentionally conservative for now.
		}
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		AddPropertySnapshot(DefaultObject, { TEXT("AbilityTags") }, Props);
		if (!Props->HasField(TEXT("AbilityTags")))
		{
			TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("severity"), TEXT("warning"));
			Issue->SetStringField(TEXT("code"), TEXT("ability_tags_unavailable"));
			Issues.Add(MakeShared<FJsonValueObject>(Issue));
		}
	}
	if (Kind == TEXT("gameplay_cue"))
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		AddPropertySnapshot(DefaultObject, { TEXT("GameplayCueTag") }, Props);
		if (!Props->HasField(TEXT("GameplayCueTag")))
		{
			TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("severity"), TEXT("warning"));
			Issue->SetStringField(TEXT("code"), TEXT("cue_tag_unavailable"));
			Issues.Add(MakeShared<FJsonValueObject>(Issue));
		}
	}
	TSharedRef<FJsonObject> Data = GasAssetToJson(Asset);
	Data->SetBoolField(TEXT("valid"), Issues.Num() == 0);
	Data->SetArrayField(TEXT("issues"), Issues);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolValidateGasProject(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Listed = ToolListGasAssets(Arguments);
	if (!Listed->GetBoolField(TEXT("ok")))
	{
		return Listed;
	}
	TArray<TSharedPtr<FJsonValue>> Results;
	if (TSharedPtr<FJsonObject> Data; TryGetObject(Listed, TEXT("data"), Data))
	{
		const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
		if (TryGetArray(Data, TEXT("assets"), Assets))
		{
			for (const TSharedPtr<FJsonValue>& AssetValue : *Assets)
			{
				TSharedPtr<FJsonObject> AssetObject = AssetValue->AsObject();
				TSharedPtr<FJsonObject> ValidateArgs = MakeShared<FJsonObject>();
				ValidateArgs->SetStringField(TEXT("asset_path"), AssetObject->GetStringField(TEXT("asset_path")));
				const TSharedRef<FJsonObject> Validation = ToolValidateGasAsset(ValidateArgs);
				if (TSharedPtr<FJsonObject> ValidationData; TryGetObject(Validation, TEXT("data"), ValidationData))
				{
					Results.Add(MakeShared<FJsonValueObject>(ValidationData.ToSharedRef()));
				}
			}
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("assets"), Results);
	Data->SetNumberField(TEXT("count"), Results.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolGasDependencyReport(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Listed = ToolListGasAssets(Arguments);
	if (!Listed->GetBoolField(TEXT("ok")))
	{
		return Listed;
	}
	TArray<TSharedPtr<FJsonValue>> AbilityRefs;
	TArray<TSharedPtr<FJsonValue>> EffectRefs;
	if (TSharedPtr<FJsonObject> Data; TryGetObject(Listed, TEXT("data"), Data))
	{
		const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
		if (TryGetArray(Data, TEXT("assets"), Assets))
		{
			for (const TSharedPtr<FJsonValue>& AssetValue : *Assets)
			{
				const TSharedPtr<FJsonObject> AssetObject = AssetValue->AsObject();
				FString LoadError;
				UObject* Asset = LoadAssetObject(AssetObject->GetStringField(TEXT("asset_path")), LoadError);
				UObject* DefaultObject = Asset ? GetEffectiveDefaultObject(Asset) : nullptr;
				if (UGameplayAbility* Ability = Cast<UGameplayAbility>(DefaultObject))
				{
					TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
					Ref->SetStringField(TEXT("ability"), Asset->GetPathName());
					Ref->SetStringField(TEXT("cooldown_ge"), Ability->GetCooldownGameplayEffect() ? Ability->GetCooldownGameplayEffect()->GetPathName() : FString());
					Ref->SetStringField(TEXT("cost_ge"), Ability->GetCostGameplayEffect() ? Ability->GetCostGameplayEffect()->GetPathName() : FString());
					AbilityRefs.Add(MakeShared<FJsonValueObject>(Ref));
				}
				if (UGameplayEffect* Effect = Cast<UGameplayEffect>(DefaultObject))
				{
					TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
					Ref->SetStringField(TEXT("effect"), Asset->GetPathName());
					TArray<TSharedPtr<FJsonValue>> Attributes;
					for (const FGameplayModifierInfo& Modifier : Effect->Modifiers)
					{
						Attributes.Add(MakeShared<FJsonValueString>(Modifier.Attribute.GetName()));
					}
					Ref->SetArrayField(TEXT("modified_attributes"), Attributes);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
					const int32 GrantedAbilityCount = Effect->GrantedAbilities.Num();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
					Ref->SetNumberField(TEXT("granted_ability_count"), GrantedAbilityCount);
					EffectRefs.Add(MakeShared<FJsonValueObject>(Ref));
				}
			}
		}
	}
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetArrayField(TEXT("abilities"), AbilityRefs);
	Report->SetArrayField(TEXT("effects"), EffectRefs);
	return MakeOk(Report);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateBasicAbilityRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Created = ToolCreateGameplayAbility(Arguments);
	if (!Created->GetBoolField(TEXT("ok")))
	{
		return Created;
	}
	TSharedPtr<FJsonObject> Data;
	TryGetObject(Created, TEXT("data"), Data);
	TSharedPtr<FJsonObject> FlowArgs = MakeShared<FJsonObject>();
	FlowArgs->SetStringField(TEXT("asset_path"), Data->GetStringField(TEXT("asset_path")));
	const TSharedRef<FJsonObject> Flow = ToolConfigureAbilityCommitFlow(FlowArgs);
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("created"), Data.ToSharedRef());
	Result->SetObjectField(TEXT("commit_flow"), Flow);
	return MakeOk(Result);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateCooldownEffectRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Created = ToolCreateGameplayEffect(Arguments);
	if (!Created->GetBoolField(TEXT("ok")))
	{
		return Created;
	}
	TSharedPtr<FJsonObject> CreatedData;
	TryGetObject(Created, TEXT("data"), CreatedData);
	TSharedPtr<FJsonObject> DurationArgs = MakeShared<FJsonObject>();
	DurationArgs->SetStringField(TEXT("asset_path"), CreatedData->GetStringField(TEXT("asset_path")));
	DurationArgs->SetStringField(TEXT("duration_policy"), TEXT("HasDuration"));
	DurationArgs->SetNumberField(TEXT("duration"), GetOptionalInt(Arguments, TEXT("duration"), 1));
	const TSharedRef<FJsonObject> Duration = ToolSetEffectDuration(DurationArgs);
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("created"), CreatedData.ToSharedRef());
	Result->SetObjectField(TEXT("duration"), Duration);
	return MakeOk(Result);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateCostEffectRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Created = ToolCreateGameplayEffect(Arguments);
	if (!Created->GetBoolField(TEXT("ok")))
	{
		return Created;
	}
	TSharedPtr<FJsonObject> CreatedData;
	TryGetObject(Created, TEXT("data"), CreatedData);
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("created"), CreatedData.ToSharedRef());
	const FString Attribute = GetOptionalString(Arguments, TEXT("attribute"));
	if (!Attribute.IsEmpty())
	{
		TSharedPtr<FJsonObject> ModifierArgs = MakeShared<FJsonObject>();
		ModifierArgs->SetStringField(TEXT("asset_path"), CreatedData->GetStringField(TEXT("asset_path")));
		ModifierArgs->SetStringField(TEXT("attribute"), Attribute);
		ModifierArgs->SetStringField(TEXT("operation"), TEXT("Additive"));
		double Magnitude = -1.0;
		Arguments->TryGetNumberField(TEXT("magnitude"), Magnitude);
		ModifierArgs->SetNumberField(TEXT("magnitude"), Magnitude);
		Result->SetObjectField(TEXT("modifier"), ToolAddEffectModifier(ModifierArgs));
	}
	return MakeOk(Result);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateDamageEffectRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	return ToolCreateCostEffectRecipe(Arguments);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateBuffEffectRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Created = ToolCreateCostEffectRecipe(Arguments);
	if (!Created->GetBoolField(TEXT("ok")))
	{
		return Created;
	}
	TSharedPtr<FJsonObject> Data;
	TryGetObject(Created, TEXT("data"), Data);
	TSharedPtr<FJsonObject> CreatedData;
	if (Data.IsValid())
	{
		TryGetObject(Data, TEXT("created"), CreatedData);
	}
	if (CreatedData.IsValid())
	{
		TSharedPtr<FJsonObject> DurationArgs = MakeShared<FJsonObject>();
		DurationArgs->SetStringField(TEXT("asset_path"), CreatedData->GetStringField(TEXT("asset_path")));
		DurationArgs->SetStringField(TEXT("duration_policy"), TEXT("HasDuration"));
		DurationArgs->SetNumberField(TEXT("duration"), GetOptionalInt(Arguments, TEXT("duration"), 5));
		Data->SetObjectField(TEXT("duration"), ToolSetEffectDuration(DurationArgs));
	}
	return Created;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListStateTreeAssets(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("content_path"), GetOptionalString(Arguments, TEXT("path"), TEXT("/Game")));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 500), 1, 5000);
	const bool bRecursive = GetOptionalBool(Arguments, TEXT("recursive"), true);
	const bool bIncludeDependencies = GetOptionalBool(Arguments, TEXT("include_dependencies"), true);
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = bRecursive;
	Filter.bIncludeOnlyOnDiskAssets = false;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> StateTrees;
	int32 SkippedExcludedCount = 0;
	for (const FAssetData& AssetData : Assets)
	{
		if (StateTrees.Num() >= Limit)
		{
			break;
		}
		if (IsAssetDataExcluded(AssetData, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}
		if (IsStateTreeAssetData(AssetData))
		{
			StateTrees.Add(MakeShared<FJsonValueObject>(StateTreeAssetDataToJson(AssetData, bIncludeDependencies)));
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("content_path"), Path);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("assets"), StateTrees);
	Data->SetNumberField(TEXT("count"), StateTrees.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFindStateTreeAssets(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("content_path"), GetOptionalString(Arguments, TEXT("path"), TEXT("/Game")));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	const FString Query = GetOptionalString(Arguments, TEXT("query")).ToLower();
	const FString ClassFilter = GetOptionalString(Arguments, TEXT("class_filter"), GetOptionalString(Arguments, TEXT("class"))).ToLower();
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 200), 1, 5000);
	const bool bRecursive = GetOptionalBool(Arguments, TEXT("recursive"), true);
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = bRecursive;
	Filter.bIncludeOnlyOnDiskAssets = false;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 SkippedExcludedCount = 0;
	for (const FAssetData& AssetData : Assets)
	{
		if (Results.Num() >= Limit)
		{
			break;
		}
		if (IsAssetDataExcluded(AssetData, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}
		if (!IsStateTreeAssetData(AssetData))
		{
			continue;
		}
		const FString SearchText = FString::Printf(TEXT("%s %s %s %s"), *AssetData.AssetName.ToString(), *AssetData.PackageName.ToString(), *AssetData.GetObjectPathString(), *AssetData.AssetClassPath.ToString()).ToLower();
		if (!Query.IsEmpty() && !SearchText.Contains(Query))
		{
			continue;
		}
		if (!ClassFilter.IsEmpty() && !AssetData.AssetClassPath.ToString().ToLower().Contains(ClassFilter))
		{
			continue;
		}
		Results.Add(MakeShared<FJsonValueObject>(StateTreeAssetDataToJson(AssetData, true)));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("content_path"), Path);
	Data->SetStringField(TEXT("query"), Query);
	Data->SetStringField(TEXT("class_filter"), ClassFilter);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("assets"), Results);
	Data->SetNumberField(TEXT("count"), Results.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolListStateTreeRelatedBlueprints(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Path = GetOptionalString(Arguments, TEXT("content_path"), GetOptionalString(Arguments, TEXT("path"), TEXT("/Game")));
	FString Error;
	if (!NormalizeFolderPath(Path, Path, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	const FString Query = GetOptionalString(Arguments, TEXT("query")).ToLower();
	const int32 Limit = FMath::Clamp(GetOptionalInt(Arguments, TEXT("limit"), 1000), 1, 5000);
	TArray<FString> ExcludedPaths;
	if (!GetExcludedPackagePaths(Arguments, ExcludedPaths, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = GetOptionalBool(Arguments, TEXT("recursive"), true);
	Filter.bIncludeOnlyOnDiskAssets = false;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> Results;
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> ByRole;
	int32 SkippedExcludedCount = 0;
	for (const FAssetData& AssetData : Assets)
	{
		if (Results.Num() >= Limit)
		{
			break;
		}
		if (IsAssetDataExcluded(AssetData, ExcludedPaths))
		{
			++SkippedExcludedCount;
			continue;
		}
		if (!AssetData.AssetClassPath.ToString().Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString Name = AssetData.AssetName.ToString();
		if (!Query.IsEmpty() && !Name.ToLower().Contains(Query))
		{
			continue;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass : nullptr;
		const FString Role = GetStateTreeBlueprintRole(GeneratedClass, Name);
		const bool bLikely = LooksLikeStateTreeRelatedBlueprintName(Name) || Role == TEXT("task") || Role == TEXT("condition") || Role == TEXT("evaluator");
		if (!bLikely)
		{
			continue;
		}

		TSharedRef<FJsonObject> Item = AssetDataToJson(AssetData);
		Item->SetStringField(TEXT("role"), Role);
		Item->SetStringField(TEXT("parent_class"), Blueprint && Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
		Item->SetStringField(TEXT("generated_class"), GeneratedClass ? GeneratedClass->GetPathName() : FString());
		Item->SetBoolField(TEXT("name_convention_match"), LooksLikeStateTreeRelatedBlueprintName(Name));
		TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Item);
		Results.Add(Value);
		ByRole.FindOrAdd(Role).Add(Value);
	}

	TSharedRef<FJsonObject> RoleObject = MakeShared<FJsonObject>();
	for (TPair<FString, TArray<TSharedPtr<FJsonValue>>>& Pair : ByRole)
	{
		RoleObject->SetArrayField(Pair.Key, Pair.Value);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("content_path"), Path);
	Data->SetArrayField(TEXT("exclude_paths"), StringArrayToJsonArray(ExcludedPaths));
	Data->SetArrayField(TEXT("blueprints"), Results);
	Data->SetObjectField(TEXT("by_role"), RoleObject);
	Data->SetNumberField(TEXT("count"), Results.Num());
	Data->SetNumberField(TEXT("skipped_excluded_count"), SkippedExcludedCount);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTree(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	return MakeOk(BuildStateTreeInspection(StateTree, true));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeState(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	const UStateTreeState* State = ResolveState(EditorData, Arguments, Error);
	if (!State)
	{
		return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
	}
	return MakeOk(StateToJson(StateTree, EditorData, State, false));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeTransitions(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!EditorData)
	{
		return MakeErrorObject(TEXT("unsupported_statetree_asset"), TEXT("StateTree has no editor data."));
	}
	TArray<const UStateTreeState*> States;
	GatherStateTreeStates(EditorData, States);
	TArray<TSharedPtr<FJsonValue>> Transitions;
	for (const UStateTreeState* State : States)
	{
		for (int32 Index = 0; Index < State->Transitions.Num(); ++Index)
		{
			Transitions.Add(MakeShared<FJsonValueObject>(TransitionToJson(EditorData, const_cast<UStateTreeState*>(State), State->Transitions[Index], MakeStatePath(State), Index)));
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("transitions"), Transitions);
	Data->SetNumberField(TEXT("count"), Transitions.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeTasks(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!EditorData)
	{
		return MakeErrorObject(TEXT("unsupported_statetree_asset"), TEXT("StateTree has no editor data."));
	}
	TArray<TSharedPtr<FJsonValue>> Tasks;
	for (int32 Index = 0; Index < EditorData->GlobalTasks.Num(); ++Index)
	{
		Tasks.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, StateTree, EditorData->GlobalTasks[Index], TEXT("global_task"), Index)));
	}
	TArray<const UStateTreeState*> States;
	GatherStateTreeStates(EditorData, States);
	for (const UStateTreeState* State : States)
	{
		for (int32 Index = 0; Index < State->Tasks.Num(); ++Index)
		{
			Tasks.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->Tasks[Index], TEXT("task"), Index)));
		}
		if (State->SingleTask.Node.IsValid())
		{
			Tasks.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->SingleTask, TEXT("task"), State->Tasks.Num())));
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tasks"), Tasks);
	Data->SetNumberField(TEXT("count"), Tasks.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeConditions(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!EditorData)
	{
		return MakeErrorObject(TEXT("unsupported_statetree_asset"), TEXT("StateTree has no editor data."));
	}
	TArray<TSharedPtr<FJsonValue>> Conditions;
	TArray<const UStateTreeState*> States;
	GatherStateTreeStates(EditorData, States);
	for (const UStateTreeState* State : States)
	{
		for (int32 Index = 0; Index < State->EnterConditions.Num(); ++Index)
		{
			Conditions.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), State->EnterConditions[Index], TEXT("enter_condition"), Index)));
		}
		for (const FStateTreeTransition& Transition : State->Transitions)
		{
			for (int32 Index = 0; Index < Transition.Conditions.Num(); ++Index)
			{
				Conditions.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, const_cast<UStateTreeState*>(State), Transition.Conditions[Index], TEXT("transition_condition"), Index)));
			}
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("conditions"), Conditions);
	Data->SetNumberField(TEXT("count"), Conditions.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeEvaluators(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	if (!EditorData)
	{
		return MakeErrorObject(TEXT("unsupported_statetree_asset"), TEXT("StateTree has no editor data."));
	}
	TArray<TSharedPtr<FJsonValue>> Evaluators;
	for (int32 Index = 0; Index < EditorData->Evaluators.Num(); ++Index)
	{
		Evaluators.Add(MakeShared<FJsonValueObject>(StateTreeNodeToJson(EditorData, StateTree, EditorData->Evaluators[Index], TEXT("evaluator"), Index)));
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("evaluators"), Evaluators);
	Data->SetNumberField(TEXT("count"), Evaluators.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeParameters(const TSharedPtr<FJsonObject>& Arguments)
{
	const TSharedRef<FJsonObject> Inspection = ToolInspectStateTree(Arguments);
	if (!Inspection->GetBoolField(TEXT("ok")))
	{
		return Inspection;
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	if (TSharedPtr<FJsonObject> SourceData; TryGetObject(Inspection, TEXT("data"), SourceData))
	{
		const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
		if (TryGetArray(SourceData, TEXT("parameters"), Parameters))
		{
			Data->SetArrayField(TEXT("parameters"), *Parameters);
		}
		const TArray<TSharedPtr<FJsonValue>>* EditorParameters = nullptr;
		if (TryGetArray(SourceData, TEXT("editor_parameters"), EditorParameters))
		{
			Data->SetArrayField(TEXT("editor_parameters"), *EditorParameters);
		}
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolInspectStateTreeBlueprintDependency(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UClass* Class = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->ParentClass;
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
	Data->SetStringField(TEXT("generated_class"), Class ? Class->GetPathName() : FString());
	Data->SetStringField(TEXT("role"), GetStateTreeBlueprintRole(Class, Blueprint->GetName()));
	Data->SetStringField(TEXT("status"), BlueprintStatusToString(Blueprint->Status));
	Data->SetArrayField(TEXT("exposed_properties"), PropertiesFromObject(Class ? Class->GetDefaultObject() : nullptr, TEXT("statetree_blueprint_default")));

	TArray<FString> ImportantFunctionNames = {
		TEXT("ReceiveLatentEnterState"),
		TEXT("ReceiveEnterState"),
		TEXT("ReceiveLatentTick"),
		TEXT("ReceiveTick"),
		TEXT("ReceiveExitState"),
		TEXT("ReceiveStateCompleted"),
		TEXT("ReceiveTestCondition"),
		TEXT("ReceiveTreeStart"),
		TEXT("ReceiveTreeStop")
	};
	TArray<TSharedPtr<FJsonValue>> Implemented;
	TArray<TSharedPtr<FJsonValue>> Graphs;
	TArray<TSharedPtr<FJsonValue>> Signals;
	TSet<FString> ReferencedAssets;
	TArray<UEdGraph*> BlueprintGraphs;
	Blueprint->GetAllGraphs(BlueprintGraphs);
	for (UEdGraph* Graph : BlueprintGraphs)
	{
		if (!Graph)
		{
			continue;
		}
		TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
		GraphObject->SetStringField(TEXT("name"), Graph->GetName());
		GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
			const FString NodeClass = Node->GetClass() ? Node->GetClass()->GetPathName() : FString();
			const FString Search = (Title + TEXT(" ") + NodeClass + TEXT(" ") + Node->GetName()).ToLower();
			if (Search.Contains(TEXT("gameplaytag")) || Search.Contains(TEXT("gameplay tag")) || Search.Contains(TEXT("ability")) || Search.Contains(TEXT("gas")) ||
				Search.Contains(TEXT("eqs")) || Search.Contains(TEXT("envquery")) || Search.Contains(TEXT("move")) || Search.Contains(TEXT("blackboard")) ||
				Search.Contains(TEXT("context")) || Search.Contains(TEXT("finish task")) || Search.Contains(TEXT("request transition")))
			{
				TSharedRef<FJsonObject> Signal = NodeToJson(Node, true);
				Signal->SetStringField(TEXT("graph"), Graph->GetName());
				Signals.Add(MakeShared<FJsonValueObject>(Signal));
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					const FString PinText = Pin->GetDefaultAsString();
					if (PinText.Contains(TEXT("/Game/")) || PinText.Contains(TEXT("/Script/")) || PinText.Contains(TEXT("GameplayTag")))
					{
						ReferencedAssets.Add(PinText);
					}
				}
			}
			Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Node, false)));
		}
		GraphObject->SetArrayField(TEXT("nodes"), Nodes);
		Graphs.Add(MakeShared<FJsonValueObject>(GraphObject));
		for (const FString& FunctionName : ImportantFunctionNames)
		{
			if (Graph->GetName().Contains(FunctionName, ESearchCase::IgnoreCase))
			{
				Implemented.Add(MakeShared<FJsonValueString>(FunctionName));
			}
		}
	}
	CollectReferencedObjectsFromStruct(Class, Class ? Class->GetDefaultObject() : nullptr, ReferencedAssets);
	Data->SetArrayField(TEXT("implemented_statetree_events"), Implemented);
	Data->SetArrayField(TEXT("graphs"), Graphs);
	Data->SetArrayField(TEXT("important_nodes"), Signals);
	Data->SetArrayField(TEXT("referenced_assets"), PathsToJsonArray(ReferencedAssets));

	TSharedPtr<FJsonObject> InspectArgs = MakeShared<FJsonObject>();
	InspectArgs->SetStringField(TEXT("asset_path"), AssetPath);
	InspectArgs->SetBoolField(TEXT("include_nodes"), GetOptionalBool(Arguments, TEXT("include_nodes"), false));
	Data->SetObjectField(TEXT("blueprint_inspection"), ToolInspectBlueprint(InspectArgs));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolValidateStateTree(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	bool bCompileSucceeded = true;
	TArray<TSharedPtr<FJsonValue>> Issues = ValidateStateTreeStatic(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true), bCompileSucceeded);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Data->SetBoolField(TEXT("compile_succeeded"), bCompileSucceeded);
	Data->SetBoolField(TEXT("valid"), bCompileSucceeded && Issues.Num() == 0);
	Data->SetArrayField(TEXT("issues"), Issues);
	Data->SetNumberField(TEXT("issue_count"), Issues.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolValidateStateTreeProject(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Listed = ToolListStateTreeAssets(Arguments);
	if (!Listed->GetBoolField(TEXT("ok")))
	{
		return Listed;
	}
	TArray<TSharedPtr<FJsonValue>> Results;
	int32 TotalIssues = 0;
	if (TSharedPtr<FJsonObject> ListedData; TryGetObject(Listed, TEXT("data"), ListedData))
	{
		const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
		if (TryGetArray(ListedData, TEXT("assets"), Assets))
		{
			for (const TSharedPtr<FJsonValue>& AssetValue : *Assets)
			{
				TSharedPtr<FJsonObject> Asset = AssetValue->AsObject();
				TSharedPtr<FJsonObject> ValidateArgs = MakeShared<FJsonObject>();
				ValidateArgs->SetStringField(TEXT("asset_path"), Asset->GetStringField(TEXT("asset_path")));
				ValidateArgs->SetBoolField(TEXT("compile"), GetOptionalBool(Arguments, TEXT("compile"), true));
				const TSharedRef<FJsonObject> Validation = ToolValidateStateTree(ValidateArgs);
				if (TSharedPtr<FJsonObject> ValidationData; TryGetObject(Validation, TEXT("data"), ValidationData))
				{
					TotalIssues += GetOptionalInt(ValidationData, TEXT("issue_count"), 0);
					Results.Add(MakeShared<FJsonValueObject>(ValidationData.ToSharedRef()));
				}
			}
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("assets"), Results);
	Data->SetNumberField(TEXT("count"), Results.Num());
	Data->SetNumberField(TEXT("issue_count"), TotalIssues);
	Data->SetBoolField(TEXT("valid"), TotalIssues == 0);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolValidateStateTreeBlueprintDependency(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Inspection = ToolInspectStateTreeBlueprintDependency(Arguments);
	if (!Inspection->GetBoolField(TEXT("ok")))
	{
		return Inspection;
	}
	TArray<TSharedPtr<FJsonValue>> Issues;
	if (TSharedPtr<FJsonObject> Data; TryGetObject(Inspection, TEXT("data"), Data))
	{
		const FString Role = GetOptionalString(Data, TEXT("role"));
		const FString ParentClass = GetOptionalString(Data, TEXT("parent_class"));
		const FString Status = GetOptionalString(Data, TEXT("status"));
		if (!(Role == TEXT("task") || Role == TEXT("condition") || Role == TEXT("evaluator")))
		{
			Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("warning"), TEXT("blueprint_role"), FString::Printf(TEXT("Blueprint parent class '%s' does not look like a StateTree task, condition, or evaluator."), *ParentClass), TEXT("Use a UStateTreeTaskBlueprintBase, UStateTreeConditionBlueprintBase, or UStateTreeEvaluatorBlueprintBase parent class."), 0.85f)));
		}
		if (Status == TEXT("Error"))
		{
			Issues.Add(MakeShared<FJsonValueObject>(MakeStateTreeIssue(TEXT("error"), TEXT("blueprint_compile"), TEXT("Blueprint status is Error."), TEXT("Compile the Blueprint and fix compiler errors."), 0.95f)));
		}
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("issues"), Issues);
	Result->SetNumberField(TEXT("issue_count"), Issues.Num());
	Result->SetBoolField(TEXT("valid"), Issues.Num() == 0);
	return MakeOk(Result);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugStatus(const TSharedPtr<FJsonObject>& Arguments)
{
	TArray<TSharedPtr<FJsonValue>> Worlds;
	int32 ComponentCount = 0;
	for (UWorld* World : GetCandidateWorlds())
	{
		TSharedRef<FJsonObject> WorldObject = MakeShared<FJsonObject>();
		WorldObject->SetStringField(TEXT("name"), World->GetName());
		WorldObject->SetStringField(TEXT("type"), LexToString(World->WorldType));
		int32 WorldComponents = 0;
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			TArray<UStateTreeComponent*> Components;
			ActorIt->GetComponents<UStateTreeComponent>(Components);
			WorldComponents += Components.Num();
		}
		ComponentCount += WorldComponents;
		WorldObject->SetNumberField(TEXT("statetree_component_count"), WorldComponents);
		Worlds.Add(MakeShared<FJsonValueObject>(WorldObject));
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("pie_running"), GEditor && GEditor->PlayWorld != nullptr);
	Data->SetArrayField(TEXT("worlds"), Worlds);
	Data->SetNumberField(TEXT("statetree_component_count"), ComponentCount);
	Data->SetNumberField(TEXT("running_trace_count"), GStateTreeTraceBuffers.Num());
	Data->SetStringField(TEXT("runtime_trace_mode"), TEXT("polling_snapshot_ring_buffer"));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugActor(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Error;
	AActor* Actor = ResolveStateTreeActor(Arguments, Error);
	if (!Actor)
	{
		return MakeErrorObject(TEXT("actor_not_found"), Error);
	}
	TArray<UStateTreeComponent*> Components;
	Actor->GetComponents<UStateTreeComponent>(Components);
	TArray<TSharedPtr<FJsonValue>> ComponentData;
	for (UStateTreeComponent* Component : Components)
	{
		ComponentData.Add(MakeShared<FJsonValueObject>(BuildComponentRuntimeSnapshot(Component)));
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("actor"), Actor->GetPathName());
	Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	Data->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
	Data->SetArrayField(TEXT("components"), ComponentData);
	Data->SetNumberField(TEXT("component_count"), ComponentData.Num());
	if (ComponentData.Num() == 0)
	{
		Data->SetStringField(TEXT("message"), TEXT("Actor has no UStateTreeComponent. It may run StateTree logic through another system not exposed as a component."));
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugSnapshot(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString ComponentPath = GetOptionalString(Arguments, TEXT("component_path"));
	if (!ComponentPath.IsEmpty())
	{
		if (UStateTreeComponent* Component = Cast<UStateTreeComponent>(ResolveObjectPath(ComponentPath)))
		{
			return MakeOk(BuildComponentRuntimeSnapshot(Component));
		}
		return MakeErrorObject(TEXT("runtime_not_available"), FString::Printf(TEXT("Could not resolve StateTreeComponent '%s'."), *ComponentPath));
	}
	return ToolStateTreeDebugActor(Arguments);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugTraceStart(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Error;
	AActor* Actor = ResolveStateTreeActor(Arguments, Error);
	if (!Actor)
	{
		return MakeErrorObject(TEXT("actor_not_found"), Error);
	}
	TArray<UStateTreeComponent*> Components;
	Actor->GetComponents<UStateTreeComponent>(Components);
	if (Components.Num() == 0)
	{
		return MakeErrorObject(TEXT("runtime_not_available"), TEXT("Actor has no StateTreeComponent to trace."));
	}

	UStateTreeComponent* Component = Components[0];
	const FString TraceId = FString::Printf(TEXT("sttrace:%d"), ++GStateTreeTraceSerial);
	FStateTreeTraceBuffer& Trace = GStateTreeTraceBuffers.Add(TraceId);
	Trace.TraceId = TraceId;
	Trace.Actor = Actor;
	Trace.Component = Component;
	Trace.StartedAt = FPlatformTime::Seconds();
	Trace.MaxEvents = FMath::Clamp(GetOptionalInt(Arguments, TEXT("max_events"), 256), 8, 4096);
	Trace.bRunning = true;
	TSharedRef<FJsonObject> Snapshot = BuildComponentRuntimeSnapshot(Component);
	Trace.LastActivePath = SnapshotActivePath(Snapshot);
	AddTraceEvent(Trace, TEXT("trace_start"), Snapshot);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("trace_id"), TraceId);
	Data->SetStringField(TEXT("actor"), Actor->GetPathName());
	Data->SetStringField(TEXT("component"), Component->GetPathName());
	Data->SetStringField(TEXT("initial_active_path"), Trace.LastActivePath);
	Data->SetStringField(TEXT("mode"), TEXT("polling_snapshot_ring_buffer"));
	Data->SetStringField(TEXT("limitations"), TEXT("This bridge records start/read/stop snapshots and state path changes observed while trace_read polls the component. UE 5.7 public component APIs do not expose every transition attempt or condition result to this plugin."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugTraceStop(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString TraceId = GetOptionalString(Arguments, TEXT("trace_id"));
	if (TraceId.IsEmpty() || !GStateTreeTraceBuffers.Contains(TraceId))
	{
		return MakeErrorObject(TEXT("trace_not_running"), TEXT("Trace id is missing or not running."));
	}
	FStateTreeTraceBuffer& Trace = GStateTreeTraceBuffers[TraceId];
	PollTrace(Trace);
	Trace.bRunning = false;
	TSharedPtr<FJsonObject> StopSnapshot;
	if (Trace.Component.IsValid())
	{
		StopSnapshot = BuildComponentRuntimeSnapshot(Trace.Component.Get());
	}
	AddTraceEvent(Trace, TEXT("trace_stop"), StopSnapshot);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("trace_id"), TraceId);
	Data->SetArrayField(TEXT("events"), Trace.Events);
	Data->SetNumberField(TEXT("event_count"), Trace.Events.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugTraceRead(const TSharedPtr<FJsonObject>& Arguments)
{
	const FString TraceId = GetOptionalString(Arguments, TEXT("trace_id"));
	if (TraceId.IsEmpty() || !GStateTreeTraceBuffers.Contains(TraceId))
	{
		return MakeErrorObject(TEXT("trace_not_running"), TEXT("Trace id is missing or not running."));
	}
	FStateTreeTraceBuffer& Trace = GStateTreeTraceBuffers[TraceId];
	PollTrace(Trace);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("trace_id"), TraceId);
	Data->SetBoolField(TEXT("running"), Trace.bRunning);
	Data->SetStringField(TEXT("actor"), Trace.Actor.IsValid() ? Trace.Actor->GetPathName() : FString());
	Data->SetStringField(TEXT("component"), Trace.Component.IsValid() ? Trace.Component->GetPathName() : FString());
	Data->SetStringField(TEXT("last_active_path"), Trace.LastActivePath);
	Data->SetArrayField(TEXT("events"), Trace.Events);
	Data->SetNumberField(TEXT("event_count"), Trace.Events.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeExplainLastTransition(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> TraceResult = ToolStateTreeDebugTraceRead(Arguments);
	if (!TraceResult->GetBoolField(TEXT("ok")))
	{
		return TraceResult;
	}
	TSharedPtr<FJsonObject> TraceData;
	TryGetObject(TraceResult, TEXT("data"), TraceData);
	TSharedPtr<FJsonObject> LastChange;
	if (TraceData.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
		if (TryGetArray(TraceData, TEXT("events"), Events))
		{
			for (int32 Index = Events->Num() - 1; Index >= 0; --Index)
			{
				TSharedPtr<FJsonObject> Event = (*Events)[Index]->AsObject();
				if (Event.IsValid() && GetOptionalString(Event, TEXT("type")) == TEXT("state_change"))
				{
					LastChange = Event;
					break;
				}
			}
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("last_transition_event"), LastChange.IsValid() ? LastChange.ToSharedRef() : MakeShared<FJsonObject>());
	Data->SetStringField(TEXT("explanation"), LastChange.IsValid() ? TEXT("The observed active state path changed between trace polls. Use the event payload snapshot plus inspect_statetree_transitions on the source state to see candidate rules and conditions.") : TEXT("No state change has been observed in this trace yet."));
	Data->SetStringField(TEXT("limitations"), TEXT("Public UE 5.7 runtime APIs do not expose the exact selected transition rule/condition results here, so this explanation is based on observed active path changes."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeExplainBlockedTransitions(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> SnapshotResult = ToolStateTreeDebugSnapshot(Arguments);
	if (!SnapshotResult->GetBoolField(TEXT("ok")))
	{
		return SnapshotResult;
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("runtime_snapshot"), SnapshotResult);
	TArray<TSharedPtr<FJsonValue>> Explanations;
	if (TSharedPtr<FJsonObject> Snapshot; TryGetObject(SnapshotResult, TEXT("data"), Snapshot))
	{
		FString StateTreePath = GetOptionalString(Snapshot, TEXT("state_tree"));
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (!StateTreePath.IsEmpty() && TryGetArray(Snapshot, TEXT("active_state_paths"), Paths) && Paths->Num() > 0)
		{
			const FString ActivePath = (*Paths)[Paths->Num() - 1]->AsString();
			TSharedPtr<FJsonObject> StateArgs = MakeShared<FJsonObject>();
			StateArgs->SetStringField(TEXT("asset_path"), StateTreePath);
			StateArgs->SetStringField(TEXT("state_path"), ActivePath);
			TSharedRef<FJsonObject> StateInspection = ToolInspectStateTreeState(StateArgs);
			if (TSharedPtr<FJsonObject> StateData; TryGetObject(StateInspection, TEXT("data"), StateData))
			{
				const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
				if (TryGetArray(StateData, TEXT("transitions"), Transitions))
				{
					for (const TSharedPtr<FJsonValue>& TransitionValue : *Transitions)
					{
						TSharedPtr<FJsonObject> Transition = TransitionValue->AsObject();
						TSharedRef<FJsonObject> Explanation = MakeShared<FJsonObject>();
						Explanation->SetObjectField(TEXT("transition"), Transition.ToSharedRef());
						FString Reason = TEXT("The bridge cannot read per-condition runtime pass/fail values from public UE 5.7 APIs. Static blockers to check: disabled transition, trigger type not currently happening, required event absent, invalid target, or one of the listed conditions returning false.");
						if (!Transition->GetBoolField(TEXT("enabled")))
						{
							Reason = TEXT("Transition is disabled.");
						}
						else if (GetOptionalString(Transition, TEXT("trigger")).Contains(TEXT("OnEvent")))
						{
							Reason = TEXT("Transition requires an event. If the event is not in the runtime event queue during selection, it will not fire.");
						}
						Explanation->SetStringField(TEXT("likely_blocker"), Reason);
						Explanation->SetStringField(TEXT("next_step"), TEXT("Inspect the transition conditions and dependent Blueprints, then send/verify the required event or property values."));
						Explanations.Add(MakeShared<FJsonValueObject>(Explanation));
					}
				}
			}
		}
	}
	Data->SetArrayField(TEXT("blocked_transition_explanations"), Explanations);
	Data->SetNumberField(TEXT("count"), Explanations.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDebugReport(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	const FString AssetPath = GetOptionalString(Arguments, TEXT("asset_path"), GetOptionalString(Arguments, TEXT("state_tree")));
	if (!AssetPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> AssetArgs = MakeShared<FJsonObject>();
		AssetArgs->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetObjectField(TEXT("static_structure"), ToolInspectStateTree(AssetArgs));
		Data->SetObjectField(TEXT("validation"), ToolValidateStateTree(AssetArgs));
		Data->SetObjectField(TEXT("dependencies"), ToolStateTreeDependencyReport(AssetArgs));
	}
	if (!GetOptionalString(Arguments, TEXT("actor"), GetOptionalString(Arguments, TEXT("actor_path"), GetOptionalString(Arguments, TEXT("actor_name")))).IsEmpty() ||
		GetOptionalBool(Arguments, TEXT("selected_actor"), false) || !GetOptionalString(Arguments, TEXT("component_path")).IsEmpty())
	{
		Data->SetObjectField(TEXT("runtime_snapshot"), ToolStateTreeDebugSnapshot(Arguments));
		Data->SetObjectField(TEXT("blocked_transition_analysis"), ToolStateTreeExplainBlockedTransitions(Arguments));
	}
	Data->SetStringField(TEXT("likely_cause_analysis"), TEXT("Use validation issues first, then compare the runtime active state to outgoing transitions and dependent Blueprint summaries. Runtime transition candidate and condition-result internals are limited by public UE 5.7 APIs."));
	Data->SetStringField(TEXT("next_steps"), TEXT("Inspect the active state's transitions, inspect every listed STT_/STC_/STE_ Blueprint, then verify runtime context data/events/parameters used by failed conditions."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeFailureReport(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("symptom"), GetOptionalString(Arguments, TEXT("symptom"), TEXT("unspecified")));
	Data->SetStringField(TEXT("focus_state"), GetOptionalString(Arguments, TEXT("state_path"), GetOptionalString(Arguments, TEXT("state"))));
	Data->SetObjectField(TEXT("debug_report"), ToolStateTreeDebugReport(Arguments));
	Data->SetObjectField(TEXT("blocked_transitions"), ToolStateTreeExplainBlockedTransitions(Arguments));
	Data->SetStringField(TEXT("explanation"), TEXT("This report combines static validation, current runtime state, and transition conditions. If a task never finishes, inspect its StateTree task Blueprint for FinishTask/BroadcastDelegate calls and latent action cleanup."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeCompareRuntimeToAsset(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> SnapshotResult = ToolStateTreeDebugSnapshot(Arguments);
	if (!SnapshotResult->GetBoolField(TEXT("ok")))
	{
		return SnapshotResult;
	}

	TSharedPtr<FJsonObject> SnapshotData = GetEnvelopeData(SnapshotResult);
	TSharedPtr<FJsonObject> ComponentSnapshot = GetFirstComponentSnapshot(SnapshotData);
	const FString StateTreePath = ComponentSnapshot.IsValid() ? GetOptionalString(ComponentSnapshot, TEXT("state_tree")) : GetOptionalString(Arguments, TEXT("asset_path"));
	if (StateTreePath.IsEmpty())
	{
		return MakeErrorObject(TEXT("runtime_not_available"), TEXT("Runtime snapshot did not expose an active StateTree asset."));
	}

	TArray<TSharedPtr<FJsonValue>> ActiveStateInspections;
	const TArray<TSharedPtr<FJsonValue>>* ActivePaths = nullptr;
	if (ComponentSnapshot.IsValid() && TryGetArray(ComponentSnapshot, TEXT("active_state_paths"), ActivePaths))
	{
		for (const TSharedPtr<FJsonValue>& PathValue : *ActivePaths)
		{
			TSharedPtr<FJsonObject> StateArgs = MakeShared<FJsonObject>();
			StateArgs->SetStringField(TEXT("asset_path"), StateTreePath);
			StateArgs->SetStringField(TEXT("state_path"), PathValue->AsString());
			ActiveStateInspections.Add(MakeShared<FJsonValueObject>(ToolInspectStateTreeState(StateArgs)));
		}
	}

	TSharedRef<FJsonObject> InspectArgs = MakeShared<FJsonObject>();
	InspectArgs->SetStringField(TEXT("asset_path"), StateTreePath);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("runtime_snapshot"), SnapshotData.IsValid() ? SnapshotData.ToSharedRef() : MakeShared<FJsonObject>());
	Data->SetObjectField(TEXT("asset_structure"), ToolInspectStateTree(InspectArgs));
	Data->SetArrayField(TEXT("active_state_static_details"), ActiveStateInspections);
	Data->SetObjectField(TEXT("blocked_transition_analysis"), ToolStateTreeExplainBlockedTransitions(Arguments));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateStateTree(const TSharedPtr<FJsonObject>& Arguments)
{
	FString RequestedPackagePath;
	FString Error;
	if (!RequireString(Arguments, TEXT("package_path"), RequestedPackagePath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	FString PackageFolderPath;
	FString AssetName;
	if (!SplitAssetPackagePath(RequestedPackagePath, PackageFolderPath, AssetName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!IsGameContentPath(PackageFolderPath))
	{
		return MakeErrorObject(TEXT("unsafe_path"), TEXT("StateTree creation is limited to /Game content paths."));
	}
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *PackageFolderPath, *AssetName);
	if (ResolveObjectPath(ObjectPathFromPackagePath(PackagePath)))
	{
		return MakeErrorObject(TEXT("asset_exists"), FString::Printf(TEXT("Asset '%s' already exists."), *PackagePath));
	}

	UClass* SchemaClass = UStateTreeComponentSchema::StaticClass();
	const FString SchemaClassPath = GetOptionalString(Arguments, TEXT("schema_class"));
	if (!SchemaClassPath.IsEmpty())
	{
		SchemaClass = ResolveClassReference(SchemaClassPath);
		if (!SchemaClass || !SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()))
		{
			return MakeErrorObject(TEXT("invalid_arguments"), FString::Printf(TEXT("Schema class '%s' is not a UStateTreeSchema."), *SchemaClassPath));
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "CreateStateTree", "LLM Create StateTree"));
	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	Factory->SetSchemaClass(SchemaClass);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackageFolderPath, UStateTree::StaticClass(), Factory);
	UStateTree* StateTree = Cast<UStateTree>(NewAsset);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("unsupported_statetree_asset"), FString::Printf(TEXT("Failed to create StateTree '%s'."), *PackagePath));
	}
	StateTree->MarkPackageDirty();

	TSharedRef<FJsonObject> Data = BuildStateTreeInspection(StateTree, true);
	Data->SetStringField(TEXT("created_asset_path"), StateTree->GetPathName());
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		FString Filename;
		if (!SaveAssetPackage(StateTree, Filename, Error))
		{
			return MakeErrorObject(TEXT("save_failed"), Error);
		}
		Data->SetStringField(TEXT("filename"), Filename);
	}
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDuplicateStateTree(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	if (!LoadStateTreeAsset(AssetPath, Error))
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	return ToolDuplicateAsset(Arguments);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddStateTreeState(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString NewName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("name"), NewName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}

	EStateTreeStateType StateType = EStateTreeStateType::State;
	const FString TypeText = GetOptionalString(Arguments, TEXT("type"));
	if (!TypeText.IsEmpty())
	{
		if (const int64 Value = ResolveEnumValueByString(StaticEnum<EStateTreeStateType>(), TypeText); Value != INDEX_NONE)
		{
			StateType = static_cast<EStateTreeStateType>(Value);
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddStateTreeState", "LLM Add StateTree State"));
	StateTree->Modify();
	EditorData->Modify();
	UStateTreeState* NewState = nullptr;
	if (GetOptionalBool(Arguments, TEXT("as_root"), false) || (GetOptionalString(Arguments, TEXT("parent_state_path")).IsEmpty() && GetOptionalString(Arguments, TEXT("parent_state_id")).IsEmpty() && EditorData->SubTrees.Num() == 0))
	{
		NewState = &EditorData->AddSubTree(FName(*NewName));
		NewState->Type = StateType;
	}
	else
	{
		TSharedPtr<FJsonObject> ParentArgs = MakeShared<FJsonObject>();
		ParentArgs->SetStringField(TEXT("state_path"), GetOptionalString(Arguments, TEXT("parent_state_path"), GetOptionalString(Arguments, TEXT("parent"))));
		ParentArgs->SetStringField(TEXT("state_id"), GetOptionalString(Arguments, TEXT("parent_state_id")));
		if (ParentArgs->GetStringField(TEXT("state_path")).IsEmpty() && ParentArgs->GetStringField(TEXT("state_id")).IsEmpty() && EditorData->SubTrees.Num() > 0)
		{
			ParentArgs->SetStringField(TEXT("state_id"), GuidToString(EditorData->SubTrees[0]->ID));
		}
		UStateTreeState* Parent = ResolveMutableState(EditorData, ParentArgs, Error);
		if (!Parent)
		{
			return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
		}
		Parent->Modify();
		NewState = &Parent->AddChildState(FName(*NewName), StateType);
	}
	if (NewState && !NewState->ID.IsValid())
	{
		NewState->ID = FGuid::NewGuid();
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));

	TSharedRef<FJsonObject> Data = StateToJson(StateTree, EditorData, NewState, false);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRenameStateTreeState(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString NewName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("new_name"), NewName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	UStateTreeState* State = ResolveMutableState(EditorData, Arguments, Error);
	if (!State)
	{
		return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RenameStateTreeState", "LLM Rename StateTree State"));
	StateTree->Modify();
	State->Modify();
	State->Name = FName(*NewName);
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	return MakeOk(StateToJson(StateTree, EditorData, State, false));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolMoveStateTreeState(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	UStateTreeState* State = ResolveMutableState(EditorData, Arguments, Error);
	if (!State)
	{
		return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
	}

	UStateTreeState* NewParent = nullptr;
	if (!GetOptionalBool(Arguments, TEXT("as_root"), false))
	{
		TSharedPtr<FJsonObject> ParentArgs = MakeShared<FJsonObject>();
		ParentArgs->SetStringField(TEXT("state_path"), GetOptionalString(Arguments, TEXT("new_parent_state_path"), GetOptionalString(Arguments, TEXT("parent_state_path"))));
		ParentArgs->SetStringField(TEXT("state_id"), GetOptionalString(Arguments, TEXT("new_parent_state_id"), GetOptionalString(Arguments, TEXT("parent_state_id"))));
		NewParent = ResolveMutableState(EditorData, ParentArgs, Error);
		if (!NewParent)
		{
			return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
		}
		for (UStateTreeState* Cursor = NewParent; Cursor; Cursor = Cursor->Parent)
		{
			if (Cursor == State)
			{
				return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Cannot move a state under one of its descendants."));
			}
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "MoveStateTreeState", "LLM Move StateTree State"));
	StateTree->Modify();
	EditorData->Modify();
	if (State->Parent)
	{
		State->Parent->Modify();
		State->Parent->Children.Remove(State);
	}
	else
	{
		EditorData->SubTrees.Remove(State);
	}
	if (NewParent)
	{
		NewParent->Modify();
		NewParent->Children.Add(State);
		State->Parent = NewParent;
	}
	else
	{
		EditorData->SubTrees.Add(State);
		State->Parent = nullptr;
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	return MakeOk(StateToJson(StateTree, EditorData, State, false));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveStateTreeState(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	UStateTreeState* State = ResolveMutableState(EditorData, Arguments, Error);
	if (!State)
	{
		return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
	}

	const FString RemovedPath = MakeStatePath(State);
	const FString RemovedId = GuidToString(State->ID);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveStateTreeState", "LLM Remove StateTree State"));
	StateTree->Modify();
	EditorData->Modify();
	if (State->Parent)
	{
		State->Parent->Modify();
		State->Parent->Children.Remove(State);
	}
	else
	{
		EditorData->SubTrees.Remove(State);
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_state_id"), RemovedId);
	Data->SetStringField(TEXT("removed_state_path"), RemovedPath);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddStateTreeTask(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ClassPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("task_class"), ClassPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	UClass* TaskClass = ResolveClassReference(ClassPath);
	FStateTreeEditorNode NewNode;

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddStateTreeTask", "LLM Add StateTree Task"));
	StateTree->Modify();
	EditorData->Modify();
	FStateTreeEditorNode* AddedNode = nullptr;
	if (GetOptionalBool(Arguments, TEXT("global"), false))
	{
		if (!InitializeStateTreeBlueprintEditorNode(NewNode, StateTree, TaskClass, TEXT("global_task"), Error))
		{
			return MakeErrorObject(TEXT("task_not_found"), Error);
		}
		AddedNode = &EditorData->GlobalTasks.Add_GetRef(NewNode);
	}
	else
	{
		UStateTreeState* State = ResolveMutableState(EditorData, Arguments, Error);
		if (!State)
		{
			return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
		}
		State->Modify();
		if (!InitializeStateTreeBlueprintEditorNode(NewNode, State, TaskClass, TEXT("task"), Error))
		{
			return MakeErrorObject(TEXT("task_not_found"), Error);
		}
		if (GetOptionalBool(Arguments, TEXT("single_task"), false))
		{
			State->SingleTask = NewNode;
			AddedNode = &State->SingleTask;
		}
		else
		{
			AddedNode = &State->Tasks.Add_GetRef(NewNode);
		}
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	return MakeOk(StateTreeNodeToJson(EditorData, StateTree, *AddedNode, TEXT("task")));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveStateTreeTask(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString NodeId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("node_id"), NodeId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveStateTreeTask", "LLM Remove StateTree Task"));
	StateTree->Modify();
	EditorData->Modify();
	bool bRemoved = RemoveStateTreeEditorNodeById(EditorData->GlobalTasks, NodeId);
	TArray<UStateTreeState*> States;
	GatherMutableStateTreeStates(EditorData, States);
	for (UStateTreeState* State : States)
	{
		if (bRemoved)
		{
			break;
		}
		State->Modify();
		bRemoved = RemoveStateTreeEditorNodeById(State->Tasks, NodeId);
		if (!bRemoved && State->SingleTask.Node.IsValid() &&
			(GuidToString(State->SingleTask.ID).Equals(NodeId, ESearchCase::IgnoreCase) || GuidToString(State->SingleTask.GetNodeID()).Equals(NodeId, ESearchCase::IgnoreCase)))
		{
			State->SingleTask = FStateTreeEditorNode();
			bRemoved = true;
		}
	}
	if (!bRemoved)
	{
		return MakeErrorObject(TEXT("task_not_found"), TEXT("Task node was not found."));
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_node_id"), NodeId);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddStateTreeCondition(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString ClassPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("condition_class"), ClassPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	UClass* ConditionClass = ResolveClassReference(ClassPath);
	FStateTreeEditorNode NewNode;

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddStateTreeCondition", "LLM Add StateTree Condition"));
	StateTree->Modify();
	EditorData->Modify();
	FStateTreeEditorNode* AddedNode = nullptr;
	const FString TransitionId = GetOptionalString(Arguments, TEXT("transition_id"));
	if (!TransitionId.IsEmpty())
	{
		UStateTreeState* OwnerState = nullptr;
		FStateTreeTransition* Transition = FindMutableTransition(EditorData, TransitionId, &OwnerState);
		if (!Transition)
		{
			return MakeErrorObject(TEXT("condition_not_found"), TEXT("Transition was not found for condition insertion."));
		}
		if (OwnerState)
		{
			OwnerState->Modify();
		}
		if (!InitializeStateTreeBlueprintEditorNode(NewNode, OwnerState ? Cast<UObject>(OwnerState) : Cast<UObject>(StateTree), ConditionClass, TEXT("transition_condition"), Error))
		{
			return MakeErrorObject(TEXT("condition_not_found"), Error);
		}
		AddedNode = &Transition->Conditions.Add_GetRef(NewNode);
	}
	else
	{
		UStateTreeState* State = ResolveMutableState(EditorData, Arguments, Error);
		if (!State)
		{
			return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
		}
		State->Modify();
		if (!InitializeStateTreeBlueprintEditorNode(NewNode, State, ConditionClass, TEXT("enter_condition"), Error))
		{
			return MakeErrorObject(TEXT("condition_not_found"), Error);
		}
		AddedNode = &State->EnterConditions.Add_GetRef(NewNode);
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	return MakeOk(StateTreeNodeToJson(EditorData, StateTree, *AddedNode, TEXT("condition")));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveStateTreeCondition(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString NodeId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("node_id"), NodeId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveStateTreeCondition", "LLM Remove StateTree Condition"));
	StateTree->Modify();
	EditorData->Modify();
	bool bRemoved = false;
	TArray<UStateTreeState*> States;
	GatherMutableStateTreeStates(EditorData, States);
	for (UStateTreeState* State : States)
	{
		State->Modify();
		bRemoved = RemoveStateTreeEditorNodeById(State->EnterConditions, NodeId);
		if (bRemoved)
		{
			break;
		}
		for (FStateTreeTransition& Transition : State->Transitions)
		{
			bRemoved = RemoveStateTreeEditorNodeById(Transition.Conditions, NodeId);
			if (bRemoved)
			{
				break;
			}
		}
		if (bRemoved)
		{
			break;
		}
	}
	if (!bRemoved)
	{
		return MakeErrorObject(TEXT("condition_not_found"), TEXT("Condition node was not found."));
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_node_id"), NodeId);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolAddStateTreeTransition(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}

	TSharedPtr<FJsonObject> SourceArgs = MakeShared<FJsonObject>();
	SourceArgs->SetStringField(TEXT("state_path"), GetOptionalString(Arguments, TEXT("source_state_path"), GetOptionalString(Arguments, TEXT("state_path"))));
	SourceArgs->SetStringField(TEXT("state_id"), GetOptionalString(Arguments, TEXT("source_state_id"), GetOptionalString(Arguments, TEXT("state_id"))));
	UStateTreeState* Source = ResolveMutableState(EditorData, SourceArgs, Error);
	if (!Source)
	{
		return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
	}
	TSharedPtr<FJsonObject> TargetArgs = MakeShared<FJsonObject>();
	TargetArgs->SetStringField(TEXT("state_path"), GetOptionalString(Arguments, TEXT("target_state_path"), GetOptionalString(Arguments, TEXT("target"))));
	TargetArgs->SetStringField(TEXT("state_id"), GetOptionalString(Arguments, TEXT("target_state_id")));
	UStateTreeState* Target = ResolveMutableState(EditorData, TargetArgs, Error);
	if (!Target)
	{
		return MakeErrorObject(Error.Contains(TEXT("ambiguous")) ? TEXT("ambiguous_state") : TEXT("state_not_found"), Error);
	}

	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	const FString TriggerText = GetOptionalString(Arguments, TEXT("trigger"));
	if (!TriggerText.IsEmpty())
	{
		if (const int64 Value = ResolveEnumValueByString(StaticEnum<EStateTreeTransitionTrigger>(), TriggerText); Value != INDEX_NONE)
		{
			Trigger = static_cast<EStateTreeTransitionTrigger>(Value);
		}
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "AddStateTreeTransition", "LLM Add StateTree Transition"));
	StateTree->Modify();
	Source->Modify();
	const FString EventTag = GetOptionalString(Arguments, TEXT("event_tag"));
	FStateTreeTransition& NewTransition = EventTag.IsEmpty() ?
		Source->AddTransition(Trigger, EStateTreeTransitionType::GotoState, Target) :
		Source->AddTransition(Trigger, FGameplayTag::RequestGameplayTag(FName(*EventTag), false), EStateTreeTransitionType::GotoState, Target);

	const FString PriorityText = GetOptionalString(Arguments, TEXT("priority"));
	if (!PriorityText.IsEmpty())
	{
		if (const int64 Value = ResolveEnumValueByString(StaticEnum<EStateTreeTransitionPriority>(), PriorityText); Value != INDEX_NONE)
		{
			NewTransition.Priority = static_cast<EStateTreeTransitionPriority>(Value);
		}
	}
	NewTransition.bTransitionEnabled = GetOptionalBool(Arguments, TEXT("enabled"), true);
	NewTransition.bDelayTransition = GetOptionalBool(Arguments, TEXT("delay_enabled"), false);
	NewTransition.DelayDuration = static_cast<float>(GetOptionalInt(Arguments, TEXT("delay_ms"), 0)) / 1000.0f;
	if (Arguments->HasField(TEXT("delay_duration")))
	{
		double Delay = 0.0;
		Arguments->TryGetNumberField(TEXT("delay_duration"), Delay);
		NewTransition.DelayDuration = static_cast<float>(Delay);
		NewTransition.bDelayTransition = Delay > 0.0;
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	return MakeOk(TransitionToJson(EditorData, Source, NewTransition, MakeStatePath(Source), Source->Transitions.Num() - 1));
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolRemoveStateTreeTransition(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString TransitionId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("transition_id"), TransitionId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "RemoveStateTreeTransition", "LLM Remove StateTree Transition"));
	StateTree->Modify();
	TArray<UStateTreeState*> States;
	GatherMutableStateTreeStates(EditorData, States);
	bool bRemoved = false;
	for (UStateTreeState* State : States)
	{
		for (int32 Index = 0; Index < State->Transitions.Num(); ++Index)
		{
			if (GuidToString(State->Transitions[Index].ID).Equals(TransitionId, ESearchCase::IgnoreCase))
			{
				State->Modify();
				State->Transitions.RemoveAt(Index);
				bRemoved = true;
				break;
			}
		}
		if (bRemoved)
		{
			break;
		}
	}
	if (!bRemoved)
	{
		return MakeErrorObject(TEXT("state_not_found"), TEXT("Transition was not found."));
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removed_transition_id"), TransitionId);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetStateTreeProperty(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString PropertyPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("property_path"), PropertyPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Missing required field 'value'."));
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}

	UObject* TargetObject = nullptr;
	const FString NodeId = GetOptionalString(Arguments, TEXT("node_id"));
	if (!NodeId.IsEmpty())
	{
		if (FStateTreeEditorNode* Node = FindMutableStateTreeNodeById(EditorData, NodeId))
		{
			TargetObject = Node->InstanceObject;
		}
		if (!TargetObject)
		{
			return MakeErrorObject(TEXT("task_not_found"), TEXT("Node was not found or does not use UObject instance data."));
		}
	}
	else
	{
		TargetObject = ResolveMutableState(EditorData, Arguments, Error);
		if (!TargetObject)
		{
			TargetObject = StateTree;
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetStateTreeProperty", "LLM Set StateTree Property"));
	StateTree->Modify();
	TSharedPtr<FJsonObject> PropertyJson;
	if (!SetEditablePropertyValue(StateTree, TargetObject, PropertyPath, Value, Error, &PropertyJson))
	{
		return MakeErrorObject(TEXT("unsupported_property_type"), Error);
	}
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Data->SetStringField(TEXT("target_object"), TargetObject->GetPathName());
	Data->SetObjectField(TEXT("property"), PropertyJson.ToSharedRef());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetStateTreeBinding(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString SourceNodeId;
	FString SourcePath;
	FString TargetNodeId;
	FString TargetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("source_node_id"), SourceNodeId, Error) ||
		!RequireString(Arguments, TEXT("source_path"), SourcePath, Error) ||
		!RequireString(Arguments, TEXT("target_node_id"), TargetNodeId, Error) ||
		!RequireString(Arguments, TEXT("target_path"), TargetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	FStateTreeEditorNode* SourceNode = FindMutableStateTreeNodeById(EditorData, SourceNodeId);
	FStateTreeEditorNode* TargetNode = FindMutableStateTreeNodeById(EditorData, TargetNodeId);
	if (!SourceNode || !TargetNode)
	{
		return MakeErrorObject(TEXT("invalid_binding"), TEXT("Source or target node id was not found."));
	}
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetStateTreeBinding", "LLM Set StateTree Binding"));
	StateTree->Modify();
	EditorData->Modify();
	if (!EditorData->AddPropertyBinding(*SourceNode, SourcePath, *TargetNode, TargetPath))
	{
		return MakeErrorObject(TEXT("invalid_binding"), TEXT("StateTree editor data rejected the binding. Check node ids and property paths."));
	}
	StateTree->MarkPackageDirty();
	CompileStateTreeIfRequested(StateTree, GetOptionalBool(Arguments, TEXT("compile"), true));
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("source_node_id"), SourceNodeId);
	Data->SetStringField(TEXT("source_path"), SourcePath);
	Data->SetStringField(TEXT("target_node_id"), TargetNodeId);
	Data->SetStringField(TEXT("target_path"), TargetPath);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCompileOrValidateStateTree(const TSharedPtr<FJsonObject>& Arguments)
{
	return ToolValidateStateTree(Arguments);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSaveStateTree(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	if (!StateTree)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}
	FString Filename;
	if (!SaveAssetPackage(StateTree, Filename, Error))
	{
		return MakeErrorObject(TEXT("save_failed"), Error);
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Data->SetStringField(TEXT("filename"), Filename);
	Data->SetBoolField(TEXT("saved"), true);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateBasicStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TArray<TSharedPtr<FJsonValue>> Steps;
	auto AddStep = [&Steps](const FString& Name, const TSharedRef<FJsonObject>& Result)
	{
		TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("step"), Name);
		Step->SetObjectField(TEXT("result"), Result);
		Steps.Add(MakeShared<FJsonValueObject>(Step));
	};

	TSharedRef<FJsonObject> CreateResult = ToolCreateStateTree(Arguments);
	AddStep(TEXT("create_statetree"), CreateResult);
	if (!CreateResult->GetBoolField(TEXT("ok")))
	{
		return CreateResult;
	}
	const FString PackagePath = GetOptionalString(Arguments, TEXT("package_path"));
	const FString AssetPath = NormalizeAssetObjectPath(PackagePath);

	auto AddState = [&](const FString& Name)
	{
		TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
		AddArgs->SetStringField(TEXT("asset_path"), AssetPath);
		AddArgs->SetStringField(TEXT("parent_state_path"), TEXT("Root"));
		AddArgs->SetStringField(TEXT("name"), Name);
		AddArgs->SetBoolField(TEXT("compile"), false);
		TSharedRef<FJsonObject> Result = ToolAddStateTreeState(AddArgs);
		AddStep(FString::Printf(TEXT("add_state_%s"), *Name), Result);
	};
	AddState(TEXT("Idle"));
	AddState(TEXT("MoveTo"));
	AddState(TEXT("Done"));

	const FString MoveTaskClass = GetOptionalString(Arguments, TEXT("move_task_class"), GetOptionalString(Arguments, TEXT("task_class")));
	if (!MoveTaskClass.IsEmpty())
	{
		TSharedPtr<FJsonObject> TaskArgs = MakeShared<FJsonObject>();
		TaskArgs->SetStringField(TEXT("asset_path"), AssetPath);
		TaskArgs->SetStringField(TEXT("state_path"), TEXT("Root/MoveTo"));
		TaskArgs->SetStringField(TEXT("task_class"), MoveTaskClass);
		TaskArgs->SetBoolField(TEXT("compile"), false);
		AddStep(TEXT("add_move_task"), ToolAddStateTreeTask(TaskArgs));
	}

	auto AddTransition = [&](const FString& Source, const FString& Target)
	{
		TSharedPtr<FJsonObject> TransitionArgs = MakeShared<FJsonObject>();
		TransitionArgs->SetStringField(TEXT("asset_path"), AssetPath);
		TransitionArgs->SetStringField(TEXT("source_state_path"), Source);
		TransitionArgs->SetStringField(TEXT("target_state_path"), Target);
		TransitionArgs->SetStringField(TEXT("trigger"), TEXT("OnStateCompleted"));
		TransitionArgs->SetBoolField(TEXT("compile"), false);
		AddStep(FString::Printf(TEXT("transition_%s_to_%s"), *Source, *Target), ToolAddStateTreeTransition(TransitionArgs));
	};
	AddTransition(TEXT("Root/Idle"), TEXT("Root/MoveTo"));
	AddTransition(TEXT("Root/MoveTo"), TEXT("Root/Done"));

	TSharedPtr<FJsonObject> ValidateArgs = MakeShared<FJsonObject>();
	ValidateArgs->SetStringField(TEXT("asset_path"), AssetPath);
	ValidateArgs->SetBoolField(TEXT("compile"), true);
	AddStep(TEXT("validate"), ToolValidateStateTree(ValidateArgs));
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		AddStep(TEXT("save"), ToolSaveStateTree(ValidateArgs));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetArrayField(TEXT("steps"), Steps);
	Data->SetStringField(TEXT("recipe"), TEXT("basic_idle_move_done"));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreatePatrolChaseAttackStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TArray<TSharedPtr<FJsonValue>> Steps;
	auto AddStep = [&Steps](const FString& Name, const TSharedRef<FJsonObject>& Result)
	{
		TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("step"), Name);
		Step->SetObjectField(TEXT("result"), Result);
		Steps.Add(MakeShared<FJsonValueObject>(Step));
	};
	TSharedRef<FJsonObject> CreateResult = ToolCreateStateTree(Arguments);
	AddStep(TEXT("create_statetree"), CreateResult);
	if (!CreateResult->GetBoolField(TEXT("ok")))
	{
		return CreateResult;
	}
	const FString AssetPath = NormalizeAssetObjectPath(GetOptionalString(Arguments, TEXT("package_path")));
	const TArray<FString> States = { TEXT("Guard"), TEXT("Patrol"), TEXT("Chase"), TEXT("Attack"), TEXT("Recover") };
	for (const FString& StateName : States)
	{
		TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
		AddArgs->SetStringField(TEXT("asset_path"), AssetPath);
		AddArgs->SetStringField(TEXT("parent_state_path"), TEXT("Root"));
		AddArgs->SetStringField(TEXT("name"), StateName);
		AddArgs->SetBoolField(TEXT("compile"), false);
		AddStep(FString::Printf(TEXT("add_state_%s"), *StateName), ToolAddStateTreeState(AddArgs));
	}

	const TMap<FString, FString> OptionalTasks = {
		{ TEXT("patrol_task_class"), TEXT("Root/Patrol") },
		{ TEXT("chase_task_class"), TEXT("Root/Chase") },
		{ TEXT("attack_task_class"), TEXT("Root/Attack") }
	};
	for (const TPair<FString, FString>& Pair : OptionalTasks)
	{
		const FString TaskClass = GetOptionalString(Arguments, *Pair.Key);
		if (!TaskClass.IsEmpty())
		{
			TSharedPtr<FJsonObject> TaskArgs = MakeShared<FJsonObject>();
			TaskArgs->SetStringField(TEXT("asset_path"), AssetPath);
			TaskArgs->SetStringField(TEXT("state_path"), Pair.Value);
			TaskArgs->SetStringField(TEXT("task_class"), TaskClass);
			TaskArgs->SetBoolField(TEXT("compile"), false);
			AddStep(FString::Printf(TEXT("add_task_%s"), *Pair.Key), ToolAddStateTreeTask(TaskArgs));
		}
	}

	const TArray<TPair<FString, FString>> Transitions = {
		{ TEXT("Root/Guard"), TEXT("Root/Patrol") },
		{ TEXT("Root/Patrol"), TEXT("Root/Chase") },
		{ TEXT("Root/Chase"), TEXT("Root/Attack") },
		{ TEXT("Root/Attack"), TEXT("Root/Recover") },
		{ TEXT("Root/Recover"), TEXT("Root/Chase") }
	};
	for (const TPair<FString, FString>& Pair : Transitions)
	{
		TSharedPtr<FJsonObject> TransitionArgs = MakeShared<FJsonObject>();
		TransitionArgs->SetStringField(TEXT("asset_path"), AssetPath);
		TransitionArgs->SetStringField(TEXT("source_state_path"), Pair.Key);
		TransitionArgs->SetStringField(TEXT("target_state_path"), Pair.Value);
		TransitionArgs->SetStringField(TEXT("trigger"), TEXT("OnStateCompleted"));
		TransitionArgs->SetBoolField(TEXT("compile"), false);
		AddStep(FString::Printf(TEXT("transition_%s_to_%s"), *Pair.Key, *Pair.Value), ToolAddStateTreeTransition(TransitionArgs));
	}

	TSharedPtr<FJsonObject> ValidateArgs = MakeShared<FJsonObject>();
	ValidateArgs->SetStringField(TEXT("asset_path"), AssetPath);
	ValidateArgs->SetBoolField(TEXT("compile"), true);
	AddStep(TEXT("validate"), ToolValidateStateTree(ValidateArgs));
	if (GetOptionalBool(Arguments, TEXT("save"), false))
	{
		AddStep(TEXT("save"), ToolSaveStateTree(ValidateArgs));
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("recipe"), TEXT("patrol_chase_attack"));
	Data->SetArrayField(TEXT("steps"), Steps);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateGasSkillStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Result = ToolCreatePatrolChaseAttackStateTreeRecipe(Arguments);
	if (!Result->GetBoolField(TEXT("ok")))
	{
		return Result;
	}
	TSharedPtr<FJsonObject> Data = GetEnvelopeData(Result);
	const FString AssetPath = Data.IsValid() ? GetOptionalString(Data, TEXT("asset_path")) : NormalizeAssetObjectPath(GetOptionalString(Arguments, TEXT("package_path")));
	const FString TaskClass = GetOptionalString(Arguments, TEXT("gas_task_class"), GetOptionalString(Arguments, TEXT("task_class")));
	if (!TaskClass.IsEmpty())
	{
		TSharedPtr<FJsonObject> TaskArgs = MakeShared<FJsonObject>();
		TaskArgs->SetStringField(TEXT("asset_path"), AssetPath);
		TaskArgs->SetStringField(TEXT("state_path"), TEXT("Root/Attack"));
		TaskArgs->SetStringField(TEXT("task_class"), TaskClass);
		TaskArgs->SetBoolField(TEXT("compile"), true);
		TSharedRef<FJsonObject> AddTask = ToolAddStateTreeTask(TaskArgs);
		if (Data.IsValid())
		{
			Data->SetObjectField(TEXT("gas_task_add_result"), AddTask);
		}
	}
	if (Data.IsValid())
	{
		Data->SetStringField(TEXT("recipe"), TEXT("gas_skill_runner"));
		Data->SetStringField(TEXT("note"), TEXT("Pass gas_task_class/STT_RunGameplayAbility and ability defaults via set_statetree_property to wire concrete GA_/GE_ assets."));
		return MakeOk(Data);
	}
	return Result;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCreateEqsPositioningStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> Result = ToolCreateBasicStateTreeRecipe(Arguments);
	if (!Result->GetBoolField(TEXT("ok")))
	{
		return Result;
	}
	TSharedPtr<FJsonObject> Data = GetEnvelopeData(Result);
	const FString AssetPath = Data.IsValid() ? GetOptionalString(Data, TEXT("asset_path")) : NormalizeAssetObjectPath(GetOptionalString(Arguments, TEXT("package_path")));
	const FString TaskClass = GetOptionalString(Arguments, TEXT("eqs_task_class"), GetOptionalString(Arguments, TEXT("task_class")));
	if (!TaskClass.IsEmpty())
	{
		TSharedPtr<FJsonObject> TaskArgs = MakeShared<FJsonObject>();
		TaskArgs->SetStringField(TEXT("asset_path"), AssetPath);
		TaskArgs->SetStringField(TEXT("state_path"), TEXT("Root/MoveTo"));
		TaskArgs->SetStringField(TEXT("task_class"), TaskClass);
		TaskArgs->SetBoolField(TEXT("compile"), true);
		TSharedRef<FJsonObject> AddTask = ToolAddStateTreeTask(TaskArgs);
		if (Data.IsValid())
		{
			Data->SetObjectField(TEXT("eqs_task_add_result"), AddTask);
		}
	}
	if (Data.IsValid())
	{
		Data->SetStringField(TEXT("recipe"), TEXT("eqs_positioning"));
		Data->SetStringField(TEXT("note"), TEXT("Pass eqs_task_class/STT_FindSurroundPosition and then set query/context/default properties with set_statetree_property."));
		return MakeOk(Data);
	}
	return Result;
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeDependencyReport(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	TSet<FString> TaskBlueprints;
	TSet<FString> ConditionBlueprints;
	TSet<FString> EvaluatorBlueprints;
	TSet<FString> ReferencedAssets;
	auto AddNode = [&](const FStateTreeEditorNode& Node, const FString& Role, const FString& StatePath, int32 Index)
	{
		TSharedRef<FJsonObject> NodeJson = StateTreeNodeToJson(EditorData, StateTree, Node, Role, Index);
		NodeJson->SetStringField(TEXT("state_path"), StatePath);
		const FString BlueprintAsset = GetOptionalString(NodeJson, TEXT("blueprint_asset"));
		if (!BlueprintAsset.IsEmpty())
		{
			if (Role.Contains(TEXT("task")))
			{
				AddUniqueString(TaskBlueprints, BlueprintAsset);
			}
			else if (Role.Contains(TEXT("condition")))
			{
				AddUniqueString(ConditionBlueprints, BlueprintAsset);
			}
			else if (Role.Contains(TEXT("evaluator")))
			{
				AddUniqueString(EvaluatorBlueprints, BlueprintAsset);
			}
			AddUniqueString(ReferencedAssets, BlueprintAsset);
		}
		CollectJsonStringSignals(NodeJson, ReferencedAssets);
		Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));
	};

	for (int32 Index = 0; Index < EditorData->Evaluators.Num(); ++Index)
	{
		AddNode(EditorData->Evaluators[Index], TEXT("evaluator"), FString(), Index);
	}
	for (int32 Index = 0; Index < EditorData->GlobalTasks.Num(); ++Index)
	{
		AddNode(EditorData->GlobalTasks[Index], TEXT("global_task"), FString(), Index);
	}
	TArray<const UStateTreeState*> States;
	GatherStateTreeStates(EditorData, States);
	for (const UStateTreeState* State : States)
	{
		for (int32 Index = 0; Index < State->Tasks.Num(); ++Index)
		{
			AddNode(State->Tasks[Index], TEXT("task"), MakeStatePath(State), Index);
		}
		if (State->SingleTask.Node.IsValid())
		{
			AddNode(State->SingleTask, TEXT("task"), MakeStatePath(State), State->Tasks.Num());
		}
		for (int32 Index = 0; Index < State->EnterConditions.Num(); ++Index)
		{
			AddNode(State->EnterConditions[Index], TEXT("enter_condition"), MakeStatePath(State), Index);
		}
		for (const FStateTreeTransition& Transition : State->Transitions)
		{
			for (int32 Index = 0; Index < Transition.Conditions.Num(); ++Index)
			{
				AddNode(Transition.Conditions[Index], TEXT("transition_condition"), MakeStatePath(State), Index);
			}
		}
		if (State->LinkedAsset)
		{
			AddUniqueString(ReferencedAssets, State->LinkedAsset->GetPathName());
		}
	}

	TArray<TSharedPtr<FJsonValue>> GasSignals;
	TArray<TSharedPtr<FJsonValue>> EqsSignals;
	TArray<TSharedPtr<FJsonValue>> MovementSignals;
	for (const TSharedPtr<FJsonValue>& NodeValue : Nodes)
	{
		TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
		const FString Text = SerializeJson(NodeObject.ToSharedRef()).ToLower();
		if (Text.Contains(TEXT("gameplay")) || Text.Contains(TEXT("ability")) || Text.Contains(TEXT("gas")) || Text.Contains(TEXT("ga_")) || Text.Contains(TEXT("ge_")))
		{
			GasSignals.Add(NodeValue);
		}
		if (Text.Contains(TEXT("eqs")) || Text.Contains(TEXT("envquery")) || Text.Contains(TEXT("surround")))
		{
			EqsSignals.Add(NodeValue);
		}
		if (Text.Contains(TEXT("move")) || Text.Contains(TEXT("navigation")) || Text.Contains(TEXT("pathfollowing")))
		{
			MovementSignals.Add(NodeValue);
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Data->SetArrayField(TEXT("nodes"), Nodes);
	Data->SetArrayField(TEXT("task_blueprints"), PathsToJsonArray(TaskBlueprints));
	Data->SetArrayField(TEXT("condition_blueprints"), PathsToJsonArray(ConditionBlueprints));
	Data->SetArrayField(TEXT("evaluator_blueprints"), PathsToJsonArray(EvaluatorBlueprints));
	Data->SetArrayField(TEXT("referenced_assets"), PathsToJsonArray(ReferencedAssets));
	Data->SetArrayField(TEXT("gas_signals"), GasSignals);
	Data->SetArrayField(TEXT("eqs_signals"), EqsSignals);
	Data->SetArrayField(TEXT("movement_signals"), MovementSignals);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeGasReport(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> DependencyReport = ToolStateTreeDependencyReport(Arguments);
	if (!DependencyReport->GetBoolField(TEXT("ok")))
	{
		return DependencyReport;
	}
	TSharedPtr<FJsonObject> DependencyData = GetEnvelopeData(DependencyReport);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("dependency_report"), DependencyData.IsValid() ? DependencyData.ToSharedRef() : MakeShared<FJsonObject>());
	if (DependencyData.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* GasSignals = nullptr;
		if (TryGetArray(DependencyData, TEXT("gas_signals"), GasSignals))
		{
			Data->SetArrayField(TEXT("gas_nodes"), *GasSignals);
			Data->SetNumberField(TEXT("gas_node_count"), GasSignals->Num());
		}
	}
	Data->SetStringField(TEXT("explanation"), TEXT("GAS signals are inferred from StateTree node descriptions, Blueprint class names, referenced assets, gameplay tag fields, and ability/effect naming."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeEqsReport(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> DependencyReport = ToolStateTreeDependencyReport(Arguments);
	if (!DependencyReport->GetBoolField(TEXT("ok")))
	{
		return DependencyReport;
	}
	TSharedPtr<FJsonObject> DependencyData = GetEnvelopeData(DependencyReport);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("dependency_report"), DependencyData.IsValid() ? DependencyData.ToSharedRef() : MakeShared<FJsonObject>());
	if (DependencyData.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* EqsSignals = nullptr;
		if (TryGetArray(DependencyData, TEXT("eqs_signals"), EqsSignals))
		{
			Data->SetArrayField(TEXT("eqs_nodes"), *EqsSignals);
			Data->SetNumberField(TEXT("eqs_node_count"), EqsSignals->Num());
		}
	}
	Data->SetStringField(TEXT("explanation"), TEXT("EQS signals are inferred from node descriptions, Blueprint names, referenced query/context assets, and EnvQuery/EQS naming."));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolStateTreeBlueprintReport(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> DependencyReport = ToolStateTreeDependencyReport(Arguments);
	if (!DependencyReport->GetBoolField(TEXT("ok")))
	{
		return DependencyReport;
	}
	TSharedPtr<FJsonObject> DependencyData = GetEnvelopeData(DependencyReport);
	TArray<TSharedPtr<FJsonValue>> BlueprintSummaries;
	if (DependencyData.IsValid())
	{
		const TCHAR* Fields[] = { TEXT("task_blueprints"), TEXT("condition_blueprints"), TEXT("evaluator_blueprints") };
		for (const TCHAR* Field : Fields)
		{
			const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
			if (TryGetArray(DependencyData, Field, Assets))
			{
				for (const TSharedPtr<FJsonValue>& AssetValue : *Assets)
				{
					TSharedPtr<FJsonObject> InspectArgs = MakeShared<FJsonObject>();
					InspectArgs->SetStringField(TEXT("asset_path"), AssetValue->AsString());
					InspectArgs->SetBoolField(TEXT("include_nodes"), GetOptionalBool(Arguments, TEXT("include_nodes"), false));
					BlueprintSummaries.Add(MakeShared<FJsonValueObject>(ToolInspectStateTreeBlueprintDependency(InspectArgs)));
				}
			}
		}
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("dependency_report"), DependencyData.IsValid() ? DependencyData.ToSharedRef() : MakeShared<FJsonObject>());
	Data->SetArrayField(TEXT("blueprints"), BlueprintSummaries);
	Data->SetNumberField(TEXT("count"), BlueprintSummaries.Num());
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolExportStateTreeMarkdown(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	FString Markdown;
	Markdown += FString::Printf(TEXT("# %s\n\n"), *StateTree->GetName());
	Markdown += FString::Printf(TEXT("- Asset: `%s`\n"), *StateTree->GetPathName());
	Markdown += FString::Printf(TEXT("- Schema: `%s`\n"), StateTree->GetSchema() ? *StateTree->GetSchema()->GetClass()->GetPathName() : TEXT(""));
	Markdown += FString::Printf(TEXT("- Ready to run: `%s`\n\n"), StateTree->IsReadyToRun() ? TEXT("true") : TEXT("false"));
	Markdown += TEXT("## Evaluators And Global Tasks\n\n");
	for (int32 Index = 0; Index < EditorData->Evaluators.Num(); ++Index)
	{
		Markdown += FString::Printf(TEXT("- Evaluator: %s\n"), *StateTreeNodeToJson(EditorData, StateTree, EditorData->Evaluators[Index], TEXT("evaluator"), Index)->GetStringField(TEXT("description")));
	}
	for (int32 Index = 0; Index < EditorData->GlobalTasks.Num(); ++Index)
	{
		Markdown += FString::Printf(TEXT("- Global task: %s\n"), *StateTreeNodeToJson(EditorData, StateTree, EditorData->GlobalTasks[Index], TEXT("global_task"), Index)->GetStringField(TEXT("description")));
	}
	Markdown += TEXT("\n## State Hierarchy\n\n");
	for (const UStateTreeState* Root : EditorData->SubTrees)
	{
		AppendStateTreeMarkdownNode(Markdown, EditorData, Root, 0);
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Data->SetStringField(TEXT("markdown"), Markdown);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolExportStateTreeMermaid(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}
	UStateTree* StateTree = LoadStateTreeAsset(AssetPath, Error);
	UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return MakeErrorObject(StateTree ? TEXT("unsupported_statetree_asset") : TEXT("load_failed"), StateTree ? TEXT("StateTree has no editor data.") : Error);
	}
	FString Mermaid = TEXT("stateDiagram-v2\n");
	for (const UStateTreeState* Root : EditorData->SubTrees)
	{
		AppendMermaidStateDeclarations(Mermaid, Root);
		Mermaid += FString::Printf(TEXT("    [*] --> %s\n"), *MermaidIdForState(Root));
	}
	for (const UStateTreeState* Root : EditorData->SubTrees)
	{
		AppendMermaidStateEdges(Mermaid, EditorData, Root);
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
	Data->SetStringField(TEXT("mermaid"), Mermaid);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolExportStateTreeDebugMarkdown(const TSharedPtr<FJsonObject>& Arguments)
{
	TSharedRef<FJsonObject> MarkdownResult = ToolExportStateTreeMarkdown(Arguments);
	if (!MarkdownResult->GetBoolField(TEXT("ok")))
	{
		return MarkdownResult;
	}
	TSharedPtr<FJsonObject> MarkdownData = GetEnvelopeData(MarkdownResult);
	FString Markdown = MarkdownData.IsValid() ? GetOptionalString(MarkdownData, TEXT("markdown")) : FString();
	Markdown += TEXT("\n## Runtime Snapshot\n\n");
	TSharedRef<FJsonObject> SnapshotResult = ToolStateTreeDebugSnapshot(Arguments);
	if (SnapshotResult->GetBoolField(TEXT("ok")))
	{
		const FString SnapshotJson = SerializeJson(SnapshotResult);
		Markdown += TEXT("```json\n");
		Markdown += SnapshotJson;
		Markdown += TEXT("\n```\n");
	}
	else
	{
		Markdown += FString::Printf(TEXT("- Runtime unavailable: %s\n"), *GetOptionalString(SnapshotResult, TEXT("message")));
	}
	Markdown += TEXT("\n## Blocked Transition Analysis\n\n");
	Markdown += TEXT("```json\n");
	Markdown += SerializeJson(ToolStateTreeExplainBlockedTransitions(Arguments));
	Markdown += TEXT("\n```\n");

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("markdown"), Markdown);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSpawnNode(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString NodeType;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("node_type"), NodeType, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UEdGraph* Graph = FindGraph(Blueprint, GraphName, Error);
	if (!Graph)
	{
		return MakeErrorObject(TEXT("graph_not_found"), Error);
	}

	const int32 X = GetOptionalInt(Arguments, TEXT("x"), 0);
	const int32 Y = GetOptionalInt(Arguments, TEXT("y"), 0);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SpawnNode", "LLM Spawn Blueprint Node"));
	Blueprint->Modify();
	Graph->Modify();

	UEdGraphNode* NewNode = nullptr;
	if (NodeType.Equals(TEXT("branch"), ESearchCase::IgnoreCase))
	{
		UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
		SetCommonNodeFields(Graph, BranchNode, X, Y);
		BranchNode->PostPlacedNewNode();
		BranchNode->AllocateDefaultPins();
		NewNode = BranchNode;
	}
	else if (NodeType.Equals(TEXT("call_function"), ESearchCase::IgnoreCase))
	{
		const FString ClassPath = GetOptionalString(Arguments, TEXT("class_path"));
		const FString FunctionName = GetOptionalString(Arguments, TEXT("function"));
		UFunction* Function = ResolveFunction(Blueprint, ClassPath, FunctionName, Error);
		if (!Function)
		{
			return MakeErrorObject(TEXT("function_not_found"), Error);
		}

		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
		SetCommonNodeFields(Graph, CallNode, X, Y);
		CallNode->SetFromFunction(Function);
		CallNode->PostPlacedNewNode();
		CallNode->AllocateDefaultPins();
		NewNode = CallNode;
	}
	else if (NodeType.Equals(TEXT("variable_get"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("variable_set"), ESearchCase::IgnoreCase))
	{
		const FString VariableName = GetOptionalString(Arguments, TEXT("variable"));
		if (VariableName.IsEmpty())
		{
			return MakeErrorObject(TEXT("invalid_arguments"), TEXT("Variable nodes require 'variable'."));
		}

		if (NodeType.Equals(TEXT("variable_get"), ESearchCase::IgnoreCase))
		{
			UK2Node_VariableGet* VariableNode = NewObject<UK2Node_VariableGet>(Graph);
			SetCommonNodeFields(Graph, VariableNode, X, Y);
			VariableNode->VariableReference.SetSelfMember(FName(*VariableName));
			VariableNode->PostPlacedNewNode();
			VariableNode->AllocateDefaultPins();
			NewNode = VariableNode;
		}
		else
		{
			UK2Node_VariableSet* VariableNode = NewObject<UK2Node_VariableSet>(Graph);
			SetCommonNodeFields(Graph, VariableNode, X, Y);
			VariableNode->VariableReference.SetSelfMember(FName(*VariableName));
			VariableNode->PostPlacedNewNode();
			VariableNode->AllocateDefaultPins();
			NewNode = VariableNode;
		}
	}
	else if (NodeType.Equals(TEXT("custom_event"), ESearchCase::IgnoreCase))
	{
		const FString EventName = GetOptionalString(Arguments, TEXT("event_name"));
		if (EventName.IsEmpty())
		{
			return MakeErrorObject(TEXT("invalid_arguments"), TEXT("custom_event nodes require 'event_name'."));
		}

		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
		SetCommonNodeFields(Graph, EventNode, X, Y);
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->bCallInEditor = GetOptionalBool(Arguments, TEXT("call_in_editor"), false);
		EventNode->PostPlacedNewNode();
		EventNode->AllocateDefaultPins();
		NewNode = EventNode;
	}
	else
	{
		return MakeErrorObject(TEXT("unsupported_node_type"), FString::Printf(TEXT("Unsupported node_type '%s'."), *NodeType));
	}

	if (!NewNode)
	{
		return MakeErrorObject(TEXT("spawn_failed"), TEXT("Node creation failed."));
	}

	MarkGraphChanged(Blueprint, Graph);
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("node"), NodeToJson(NewNode));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolConnectPins(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString FromNodeId;
	FString FromPinName;
	FString ToNodeId;
	FString ToPinName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("from_node"), FromNodeId, Error) ||
		!RequireString(Arguments, TEXT("from_pin"), FromPinName, Error) ||
		!RequireString(Arguments, TEXT("to_node"), ToNodeId, Error) ||
		!RequireString(Arguments, TEXT("to_pin"), ToPinName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName, Error) : nullptr;
	UEdGraphNode* FromNode = Graph ? FindNode(Graph, FromNodeId, Error) : nullptr;
	UEdGraphPin* FromPin = FromNode ? FindPin(FromNode, FromPinName, EGPD_Output, Error) : nullptr;
	UEdGraphNode* ToNode = FromPin ? FindNode(Graph, ToNodeId, Error) : nullptr;
	UEdGraphPin* ToPin = ToNode ? FindPin(ToNode, ToPinName, EGPD_Input, Error) : nullptr;
	if (!ToPin)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error);
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "ConnectPins", "LLM Connect Blueprint Pins"));
	Blueprint->Modify();
	Graph->Modify();
	FromNode->Modify();
	ToNode->Modify();

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema || !Schema->TryCreateConnection(FromPin, ToPin))
	{
		return MakeErrorObject(TEXT("connection_failed"), FString::Printf(TEXT("Schema refused connection %s.%s -> %s.%s."), *FromNodeId, *FromPinName, *ToNodeId, *ToPinName));
	}

	MarkGraphChanged(Blueprint, Graph);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("from"), PinToJson(FromPin));
	Data->SetObjectField(TEXT("to"), PinToJson(ToPin));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSetPinDefault(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString NodeId;
	FString PinName;
	FString Value;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("node"), NodeId, Error) ||
		!RequireString(Arguments, TEXT("pin"), PinName, Error) ||
		!RequireString(Arguments, TEXT("value"), Value, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName, Error) : nullptr;
	UEdGraphNode* Node = Graph ? FindNode(Graph, NodeId, Error) : nullptr;
	UEdGraphPin* Pin = Node ? FindPin(Node, PinName, EGPD_Input, Error) : nullptr;
	if (!Pin)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error);
	}

	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "SetPinDefault", "LLM Set Blueprint Pin Default"));
	Blueprint->Modify();
	Graph->Modify();
	Node->Modify();

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		return MakeErrorObject(TEXT("schema_missing"), TEXT("Graph has no schema."));
	}

	Schema->TrySetDefaultValue(*Pin, Value);
	MarkGraphChanged(Blueprint, Graph);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("pin"), PinToJson(Pin));
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolFormatBlueprintGraph(const TSharedPtr<FJsonObject>& Arguments)
{
	// DEPRECATED: both strategies produce layouts that are unreadable for humans.
	// Retained only for callers explicitly opting into the legacy behavior.
	if (!GetOptionalBool(Arguments, TEXT("allow_deprecated_automatic_layout"), false))
	{
		return MakeErrorObject(TEXT("deprecated_tool"), TEXT("format_blueprint_graph and both automatic layout strategies are deprecated: their node placements are unreadable for humans. Prefer inspected, manually chosen NodePosX/NodePosY coordinates using set_property. Legacy execution requires allow_deprecated_automatic_layout=true."));
	}

	FString AssetPath;
	FString GraphName;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	const int32 OriginX = GetOptionalInt(Arguments, TEXT("origin_x"), 0);
	const int32 OriginY = GetOptionalInt(Arguments, TEXT("origin_y"), 0);
	const int32 ColumnSpacing = FMath::Clamp(GetOptionalInt(Arguments, TEXT("column_spacing"), 320), 120, 2000);
	const int32 RowSpacing = FMath::Clamp(GetOptionalInt(Arguments, TEXT("row_spacing"), 180), 80, 2000);
	const FString Strategy = GetOptionalString(Arguments, TEXT("strategy"), TEXT("control_flow_parameters"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName, Error) : nullptr;
	if (!Graph)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error);
	}

	TArray<UEdGraphNode*> Nodes;
	TSet<UEdGraphNode*> NodeSet;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Nodes.Add(Node);
		NodeSet.Add(Node);
	}

	if (Nodes.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> EmptyNodes;
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
		Data->SetStringField(TEXT("graph"), Graph->GetName());
		Data->SetNumberField(TEXT("moved_count"), 0);
		Data->SetArrayField(TEXT("nodes"), EmptyNodes);
		return MakeOk(Data);
	}

	Nodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		if (A.NodePosX != B.NodePosX)
		{
			return A.NodePosX < B.NodePosX;
		}
		if (A.NodePosY != B.NodePosY)
		{
			return A.NodePosY < B.NodePosY;
		}
		return A.GetName() < B.GetName();
	});

	TMap<UEdGraphNode*, int32> FinalDepths;
	TMap<UEdGraphNode*, int32> FinalRows;
	FString ResolvedStrategy = Strategy;
	const bool bHasExecNodes = Nodes.ContainsByPredicate([](const UEdGraphNode* Node)
	{
		return NodeHasExecPin(Node);
	});

	if (Strategy.Equals(TEXT("dependency_layered"), ESearchCase::IgnoreCase) || !bHasExecNodes)
	{
		ResolvedStrategy = TEXT("dependency_layered");
		TMap<UEdGraphNode*, TSet<UEdGraphNode*>> OutgoingNodes;
		TMap<UEdGraphNode*, int32> IncomingCounts;
		for (UEdGraphNode* Node : Nodes)
		{
			IncomingCounts.Add(Node, 0);
			FinalDepths.Add(Node, 0);
		}

		for (UEdGraphNode* Node : Nodes)
		{
			TSet<UEdGraphNode*>& Outputs = OutgoingNodes.FindOrAdd(Node);
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					if (!LinkedNode || LinkedNode == Node || !NodeSet.Contains(LinkedNode) || !IsNodeInGraph(Graph, LinkedNode))
					{
						continue;
					}

					const int32 PreviousCount = Outputs.Num();
					Outputs.Add(LinkedNode);
					if (Outputs.Num() != PreviousCount)
					{
						IncomingCounts.FindOrAdd(LinkedNode)++;
					}
				}
			}
		}

		bool bChangedDepth = true;
		for (int32 Iteration = 0; bChangedDepth && Iteration < Nodes.Num(); ++Iteration)
		{
			bChangedDepth = false;
			for (UEdGraphNode* Node : Nodes)
			{
				const int32 SourceDepth = FinalDepths.FindRef(Node);
				const TSet<UEdGraphNode*>* Outputs = OutgoingNodes.Find(Node);
				if (!Outputs)
				{
					continue;
				}

				for (UEdGraphNode* LinkedNode : *Outputs)
				{
					const int32 NewDepth = SourceDepth + 1;
					int32& CurrentDepth = FinalDepths.FindOrAdd(LinkedNode);
					if (NewDepth > CurrentDepth)
					{
						CurrentDepth = NewDepth;
						bChangedDepth = true;
					}
				}
			}
		}

		TMap<int32, TArray<UEdGraphNode*>> NodesByDepth;
		for (UEdGraphNode* Node : Nodes)
		{
			NodesByDepth.FindOrAdd(FinalDepths.FindRef(Node)).Add(Node);
		}

		TArray<int32> SortedDepths;
		NodesByDepth.GetKeys(SortedDepths);
		SortedDepths.Sort();

		for (const int32 Depth : SortedDepths)
		{
			TArray<UEdGraphNode*>& ColumnNodes = NodesByDepth.FindChecked(Depth);
			ColumnNodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				if (A.NodePosY != B.NodePosY)
				{
					return A.NodePosY < B.NodePosY;
				}
				if (A.NodePosX != B.NodePosX)
				{
					return A.NodePosX < B.NodePosX;
				}
				return A.GetName() < B.GetName();
			});

			for (int32 Row = 0; Row < ColumnNodes.Num(); ++Row)
			{
				FinalRows.Add(ColumnNodes[Row], Row);
			}
		}
	}
	else
	{
		ResolvedStrategy = TEXT("control_flow_parameters");
		TMap<UEdGraphNode*, TArray<UEdGraphNode*>> ExecOutgoingNodes;
		TMap<UEdGraphNode*, int32> ExecIncomingCounts;
		TArray<UEdGraphNode*> ExecNodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (NodeHasExecPin(Node))
			{
				ExecNodes.Add(Node);
				ExecIncomingCounts.Add(Node, 0);
			}
		}

		for (UEdGraphNode* Node : Nodes)
		{
			TArray<UEdGraphNode*>& Outputs = ExecOutgoingNodes.FindOrAdd(Node);
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					if (!LinkedNode || LinkedNode == Node || !NodeSet.Contains(LinkedNode) || !IsNodeInGraph(Graph, LinkedNode) || Outputs.Contains(LinkedNode))
					{
						continue;
					}

					Outputs.Add(LinkedNode);
					ExecIncomingCounts.FindOrAdd(LinkedNode)++;
				}
			}
		}

		TArray<UEdGraphNode*> Roots;
		for (UEdGraphNode* Node : ExecNodes)
		{
			if (ExecIncomingCounts.FindRef(Node) == 0)
			{
				Roots.Add(Node);
			}
		}
		Roots.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			if (A.NodePosY != B.NodePosY)
			{
				return A.NodePosY < B.NodePosY;
			}
			if (A.NodePosX != B.NodePosX)
			{
				return A.NodePosX < B.NodePosX;
			}
			return A.GetName() < B.GetName();
		});

		int32 NextLane = 0;
		TFunction<void(UEdGraphNode*, int32, int32)> PlaceExecNode;
		PlaceExecNode = [&](UEdGraphNode* Node, const int32 Depth, const int32 Lane)
		{
			if (!Node || FinalRows.Contains(Node))
			{
				return;
			}

			FinalDepths.Add(Node, Depth);
			FinalRows.Add(Node, Lane);

			const TArray<UEdGraphNode*>* Outputs = ExecOutgoingNodes.Find(Node);
			if (!Outputs)
			{
				return;
			}

			for (int32 OutputIndex = 0; OutputIndex < Outputs->Num(); ++OutputIndex)
			{
				UEdGraphNode* LinkedNode = (*Outputs)[OutputIndex];
				const int32 ChildLane = OutputIndex == 0 ? Lane : NextLane++;
				PlaceExecNode(LinkedNode, Depth + 1, ChildLane);
			}
		};

		for (UEdGraphNode* Root : Roots)
		{
			if (FinalRows.Contains(Root))
			{
				continue;
			}
			PlaceExecNode(Root, 0, NextLane++);
		}

		TMap<int32, TSet<int32>> UsedRowsByDepth;
		for (const TPair<UEdGraphNode*, int32>& Pair : FinalRows)
		{
			if (Pair.Key)
			{
				UsedRowsByDepth.FindOrAdd(FinalDepths.FindRef(Pair.Key)).Add(Pair.Value);
			}
		}

		auto ReserveRow = [&UsedRowsByDepth](const int32 Depth, const int32 DesiredRow) -> int32
		{
			TSet<int32>& UsedRows = UsedRowsByDepth.FindOrAdd(Depth);
			int32 Row = DesiredRow;
			while (UsedRows.Contains(Row))
			{
				--Row;
			}
			UsedRows.Add(Row);
			return Row;
		};

		auto FindResolvedConsumer = [&FinalDepths, &FinalRows, &Graph, &NodeSet](UEdGraphNode* Node) -> UEdGraphNode*
		{
			UEdGraphNode* BestNode = nullptr;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					if (!LinkedNode || LinkedNode == Node || !NodeSet.Contains(LinkedNode) || !IsNodeInGraph(Graph, LinkedNode) || !FinalRows.Contains(LinkedNode))
					{
						continue;
					}

					if (!BestNode ||
						FinalDepths.FindRef(LinkedNode) < FinalDepths.FindRef(BestNode) ||
						(FinalDepths.FindRef(LinkedNode) == FinalDepths.FindRef(BestNode) && FinalRows.FindRef(LinkedNode) < FinalRows.FindRef(BestNode)) ||
						(FinalDepths.FindRef(LinkedNode) == FinalDepths.FindRef(BestNode) && FinalRows.FindRef(LinkedNode) == FinalRows.FindRef(BestNode) && LinkedNode->GetName() < BestNode->GetName()))
					{
						BestNode = LinkedNode;
					}
				}
			}
			return BestNode;
		};

		TMap<UEdGraphNode*, int32> ParameterSlotsByConsumer;
		bool bPlacedDataNode = true;
		for (int32 Iteration = 0; bPlacedDataNode && Iteration < Nodes.Num(); ++Iteration)
		{
			bPlacedDataNode = false;
			for (UEdGraphNode* Node : Nodes)
			{
				if (!Node || FinalRows.Contains(Node) || NodeHasExecPin(Node))
				{
					continue;
				}

				UEdGraphNode* ConsumerNode = FindResolvedConsumer(Node);
				if (!ConsumerNode)
				{
					continue;
				}

				const int32 Depth = FinalDepths.FindRef(ConsumerNode);
				const int32 Slot = ParameterSlotsByConsumer.FindOrAdd(ConsumerNode)++;
				const int32 Row = ReserveRow(Depth, FinalRows.FindRef(ConsumerNode) - Slot - 1);
				FinalDepths.Add(Node, Depth);
				FinalRows.Add(Node, Row);
				bPlacedDataNode = true;
			}
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (!FinalRows.Contains(Node))
			{
				FinalDepths.Add(Node, 0);
				FinalRows.Add(Node, ReserveRow(0, NextLane++));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> MovedNodes;
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "FormatBlueprintGraph", "LLM Format Blueprint Graph"));
	Blueprint->Modify();
	Graph->Modify();

	for (UEdGraphNode* Node : Nodes)
	{
		const int32 OldX = Node->NodePosX;
		const int32 OldY = Node->NodePosY;
		const int32 NewX = OriginX + FinalDepths.FindRef(Node) * ColumnSpacing;
		const int32 NewY = OriginY + FinalRows.FindRef(Node) * RowSpacing;
		if (OldX == NewX && OldY == NewY)
		{
			continue;
		}

		Node->Modify();
		Node->NodePosX = NewX;
		Node->NodePosY = NewY;
		MovedNodes.Add(MakeShared<FJsonValueObject>(NodeMoveToJson(Node, OldX, OldY)));
	}

	MarkGraphChanged(Blueprint, Graph);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("graph"), Graph->GetName());
	Data->SetStringField(TEXT("strategy"), ResolvedStrategy);
	Data->SetBoolField(TEXT("deprecated"), true);
	Data->SetStringField(TEXT("note"), TEXT("DEPRECATED automatic layout, explicitly opted in by this caller. Both strategies produce placements that are unreadable for humans. Prefer manual NodePosX/NodePosY placement with set_property."));
	Data->SetNumberField(TEXT("moved_count"), MovedNodes.Num());
	Data->SetArrayField(TEXT("nodes"), MovedNodes);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolDeleteNode(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString GraphName;
	FString NodeId;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error) ||
		!RequireString(Arguments, TEXT("graph"), GraphName, Error) ||
		!RequireString(Arguments, TEXT("node"), NodeId, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	UEdGraph* Graph = Blueprint ? FindGraph(Blueprint, GraphName, Error) : nullptr;
	UEdGraphNode* Node = Graph ? FindNode(Graph, NodeId, Error) : nullptr;
	if (!Node)
	{
		return MakeErrorObject(TEXT("resolve_failed"), Error);
	}

	const FString DeletedNodeId = GuidToString(Node->NodeGuid);
	const FScopedTransaction Transaction(NSLOCTEXT("LLMBlueprintBridge", "DeleteNode", "LLM Delete Blueprint Node"));
	Blueprint->Modify();
	Graph->Modify();
	Node->Modify();
	Node->BreakAllNodeLinks();
	Node->DestroyNode();
	MarkGraphChanged(Blueprint, Graph);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("deleted_node_id"), DeletedNodeId);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolCompileBlueprint(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("status"), BlueprintStatusToString(Blueprint->Status));
	Data->SetNumberField(TEXT("errors"), Results.NumErrors);
	Data->SetNumberField(TEXT("warnings"), Results.NumWarnings);
	Data->SetArrayField(TEXT("messages"), Messages);
	return MakeOk(Data);
}

TSharedRef<FJsonObject> FLLMBlueprintBridgeServer::ToolSaveBlueprint(const TSharedPtr<FJsonObject>& Arguments)
{
	FString AssetPath;
	FString Error;
	if (!RequireString(Arguments, TEXT("asset_path"), AssetPath, Error))
	{
		return MakeErrorObject(TEXT("invalid_arguments"), Error);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return MakeErrorObject(TEXT("load_failed"), Error);
	}

	UPackage* Package = Blueprint->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("filename"), Filename);
	Data->SetBoolField(TEXT("saved"), bSaved);
	if (!bSaved)
	{
		return MakeErrorObject(TEXT("save_failed"), FString::Printf(TEXT("Failed to save '%s' to '%s'."), *Blueprint->GetPathName(), *Filename));
	}
	return MakeOk(Data);
}

UBlueprint* FLLMBlueprintBridgeServer::LoadBlueprint(const FString& AssetPath, FString& OutError) const
{
	const FString ObjectPath = NormalizeAssetObjectPath(AssetPath);
	UObject* Object = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath);
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint asset '%s' (normalized to '%s')."), *AssetPath, *ObjectPath);
	}
	return Blueprint;
}

UEdGraph* FLLMBlueprintBridgeServer::FindGraph(UBlueprint* Blueprint, const FString& GraphName, FString& OutError) const
{
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Names repeat (every transition rule graph is "Transition"), so also accept the graph's path relative
	// to the Blueprint, as reported by inspect_blueprint's graphs[].path, or its full object path.
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && (Graph->GetPathName(Blueprint).Equals(GraphName, ESearchCase::IgnoreCase) ||
			Graph->GetPathName().Equals(GraphName, ESearchCase::IgnoreCase)))
		{
			return Graph;
		}
	}

	OutError = FString::Printf(TEXT("Graph '%s' was not found in '%s'."), *GraphName, *Blueprint->GetPathName());
	return nullptr;
}

UEdGraphNode* FLLMBlueprintBridgeServer::FindNode(UEdGraph* Graph, const FString& NodeIdOrName, FString& OutError) const
{
	FGuid Guid;
	const bool bHasGuid = FGuid::Parse(NodeIdOrName, Guid);
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		if ((bHasGuid && Node->NodeGuid == Guid) || Node->GetName().Equals(NodeIdOrName, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	OutError = FString::Printf(TEXT("Node '%s' was not found in graph '%s'."), *NodeIdOrName, *Graph->GetName());
	return nullptr;
}

UEdGraphPin* FLLMBlueprintBridgeServer::FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction, FString& OutError) const
{
	FGuid PinGuid;
	const bool bHasGuid = FGuid::Parse(PinName, PinGuid);

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->WasTrashed())
		{
			continue;
		}

		const bool bDirectionMatches = Direction == EGPD_MAX || Pin->Direction == Direction;
		const bool bNameMatches = Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase);
		const bool bGuidMatches = bHasGuid && Pin->PinId == PinGuid;
		if (bDirectionMatches && (bNameMatches || bGuidMatches))
		{
			return Pin;
		}
	}

	OutError = FString::Printf(TEXT("Pin '%s' was not found on node '%s'."), *PinName, *Node->GetName());
	return nullptr;
}

UClass* FLLMBlueprintBridgeServer::ResolveClass(const FString& ClassPathOrName) const
{
	if (ClassPathOrName.IsEmpty())
	{
		return nullptr;
	}

	const FString Path = StripObjectExportWrapper(ClassPathOrName);
	if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *Path))
	{
		return LoadedClass;
	}

	if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *NormalizeAssetObjectPath(Path)))
	{
		return Blueprint->GeneratedClass;
	}

	const FString ShortName = FPackageName::GetShortName(Path).Replace(TEXT("_C"), TEXT(""));
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (Class->GetName().Equals(Path, ESearchCase::IgnoreCase) ||
			Class->GetName().Equals(ShortName, ESearchCase::IgnoreCase) ||
			Class->GetPathName().Equals(Path, ESearchCase::IgnoreCase))
		{
			return Class;
		}
	}

	return nullptr;
}

UFunction* FLLMBlueprintBridgeServer::ResolveFunction(UBlueprint* Blueprint, const FString& ClassPathOrName, const FString& FunctionName, FString& OutError) const
{
	if (FunctionName.IsEmpty())
	{
		OutError = TEXT("Missing required field 'function'.");
		return nullptr;
	}

	TArray<UClass*> SearchClasses;
	if (!ClassPathOrName.IsEmpty())
	{
		if (UClass* Class = ResolveClass(ClassPathOrName))
		{
			SearchClasses.Add(Class);
		}
		else
		{
			OutError = FString::Printf(TEXT("Could not resolve class '%s'."), *ClassPathOrName);
			return nullptr;
		}
	}
	else
	{
		if (Blueprint->SkeletonGeneratedClass)
		{
			SearchClasses.Add(Blueprint->SkeletonGeneratedClass);
		}
		if (Blueprint->GeneratedClass)
		{
			SearchClasses.Add(Blueprint->GeneratedClass);
		}
		if (Blueprint->ParentClass)
		{
			SearchClasses.Add(Blueprint->ParentClass);
		}
	}

	for (UClass* Class : SearchClasses)
	{
		if (UFunction* Function = Class ? Class->FindFunctionByName(FName(*FunctionName)) : nullptr)
		{
			return Function;
		}
	}

	OutError = FString::Printf(TEXT("Function '%s' was not found%s."), *FunctionName, ClassPathOrName.IsEmpty() ? TEXT(" in the Blueprint context") : *FString::Printf(TEXT(" on '%s'"), *ClassPathOrName));
	return nullptr;
}

bool FLLMBlueprintBridgeServer::BuildPinType(const TSharedPtr<FJsonObject>& TypeObject, FEdGraphPinType& OutPinType, FString& OutError) const
{
	FString Category;
	if (!RequireString(TypeObject, TEXT("category"), Category, OutError))
	{
		return false;
	}

	OutPinType.ResetToDefaults();
	OutPinType.ContainerType = ContainerFromString(GetOptionalString(TypeObject, TEXT("container"), TEXT("none")));
	OutPinType.bIsReference = GetOptionalBool(TypeObject, TEXT("is_reference"), false);
	OutPinType.bIsConst = GetOptionalBool(TypeObject, TEXT("is_const"), false);

	if (Category.Equals(TEXT("bool"), ESearchCase::IgnoreCase) || Category.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Category.Equals(TEXT("byte"), ESearchCase::IgnoreCase) || Category.Equals(TEXT("enum"), ESearchCase::IgnoreCase))
	{
		// Blueprint enums are byte pins whose sub-category object is the UEnum.
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		const FString EnumPath = GetOptionalString(TypeObject, TEXT("enum_path"));
		if (!EnumPath.IsEmpty())
		{
			const FString CleanPath = StripObjectExportWrapper(EnumPath);
			UEnum* Enum = LoadObject<UEnum>(nullptr, *CleanPath);
			if (!Enum)
			{
				Enum = FindFirstObject<UEnum>(*CleanPath, EFindFirstObjectOptions::None);
			}
			if (!Enum)
			{
				OutError = FString::Printf(TEXT("Could not resolve enum '%s'."), *EnumPath);
				return false;
			}
			OutPinType.PinSubCategoryObject = Enum;
		}
		else if (Category.Equals(TEXT("enum"), ESearchCase::IgnoreCase))
		{
			OutError = TEXT("Category 'enum' needs 'enum_path'.");
			return false;
		}
	}
	else if (Category.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Category.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Category.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Category.Equals(TEXT("float"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (Category.Equals(TEXT("double"), ESearchCase::IgnoreCase) || Category.Equals(TEXT("real"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (Category.Equals(TEXT("string"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Category.Equals(TEXT("name"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Category.Equals(TEXT("text"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Category.Equals(TEXT("object"), ESearchCase::IgnoreCase))
	{
		const FString ClassPath = GetOptionalString(TypeObject, TEXT("class_path"), TEXT("/Script/CoreUObject.Object"));
		UClass* Class = ResolveClass(ClassPath);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("Could not resolve object class '%s'."), *ClassPath);
			return false;
		}
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = Class;
	}
	else if (Category.Equals(TEXT("class"), ESearchCase::IgnoreCase))
	{
		const FString ClassPath = GetOptionalString(TypeObject, TEXT("class_path"), TEXT("/Script/CoreUObject.Object"));
		UClass* Class = ResolveClass(ClassPath);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("Could not resolve class type '%s'."), *ClassPath);
			return false;
		}
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		OutPinType.PinSubCategoryObject = Class;
	}
	else if (Category.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (Category.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (Category.Equals(TEXT("transform"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (Category.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
	{
		const FString StructPath = GetOptionalString(TypeObject, TEXT("struct_path"));
		UScriptStruct* Struct = StructPath.IsEmpty() ? nullptr : LoadObject<UScriptStruct>(nullptr, *StripObjectExportWrapper(StructPath));
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("Could not resolve struct '%s'."), *StructPath);
			return false;
		}
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = Struct;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported pin category '%s'."), *Category);
		return false;
	}

	return true;
}
