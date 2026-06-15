/** @file  MachineBuilder -- match a device tree against component bundles (see header). */

#include "MachineBuilder.h"
#include "DeviceTree.h"
#include "DeviceNode.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <dlfcn.h>
#endif

namespace LibCPU {

typedef IDevice *(*DeviceEntryFn) (void);

// Read the <string> entries of the array under <key>Key</key> in a (generated) Info.plist. The
// plists are machine-written and simple, so a textual scan is enough -- and it keeps matching
// cross-platform and free of any code load, which is the whole point of plist matching.
static std::vector<std::string>
ReadPlistStringArray (std::string CONST &PlistPath, CHAR8 CONST *pKey)
{
    std::vector<std::string> Out;
    std::FILE *pF = std::fopen (PlistPath.c_str (), "rb");
    if (pF == nullptr) { return Out; }
    std::fseek (pF, 0, SEEK_END);
    long Size = std::ftell (pF);
    std::fseek (pF, 0, SEEK_SET);
    std::string Text (Size > 0 ? (size_t) Size : 0, '\0');
    if (!Text.empty ()) {
        if (std::fread (&Text[0], 1, Text.size (), pF) != Text.size ()) { Text.clear (); }
    }
    std::fclose (pF);

    std::string KeyTag = std::string ("<key>") + pKey + "</key>";
    size_t K = Text.find (KeyTag);
    if (K == std::string::npos) { return Out; }
    size_t A = Text.find ("<array>", K);
    if (A == std::string::npos) { return Out; }
    size_t End = Text.find ("</array>", A);
    if (End == std::string::npos) { End = Text.size (); }
    size_t P = A;
    for (;;) {
        size_t S = Text.find ("<string>", P);
        if (S == std::string::npos || S >= End) { break; }
        S += 8;
        size_t E = Text.find ("</string>", S);
        if (E == std::string::npos || E > End) { break; }
        Out.push_back (Text.substr (S, E - S));
        P = E + 9;
    }
    return Out;
}

// Load a matched bundle and create its component. macOS goes through CFBundle (the bundle stays
// mapped for the process lifetime, as for backend bundles); elsewhere through dlopen.
static IDevice *
LoadDeviceBundle (std::string CONST &BundlePath)
{
#if defined(__APPLE__)
    CFStringRef PathStr = CFStringCreateWithCString (nullptr, BundlePath.c_str (), kCFStringEncodingUTF8);
    if (PathStr == nullptr) { return nullptr; }
    CFURLRef Url = CFURLCreateWithFileSystemPath (nullptr, PathStr, kCFURLPOSIXPathStyle, true);
    CFRelease (PathStr);
    if (Url == nullptr) { return nullptr; }
    CFBundleRef Bundle = CFBundleCreate (nullptr, Url);
    CFRelease (Url);
    if (Bundle == nullptr) { return nullptr; }
    CFStringRef FnName = CFStringCreateWithCString (nullptr, LIBCPU_MODULE_DEVICE_ENTRY_NAME, kCFStringEncodingUTF8);
    DeviceEntryFn pEntry = (DeviceEntryFn) CFBundleGetFunctionPointerForName (Bundle, FnName);
    CFRelease (FnName);
    if (pEntry == nullptr) { CFRelease (Bundle); return nullptr; }
    return pEntry ();                                        // Bundle intentionally left mapped
#else
    void *pHandle = dlopen (BundlePath.c_str (), RTLD_NOW | RTLD_LOCAL);
    if (pHandle == nullptr) { return nullptr; }
    DeviceEntryFn pEntry = (DeviceEntryFn) dlsym (pHandle, LIBCPU_MODULE_DEVICE_ENTRY_NAME);
    if (pEntry == nullptr) { dlclose (pHandle); return nullptr; }
    return pEntry ();
#endif
}

MachineBuilder::~MachineBuilder ()
{
    for (MATCHED_DEVICE &D : m_Devices) {
        if (D.pDevice != nullptr) { D.pDevice->Release (); }
    }
}

void
MachineBuilder::AddBundleDirectory (CHAR8 CONST *pDir)
{
    DIR *pD = opendir (pDir);
    if (pD == nullptr) { return; }
    struct dirent *pE;
    while ((pE = readdir (pD)) != nullptr) {
        std::string Name = pE->d_name;
        if (Name.size () < 8 || Name.compare (Name.size () - 7, 7, ".device") != 0) { continue; }
        std::string Bundle = std::string (pDir) + "/" + Name;
        std::string Plist  = Bundle + "/Contents/Info.plist";
        std::vector<std::string> Matches = ReadPlistStringArray (Plist, LIBCPU_DEVICE_MATCH_KEY);
        for (std::string CONST &M : Matches) {
            m_Match.push_back (std::make_pair (M, Bundle));
        }
    }
    closedir (pD);
}

// The "compatible" property is a list of NUL-terminated strings; split it.
static std::vector<std::string>
CompatibleList (DtNode &Node)
{
    std::vector<std::string> Out;
    std::string Key = "compatible";
    DT_PROP *pProp = Node.FindProp (Key);
    if (pProp == nullptr) { return Out; }
    size_t I = 0;
    while (I < pProp->Value.size ()) {
        std::string S ((CHAR8 CONST *) &pProp->Value[I]);
        if (!S.empty ()) { Out.push_back (S); }
        I += S.size () + 1;
    }
    return Out;
}

// The base file name of a bundle path ("/x/y/i8259.device" -> "i8259").
static std::string
BundleDisplayName (std::string CONST &Path)
{
    size_t Slash = Path.find_last_of ('/');
    std::string Base = Slash == std::string::npos ? Path : Path.substr (Slash + 1);
    size_t Dot = Base.rfind (".device");
    return Dot == std::string::npos ? Base : Base.substr (0, Dot);
}

bool
MachineBuilder::WalkNode (DtNode &Node, std::string CONST &Path, std::string *pError)
{
    std::vector<std::string> Compat = CompatibleList (Node);
    if (!Compat.empty ()) {
        bool Bound = false;
        for (std::string CONST &C : Compat) {
            for (std::pair<std::string, std::string> CONST &M : m_Match) {
                if (M.first != C) { continue; }
                IDevice *pDev = LoadDeviceBundle (M.second);
                if (pDev == nullptr) {
                    *pError = "failed to load device bundle " + M.second;
                    return false;
                }
                ComPtr<IDeviceNode> NodeCom (MakeDeviceNode (&Node));
                pDev->Configure (NodeCom);
                MATCHED_DEVICE Md;
                Md.NodePath   = Path;
                Md.NodeName   = Node.Name;
                Md.BundleName = BundleDisplayName (M.second);
                Md.MatchedOn  = C;
                Md.pDevice    = pDev;
                m_Devices.push_back (std::move (Md));
                Bound = true;
                break;
            }
            if (Bound) { break; }
        }
        if (!Bound) { m_Unmatched.push_back (Path + ": " + Compat.front ()); }
    }
    for (DtNode &Child : Node.Children) {
        std::string ChildPath = (Path == "/") ? ("/" + Child.Name) : (Path + "/" + Child.Name);
        if (!WalkNode (Child, ChildPath, pError)) { return false; }
    }
    return true;
}

bool
MachineBuilder::Build (DeviceTree &Tree, std::string *pError)
{
    std::string Root = "/";
    return WalkNode (Tree.Root, Root, pError);
}

} // namespace LibCPU
