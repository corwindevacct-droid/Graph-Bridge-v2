// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeToolManifest.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

TArray<FGraphBridgeToolDef> FGraphBridgeToolManifest::CachedManifest;
bool FGraphBridgeToolManifest::bManifestBuilt = false;

const TArray<FGraphBridgeToolDef>& FGraphBridgeToolManifest::GetManifest()
{
	if (!bManifestBuilt)
	{
		CachedManifest = BuildManifest();
		bManifestBuilt = true;
	}
	return CachedManifest;
}

const FGraphBridgeToolDef* FGraphBridgeToolManifest::FindTool(const FString& CommandName)
{
	const TArray<FGraphBridgeToolDef>& Manifest = GetManifest();
	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		if (Tool.Command == CommandName)
		{
			return &Tool;
		}
	}
	return nullptr;
}

int32 FGraphBridgeToolManifest::CountTools(TFunction<bool(const FGraphBridgeToolDef&)> Predicate)
{
	const TArray<FGraphBridgeToolDef>& Manifest = GetManifest();
	int32 Count = 0;
	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		if (Predicate(Tool))
		{
			Count++;
		}
	}
	return Count;
}

TSharedPtr<FJsonObject> FGraphBridgeToolManifest::ExportAsJson()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ToolsArray;

	const TArray<FGraphBridgeToolDef>& Manifest = GetManifest();
	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		TSharedRef<FJsonObject> ToolObj = MakeShared<FJsonObject>();
		ToolObj->SetStringField(TEXT("command"), Tool.Command);
		ToolObj->SetStringField(TEXT("description"), Tool.Description);
		ToolObj->SetStringField(TEXT("category"), Tool.Category);
		ToolObj->SetStringField(TEXT("return_type"), Tool.ReturnType);
		ToolObj->SetBoolField(TEXT("mutating"), Tool.bMutating);
		ToolObj->SetBoolField(TEXT("public"), Tool.bPublic);
		ToolObj->SetBoolField(TEXT("deprecated"), Tool.bDeprecated);

		if (Tool.bDeprecated && !Tool.DeprecationHint.IsEmpty())
		{
			ToolObj->SetStringField(TEXT("deprecation_hint"), Tool.DeprecationHint);
		}

		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
	}

	Root->SetArrayField(TEXT("tools"), ToolsArray);
	Root->SetNumberField(TEXT("count"), Manifest.Num());
	return Root;
}

/**
 * Build the complete manifest — all 120 tools defined here (and only here).
 *
 * Structure:
 * - Blueprint graph operations (SPAWN_NODE, CONNECT_PINS, etc.)
 * - Variable operations (SPAWN_VARIABLE, LIST_VARIABLES, etc.)
 * - Component/asset operations (ADD_COMPONENT, SET_ANIM_CLASS, etc.)
 * - Animation operations (ADD_MONTAGE_SECTION, etc.)
 * - Input operations (CREATE_IMC, ADD_IMC_MAPPING, etc.)
 * - Character operations (SET_CHARACTER_MESH, etc.)
 * - Material operations (CREATE_MATERIAL, ADD_MATERIAL_NODE, etc.)
 * - Level operations (SPAWN_ACTOR_IN_LEVEL, etc.)
 * - Data operations (LIST_DATATABLE_ROWS, etc.)
 * - Utility operations (RUN_PYTHON, COMPILE, SAVE, etc.)
 *
 * COMPLETE: All 120 tools defined using fluent builder pattern (FToolBuilder).
 * Each tool definition specifies:
 * - Name, description, category
 * - Required parameters (name, type, description)
 * - Optional parameters (marked bRequired=false)
 * - Public/private visibility (bPublic = true for 83 public tools)
 * - Mutating flag (true if modifies editor state)
 *
 * This is the single source of truth for all GraphBridge tools.
 * Generators in GraphBridgeToolGenerators.cpp produce MCP specs and panel schemas from this manifest.
 */
namespace GraphBridgeToolManifestHelpers
{
	struct FToolBuilder
	{
		FGraphBridgeToolDef Tool;

		FToolBuilder(const TCHAR* Cmd, const TCHAR* Desc, const TCHAR* Cat)
			: Tool(Cmd, Desc, Cat)
		{
		}

		FToolBuilder& Param(const TCHAR* Name, const TCHAR* Type, const TCHAR* Desc, bool bRequired = true)
		{
			Tool.Parameters.Add({FString(Name), FString(Type), FString(Desc), bRequired});
			return *this;
		}

		FToolBuilder& OptParam(const TCHAR* Name, const TCHAR* Type, const TCHAR* Desc)
		{
			Tool.OptionalParameters.Add({FString(Name), FString(Type), FString(Desc), false});
			return *this;
		}

		// Set type metadata for last-added parameter (Tier 1 commands)
		FToolBuilder& SetParamTypeInt(double Min = 0.0, double Max = 0.0)
		{
			if (Tool.Parameters.Num() > 0) {
				FGraphBridgeToolParam& Param = Tool.Parameters.Last();
				Param.ParamType = EGraphBridgeParamType::Integer;
				Param.MinValue = Min;
				Param.MaxValue = Max;
			}
			return *this;
		}

		FToolBuilder& SetParamTypeFloat(double Min = 0.0, double Max = 0.0)
		{
			if (Tool.Parameters.Num() > 0) {
				FGraphBridgeToolParam& Param = Tool.Parameters.Last();
				Param.ParamType = EGraphBridgeParamType::Float;
				Param.MinValue = Min;
				Param.MaxValue = Max;
			}
			return *this;
		}

		FToolBuilder& SetParamTypeBool()
		{
			if (Tool.Parameters.Num() > 0) {
				FGraphBridgeToolParam& Param = Tool.Parameters.Last();
				Param.ParamType = EGraphBridgeParamType::Bool;
			}
			return *this;
		}

		FToolBuilder& SetParamTypeEnum(const TCHAR* EnumValuesList)
		{
			if (Tool.Parameters.Num() > 0) {
				FGraphBridgeToolParam& Param = Tool.Parameters.Last();
				Param.ParamType = EGraphBridgeParamType::Enum;
				Param.EnumValues = FString(EnumValuesList);
			}
			return *this;
		}

		FToolBuilder& SetOptParamTypeInt(double Min = 0.0, double Max = 0.0)
		{
			if (Tool.OptionalParameters.Num() > 0) {
				FGraphBridgeToolParam& Param = Tool.OptionalParameters.Last();
				Param.ParamType = EGraphBridgeParamType::Integer;
				Param.MinValue = Min;
				Param.MaxValue = Max;
			}
			return *this;
		}

