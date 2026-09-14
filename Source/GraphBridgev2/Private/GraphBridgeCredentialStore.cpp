// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeCredentialStore.h"
#include "GraphBridgeSettings.h"
#include "GraphBridgev2.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformMisc.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
    // TargetName for Windows Credential Manager. ":" separates a namespace
    // from the credential name by convention (matches how e.g. git stores
    // multiple credentials under one generic-credential target scheme).
    const TCHAR* GCredTargetName = TEXT("GraphBridgeAI:ApiKey");
}

bool FGraphBridgeCredentialStore::PlatformGet(FString& OutApiKey)
{
    PCREDENTIALW Cred = nullptr;
    if (!CredReadW(GCredTargetName, CRED_TYPE_GENERIC, 0, &Cred) || !Cred)
    {
        return false;
    }
    // Stored as null-terminated UTF-8 by PlatformSet below -- self-consistent
    // round trip, avoids any TCHAR-width assumptions on the stored bytes.
    OutApiKey = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Cred->CredentialBlob)));
    CredFree(Cred);
    return !OutApiKey.IsEmpty();
}

bool FGraphBridgeCredentialStore::PlatformSet(const FString& ApiKey)
{
    FTCHARToUTF8 Utf8(*ApiKey);

    CREDENTIALW Cred = {};
    Cred.Type = CRED_TYPE_GENERIC;
    Cred.TargetName = const_cast<LPWSTR>(GCredTargetName);
    Cred.CredentialBlobSize = static_cast<DWORD>(Utf8.Length() + 1); // include trailing NUL
    Cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ANSICHAR*>(Utf8.Get()));
    Cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    Cred.UserName = const_cast<LPWSTR>(TEXT("GraphBridgeAI"));

    return CredWriteW(&Cred, 0) != 0;
}

bool FGraphBridgeCredentialStore::PlatformClear()
{
    // Windows BOOL, not UE's own type -- compare against 0 rather than
    // TRUE/FALSE, which are only defined within the Allow/HideWindowsPlatformTypes
    // guard around the <wincred.h> include above, not down here.
    const bool bDeleted = CredDeleteW(GCredTargetName, CRED_TYPE_GENERIC, 0) != 0;
    // ERROR_NOT_FOUND means there was nothing to clear -- not a failure.
    return bDeleted || GetLastError() == ERROR_NOT_FOUND;
}

#elif PLATFORM_MAC && GRAPHBRIDGE_ENABLE_MAC_KEYCHAIN

// Real Keychain Services implementation, written against the stable,
// long-standing generic-password API (SecItemAdd/CopyMatching/Update/Delete).
// Disabled by GRAPHBRIDGE_ENABLE_MAC_KEYCHAIN (see GraphBridgev2.Build.cs) for
// v2.0.1: it was never compiled in this dev environment (no Mac toolchain
// available), and "falls back to the env var on failure" is a RUNTIME
// property that does not help if this file fails to compile at all --
// that would mean no build for Mac customers, not a degraded one. Build and
// smoke-test GetApiKey()/SetApiKey()/ClearApiKey() on a real Mac, then flip
// the flag on, before re-enabling this for a later release. Until then, Mac
// falls through to PlatformGet/Set/Clear's env-var-only implementation below.
namespace
{
    CFMutableDictionaryRef MakeQuery()
    {
        CFMutableDictionaryRef Query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(Query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(Query, kSecAttrService, CFSTR("GraphBridgeAI"));
        CFDictionarySetValue(Query, kSecAttrAccount, CFSTR("ApiKey"));
        return Query;
    }
}

bool FGraphBridgeCredentialStore::PlatformGet(FString& OutApiKey)
{
    CFMutableDictionaryRef Query = MakeQuery();
    CFDictionarySetValue(Query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(Query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef Result = nullptr;
    const OSStatus Status = SecItemCopyMatching(Query, &Result);
    CFRelease(Query);

    if (Status != errSecSuccess || !Result)
    {
        return false;
    }

    CFDataRef Data = static_cast<CFDataRef>(Result);
    // Stored as null-terminated UTF-8 by PlatformSet below.
    OutApiKey = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(CFDataGetBytePtr(Data))));
    CFRelease(Result);
    return !OutApiKey.IsEmpty();
}

bool FGraphBridgeCredentialStore::PlatformSet(const FString& ApiKey)
{
    FTCHARToUTF8 Utf8(*ApiKey);
    CFDataRef ValueData = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(Utf8.Get()), Utf8.Length() + 1);

