Scriptname DLLTweaksVRMCM extends MCM_ConfigBase

String Function GetModName()
    return "DLLTweaksVR"
EndFunction

Function EnsureModName()
    If ModName == ""
        ModName = "DLLTweaksVR"
    EndIf
EndFunction

Event OnConfigInit()
    EnsureModName()
    ReloadSettings()
EndEvent

Event OnGameReload()
    EnsureModName()
    parent.OnGameReload()
    ReloadSettings()
EndEvent

Event OnSettingChange(String a_ID)
    ReloadSettings()
EndEvent

Function ReloadSettings() native
