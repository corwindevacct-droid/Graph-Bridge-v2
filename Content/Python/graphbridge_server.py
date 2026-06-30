# -*- coding: utf-8 -*-
# GraphBridge AI - graphbridge_server.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
server.py
Interactive debug shell — type pipe-delimited commands and send them directly
to the Unreal GraphBridge WebSocket server.

Usage:
    python server.py
    > COMPILE|/Game/BP_FlyingCharacter.BP_FlyingCharacter
    > LIST_ASSETS|BP_Flying
    > quit
"""

import asyncio
import websockets

UE_URI = "ws://127.0.0.1:8080"


async def session():
    print(f"Connecting to GraphBridge at {UE_URI} ...")
    try:
        async with websockets.connect(UE_URI) as ws:
            print("Connected. Type a command and press Enter. Type 'quit' to exit.\n")
            # Use get_running_loop() — get_event_loop() is deprecated in Python 3.10+
            loop = asyncio.get_running_loop()
            while True:
                command = await loop.run_in_executor(None, input, "> ")
                command = command.strip()
                if command.lower() == "quit":
                    break
                if not command:
                    continue
                await ws.send(command)
                try:
                    response = await asyncio.wait_for(ws.recv(), timeout=3.0)
                    print(f"  UE: {response}")
                except asyncio.TimeoutError:
                    print("  (no response within 3s)")

    except ConnectionRefusedError:
        print(f"\nERROR: Connection refused at {UE_URI}")
        print("Make sure the Unreal Editor is open with the GraphBridgev2 plugin loaded.")
    except Exception as e:
        print(f"\nERROR: {e}")


if __name__ == "__main__":
    asyncio.run(session())
