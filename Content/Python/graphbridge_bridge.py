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
