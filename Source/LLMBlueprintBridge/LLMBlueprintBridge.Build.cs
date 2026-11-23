using UnrealBuildTool;

public sealed class LLMBlueprintBridge : ModuleRules
{
	public LLMBlueprintBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange([
			"Core",
			"DeveloperSettings"
		]);

		PrivateDependencyModuleNames.AddRange([
			"AnimGraph",
			"AssetDefinition",
			"AssetTools",
			"AssetRegistry",
			"BlueprintGraph",
			"ContentBrowser",
			"CoreUObject",
			"DataTableEditor",
			"Engine",
			"GameplayAbilities",
			"GitAssetDiff",
			"GameplayStateTreeModule",
			"GameplayTags",
			"GameplayTagsEditor",
			"GameplayTasks",
			"HTTPServer",
			"IKRig",
			"IKRigEditor",
			"Json",
			"JsonUtilities",
			"Kismet",
			"KismetCompiler",
			"LevelSequence",
			"MaterialEditor",
			"MovieScene",
			"MovieSceneCaptureDialog",
			"MovieSceneTracks",
			"Niagara",
			"NiagaraEditor",
			"Projects",
			"RenderCore",
			"RHI",
			"PropertyBindingUtils",
			"SequencerScriptingEditor",
			"Slate",
			"SlateCore",
			"StateTreeEditorModule",
			"StateTreeModule",
			"UnrealEd",
			"UniversalObjectLocator",
			"UMG",
			"UMGEditor"
		]);
	}
}