    CFMutableDictionaryRef Query = MakeQuery();
    CFMutableDictionaryRef Attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(Attributes, kSecValueData, ValueData);

    OSStatus Status = SecItemUpdate(Query, Attributes);
    if (Status == errSecItemNotFound)
    {
        CFDictionarySetValue(Query, kSecValueData, ValueData);
        Status = SecItemAdd(Query, nullptr);
    }

    CFRelease(ValueData);
    CFRelease(Attributes);
    CFRelease(Query);
    return Status == errSecSuccess;
}

bool FGraphBridgeCredentialStore::PlatformClear()
{
    CFMutableDictionaryRef Query = MakeQuery();
    const OSStatus Status = SecItemDelete(Query);
    CFRelease(Query);
    return Status == errSecSuccess || Status == errSecItemNotFound;
}

#else // other platforms -- no first-party OS credential store wired up yet

bool FGraphBridgeCredentialStore::PlatformGet(FString&) { return false; }
bool FGraphBridgeCredentialStore::PlatformSet(const FString&) { return false; }
bool FGraphBridgeCredentialStore::PlatformClear() { return false; }

#endif

FString FGraphBridgeCredentialStore::GetApiKey()
{
    FString Key;
    if (PlatformGet(Key) && !Key.IsEmpty())
    {
        return Key;
    }
    return FPlatformMisc::GetEnvironmentVariable(TEXT("GRAPHBRIDGE_API_KEY"));
}

bool FGraphBridgeCredentialStore::SetApiKey(const FString& ApiKey)
{
    return PlatformSet(ApiKey);
}

bool FGraphBridgeCredentialStore::ClearApiKey()
{
    return PlatformClear();
}

void FGraphBridgeCredentialStore::MigrateFromIniIfNeeded()
{
    UGraphBridgeSettings* Settings = UGraphBridgeSettings::Get();
    const FString IniPath = Settings->GetDefaultConfigFilename();

    FString OldApiKey;
    const bool bFound = GConfig->GetString(TEXT("GraphBridge"), TEXT("ApiKey"), OldApiKey, IniPath);
    if (!bFound || OldApiKey.IsEmpty())
    {
        return;
    }

    SetApiKey(OldApiKey);

    // Blank (not remove -- keeps the ini diff minimal) the plaintext value
    // now that it lives in OS credential storage.
    GConfig->SetString(TEXT("GraphBridge"), TEXT("ApiKey"), TEXT(""), IniPath);
    GConfig->Flush(false, IniPath);

    UE_LOG(LogGraphBridge, Warning,
        TEXT("GraphBridge: migrated your API key out of %s into OS credential storage and ")
        TEXT("cleared the plaintext value. If this project's Config folder was ever committed ")
        TEXT("to version control, that key may still be present in your git history -- rotate ")
        TEXT("it with your provider (Anthropic/OpenAI) to be safe."), *IniPath);

#if WITH_EDITOR
    // A one-time toast in addition to the log line -- this is security-
    // relevant and easy to miss in the Output Log.
    FNotificationInfo Info(NSLOCTEXT("GraphBridge", "ApiKeyMigrated",
        "GraphBridge AI: your API key was moved out of project config into OS credential "
        "storage. If this project was ever committed to version control, rotate that key -- "
        "it may still be present in your git history."));
    Info.ExpireDuration = 15.0f;
    Info.bUseSuccessFailIcons = false;
    FSlateNotificationManager::Get().AddNotification(Info);
#endif
}
