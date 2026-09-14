// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Stores the LLM provider API key outside of project config / version
// control. Previously UGraphBridgeSettings::ApiKey was a `defaultconfig`
// UPROPERTY, which persisted it in plaintext to
// <Project>/Config/DefaultEditorPerProjectUserSettings.ini -- a file inside
// the user's repository.
//
// Storage order:
//   1. OS credential store (Windows Credential Manager / macOS Keychain).
//   2. GRAPHBRIDGE_API_KEY environment variable, if the platform store is
//      unavailable or empty (e.g. Linux, or a platform store write failed).
//
// SetApiKey always writes to the OS store when one is available for the
// current platform; it never writes environment variables (there is no
// portable, persistent way to do that from an editor plugin).
class GRAPHBRIDGEV2_API FGraphBridgeCredentialStore
{
public:
    static FString GetApiKey();
    static bool SetApiKey(const FString& ApiKey);
    static bool ClearApiKey();

    // One-time upgrade path: if an old plaintext ApiKey value is still
    // sitting in DefaultEditorPerProjectUserSettings.ini's [GraphBridge]
    // section (from before this class existed), migrate it into the OS
    // credential store, blank the ini value, and show a one-time editor
    // warning that the key may be present in the user's version control
    // history and should be rotated. Safe to call unconditionally at
    // startup -- it is a no-op once the ini value has been cleared.
    static void MigrateFromIniIfNeeded();

private:
    static bool PlatformGet(FString& OutApiKey);
    static bool PlatformSet(const FString& ApiKey);
    static bool PlatformClear();
};
