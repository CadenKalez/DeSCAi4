// MacPreferences (0x100641b5c..0x100642135): the game's user defaults, stored as strings under the application's
// CFPreferences domain. Everything is string-typed: integers and floats are parsed with atoi / atof, booleans are the
// integer != 0, and rectangles are four keys "<name>.origin.x/.origin.y/.size.width/.size.height".
// The store behind it is a seam (the real one is CFPreferences; tests install a memory one).
#pragma once
#include <string>

namespace sgl {

class IPreferencesStore {
public:
    virtual ~IPreferencesStore() = default;
    virtual bool CopyString(const char* key, std::string& out) = 0;   // false when the key is not set
    virtual void SetString(const char* key, const char* value) = 0;
    virtual void Synchronize() = 0;
};
// The installed store (the CFPreferences-backed one on Apple platforms, an in-memory one elsewhere).
IPreferencesStore* PreferencesStore();
void SetPreferencesStore(IPreferencesStore* store);
// The store used when none is installed: CFPreferences on Apple platforms (MacPreferencesCF.cpp).
IPreferencesStore* DefaultPreferencesStore();

struct CGRect { double x, y, width, height; };   // origin then size, doubles as CoreGraphics

struct MacPreferences {
    static void GetString(const char* key, char* out, int capacity, const char* defaultValue);   // @ 0x100641b5c
    static void PutString(const char* key, const char* value);                                    // @ 0x100641bf6
    static int GetInteger(const char* key, int defaultValue);                                     // @ 0x100641c53
    static void PutInteger(const char* key, int value);                                           // @ 0x100641cbe
    static float GetFloat(const char* key, float defaultValue);                                   // @ 0x100641d25
    static bool GetBoolean(const char* key, bool defaultValue);                                   // @ 0x100641da1
    static void PutBoolean(const char* key, bool value);                                          // @ 0x100641db5
    static void GetRect(const char* key, CGRect& out);                                            // @ 0x100641e1c
    static void PutRect(const char* key, const CGRect& rect);                                     // @ 0x100641f7c
    static void Synchronize();                                                                    // @ 0x100642135
};

}  // namespace sgl
