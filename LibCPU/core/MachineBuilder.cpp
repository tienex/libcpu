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
                Md.pNode      = &Node;
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

// Read big-endian cell Index (a 4-byte word) of property pProp; returns false past the end.
static bool
ReadCell (DT_PROP CONST *pProp, size_t Index, UINT32 *pValue)
{
    size_t Off = Index * 4;
    if (pProp == nullptr || Off + 4 > pProp->Value.size ()) { return false; }
    UINT8 CONST *p = &pProp->Value[Off];
    *pValue = ((UINT32) p[0] << 24) | ((UINT32) p[1] << 16) | ((UINT32) p[2] << 8) | (UINT32) p[3];
    return true;
}

void
MachineBuilder::ResolveSignalLinks ()
{
    // Map every assigned phandle to the matched component carrying it (its "phandle" cell).
    std::vector<std::pair<UINT32, IDevice *>> ByPhandle;
    for (MATCHED_DEVICE CONST &D : m_Devices) {
        std::string Key = "phandle";
        UINT32 Ph = 0;
        if (D.pNode != nullptr && ReadCell (D.pNode->FindProp (Key), 0, &Ph)) {
            ByPhandle.push_back (std::make_pair (Ph, D.pDevice));
        }
    }

    // Connect each sink's declared "signals = <&source LINE>, ..." edges to the named source.
    for (MATCHED_DEVICE CONST &D : m_Devices) {
        std::string Key = "signals";
        DT_PROP CONST *pSig = (D.pNode != nullptr) ? D.pNode->FindProp (Key) : nullptr;
        if (pSig == nullptr) { continue; }
        ISignalSink *pSink = nullptr;
        D.pDevice->QueryInterface (IID_ISignalSink, (VOID **) &pSink);
        if (pSink == nullptr) { continue; }
        size_t Cells = pSig->Value.size () / 4;
        for (size_t I = 0; I + 1 < Cells; I += 2) {
            UINT32 Ph = 0, Line = 0;
            ReadCell (pSig, I, &Ph);
            ReadCell (pSig, I + 1, &Line);
            IDevice *pSrcDev = nullptr;
            for (std::pair<UINT32, IDevice *> CONST &E : ByPhandle) {
                if (E.first == Ph) { pSrcDev = E.second; break; }
            }
            if (pSrcDev == nullptr) { continue; }
            ISignalSource *pSrc = nullptr;
            pSrcDev->QueryInterface (IID_ISignalSource, (VOID **) &pSrc);
            if (pSrc != nullptr) { pSrc->ConnectSink (pSink, Line); pSrc->Release (); }
        }
        pSink->Release ();
    }

    // Attach each storage controller's declared "disks = <&drive0>, <&drive1>, ..." media. Every
    // phandle names a generic block medium; the controller receives it as a unit (drive 0, 1, ...),
    // independent of the controller's bus (ST-506, IDE, SCSI, ...).
    for (MATCHED_DEVICE CONST &D : m_Devices) {
        std::string Key = "disks";
        DT_PROP CONST *pDisks = (D.pNode != nullptr) ? D.pNode->FindProp (Key) : nullptr;
        if (pDisks == nullptr) { continue; }
        IStorageController *pCtl = nullptr;
        D.pDevice->QueryInterface (IID_IStorageController, (VOID **) &pCtl);
        if (pCtl == nullptr) { continue; }
        size_t Cells = pDisks->Value.size () / 4;
        UINT32 Unit  = 0;
        for (size_t I = 0; I < Cells; I++) {
            UINT32 Ph = 0;
            ReadCell (pDisks, I, &Ph);
            IDevice *pMedDev = nullptr;
            for (std::pair<UINT32, IDevice *> CONST &E : ByPhandle) {
                if (E.first == Ph) { pMedDev = E.second; break; }
            }
            if (pMedDev == nullptr) { continue; }
            IBlockMedium *pMed = nullptr;
            pMedDev->QueryInterface (IID_IBlockMedium, (VOID **) &pMed);
            if (pMed != nullptr) { pCtl->AttachMedium (Unit++, pMed); pMed->Release (); }
        }
        pCtl->Release ();
    }
}

bool
MachineBuilder::Build (DeviceTree &Tree, std::string *pError)
{
    std::string Root = "/";
    if (!WalkNode (Tree.Root, Root, pError)) { return false; }
    ResolveSignalLinks ();
    return true;
}

} // namespace LibCPU
