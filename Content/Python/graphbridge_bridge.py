# GraphBridge AI â€” graphbridge_bridge.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
websocket_bridge.py
Core async wrapper around the GraphBridgev2 WebSocket protocol.

Every public method maps 1:1 to a command the C++ router understands.
Dead commands (SET_FUNCTION, SPAWN_VARIABLE_GETTER) have been removed —
they are not implemented on the C++ side and would return an error.
"""

import asyncio
import json
import websockets


class UnrealBridge:
    def __init__(self, uri: str = "ws://127.0.0.1:8080", timeout: float = 10.0):
        self.uri = uri
        self.websocket = None
        self.timeout = timeout

    # ------------------------------------------------------------------
    # Connection management
    # ------------------------------------------------------------------

    async def connect(self, retries: int = 5, delay: float = 1.0) -> bool:
        """
        Connect to the Unreal GraphBridge WebSocket server.
        Retries up to `retries` times with `delay` seconds between attempts.
        Returns True on success, False if all attempts fail.
        """
        for attempt in range(1, retries + 1):
            try:
                self.websocket = await websockets.connect(self.uri)
                print(f"[Bridge] Connected to {self.uri}")
                return True
            except Exception as e:
                print(f"[Bridge] Attempt {attempt}/{retries} failed: {e}")
                if attempt < retries:
                    await asyncio.sleep(delay)
        print("[Bridge] Could not connect. Is the Unreal editor open with the plugin loaded?")
        return False

    async def close(self):
        if self.websocket:
            await self.websocket.close()
            self.websocket = None
            print("[Bridge] Disconnected")

    async def _send_command(self, command_str: str) -> dict | None:
        """
        Send a raw pipe-delimited command string and return the parsed JSON response.
        Returns None if not connected or if the response cannot be parsed.
        """
        if not self.websocket:
            print("[Bridge] Not connected — call connect() first")
            return None
        try:
            await self.websocket.send(command_str)
            raw = await asyncio.wait_for(self.websocket.recv(), timeout=self.timeout)
            return json.loads(raw)
        except asyncio.TimeoutError:
            print(f"[Bridge] Command timed out after {self.timeout}s: {command_str!r}")
            return None
        except json.JSONDecodeError:
            print(f"[Bridge] Bad JSON response: {raw!r}")
            return None
        except websockets.ConnectionClosed:
            print("[Bridge] Connection closed unexpectedly")
            self.websocket = None
            return None

    # ------------------------------------------------------------------
    # Graph node operations
    # ------------------------------------------------------------------

    async def spawn_node(self, bp_path: str, node_class: str,
                         comment: str, x: int, y: int) -> dict | None:
        """
        Spawn a graph node.
        node_class: partial or full UClass name, e.g. "K2Node_CallFunction"
        Returns payload containing the new node's GUID.
        """
        return await self._send_command(
            f"SPAWN_NODE|{bp_path}|{node_class}|{comment}|{x}|{y}")

    async def spawn_event_node(self, bp_path: str, event_func_name: str,
                               comment: str, x: int, y: int) -> dict | None:
        """
        Spawn a class-level override event node (K2Node_Event).

        event_func_name: internal UFunction name on the parent class:
            ReceiveAnyDamage      -> Any Damage event
            ReceiveBeginPlay      -> Event BeginPlay
            ReceiveHit            -> Event Hit
            ReceivePointDamage    -> Point Damage event
            ReceiveRadialDamage   -> Radial Damage event
            ReceiveTick           -> Event Tick
            ReceiveDestroyed      -> Event Destroyed
            ReceiveEndPlay        -> Event End Play

        Use this instead of spawn_node() for ALL event nodes.
        spawn_node() with "Event" may produce a dead ActorBoundEvent node.
        """
        return await self._send_command(
            f"SPAWN_EVENT_NODE|{bp_path}|{event_func_name}|{comment}|{x}|{y}")


    async def connect_pins(self, bp_path: str,
                           node_a: str, pin_a: str,
                           node_b: str, pin_b: str) -> dict | None:
        """Connect an output pin on node_a to an input pin on node_b."""
        return await self._send_command(
            f"CONNECT_PINS|{bp_path}|{node_a}|{pin_a}|{node_b}|{pin_b}")

    async def disconnect_pins(self, bp_path: str,
                              node_a: str, pin_a: str,
                              node_b: str, pin_b: str) -> dict | None:
        """Break all connections on pin_a of node_a."""
        return await self._send_command(
            f"DISCONNECT_PINS|{bp_path}|{node_a}|{pin_a}|{node_b}|{pin_b}")

    async def delete_node(self, bp_path: str, node_id: str) -> dict | None:
        """Delete a node by GUID or comment string."""
        return await self._send_command(f"DELETE_NODE|{bp_path}|{node_id}")

    async def clear_nodes(self, bp_path: str, comment_match: str) -> dict | None:
        """Delete all nodes whose comment contains comment_match."""
        return await self._send_command(f"CLEAR_NODES|{bp_path}|{comment_match}")

    async def set_pin_default(self, bp_path: str, node_id: str,
                              pin_name: str, default_value: str) -> dict | None:
        """Set the literal default value on an unconnected input pin."""
        return await self._send_command(
            f"SET_PIN_DEFAULT|{bp_path}|{node_id}|{pin_name}|{default_value}")

    async def get_node_pins(self, bp_path: str, node_name: str) -> dict | None:
        """
        Return a comma-separated list of pin descriptors for a node.
        Each descriptor is formatted as IN:PinName or OUT:PinName.
        node_name can be the node title, comment, or GUID.
        """
        return await self._send_command(f"GET_NODE_PINS|{bp_path}|{node_name}")

    async def get_pin_connections(self, bp_path: str, node_guid: str, pin_name: str) -> list[dict]:
        """
        List what a pin is connected to. Call this before CONNECT_PINS to avoid
        duplicate connections or schema errors.

        Returns a list of dicts: [{"node_guid": ..., "pin_name": ...}, ...]
        Empty list if the pin has no connections. Raises nothing on error —
        check result["success"] is False / result["message"] for ERR: details
        if you need to distinguish "not connected" from "node/pin not found".
        """
        result = await self._send_command(f"GET_PIN_CONNECTIONS|{bp_path}|{node_guid}|{pin_name}")
        if not result or not result.get("success") or not result.get("payload"):
            return []
        connections = []
        for entry in result["payload"].split(","):
            if ":" not in entry:
                continue
            guid, pin = entry.split(":", 1)
            connections.append({"node_guid": guid, "pin_name": pin})
        return connections

    async def get_pin_default(self, bp_path: str, node_guid: str, pin_name: str) -> dict | None:
        """
        Read the current default value on a pin before deciding whether to
        overwrite it with SET_PIN_DEFAULT, or to audit graph state.

        result["payload"] is the DefaultValue string, or the asset path of
        DefaultObject for object/class pins. Empty payload (success=True)
        means the pin has no default set.
        """
        return await self._send_command(f"GET_PIN_DEFAULT|{bp_path}|{node_guid}|{pin_name}")

    async def create_blueprint(self, asset_path: str, parent_class: str) -> dict | None:
        """
        Create a new Blueprint asset inheriting from parent_class.

        asset_path:   e.g. "/Game/Blueprints/BP_Enemy"
        parent_class: short C++ name, A/U prefix optional, e.g. "Character",
                      "Actor", "Pawn", "ActorComponent", "GameModeBase",
                      "PlayerController", "HUD", "GameInstance", "GameState",
                      "PlayerState"

        result["payload"] is the canonical asset path ("PackagePath.AssetName")
        on success.
        """
        return await self._send_command(f"CREATE_BLUEPRINT|{asset_path}|{parent_class}")

    async def get_compile_errors(self, bp_path: str) -> list[dict]:
        """
        Compile the Blueprint and return detailed diagnostics instead of the
        simple pass/fail that compile() gives — use this when a compile fails
        and the agent needs to know exactly what to fix.

        Returns a list of dicts: [{"type": "ERROR"|"WARNING", "message": str}, ...]
        Empty list means the compile was clean (no errors or warnings).
        """
        result = await self._send_command(f"GET_COMPILE_ERRORS|{bp_path}")
        if not result or not result.get("success") or not result.get("payload"):
            return []
        if result["payload"] == "CLEAN":
            return []
        issues = []
        for entry in result["payload"].split("|"):
            if ":" not in entry:
                continue
            kind, message = entry.split(":", 1)
            issues.append({"type": kind, "message": message})
        return issues

    # ------------------------------------------------------------------
    # Variable operations
    # ------------------------------------------------------------------

    async def spawn_variable(self, bp_path: str, var_name: str,
                             type_string: str, category: str = "Default") -> dict | None:
        """
        Add a new member variable to a Blueprint.

        type_string values:
            Primitives : "bool", "int32", "int64", "float", "double",
                         "byte", "FString", "FName", "FText"
            Structs    : "FVector", "FVector2D", "FRotator",
                         "FTransform", "FLinearColor"
            Prefixed   : "object:ACharacter", "class:APawn"

        The actual variable name (with any uniqueness suffix) is in result["payload"].
        Always use result["payload"] — not result["message"] — to get the real name.
        """
        return await self._send_command(
            f"SPAWN_VARIABLE|{bp_path}|{var_name}|{type_string}|{category}")

    async def set_variable_default(self, bp_path: str,
                                   var_name: str, default_value: str) -> dict | None:
        """
        Set the CDO default value of a Blueprint member variable.

        default_value format by type:
            bool        ->  "true" / "false"
            int32/float ->  "42" / "3.14"
            FVector     ->  "(X=1.0,Y=0.0,Z=0.0)"
            FRotator    ->  "(Pitch=0.0,Yaw=90.0,Roll=0.0)"
            FLinearColor->  "(R=1.0,G=0.0,B=0.0,A=1.0)"
            FString     ->  "Hello World"

        Always follow with compile() to bake the value into the CDO.
        """
        return await self._send_command(
            f"SET_VARIABLE_DEFAULT|{bp_path}|{var_name}|{default_value}")

    # ------------------------------------------------------------------
    # Blueprint lifecycle
    # ------------------------------------------------------------------

    async def close_blueprint(self, bp_path: str) -> dict | None:
        """
        Close the Blueprint Editor for this asset.
        ALWAYS call this before any SPAWN_NODE sequence — the SCS preview
        viewport ticks against spawned nodes and causes a LowLevelFatalError
        if the editor is open during construction.
        """
        return await self._send_command(f"CLOSE_BLUEPRINT|{bp_path}")

    async def open_blueprint(self, bp_path: str) -> dict | None:
        """Reopen the Blueprint Editor after node spawning is complete."""
        return await self._send_command(f"OPEN_BLUEPRINT|{bp_path}")

    async def compile(self, bp_path: str) -> dict | None:
        """Compile the Blueprint. Always call this after graph/variable changes."""
        return await self._send_command(f"COMPILE|{bp_path}")

    async def save_blueprint(self, bp_path: str) -> dict | None:
        """
        Trigger an on-disk save of the Blueprint package.
        SAVE_BLUEPRINT marks the package dirty and calls UPackage::SavePackage,
        writing the .uasset to disk immediately. The Editor reflects the saved
        state without requiring a manual Ctrl+S.
        """
        return await self._send_command(f"SAVE_BLUEPRINT|{bp_path}")

    async def set_input_action(self, bp_path: str,
                               node_id: str, input_action_path: str) -> dict | None:
        """Wire a UInputAction asset to an input action node (legacy or Enhanced Input)."""
        return await self._send_command(
            f"SET_INPUT_ACTION|{bp_path}|{node_id}|{input_action_path}")

    async def set_variable_ref(self, bp_path: str,
                               node_id: str, var_name: str) -> dict | None:
        """
        Bind a K2Node_VariableGet/Set to a named Blueprint variable and
        reconstruct its pins so the output type is resolved before connecting.
        Call immediately after spawn_node for any variable getter/setter node.

        var_name: exact variable name as it appears in the Blueprint panel,
                  e.g. "FlightComponent"
        """
        return await self._send_command(
            f"SET_VARIABLE_REF|{bp_path}|{node_id}|{var_name}")

    async def set_function_ref(self, bp_path: str, node_id: str,
                               class_name: str, function_name: str) -> dict | None:
        """
        Set the function reference on a K2Node_CallFunction and rebuild its pins.
        Call this immediately after spawn_node for any CallFunction node — before
        compile — otherwise the node has no exec pins.

        class_name:    C++ class name, e.g. "FlightComponent" or "UFlightComponent"
        function_name: Exact UFUNCTION name, e.g. "ToggleFlight"
        """
        return await self._send_command(
            f"SET_FUNCTION_REF|{bp_path}|{node_id}|{class_name}|{function_name}")

    async def add_component(self, bp_path: str,
                            component_class: str,
                            component_name: str) -> dict | None:
        """
        Add a component to a Blueprint's SimpleConstructionScript.
        Compiles the Blueprint automatically after adding.

        component_class: C++ class name e.g. "ProjectileMovementComponent"
                         or Blueprint asset path e.g. "/Game/BP_MyComp.BP_MyComp"
        component_name:  Variable name for the component in the Blueprint panel.

        After this call, use SET_VARIABLE_REF with component_name to get a
        reference node in the EventGraph.
        """
        return await self._send_command(
            f"ADD_COMPONENT|{bp_path}|{component_class}|{component_name}")

    async def set_event_ref(self, bp_path: str,
                            node_id: str, function_name: str) -> dict | None:
        """
        Bind a K2Node_Event to a named event on the parent class chain and
        reconstruct its pins. Call immediately after spawn_node for event nodes.

        function_name: internal UFunction name, e.g. "ReceiveBeginPlay"
                       (ReceiveBeginPlay = Event BeginPlay on AActor)

        Requires SET_EVENT_REF to be implemented in GraphBridgeAutomationLibrary.cpp.
        """
        return await self._send_command(
            f"SET_EVENT_REF|{bp_path}|{node_id}|{function_name}")

    async def set_subsystem_class(self, bp_path: str,
                                   node_guid: str,
                                   subsystem_class_name: str) -> dict | None:
        """
        Set the Class pin on a Get Local Player Subsystem node
        and rebuild its output pins with the correct type.

        subsystem_class_name examples:
            EnhancedInputLocalPlayerSubsystem
            EnhancedInputWorldSubsystem

        Call immediately after spawning a Get Local Player Subsystem node,
        before trying to connect its ReturnValue to anything.
        """
        return await self._send_command(
            f"SET_SUBSYSTEM_CLASS|{bp_path}|{node_guid}|{subsystem_class_name}")

    # ------------------------------------------------------------------
    # Function graphs, node positioning, level actor placement (v1.7)
    # ------------------------------------------------------------------

    async def create_function(self, bp_path: str, function_name: str) -> dict | None:
        """
        Create a new custom function graph in a Blueprint, with an
        auto-generated FunctionEntry node.

        result["payload"] is the actual function name used (may differ if
        uniquified) on success. Always use result["payload"] to get the
        real name for SPAWN_NODE_IN_GRAPH's graph_name argument.
        """
        return await self._send_command(f"CREATE_FUNCTION|{bp_path}|{function_name}")

    async def spawn_node_in_graph(self, bp_path: str, graph_name: str, node_class: str,
                                  comment: str, x: int, y: int) -> dict | None:
        """
        Spawn a graph node in a named graph instead of always EventGraph.

        graph_name: "EventGraph" for the main event graph, or the exact name
                    of a function graph (e.g. the value returned by
                    create_function(), or "UserConstructionScript").
        node_class: partial or full UClass name, e.g. "K2Node_CallFunction"
        Returns payload containing the new node's GUID.
        """
        return await self._send_command(
            f"SPAWN_NODE_IN_GRAPH|{bp_path}|{graph_name}|{node_class}|{comment}|{x}|{y}")

    async def set_node_position(self, bp_path: str, node_guid: str, x: int, y: int) -> dict | None:
        """
        Move a node to specific graph coordinates. Searches EventGraph and
        every function graph (including ones created via create_function())
        for node_guid, so this works regardless of which graph the node is in.
        """
        return await self._send_command(f"SET_NODE_POSITION|{bp_path}|{node_guid}|{x}|{y}")

    async def create_input_action(self, asset_path: str, value_type: str) -> dict | None:
        """
        Create a new UInputAction asset at asset_path.

        value_type: "bool", "float" (alias for axis1d), "axis1d", "axis2d", "axis3d"
        result["payload"] is the canonical asset path ("PackagePath.AssetName")
        on success.
        """
        return await self._send_command(f"CREATE_INPUT_ACTION|{asset_path}|{value_type}")

    async def spawn_actor_in_level(self, bp_path: str, x: float, y: float, z: float,
                                   rot_yaw: float) -> dict | None:
        """
        Place a Blueprint actor instance in the current editor level (not
        the PIE game world).

        result["payload"] is the spawned actor's auto-assigned label on
        success — use it with set_actor_transform() / delete_level_actor().
        """
        return await self._send_command(
            f"SPAWN_ACTOR_IN_LEVEL|{bp_path}|{x}|{y}|{z}|{rot_yaw}")

    async def list_level_actors(self, filter_str: str = "") -> list[dict]:
        """
        List all actors in the current editor level.
        filter_str is an optional case-insensitive substring match on class
        name or actor label.

        Returns a list of dicts: { label, class_name, x, y, z }
        """
        result = await self._send_command(f"LIST_LEVEL_ACTORS|{filter_str}")
        if not result or not result.get("success") or not result.get("payload"):
            return []

        actors = []
        for entry in result["payload"].split("|"):
            parts = entry.split("~")
            if len(parts) == 5:
                actors.append({
                    "label":      parts[0],
                    "class_name": parts[1],
                    "x": float(parts[2]), "y": float(parts[3]), "z": float(parts[4]),
                })
        return actors

    async def set_actor_transform(self, actor_label: str,
                                  x: float, y: float, z: float,
                                  pitch: float, yaw: float, roll: float,
                                  sx: float = 1.0, sy: float = 1.0, sz: float = 1.0) -> dict | None:
        """
        Move, rotate and scale a level actor by its label.
        actor_label: exact label as shown in LIST_LEVEL_ACTORS / the World Outliner.
        """
        return await self._send_command(
            f"SET_ACTOR_TRANSFORM|{actor_label}|{x}|{y}|{z}|{pitch}|{yaw}|{roll}|{sx}|{sy}|{sz}")

    async def delete_level_actor(self, actor_label: str) -> dict | None:
        """Remove an actor from the current editor level by its label."""
        return await self._send_command(f"DELETE_LEVEL_ACTOR|{actor_label}")

    # ------------------------------------------------------------------
    # Discovery / query
    # ------------------------------------------------------------------

    async def list_nodes(self, bp_path: str) -> list[dict]:
        """
        List all nodes in a Blueprint's EventGraph.
        Returns a list of dicts: { guid, title, comment, class_name }

        This is the foundation of the scan-first workflow — call this
        before any manipulation to get ground truth on what exists.
        """
        result = await self._send_command(f"LIST_NODES|{bp_path}")
        if not result or not result.get("success") or not result.get("payload"):
            return []

        nodes = []
        for entry in result["payload"].split("|"):
            parts = entry.split("~", 3)
            if len(parts) == 4:
                nodes.append({
                    "guid":       parts[0],
                    "title":      parts[1],
                    "comment":    parts[2],
                    "class_name": parts[3],
                })
        return nodes

    async def find_node_class(self, partial_name: str) -> dict | None:
        """
        Search all loaded UClasses derived from UEdGraphNode whose name
        contains partial_name. Returns comma-separated full class paths.
        """
        return await self._send_command(f"FIND_NODE_CLASS|{partial_name}")

    async def list_assets(self, filter_str: str = "") -> dict | None:
        """
        List Blueprint assets in the Asset Registry, optionally filtered
        by a substring match on the asset path.
        """
        return await self._send_command(f"LIST_ASSETS|{filter_str}")

    # ------------------------------------------------------------------
    # UMG widget + Material graph commands (v1.8)
    # ------------------------------------------------------------------

    async def create_widget_blueprint(self, asset_path: str) -> dict | None:
        """
        Create a new UMG Widget Blueprint (UUserWidget parent) at asset_path,
        with an empty Canvas Panel root ready for add_widget_element().

        result["payload"] is the canonical asset path on success.
        """
        return await self._send_command(f"CREATE_WIDGET_BLUEPRINT|{asset_path}")

    async def add_widget_element(self, widget_bp_path: str, element_type: str,
                                 name: str, x: int, y: int, w: int, h: int) -> dict | None:
        """
        Add a UI element to a Widget Blueprint's root canvas panel.

        element_type: Button, Text, Image, ProgressBar, VerticalBox,
                      HorizontalBox, Canvas, Overlay, Border, Slider, EditableText
        x, y: position on the canvas. w, h: size.
        name: widget name — must be unique within the Widget Blueprint, and
              is used by set_widget_text() to find this element later.
        """
        return await self._send_command(
            f"ADD_WIDGET_ELEMENT|{widget_bp_path}|{element_type}|{name}|{x}|{y}|{w}|{h}")

    async def set_widget_text(self, widget_bp_path: str, element_name: str, text: str) -> dict | None:
        """
        Set the default text on a Text/EditableText widget element.
        element_name: the name passed to add_widget_element().
        """
        return await self._send_command(f"SET_WIDGET_TEXT|{widget_bp_path}|{element_name}|{text}")

    async def create_material(self, asset_path: str, blend_mode: str) -> dict | None:
        """
        Create a new Material asset at asset_path.
        blend_mode: Opaque, Translucent, Masked, Additive
        result["payload"] is the canonical asset path on success.
        """
        return await self._send_command(f"CREATE_MATERIAL|{asset_path}|{blend_mode}")

    async def add_material_node(self, material_path: str, node_type: str, x: int, y: int) -> dict | None:
        """
        Add an expression node to a Material's graph.

        node_type: Multiply, Add, Lerp, Texture, Constant, Constant3,
                   Param, Param3, Fresnel, Time

        result["payload"] is the new node's index — use this with
        connect_material_pins() / set_material_result() to address the node.
        """
        return await self._send_command(f"ADD_MATERIAL_NODE|{material_path}|{node_type}|{x}|{y}")

    async def connect_material_pins(self, material_path: str, node_index_a: int, output_pin: str,
                                    node_index_b: int, input_pin: str) -> dict | None:
        """
        Connect an output pin of the expression at node_index_a to an input
        pin of the expression at node_index_b.

        output_pin / input_pin: pin name, or "_" to use the first pin.
        Use "_" rather than "" — the wire protocol is pipe-delimited and
        drops empty segments, which would otherwise misalign the command.
        """
        return await self._send_command(
            f"CONNECT_MATERIAL_PINS|{material_path}|{node_index_a}|{output_pin}|{node_index_b}|{input_pin}")

    async def set_material_result(self, material_path: str, channel: str,
                                  node_index: int, output_pin: str) -> dict | None:
        """
        Connect an expression node's output to a final material property.

        channel: BaseColor, Metallic, Roughness, Normal, Emissive, Opacity,
                 OpacityMask, WorldPositionOffset
        output_pin: pin name, or "_" to use the first pin.
        """
        return await self._send_command(
            f"SET_MATERIAL_RESULT|{material_path}|{channel}|{node_index}|{output_pin}")

    async def compile_material(self, material_path: str) -> list[dict]:
        """
        Force a material recompile and return compile diagnostics.

        Returns a list of dicts: [{"type": "ERROR", "message": str}, ...]
        Empty list means the compile was clean.
        """
        result = await self._send_command(f"COMPILE_MATERIAL|{material_path}")
        if not result or not result.get("success") or not result.get("payload"):
            return []
        if result["payload"] == "CLEAN":
            return []
        issues = []
        for entry in result["payload"].split("|"):
            if ":" not in entry:
                continue
            kind, message = entry.split(":", 1)
            issues.append({"type": kind, "message": message})
        return issues

    async def close_material(self, material_path: str) -> dict | None:
        """
        Close the Material Editor for this asset. Call this before mutating
        a material's expression graph if it might be open for editing.
        """
        return await self._send_command(f"CLOSE_MATERIAL|{material_path}")

    # ------------------------------------------------------------------
    # Blueprint completeness — enums, structs, function libraries (v1.10)
    # ------------------------------------------------------------------

    async def create_enum(self, asset_path: str, enumerator_names: list[str]) -> dict | None:
        """
        Create a new UUserDefinedEnum asset with the given enumerator
        display names, in order.

        result["payload"] is the canonical asset path ("PackagePath.AssetName")
        on success.
        """
        names_str = ",".join(enumerator_names)
        return await self._send_command(f"CREATE_ENUM|{asset_path}|{names_str}")

    async def create_struct(self, asset_path: str) -> dict | None:
        """
        Create a new UUserDefinedStruct asset. The engine itself seeds one
        default bool member — this is intentional (UUserDefinedStruct assumes
        at least one variable exists) — use add_struct_member() to add real
        fields afterward.

        result["payload"] is the canonical asset path ("PackagePath.AssetName")
        on success.
        """
        return await self._send_command(f"CREATE_STRUCT|{asset_path}")

    async def add_struct_member(self, struct_path: str, member_name: str, member_type: str) -> dict | None:
        """
        Add a member variable to an existing UUserDefinedStruct.

        member_type: same names as spawn_variable()/add_variable() — bool,
        int32, float, FString, FVector, object:ClassName, etc.
        """
        return await self._send_command(f"ADD_STRUCT_MEMBER|{struct_path}|{member_name}|{member_type}")

    async def create_function_library(self, asset_path: str) -> dict | None:
        """
        Create a new Blueprint Function Library asset (ParentClass =
        BlueprintFunctionLibrary, BlueprintType = BPTYPE_FunctionLibrary).
        Functions added to it via create_function() are static and callable
        from any Blueprint without a target pin.

        result["payload"] is the canonical asset path ("PackagePath.AssetName")
        on success.
        """
        return await self._send_command(f"CREATE_FUNCTION_LIBRARY|{asset_path}")

    # ------------------------------------------------------------------
    # Blueprint completeness Phase 2 — local variables, variable metadata,
    # event dispatchers (v1.11)
    # ------------------------------------------------------------------

    async def add_local_variable(self, bp_path: str, function_graph_name: str,
                                  var_name: str, var_type: str, default_value: str = "") -> dict | None:
        """
        Add a variable scoped to a single function graph (not the class).

        function_graph_name: "EventGraph" or any function graph name created
        via create_function().
        var_type: same names as ResolveTypeString (bool, float, FString,
        FVector, object:ClassName, etc).
        """
        return await self._send_command(
            f"ADD_LOCAL_VARIABLE|{bp_path}|{function_graph_name}|{var_name}|{var_type}|{default_value}")

    async def set_variable_metadata(self, bp_path: str, var_name: str,
                                     meta_key: str, meta_value: str) -> dict | None:
        """
        Set a single metadata key/value pair on a class (member) variable —
        e.g. Category, ToolTip, ExposeOnSpawn, ClampMin, ClampMax.

        Class variables ONLY. Local (function-scoped) variable metadata is
        silently dropped at compile time by the engine itself (Epic bug
        UE-239861, open) — this deliberately does not support local variables.
        """
        return await self._send_command(f"SET_VARIABLE_METADATA|{bp_path}|{var_name}|{meta_key}|{meta_value}")

    async def create_event_dispatcher(self, bp_path: str, dispatcher_name: str,
                                       params: list[tuple[str, str]] | None = None) -> dict | None:
        """
        Create a new Event Dispatcher (multicast delegate) member variable
        plus its signature graph.

        params: list of (type, name) tuples, e.g. [("int32", "Amount"),
        ("FString", "Reason")]. Types use the same names as ResolveTypeString.
        """
        params_str = ",".join(f"{t}:{n}" for t, n in (params or []))
        return await self._send_command(f"CREATE_EVENT_DISPATCHER|{bp_path}|{dispatcher_name}|{params_str}")
