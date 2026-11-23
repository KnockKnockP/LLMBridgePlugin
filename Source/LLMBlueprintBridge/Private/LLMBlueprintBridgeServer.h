#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "HttpResultCallback.h"
#include "LLMBlueprintBridgeMcpEndpoint.h"
#include "LLMBlueprintBridgeToolInvoker.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class IHttpRouter;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
struct FEdGraphPinType;

class FLLMBlueprintBridgeServer : public ILLMBlueprintBridgeToolInvoker
{
public:
	void Start();
	void Stop();

	//~ Begin ILLMBlueprintBridgeToolInvoker
	virtual TSharedRef<FJsonObject> InvokeToolByName(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments) override;
	//~ End ILLMBlueprintBridgeToolInvoker

private:
	bool HandleStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleInvoke(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	bool IsAuthorized(const FHttpServerRequest& Request) const;

	TSharedRef<FJsonObject> InvokeTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStatus(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListBlueprints(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindBlueprintAssets(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectBlueprint(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectBlueprintGitRevision(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDiffBlueprintGraphAgainstGitRevision(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindFunctions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateBlueprint(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddWidget(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveWidget(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolMoveWidget(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCopyNodes(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddBlueprintInterface(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddVariable(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDeleteVariable(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindVariableReferences(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddFunction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRenameFunction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDeleteFunctionGraph(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindFunctionReferences(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRetargetFunctionCall(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListNativeEvents(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSpawnNativeEvent(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindBlueprintActions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSpawnBlueprintAction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListComponents(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddComponent(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveComponent(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAttachComponent(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetComponentTransform(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetComponentProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGetClassDefaults(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetClassDefault(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGetProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindAssets(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateFolder(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDuplicateAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRenameAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolMoveAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDeleteAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSaveAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSaveAllDirty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDiffAssetAgainstGitRevision(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateDataTable(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectDataTable(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGetDataTableRows(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetDataTableRow(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddDataTableRow(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveDataTableRow(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRenameDataTableRow(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectMaterial(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectMaterialFunction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetupOcclusionMaskFunction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetupTexturedOcclusionMaskFunction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetMaterialProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateMaterialExpression(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolConnectMaterialProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolClearMaterialExpressions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDeleteMaterialExpression(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRecompileMaterial(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetupCameraCutoutMaterial(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListGasAssets(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectGameplayAbility(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectGameplayEffect(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectAttributeSet(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectGameplayCue(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectGameplayTags(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateGameplayAbility(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateGameplayEffect(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateGameplayCue(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateGameplayModMagnitudeCalculation(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddGameplayTag(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveGameplayTag(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindGameplayTags(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetGameplayTagContainer(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetAbilityPolicy(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetAbilityTags(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddAbilityTaskNode(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolConfigureAbilityCommitFlow(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolConfigureAbilityCooldown(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolConfigureAbilityCost(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetEffectDuration(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddEffectModifier(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveEffectModifier(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetEffectModifier(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddGrantedAbility(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetEffectStacking(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddEffectExecution(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectAttributeReferences(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGenerateAttributeSetCpp(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolValidateAttributeReplication(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolValidateGasAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolValidateGasProject(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGasDependencyReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateBasicAbilityRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateCooldownEffectRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateCostEffectRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateDamageEffectRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateBuffEffectRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSpawnNode(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolConnectPins(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetPinDefault(const TSharedPtr<FJsonObject>& Arguments);
	/** DEPRECATED automatic layout; prefer manual NodePosX/NodePosY placement. Legacy calls require explicit opt-in. */
	TSharedRef<FJsonObject> ToolFormatBlueprintGraph(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDeleteNode(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddAnimState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddAnimTransition(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCompileBlueprint(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSaveBlueprint(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListStateTreeAssets(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListStateTreeRelatedBlueprints(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFindStateTreeAssets(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTree(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeTransitions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeTasks(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeConditions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeEvaluators(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeParameters(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectStateTreeBlueprintDependency(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolValidateStateTree(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolValidateStateTreeProject(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolValidateStateTreeBlueprintDependency(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugStatus(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugActor(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugSnapshot(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugTraceStart(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugTraceStop(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugTraceRead(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeExplainLastTransition(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeExplainBlockedTransitions(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDebugReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeFailureReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeCompareRuntimeToAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateStateTree(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolDuplicateStateTree(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddStateTreeState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRenameStateTreeState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolMoveStateTreeState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveStateTreeState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddStateTreeTask(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveStateTreeTask(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddStateTreeCondition(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveStateTreeCondition(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddStateTreeTransition(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveStateTreeTransition(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetStateTreeProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetStateTreeBinding(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCompileOrValidateStateTree(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSaveStateTree(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateBasicStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreatePatrolChaseAttackStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateGasSkillStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateEqsPositioningStateTreeRecipe(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeDependencyReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeGasReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeEqsReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolStateTreeBlueprintReport(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolExportStateTreeMarkdown(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolExportStateTreeMermaid(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolExportStateTreeDebugMarkdown(const TSharedPtr<FJsonObject>& Arguments);

	// Animation (LLMBlueprintBridgeServer_Animation.cpp)
	TSharedRef<FJsonObject> ToolInspectAnimSequence(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGetRootMotionTrack(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetRootMotionKeys(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolFlattenRootMotionAxis(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGetBoneComponentTransform(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListSkeletonBones(const TSharedPtr<FJsonObject>& Arguments);

	// Anim notifies (LLMBlueprintBridgeServer_AnimNotify.cpp)
	TSharedRef<FJsonObject> ToolInspectAnimNotifies(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddAnimNotify(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveAnimNotify(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectIKRetargeter(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateRetargetPose(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetRetargetPoseBoneRotation(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAutoAlignRetargetPose(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolResetRetargetPose(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRetargetAnimations(const TSharedPtr<FJsonObject>& Arguments);

	// Niagara (LLMBlueprintBridgeServer_Niagara.cpp)
	TSharedRef<FJsonObject> ToolInspectNiagaraSystem(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetNiagaraUserParameter(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetNiagaraModuleInput(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolAddNiagaraEmitter(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetNiagaraEmitterProperty(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCompileNiagaraSystem(const TSharedPtr<FJsonObject>& Arguments);

	// Sequencer (LLMBlueprintBridgeServer_Sequencer.cpp)
	TSharedRef<FJsonObject> ToolCreateLevelSequence(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolInspectLevelSequence(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetLevelSequenceChannelKeys(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolOpenLevelSequence(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolBakeLevelSequenceAnimation(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSetLevelSequenceAnimation(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolSpawnLevelActor(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolPieControl(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolPieCallActorFunction(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolPieGetActorState(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolExecConsoleCommand(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolGetFrameStats(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolReadLog(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolListDirtyPackages(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCreateAsset(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolRemoveLevelSequenceTrack(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> ToolCopyLevelSequenceBindings(const TSharedPtr<FJsonObject>& Arguments);
	TSharedRef<FJsonObject> WriteRootMotionTrack(const TSharedPtr<FJsonObject>& Arguments,
		TFunctionRef<bool(int32, TArray<FVector3f>&, TArray<FQuat4f>&, FString&)> Mutate,
		const FString& TransactionTitle);

	UBlueprint* LoadBlueprint(const FString& AssetPath, FString& OutError) const;
	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName, FString& OutError) const;
	UEdGraphNode* FindNode(UEdGraph* Graph, const FString& NodeIdOrName, FString& OutError) const;
	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction, FString& OutError) const;
	UClass* ResolveClass(const FString& ClassPathOrName) const;
	UFunction* ResolveFunction(UBlueprint* Blueprint, const FString& ClassPathOrName, const FString& FunctionName, FString& OutError) const;
	bool BuildPinType(const TSharedPtr<FJsonObject>& TypeObject, FEdGraphPinType& OutPinType, FString& OutError) const;

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
	TUniquePtr<FLLMBlueprintBridgeMcpEndpoint> McpEndpoint;
	uint32 Port = 0;
};