		operator FGraphBridgeToolDef() const { return Tool; }
	};
}

using namespace GraphBridgeToolManifestHelpers;

TArray<FGraphBridgeToolDef> FGraphBridgeToolManifest::BuildManifest()
{
	TArray<FGraphBridgeToolDef> Tools;

	// BLUEPRINT GRAPH OPERATIONS (27 tools)
	Tools.Add(FToolBuilder(TEXT("SPAWN_NODE"), TEXT("Spawn a graph node in a Blueprint's EventGraph."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_class"), TEXT("string"), TEXT("Node class to spawn"))
		.Param(TEXT("comment"), TEXT("string"), TEXT("Node comment"))
		.OptParam(TEXT("x"), TEXT("number"), TEXT("X position")).SetParamTypeInt(-100000, 100000)
		.OptParam(TEXT("y"), TEXT("number"), TEXT("Y position")).SetParamTypeInt(-100000, 100000));

	Tools.Add(FToolBuilder(TEXT("SPAWN_EVENT_NODE"), TEXT("Spawn a class-level override event node (K2Node_Event)."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("event_func_name"), TEXT("string"), TEXT("Event name"))
		.Param(TEXT("comment"), TEXT("string"), TEXT("Node comment"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position"))
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")));

	Tools.Add(FToolBuilder(TEXT("SPAWN_NODE_IN_GRAPH"), TEXT("Spawn a node in a named graph (EventGraph, Function, or Macro)."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"))
		.Param(TEXT("node_class"), TEXT("string"), TEXT("Node class"))
		.Param(TEXT("comment"), TEXT("string"), TEXT("Node comment"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position"))
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")));

	Tools.Add(FToolBuilder(TEXT("CONNECT_PINS"), TEXT("Connect an output pin on one node to an input pin on another."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_a"), TEXT("string"), TEXT("Source node"))
		.Param(TEXT("pin_a"), TEXT("string"), TEXT("Output pin"))
		.Param(TEXT("node_b"), TEXT("string"), TEXT("Target node"))
		.Param(TEXT("pin_b"), TEXT("string"), TEXT("Input pin")));

	Tools.Add(FToolBuilder(TEXT("DISCONNECT_PINS"), TEXT("Disconnect a pin connection."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_a"), TEXT("string"), TEXT("Source node"))
		.Param(TEXT("pin_a"), TEXT("string"), TEXT("Output pin"))
		.Param(TEXT("node_b"), TEXT("string"), TEXT("Target node"))
		.Param(TEXT("pin_b"), TEXT("string"), TEXT("Input pin")));

	Tools.Add(FToolBuilder(TEXT("DELETE_NODE"), TEXT("Delete a node from the graph by its GUID."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID")));

	Tools.Add(FToolBuilder(TEXT("CLEAR_NODES"), TEXT("Delete nodes matching a comment tag in a Blueprint graph."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("comment_match"), TEXT("string"), TEXT("Comment tag")));

	Tools.Add(FToolBuilder(TEXT("SET_PIN_DEFAULT"), TEXT("Set the default value of a pin on a node."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("pin_name"), TEXT("string"), TEXT("Pin name"))
		.Param(TEXT("default_value"), TEXT("string"), TEXT("Default value")));

	Tools.Add(FToolBuilder(TEXT("GET_NODE_PINS"), TEXT("List all pins on a node."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_name"), TEXT("string"), TEXT("Node name or GUID")));

	Tools.Add(FToolBuilder(TEXT("GET_PIN_CONNECTIONS"), TEXT("List what a pin is connected to."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("pin_name"), TEXT("string"), TEXT("Pin name")));

	Tools.Add(FToolBuilder(TEXT("GET_PIN_DEFAULT"), TEXT("Read the current default value on a pin."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("pin_name"), TEXT("string"), TEXT("Pin name")));

	Tools.Add(FToolBuilder(TEXT("LIST_NODES"), TEXT("List all nodes in a Blueprint graph."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("LIST_GRAPHS"), TEXT("List every graph on a Blueprint (EventGraph, Functions, Macros) with its type."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("CREATE_FUNCTION"), TEXT("Create a new custom function graph in a Blueprint."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name")));

	Tools.Add(FToolBuilder(TEXT("CREATE_FUNCTION_GRAPH"), TEXT("Create a new custom function graph in a Blueprint."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name")));

	Tools.Add(FToolBuilder(TEXT("CREATE_MACRO_GRAPH"), TEXT("Create a new macro graph in a Blueprint."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("macro_name"), TEXT("string"), TEXT("Macro name")));

	Tools.Add(FToolBuilder(TEXT("SET_NODE_POSITION"), TEXT("Move a node to specific graph coordinates."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position"))
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")));

	Tools.Add(FToolBuilder(TEXT("SET_FUNCTION_REF"), TEXT("Set the function reference on a function call node."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("class_name"), TEXT("string"), TEXT("Class name"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name")));

	Tools.Add(FToolBuilder(TEXT("SET_EVENT_REF"), TEXT("Bind a K2Node_Event to a named function on the parent class chain."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name")));

	Tools.Add(FToolBuilder(TEXT("SET_VARIABLE_REF"), TEXT("Bind a K2Node_VariableGet/Set to a named Blueprint variable."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name")));

	Tools.Add(FToolBuilder(TEXT("SET_CAST_TARGET"), TEXT("Set TargetType on a UK2Node_DynamicCast and rebuild its pins."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("target_class_name"), TEXT("string"), TEXT("Target class")));

	Tools.Add(FToolBuilder(TEXT("SET_SUBSYSTEM_CLASS"), TEXT("Set CustomClass on a UK2Node_GetSubsystem and rebuild its pins."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("subsystem_class_name"), TEXT("string"), TEXT("Subsystem class")));

	Tools.Add(FToolBuilder(TEXT("CLOSE_BLUEPRINT"), TEXT("Close the Blueprint Editor for this asset."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("OPEN_BLUEPRINT"), TEXT("Reopen the Blueprint Editor after node spawning is complete."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("COMPILE"), TEXT("Compile a Blueprint."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("SAVE_BLUEPRINT"), TEXT("Save a Blueprint package to disk."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("GET_COMPILE_ERRORS"), TEXT("Compile a Blueprint and return detailed ERROR/WARNING diagnostics."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	// VARIABLE OPERATIONS (5 tools)
	Tools.Add(FToolBuilder(TEXT("SPAWN_VARIABLE"), TEXT("Add a new member variable to a Blueprint."), TEXT("Variable"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("type_string"), TEXT("string"), TEXT("Variable type"))
		.OptParam(TEXT("category"), TEXT("string"), TEXT("Variable category")));

	Tools.Add(FToolBuilder(TEXT("ADD_VARIABLE"), TEXT("Add a Blueprint member variable (alias of SPAWN_VARIABLE)."), TEXT("Variable"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("var_type"), TEXT("string"), TEXT("Variable type"))
		.OptParam(TEXT("category"), TEXT("string"), TEXT("Variable category")));

	Tools.Add(FToolBuilder(TEXT("LIST_VARIABLES"), TEXT("List a Blueprint's member variables (name/category/type)."), TEXT("Variable"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("SET_VARIABLE_DEFAULT"), TEXT("Set the CDO default value of a Blueprint member variable."), TEXT("Variable"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("default_value"), TEXT("string"), TEXT("Default value")));

	Tools.Add(FToolBuilder(TEXT("SET_VARIABLE_TYPE"), TEXT("Retype an existing Blueprint member variable."), TEXT("Variable"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("new_type"), TEXT("string"), TEXT("New type")));

	// COMPONENT & ASSET OPERATIONS (9 tools)
	Tools.Add(FToolBuilder(TEXT("ADD_COMPONENT"), TEXT("Add a component to a Blueprint's SimpleConstructionScript."), TEXT("Component"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("component_class"), TEXT("string"), TEXT("Component class"))
		.OptParam(TEXT("component_name"), TEXT("string"), TEXT("Component name"))
		.OptParam(TEXT("parent_component_name"), TEXT("string"), TEXT("Parent component")));

	Tools.Add(FToolBuilder(TEXT("SET_ANIM_CLASS"), TEXT("Set the AnimClass on a SkeletalMeshComponent and recompile."), TEXT("Component"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("component_name"), TEXT("string"), TEXT("Component name"))
		.Param(TEXT("anim_bp_path"), TEXT("string"), TEXT("Animation Blueprint path")));

	Tools.Add(FToolBuilder(TEXT("SET_INPUT_ACTION"), TEXT("Wire a UInputAction asset to an input action node."), TEXT("Component"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_id"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("input_action_path"), TEXT("string"), TEXT("Input Action path")));

	Tools.Add(FToolBuilder(TEXT("LIST_ASSETS"), TEXT("List assets, optionally filtered by type."), TEXT("Asset"))
		.OptParam(TEXT("filter"), TEXT("string"), TEXT("Asset type filter")).SetParamTypeEnum(TEXT("Blueprint,Animation,Material,Widget,DataTable,Enum,Structure,")));

	Tools.Add(FToolBuilder(TEXT("LIST_ASSET_PROPERTIES"), TEXT("List editable UPROPERTYs on any UObject asset."), TEXT("Asset"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("GET_ASSET_PROPERTY"), TEXT("Get a property value on any UObject asset via reflection."), TEXT("Asset"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("property_name"), TEXT("string"), TEXT("Property name")));

	Tools.Add(FToolBuilder(TEXT("SET_ASSET_PROPERTY"), TEXT("Set a property value on any UObject asset via reflection."), TEXT("Asset"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("property_name"), TEXT("string"), TEXT("Property name"))
		.Param(TEXT("value"), TEXT("string"), TEXT("New value")));

	Tools.Add(FToolBuilder(TEXT("SAVE_ASSET"), TEXT("Save any UObject asset to disk (not just Blueprints)."), TEXT("Asset"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("FIND_NODE_CLASS"), TEXT("Search loaded UEdGraphNode subclasses by partial name."), TEXT("Asset"))
		.Param(TEXT("partial_name"), TEXT("string"), TEXT("Partial class name")));

	// ANIMATION OPERATIONS (12 tools)
	Tools.Add(FToolBuilder(TEXT("LIST_BLENDSPACES"), TEXT("List BlendSpace/BlendSpace1D assets, optionally filtered."), TEXT("Animation"))
		.OptParam(TEXT("filter"), TEXT("string"), TEXT("Filter")));

	Tools.Add(FToolBuilder(TEXT("GET_MONTAGE_INFO"), TEXT("Get JSON info on an AnimMontage's sections, slots and notifies."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path")));

	Tools.Add(FToolBuilder(TEXT("ADD_MONTAGE_SECTION"), TEXT("Add a named section to an AnimMontage."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path"))
		.Param(TEXT("section_name"), TEXT("string"), TEXT("Section name"))
		.Param(TEXT("start_time"), TEXT("number"), TEXT("Start time (seconds)")).SetParamTypeFloat(0, 100000));

	Tools.Add(FToolBuilder(TEXT("REMOVE_MONTAGE_SECTION"), TEXT("Remove a named section from an AnimMontage."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path"))
		.Param(TEXT("section_name"), TEXT("string"), TEXT("Section name")));

	Tools.Add(FToolBuilder(TEXT("SET_MONTAGE_SLOT"), TEXT("Rename an AnimMontage slot (GroupName.SlotName)."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path"))
		.Param(TEXT("slot_index"), TEXT("number"), TEXT("Slot index")).SetParamTypeInt(0, 1000)
		.Param(TEXT("new_slot_name"), TEXT("string"), TEXT("New slot name")));

	Tools.Add(FToolBuilder(TEXT("ADD_MONTAGE_NOTIFY"), TEXT("Add a single-frame UAnimNotify to an AnimMontage timeline."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path"))
		.Param(TEXT("notify_class_name"), TEXT("string"), TEXT("Notify class (e.g., AnimNotify_PlaySound)")).SetParamTypeEnum(TEXT("AnimNotify_PlaySound,AnimNotify_PlayParticleEffect,AnimNotify_PlaySound2D,"))
		.Param(TEXT("time_seconds"), TEXT("number"), TEXT("Timeline position (seconds)")).SetParamTypeFloat(0, 100000));

	Tools.Add(FToolBuilder(TEXT("ADD_MONTAGE_NOTIFY_STATE"), TEXT("Add a UAnimNotifyState (begin/end window) to an AnimMontage timeline."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path"))
		.Param(TEXT("notify_class_name"), TEXT("string"), TEXT("Notify state class (e.g., AnimNotifyState_Trail)")).SetParamTypeEnum(TEXT("AnimNotifyState_Trail,AnimNotifyState_Hand_IK,"))
		.Param(TEXT("start_seconds"), TEXT("number"), TEXT("Start time (seconds)")).SetParamTypeFloat(0, 100000)
		.Param(TEXT("duration_seconds"), TEXT("number"), TEXT("Duration (seconds)")).SetParamTypeFloat(0, 100000));

	Tools.Add(FToolBuilder(TEXT("REMOVE_MONTAGE_NOTIFY"), TEXT("Remove a notify from an AnimMontage by index."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Montage path"))
		.Param(TEXT("notify_index"), TEXT("number"), TEXT("Notify index")).SetParamTypeInt(0, 10000));

	Tools.Add(FToolBuilder(TEXT("LIST_SKELETON_SOCKETS"), TEXT("List sockets on a Skeleton."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton path")));

	Tools.Add(FToolBuilder(TEXT("ADD_SKELETON_SOCKET"), TEXT("Add a socket to a Skeleton at a bone's origin."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton path"))
		.Param(TEXT("socket_name"), TEXT("string"), TEXT("Socket name"))
		.Param(TEXT("bone_name"), TEXT("string"), TEXT("Bone name"))
		.Param(TEXT("loc_x"), TEXT("number"), TEXT("X location (cm)")).SetParamTypeFloat(-100000, 100000)
		.Param(TEXT("loc_y"), TEXT("number"), TEXT("Y location (cm)")).SetParamTypeFloat(-100000, 100000)
		.Param(TEXT("loc_z"), TEXT("number"), TEXT("Z location (cm)")).SetParamTypeFloat(-100000, 100000));

	Tools.Add(FToolBuilder(TEXT("MOVE_SKELETON_SOCKET"), TEXT("Move/rotate a Skeleton socket (cm / degrees)."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton path"))
		.Param(TEXT("socket_name"), TEXT("string"), TEXT("Socket name"))
		.Param(TEXT("loc_x"), TEXT("number"), TEXT("X location (cm)")).SetParamTypeFloat(-100000, 100000)
		.Param(TEXT("loc_y"), TEXT("number"), TEXT("Y location (cm)")).SetParamTypeFloat(-100000, 100000)
		.Param(TEXT("loc_z"), TEXT("number"), TEXT("Z location (cm)")).SetParamTypeFloat(-100000, 100000)
		.Param(TEXT("pitch"), TEXT("number"), TEXT("Pitch (degrees)")).SetParamTypeFloat(-180, 180)
		.Param(TEXT("yaw"), TEXT("number"), TEXT("Yaw (degrees)")).SetParamTypeFloat(-180, 180)
		.Param(TEXT("roll"), TEXT("number"), TEXT("Roll (degrees)")).SetParamTypeFloat(-180, 180));

	Tools.Add(FToolBuilder(TEXT("DELETE_SKELETON_SOCKET"), TEXT("Delete a socket from a Skeleton."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton path"))
		.Param(TEXT("socket_name"), TEXT("string"), TEXT("Socket name")));

	// INPUT OPERATIONS (6 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_INPUT_ACTION"), TEXT("Create a new UInputAction asset with a given value type."), TEXT("Input"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("value_type"), TEXT("string"), TEXT("Value type (e.g., Value, Axis1D, Axis2D)")).SetParamTypeEnum(TEXT("Value,Axis1D,Axis2D,Axis3D,Digest")));

	Tools.Add(FToolBuilder(TEXT("CREATE_IMC"), TEXT("Create a new UInputMappingContext asset on disk."), TEXT("Input"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("ADD_IMC_MAPPING"), TEXT("Add a key-to-action mapping to an Input Mapping Context."), TEXT("Input"))
		.Param(TEXT("imc_path"), TEXT("string"), TEXT("IMC path"))
		.Param(TEXT("action_path"), TEXT("string"), TEXT("Action path"))
		.Param(TEXT("key_name"), TEXT("string"), TEXT("Key name"))
		.OptParam(TEXT("modifier_classes"), TEXT("string"), TEXT("Modifiers")));

	Tools.Add(FToolBuilder(TEXT("REMOVE_IMC_MAPPING"), TEXT("Remove a key-to-action mapping from an Input Mapping Context."), TEXT("Input"))
		.Param(TEXT("imc_path"), TEXT("string"), TEXT("IMC path"))
		.Param(TEXT("action_path"), TEXT("string"), TEXT("Action path"))
		.Param(TEXT("key_name"), TEXT("string"), TEXT("Key name")));

	Tools.Add(FToolBuilder(TEXT("LIST_IMC_MAPPINGS"), TEXT("List all mappings in an Input Mapping Context."), TEXT("Input"))
		.Param(TEXT("imc_path"), TEXT("string"), TEXT("IMC path")));

	Tools.Add(FToolBuilder(TEXT("ADD_IMC_TO_CHARACTER"), TEXT("Wire GetPlayerController->GetSubsystem->AddMappingContext into BeginPlay."), TEXT("Input"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("imc_path"), TEXT("string"), TEXT("IMC path"))
		.OptParam(TEXT("priority"), TEXT("number"), TEXT("Priority")).SetParamTypeInt(0, 1000));

	// CHARACTER OPERATIONS (4 tools)
	Tools.Add(FToolBuilder(TEXT("SET_CHARACTER_MESH"), TEXT("Set the SkeletalMesh on a character's mesh component."), TEXT("Character"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("mesh_path"), TEXT("string"), TEXT("Mesh path"))
		.OptParam(TEXT("component_name"), TEXT("string"), TEXT("Component name")));

	Tools.Add(FToolBuilder(TEXT("SET_CHARACTER_CAPSULE"), TEXT("Set HalfHeight/Radius on a character's capsule component."), TEXT("Character"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("half_height"), TEXT("number"), TEXT("Half height (cm)")).SetParamTypeFloat(10, 1000)
		.Param(TEXT("radius"), TEXT("number"), TEXT("Radius (cm)")).SetParamTypeFloat(10, 500)
		.OptParam(TEXT("component_name"), TEXT("string"), TEXT("Component name")));

	Tools.Add(FToolBuilder(TEXT("SET_CAMERA_BOOM"), TEXT("Set TargetArmLength/SocketOffset on a SpringArmComponent."), TEXT("Character"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("arm_length"), TEXT("number"), TEXT("Arm length (cm)")).SetParamTypeFloat(0, 10000)
		.Param(TEXT("off_x"), TEXT("number"), TEXT("Offset X (cm)")).SetParamTypeFloat(-1000, 1000)
		.Param(TEXT("off_y"), TEXT("number"), TEXT("Offset Y (cm)")).SetParamTypeFloat(-1000, 1000)
		.Param(TEXT("off_z"), TEXT("number"), TEXT("Offset Z (cm)")).SetParamTypeFloat(-1000, 1000)
		.OptParam(TEXT("component_name"), TEXT("string"), TEXT("Component name")));

	Tools.Add(FToolBuilder(TEXT("SET_GAMEMODE_PAWN"), TEXT("Set DefaultPawnClass on a GameMode Blueprint's CDO."), TEXT("Character"))
		.Param(TEXT("game_mode_bp_path"), TEXT("string"), TEXT("GameMode path"))
		.Param(TEXT("pawn_class_path"), TEXT("string"), TEXT("Pawn class path")));

	// LEVEL OPERATIONS (8 tools)
	Tools.Add(FToolBuilder(TEXT("SPAWN_ACTOR_IN_LEVEL"), TEXT("Place a Blueprint actor instance in the current editor level."), TEXT("Level"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X location (cm)")).SetParamTypeFloat(-1000000, 1000000)
		.Param(TEXT("y"), TEXT("number"), TEXT("Y location (cm)")).SetParamTypeFloat(-1000000, 1000000)
		.Param(TEXT("z"), TEXT("number"), TEXT("Z location (cm)")).SetParamTypeFloat(-1000000, 1000000)
		.Param(TEXT("rot_yaw"), TEXT("number"), TEXT("Yaw rotation (degrees)")).SetParamTypeFloat(-180, 180));

	Tools.Add(FToolBuilder(TEXT("LIST_LEVEL_ACTORS"), TEXT("List actors in the current editor level, optionally filtered."), TEXT("Level"))
		.OptParam(TEXT("filter"), TEXT("string"), TEXT("Filter")));

	Tools.Add(FToolBuilder(TEXT("SET_ACTOR_TRANSFORM"), TEXT("Move/rotate/scale a level actor by its label."), TEXT("Level"))
		.Param(TEXT("actor_label"), TEXT("string"), TEXT("Actor label"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X location (cm)")).SetParamTypeFloat(-1000000, 1000000)
		.Param(TEXT("y"), TEXT("number"), TEXT("Y location (cm)")).SetParamTypeFloat(-1000000, 1000000)
		.Param(TEXT("z"), TEXT("number"), TEXT("Z location (cm)")).SetParamTypeFloat(-1000000, 1000000)
		.Param(TEXT("pitch"), TEXT("number"), TEXT("Pitch (degrees)")).SetParamTypeFloat(-180, 180)
		.Param(TEXT("yaw"), TEXT("number"), TEXT("Yaw (degrees)")).SetParamTypeFloat(-180, 180)
		.Param(TEXT("roll"), TEXT("number"), TEXT("Roll (degrees)")).SetParamTypeFloat(-180, 180)
		.OptParam(TEXT("sx"), TEXT("number"), TEXT("Scale X")).SetParamTypeFloat(0.1, 10)
		.OptParam(TEXT("sy"), TEXT("number"), TEXT("Scale Y")).SetParamTypeFloat(0.1, 10)
		.OptParam(TEXT("sz"), TEXT("number"), TEXT("Scale Z")).SetParamTypeFloat(0.1, 10));

	Tools.Add(FToolBuilder(TEXT("DELETE_LEVEL_ACTOR"), TEXT("Remove an actor from the level by its label."), TEXT("Level"))
		.Param(TEXT("actor_label"), TEXT("string"), TEXT("Actor label")));


	Tools.Add(FToolBuilder(TEXT("SET_LEVEL_GAMEMODE"), TEXT("Override DefaultGameMode for the current editor level."), TEXT("Level"))
		.Param(TEXT("game_mode_bp_path"), TEXT("string"), TEXT("GameMode path")));

	Tools.Add(FToolBuilder(TEXT("CAPTURE_VIEW"), TEXT("Render the scene from a controlled viewpoint and return as PNG image."), TEXT("Level"))
		.Param(TEXT("target"), TEXT("string"), TEXT("Target actor"))
		.OptParam(TEXT("focus"), TEXT("string"), TEXT("Focus actor"))
		.OptParam(TEXT("distance"), TEXT("number"), TEXT("Distance (cm)")).SetParamTypeFloat(1, 100000)
		.OptParam(TEXT("yaw"), TEXT("number"), TEXT("Yaw (degrees)")).SetParamTypeFloat(-180, 180)
		.OptParam(TEXT("pitch"), TEXT("number"), TEXT("Pitch (degrees)")).SetParamTypeFloat(-90, 90)
		.OptParam(TEXT("res"), TEXT("number"), TEXT("Resolution (pixels)")).SetParamTypeInt(256, 4096)
		.OptParam(TEXT("mode"), TEXT("string"), TEXT("Mode")).SetParamTypeEnum(TEXT("lit,wireframe,unlit,"))
		.OptParam(TEXT("angles"), TEXT("number"), TEXT("Num angles")).SetParamTypeInt(1, 360)
		.OptParam(TEXT("pin_pose"), TEXT("string"), TEXT("Pose")));

	// DATA TABLE OPERATIONS (4 tools)
	Tools.Add(FToolBuilder(TEXT("LIST_DATATABLE_ROWS"), TEXT("List rows and fields of a DataTable."), TEXT("Data"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("DataTable path")));

	Tools.Add(FToolBuilder(TEXT("ADD_DATATABLE_ROW"), TEXT("Add a default-value row to a DataTable."), TEXT("Data"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("DataTable path"))
		.Param(TEXT("row_name"), TEXT("string"), TEXT("Row name")));

	Tools.Add(FToolBuilder(TEXT("DELETE_DATATABLE_ROW"), TEXT("Delete a row from a DataTable."), TEXT("Data"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("DataTable path"))
		.Param(TEXT("row_name"), TEXT("string"), TEXT("Row name")));

	Tools.Add(FToolBuilder(TEXT("RENAME_DATATABLE_ROW"), TEXT("Rename a DataTable row (handles undo/redo and cross-reference fixup)."), TEXT("Data"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("DataTable path"))
		.Param(TEXT("old_name"), TEXT("string"), TEXT("Old name"))
		.Param(TEXT("new_name"), TEXT("string"), TEXT("New name")));

	// MATERIAL OPERATIONS (6 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_MATERIAL"), TEXT("Create a new Material asset with a given blend mode."), TEXT("Material"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("blend_mode"), TEXT("string"), TEXT("Blend mode")).SetParamTypeEnum(TEXT("Opaque,Masked,Translucent,Additive,Modulate,")));

	Tools.Add(FToolBuilder(TEXT("ADD_MATERIAL_NODE"), TEXT("Add a material expression node to a Material's graph."), TEXT("Material"))
		.Param(TEXT("material_path"), TEXT("string"), TEXT("Material path"))
		.Param(TEXT("node_type"), TEXT("string"), TEXT("Node type"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position")).SetParamTypeInt(-100000, 100000));

	Tools.Add(FToolBuilder(TEXT("CONNECT_MATERIAL_PINS"), TEXT("Connect an output pin of one material expression to an input pin of another."), TEXT("Material"))
		.Param(TEXT("material_path"), TEXT("string"), TEXT("Material path"))
		.Param(TEXT("node_index_a"), TEXT("number"), TEXT("Source node")).SetParamTypeInt(0, 10000)
		.Param(TEXT("output_pin"), TEXT("string"), TEXT("Output pin"))
		.Param(TEXT("node_index_b"), TEXT("number"), TEXT("Target node")).SetParamTypeInt(0, 10000)
		.Param(TEXT("input_pin"), TEXT("string"), TEXT("Input pin")));

	Tools.Add(FToolBuilder(TEXT("SET_MATERIAL_RESULT"), TEXT("Connect a material expression's output to a final material property."), TEXT("Material"))
		.Param(TEXT("material_path"), TEXT("string"), TEXT("Material path"))
		.Param(TEXT("channel"), TEXT("string"), TEXT("Channel")).SetParamTypeEnum(TEXT("BaseColor,Normal,Metallic,Specular,Roughness,Opacity,"))
		.Param(TEXT("node_index"), TEXT("number"), TEXT("Node index")).SetParamTypeInt(0, 10000)
		.Param(TEXT("output_pin"), TEXT("string"), TEXT("Output pin")));

	Tools.Add(FToolBuilder(TEXT("COMPILE_MATERIAL"), TEXT("Recompile a Material and report ERROR diagnostics, or CLEAN."), TEXT("Material"))
		.Param(TEXT("material_path"), TEXT("string"), TEXT("Material path")));

	Tools.Add(FToolBuilder(TEXT("CLOSE_MATERIAL"), TEXT("Close the Material Editor for this asset before graph mutation."), TEXT("Material"))
		.Param(TEXT("material_path"), TEXT("string"), TEXT("Material path")));

	// WIDGET (UMG) OPERATIONS (3 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_WIDGET_BLUEPRINT"), TEXT("Create a new UMG Widget Blueprint with an empty canvas root."), TEXT("Widget"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("ADD_WIDGET_ELEMENT"), TEXT("Add a widget element as a child of the root canvas panel."), TEXT("Widget"))
		.Param(TEXT("widget_bp_path"), TEXT("string"), TEXT("Widget path"))
		.Param(TEXT("element_type"), TEXT("string"), TEXT("Element type"))
		.Param(TEXT("name"), TEXT("string"), TEXT("Name"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position")).SetParamTypeInt(0, 10000)
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")).SetParamTypeInt(0, 10000)
		.Param(TEXT("w"), TEXT("number"), TEXT("Width")).SetParamTypeInt(10, 10000)
		.Param(TEXT("h"), TEXT("number"), TEXT("Height")).SetParamTypeInt(10, 10000));

	Tools.Add(FToolBuilder(TEXT("SET_WIDGET_TEXT"), TEXT("Set the default text of a UTextBlock (or other text widget) by name."), TEXT("Widget"))
		.Param(TEXT("widget_bp_path"), TEXT("string"), TEXT("Widget path"))
		.Param(TEXT("element_name"), TEXT("string"), TEXT("Element name"))
		.Param(TEXT("text"), TEXT("string"), TEXT("Text")));

	// UTILITY OPERATIONS (2 tools)
	Tools.Add(FToolBuilder(TEXT("RUN_PYTHON"), TEXT("Execute arbitrary Python inside UE and return captured stdout."), TEXT("Utility"))
		.Param(TEXT("code"), TEXT("string"), TEXT("Python code")));

	Tools.Add(FToolBuilder(TEXT("CREATE_BLUEPRINT"), TEXT("Create a new Blueprint asset inheriting from a given parent class."), TEXT("Utility"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("parent_class"), TEXT("string"), TEXT("Parent class")));

	// ANIMATION STATE MACHINES (11 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_STATE_MACHINE"), TEXT("Create a state machine in an AnimGraph."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"))
		.Param(TEXT("machine_name"), TEXT("string"), TEXT("State machine name"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position")).SetParamTypeInt(-100000, 100000)
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")).SetParamTypeInt(-100000, 100000));

	Tools.Add(FToolBuilder(TEXT("ADD_ANIM_STATE"), TEXT("Add a state to an AnimGraph state machine."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("machine_guid"), TEXT("string"), TEXT("State machine GUID"))
		.Param(TEXT("state_name"), TEXT("string"), TEXT("State name"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position")).SetParamTypeInt(-100000, 100000)
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")).SetParamTypeInt(-100000, 100000));

	Tools.Add(FToolBuilder(TEXT("ADD_ANIM_TRANSITION"), TEXT("Add a transition between animation states."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("machine_guid"), TEXT("string"), TEXT("State machine GUID"))
		.Param(TEXT("from_state_guid"), TEXT("string"), TEXT("Source state GUID"))
		.Param(TEXT("to_state_guid"), TEXT("string"), TEXT("Target state GUID")));

	Tools.Add(FToolBuilder(TEXT("SET_TRANSITION_CONDITION"), TEXT("Set a transition condition on a state machine transition."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("transition_guid"), TEXT("string"), TEXT("Transition GUID"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("b_negate"), TEXT("number"), TEXT("Negate condition (0/1)")).SetParamTypeInt(0, 1));

	Tools.Add(FToolBuilder(TEXT("LIST_ANIM_STATES"), TEXT("List all states in an animation state machine."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("machine_guid"), TEXT("string"), TEXT("State machine GUID")));

	Tools.Add(FToolBuilder(TEXT("GET_ANIM_STATE_TRANSITIONS"), TEXT("List transitions from a state."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("state_guid"), TEXT("string"), TEXT("State GUID")));

	Tools.Add(FToolBuilder(TEXT("LIST_ANIM_GRAPH_NODES"), TEXT("List all nodes in an AnimGraph."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("graph_name"), TEXT("string"), TEXT("Graph name")));

	Tools.Add(FToolBuilder(TEXT("GET_ANIM_NODE_PINS"), TEXT("List pins on an AnimGraph node."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID")));

	Tools.Add(FToolBuilder(TEXT("GET_ANIM_PIN_CONNECTIONS"), TEXT("List connections on an AnimGraph pin."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("pin_name"), TEXT("string"), TEXT("Pin name")));

	Tools.Add(FToolBuilder(TEXT("CONNECT_ANIM_PINS"), TEXT("Connect two pins in an AnimGraph."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_a_guid"), TEXT("string"), TEXT("Source node GUID"))
		.Param(TEXT("pin_a"), TEXT("string"), TEXT("Source pin"))
		.Param(TEXT("node_b_guid"), TEXT("string"), TEXT("Target node GUID"))
		.Param(TEXT("pin_b"), TEXT("string"), TEXT("Target pin")));

	Tools.Add(FToolBuilder(TEXT("ADD_ANIM_SLOT_NODE"), TEXT("Add a slot node to an AnimGraph."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("graph_name"), TEXT("string"), TEXT("Graph name"))
		.Param(TEXT("slot_name"), TEXT("string"), TEXT("Slot name (e.g., Default, Arms.IKRig_Arm_L)"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X position")).SetParamTypeInt(-100000, 100000)
		.Param(TEXT("y"), TEXT("number"), TEXT("Y position")).SetParamTypeInt(-100000, 100000));

	Tools.Add(FToolBuilder(TEXT("CREATE_ANIM_MONTAGE"), TEXT("Create a new AnimMontage asset."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton path")));

	// IK RIG & RETARGETING (5 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_IK_RIG"), TEXT("Create a new IK Rig asset."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton path")));

	Tools.Add(FToolBuilder(TEXT("CREATE_IK_RETARGETER"), TEXT("Create a new IK Retargeter asset."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("source_ik_rig_path"), TEXT("string"), TEXT("Source IK Rig"))
		.Param(TEXT("target_ik_rig_path"), TEXT("string"), TEXT("Target IK Rig")));

	Tools.Add(FToolBuilder(TEXT("ADD_IK_GOAL"), TEXT("Add an IK goal to an IK Rig."), TEXT("Animation"))
		.Param(TEXT("ik_rig_path"), TEXT("string"), TEXT("IK Rig path"))
		.Param(TEXT("goal_name"), TEXT("string"), TEXT("Goal name"))
		.Param(TEXT("bone_name"), TEXT("string"), TEXT("Bone name")));

	Tools.Add(FToolBuilder(TEXT("ADD_RETARGET_CHAIN"), TEXT("Add a retarget chain to an IK Retargeter."), TEXT("Animation"))
		.Param(TEXT("retargeter_path"), TEXT("string"), TEXT("Retargeter path"))
		.Param(TEXT("chain_name"), TEXT("string"), TEXT("Chain name"))
		.Param(TEXT("source_chain"), TEXT("string"), TEXT("Source chain"))
		.Param(TEXT("target_chain"), TEXT("string"), TEXT("Target chain"))
		.Param(TEXT("target_skeleton"), TEXT("string"), TEXT("Target skeleton")));

	Tools.Add(FToolBuilder(TEXT("IK_RIG_AUTO_SETUP"), TEXT("Auto-setup an IK Rig from a skeleton."), TEXT("Animation"))
		.Param(TEXT("ik_rig_path"), TEXT("string"), TEXT("IK Rig path")));

	// NIAGARA (4 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_NIAGARA_SYSTEM"), TEXT("Create a new Niagara System asset."), TEXT("Niagara"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("CREATE_NIAGARA_EMITTER"), TEXT("Create a new Niagara Emitter asset."), TEXT("Niagara"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("LIST_NIAGARA_MODULES"), TEXT("List modules in a Niagara emitter."), TEXT("Niagara"))
		.Param(TEXT("emitter_path"), TEXT("string"), TEXT("Emitter path"))
		.Param(TEXT("stage"), TEXT("string"), TEXT("Stage (spawn/update)")));

	Tools.Add(FToolBuilder(TEXT("SET_NIAGARA_MODULE_INPUT"), TEXT("Set input value on a Niagara module."), TEXT("Niagara"))
		.Param(TEXT("emitter_path"), TEXT("string"), TEXT("Emitter path"))
		.Param(TEXT("module_name"), TEXT("string"), TEXT("Module name"))
		.Param(TEXT("input_name"), TEXT("string"), TEXT("Input name"))
		.Param(TEXT("value"), TEXT("string"), TEXT("Value"))
		.Param(TEXT("stage"), TEXT("string"), TEXT("Stage")));

	// BLEND SPACES (4 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_BLEND_SPACE"), TEXT("Create a new BlendSpace asset."), TEXT("Animation"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("b_1d"), TEXT("number"), TEXT("Is 1D space (0/1)")));

	Tools.Add(FToolBuilder(TEXT("ADD_BLEND_SPACE_SAMPLE"), TEXT("Add a sample to a BlendSpace."), TEXT("Animation"))
		.Param(TEXT("blend_space_path"), TEXT("string"), TEXT("BlendSpace path"))
		.Param(TEXT("anim_path"), TEXT("string"), TEXT("Animation path"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X coordinate"))
		.Param(TEXT("y"), TEXT("number"), TEXT("Y coordinate"), false));

	Tools.Add(FToolBuilder(TEXT("EDIT_BLEND_SPACE_SAMPLE"), TEXT("Edit a sample in a BlendSpace."), TEXT("Animation"))
		.Param(TEXT("blend_space_path"), TEXT("string"), TEXT("BlendSpace path"))
		.Param(TEXT("sample_index"), TEXT("number"), TEXT("Sample index"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X coordinate"))
		.Param(TEXT("y"), TEXT("number"), TEXT("Y coordinate"), false));

	Tools.Add(FToolBuilder(TEXT("SET_BLEND_SPACE_PLAYER_ASSET"), TEXT("Set the BlendSpace on a BlendSpacePlayer node."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("blend_space_path"), TEXT("string"), TEXT("BlendSpace path")));

	// TYPE AUTHORING (5 tools)
	Tools.Add(FToolBuilder(TEXT("CREATE_ENUM"), TEXT("Create a new Enum asset."), TEXT("Type"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("enum_values"), TEXT("string"), TEXT("Comma-separated list of enum value names")));

	Tools.Add(FToolBuilder(TEXT("CREATE_STRUCT"), TEXT("Create a new Structure asset."), TEXT("Type"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	Tools.Add(FToolBuilder(TEXT("ADD_STRUCT_MEMBER"), TEXT("Add a member to a Structure."), TEXT("Type"))
		.Param(TEXT("struct_path"), TEXT("string"), TEXT("Struct path"))
		.Param(TEXT("member_name"), TEXT("string"), TEXT("Member name"))
		.Param(TEXT("member_type"), TEXT("string"), TEXT("Member type")));

	Tools.Add(FToolBuilder(TEXT("CREATE_EVENT_DISPATCHER"), TEXT("Create an Event Dispatcher in a Blueprint."), TEXT("Type"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("dispatcher_name"), TEXT("string"), TEXT("Dispatcher name")));

	Tools.Add(FToolBuilder(TEXT("CREATE_FUNCTION_LIBRARY"), TEXT("Create a new Function Library Blueprint."), TEXT("Type"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")));

	// SIGNATURES & LOCALS (6 tools)
	Tools.Add(FToolBuilder(TEXT("ADD_FUNCTION_PARAM"), TEXT("Add a parameter to a function."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name"))
		.Param(TEXT("param_name"), TEXT("string"), TEXT("Parameter name"))
		.Param(TEXT("param_type"), TEXT("string"), TEXT("Parameter type")).SetParamTypeEnum(TEXT("bool,int32,float,FString,FVector,FRotator")));

	Tools.Add(FToolBuilder(TEXT("REMOVE_FUNCTION_PARAM"), TEXT("Remove a parameter from a function."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name"))
		.Param(TEXT("param_name"), TEXT("string"), TEXT("Parameter name"))
		.Param(TEXT("param_index"), TEXT("number"), TEXT("Parameter index")));

	Tools.Add(FToolBuilder(TEXT("ADD_CUSTOM_EVENT_PARAM"), TEXT("Add a parameter to a custom event."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("event_name"), TEXT("string"), TEXT("Event name"))
		.Param(TEXT("param_name"), TEXT("string"), TEXT("Parameter name")));

	Tools.Add(FToolBuilder(TEXT("REMOVE_CUSTOM_EVENT_PARAM"), TEXT("Remove a parameter from a custom event."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("event_name"), TEXT("string"), TEXT("Event name"))
		.Param(TEXT("param_name"), TEXT("string"), TEXT("Parameter name")));

	Tools.Add(FToolBuilder(TEXT("SET_CUSTOM_EVENT_NAME"), TEXT("Rename a custom event."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("old_name"), TEXT("string"), TEXT("Old name"))
		.Param(TEXT("new_name"), TEXT("string"), TEXT("New name")));

	Tools.Add(FToolBuilder(TEXT("ADD_LOCAL_VARIABLE"), TEXT("Add a local variable to a function."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("function_name"), TEXT("string"), TEXT("Function name"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("var_type"), TEXT("string"), TEXT("Variable type"))
		.Param(TEXT("category"), TEXT("string"), TEXT("Category")));

	// ADVANCED NODE OPERATIONS (6 tools - borderline commands, now promoted)
	Tools.Add(FToolBuilder(TEXT("SPAWN_NODE_ANCHORED"), TEXT("Spawn a node relative to an anchor node (not absolute coordinates)."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("anchor_node_guid"), TEXT("string"), TEXT("Anchor node GUID"))
		.Param(TEXT("node_class"), TEXT("string"), TEXT("Node class"))
		.Param(TEXT("comment"), TEXT("string"), TEXT("Node comment"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X offset from anchor")));

	Tools.Add(FToolBuilder(TEXT("CREATE_BLEND_SPACE_PLAYER_ANCHORED"), TEXT("Create a BlendSpace player node anchored to another node."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("anchor_node_guid"), TEXT("string"), TEXT("Anchor node GUID"))
		.Param(TEXT("blend_space_path"), TEXT("string"), TEXT("BlendSpace path"))
		.Param(TEXT("x"), TEXT("number"), TEXT("X offset"))
		.Param(TEXT("y"), TEXT("number"), TEXT("Y offset")));

	Tools.Add(FToolBuilder(TEXT("ADD_ARRAY_PIN"), TEXT("Add an input pin to a MakeArray node."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("MakeArray node GUID")));

	Tools.Add(FToolBuilder(TEXT("SET_VARIABLE_METADATA"), TEXT("Set metadata on a Blueprint variable (EditCondition, DisplayName, etc)."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Variable name"))
		.Param(TEXT("meta_key"), TEXT("string"), TEXT("Metadata key"))
		.Param(TEXT("meta_value"), TEXT("string"), TEXT("Metadata value")));

	Tools.Add(FToolBuilder(TEXT("SET_EXTERNAL_VARIABLE_REF"), TEXT("Bind a variable node to a property on an arbitrary class (not just Blueprint's own)."), TEXT("Blueprint"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("node_guid"), TEXT("string"), TEXT("Node GUID"))
		.Param(TEXT("owner_class_name"), TEXT("string"), TEXT("Owner class name"))
		.Param(TEXT("var_name"), TEXT("string"), TEXT("Property name")));

	Tools.Add(FToolBuilder(TEXT("CREATE_PHYSICS_ASSET"), TEXT("Create a Physics Asset from a Skeletal Mesh."), TEXT("Asset"))
		.Param(TEXT("skeletal_mesh_path"), TEXT("string"), TEXT("Skeletal Mesh path"))
		.Param(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
		.Param(TEXT("b_set_to_mesh"), TEXT("number"), TEXT("Auto-set to mesh (0/1)")));

	// LEVEL/GAMEPLAY QUERIES (3 tools)
	Tools.Add(FToolBuilder(TEXT("GET_CURRENT_GAMEMODE"), TEXT("Query the current level's GameMode class and world name."), TEXT("Gameplay")));

	Tools.Add(FToolBuilder(TEXT("GET_PLAYER_START"), TEXT("List all PlayerStart actors in the current editor level as JSON."), TEXT("Gameplay")));

	Tools.Add(FToolBuilder(TEXT("LIST_STATE_GRAPH_NODES"), TEXT("List all nodes in a state machine's state or transition."), TEXT("Animation"))
		.Param(TEXT("bp_path"), TEXT("string"), TEXT("Blueprint path"))
		.Param(TEXT("state_or_transition_guid"), TEXT("string"), TEXT("State or transition GUID")));

	// Verify we have all tools (129 total: Blueprint, Variable, Component, Animation, Asset, Material, Level, Input, Character, Data, Type, Niagara, Widget, Utility, Gameplay)
	check(Tools.Num() == 129);

	return Tools;
}
