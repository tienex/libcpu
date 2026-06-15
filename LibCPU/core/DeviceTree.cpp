/** @file  DeviceTree -- model + device-tree format readers/writers (see DeviceTree.h). */

#include "DeviceTree.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <map>

namespace LibCPU {

// ===========================================================================
//  Big-endian cell helpers (device-tree values are big-endian by convention).
// ===========================================================================

static UINT32
Be32 (UINT8 CONST *p)
{
    return ((UINT32) p[0] << 24) | ((UINT32) p[1] << 16) | ((UINT32) p[2] << 8) | (UINT32) p[3];
}

static UINT64
Be64 (UINT8 CONST *p)
{
    return ((UINT64) Be32 (p) << 32) | (UINT64) Be32 (p + 4);
}

static void
PushBe32 (std::vector<UINT8> *pOut, UINT32 V)
{
    pOut->push_back ((UINT8) (V >> 24));
    pOut->push_back ((UINT8) (V >> 16));
    pOut->push_back ((UINT8) (V >> 8));
    pOut->push_back ((UINT8) V);
}

static void
PushBe64 (std::vector<UINT8> *pOut, UINT64 V)
{
    PushBe32 (pOut, (UINT32) (V >> 32));
    PushBe32 (pOut, (UINT32) V);
}

static void
AlignTo (std::vector<UINT8> *pOut, size_t Align)
{
    while (pOut->size () % Align != 0) { pOut->push_back (0); }
}

// ===========================================================================
//  DtNode -- property and child access.
// ===========================================================================

DT_PROP *
DtNode::FindProp (std::string CONST &Name)
{
    for (DT_PROP &P : Props) {
        if (P.Name == Name) { return &P; }
    }
    return nullptr;
}

DT_PROP CONST *
DtNode::FindProp (std::string CONST &Name) CONST
{
    for (DT_PROP CONST &P : Props) {
        if (P.Name == Name) { return &P; }
    }
    return nullptr;
}

void
DtNode::SetProp (std::string CONST &Name, std::vector<UINT8> CONST &Value)
{
    if (DT_PROP *P = FindProp (Name)) {
        P->Value = Value;
        return;
    }
    DT_PROP P;
    P.Name  = Name;
    P.Value = Value;
    Props.push_back (std::move (P));
}

void
DtNode::SetPropString (std::string CONST &Name, std::string CONST &Value)
{
    std::vector<UINT8> Bytes (Value.begin (), Value.end ());
    Bytes.push_back (0);                                     // device-tree strings are NUL-terminated
    SetProp (Name, Bytes);
}

void
DtNode::SetPropU32 (std::string CONST &Name, UINT32 Value)
{
    std::vector<UINT8> Bytes;
    PushBe32 (&Bytes, Value);
    SetProp (Name, Bytes);
}

void
DtNode::SetPropEmpty (std::string CONST &Name)
{
    std::vector<UINT8> Empty;
    SetProp (Name, Empty);
}

DtNode *
DtNode::FindChild (std::string CONST &Name)
{
    for (DtNode &C : Children) {
        if (C.Name == Name) { return &C; }
    }
    return nullptr;
}

DtNode *
DtNode::AddChild (std::string CONST &Name)
{
    if (DtNode *C = FindChild (Name)) { return C; }
    DtNode C;
    C.Name = Name;
    Children.push_back (std::move (C));
    return &Children.back ();
}

// ===========================================================================
//  Flat Device Tree (DTB) -- binary read.
// ===========================================================================

enum {
    FDT_MAGIC      = 0xd00dfeed,
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE   = 2,
    FDT_PROP       = 3,
    FDT_NOP        = 4,
    FDT_END        = 9
};

// Walk the structure block from *pOff, filling pNode's properties and children, until the
// matching FDT_END_NODE. Returns false on any malformed token/overrun.
static bool
ReadFdtNode (UINT8 CONST *pStruct, size_t StructLen, UINT8 CONST *pStrings, size_t StringsLen,
             size_t *pOff, DtNode *pNode)
{
    while (*pOff + 4 <= StructLen) {
        UINT32 Tok = Be32 (pStruct + *pOff);
        *pOff += 4;
        if (Tok == FDT_END_NODE) {
            return true;
        }
        if (Tok == FDT_NOP) {
            continue;
        }
        if (Tok == FDT_PROP) {
            if (*pOff + 8 > StructLen) { return false; }
            UINT32 Len     = Be32 (pStruct + *pOff);
            UINT32 NameOff = Be32 (pStruct + *pOff + 4);
            *pOff += 8;
            if (*pOff + Len > StructLen || NameOff >= StringsLen) { return false; }
            DT_PROP P;
            CHAR8 CONST *pName = (CHAR8 CONST *) pStrings + NameOff;
            P.Name.assign (pName, strnlen (pName, StringsLen - NameOff));
            P.Value.assign (pStruct + *pOff, pStruct + *pOff + Len);
            pNode->Props.push_back (std::move (P));
            *pOff += Len;
            *pOff = (*pOff + 3) & ~(size_t) 3;               // properties pad to 4 bytes
            continue;
        }
        if (Tok == FDT_BEGIN_NODE) {
            CHAR8 CONST *pName = (CHAR8 CONST *) pStruct + *pOff;
            size_t NameLen = strnlen (pName, StructLen - *pOff);
            DtNode Child;
            Child.Name.assign (pName, NameLen);
            *pOff += NameLen + 1;
            *pOff = (*pOff + 3) & ~(size_t) 3;
            if (!ReadFdtNode (pStruct, StructLen, pStrings, StringsLen, pOff, &Child)) { return false; }
            pNode->Children.push_back (std::move (Child));
            continue;
        }
        return false;                                        // unknown token
    }
    return false;                                            // ran out before FDT_END_NODE
}

static bool
ReadFdt (UINT8 CONST *pData, size_t Len, DeviceTree *pTree, std::string *pError)
{
    if (Len < 40 || Be32 (pData) != FDT_MAGIC) {
        *pError = "not a flattened device tree (bad magic)";
        return false;
    }
    UINT32 TotalSize  = Be32 (pData + 4);
    UINT32 OffStruct  = Be32 (pData + 8);
    UINT32 OffStrings = Be32 (pData + 12);
    UINT32 OffRsv     = Be32 (pData + 16);
    pTree->BootCpuId  = Be32 (pData + 28);
    UINT32 SizeStruct = Be32 (pData + 36);
    if (TotalSize > Len || OffStruct + SizeStruct > Len || OffStrings > Len) {
        *pError = "device tree header offsets out of range";
        return false;
    }

    // Memory reservation block: (u64 addr, u64 size) pairs ending at (0, 0).
    for (size_t O = OffRsv; O + 16 <= Len; O += 16) {
        UINT64 Addr = Be64 (pData + O);
        UINT64 Size = Be64 (pData + O + 8);
        if (Addr == 0 && Size == 0) { break; }
        pTree->MemReserve.push_back (std::make_pair (Addr, Size));
    }

    UINT8 CONST *pStruct  = pData + OffStruct;
    UINT8 CONST *pStrings = pData + OffStrings;
    size_t StringsLen = (OffStrings <= Len) ? (Len - OffStrings) : 0;
    size_t Off = 0;
    UINT32 First = Be32 (pStruct);
    if (First != FDT_BEGIN_NODE) {
        *pError = "device tree structure does not begin with a node";
        return false;
    }
    Off += 4;
    CHAR8 CONST *pRootName = (CHAR8 CONST *) pStruct + Off;
    size_t RootLen = strnlen (pRootName, SizeStruct - Off);
    pTree->Root.Name.assign (pRootName, RootLen);            // normally empty
    Off += RootLen + 1;
    Off = (Off + 3) & ~(size_t) 3;
    if (!ReadFdtNode (pStruct, SizeStruct, pStrings, StringsLen, &Off, &pTree->Root)) {
        *pError = "malformed device tree structure block";
        return false;
    }
    return true;
}

// ===========================================================================
//  Flat Device Tree (DTB) -- binary write.
// ===========================================================================

// The strings block: names are pooled and de-duplicated; a name's offset is reused on repeat.
typedef struct _FDT_STRINGS {
    std::vector<UINT8>            Bytes;
    std::map<std::string, UINT32> Offsets;
} FDT_STRINGS;

static UINT32
InternString (FDT_STRINGS *pS, std::string CONST &Name)
{
    auto It = pS->Offsets.find (Name);
    if (It != pS->Offsets.end ()) { return It->second; }
    UINT32 Off = (UINT32) pS->Bytes.size ();
    pS->Offsets[Name] = Off;
    pS->Bytes.insert (pS->Bytes.end (), Name.begin (), Name.end ());
    pS->Bytes.push_back (0);
    return Off;
}

static void
WriteFdtNode (DtNode CONST &Node, std::vector<UINT8> *pStruct, FDT_STRINGS *pStrings)
{
    PushBe32 (pStruct, FDT_BEGIN_NODE);
    pStruct->insert (pStruct->end (), Node.Name.begin (), Node.Name.end ());
    pStruct->push_back (0);
    AlignTo (pStruct, 4);
    for (DT_PROP CONST &P : Node.Props) {
        PushBe32 (pStruct, FDT_PROP);
        PushBe32 (pStruct, (UINT32) P.Value.size ());
        PushBe32 (pStruct, InternString (pStrings, P.Name));
        pStruct->insert (pStruct->end (), P.Value.begin (), P.Value.end ());
        AlignTo (pStruct, 4);
    }
    for (DtNode CONST &C : Node.Children) {
        WriteFdtNode (C, pStruct, pStrings);
    }
    PushBe32 (pStruct, FDT_END_NODE);
}

static void
WriteFdt (DeviceTree CONST &Tree, std::vector<UINT8> *pOut)
{
    std::vector<UINT8> Struct;
    FDT_STRINGS Strings;
    WriteFdtNode (Tree.Root, &Struct, &Strings);
    PushBe32 (&Struct, FDT_END);

    std::vector<UINT8> Rsv;
    for (std::pair<UINT64, UINT64> CONST &R : Tree.MemReserve) {
        PushBe64 (&Rsv, R.first);
        PushBe64 (&Rsv, R.second);
    }
    PushBe64 (&Rsv, 0);                                      // terminating (0, 0)
    PushBe64 (&Rsv, 0);

    UINT32 CONST HeaderSize = 40;
    UINT32 OffRsv     = HeaderSize;                          // header is a multiple of 8, so already aligned
    UINT32 OffStruct  = OffRsv + (UINT32) Rsv.size ();
    OffStruct = (OffStruct + 3) & ~(UINT32) 3;
    UINT32 OffStrings = OffStruct + (UINT32) Struct.size ();
    UINT32 TotalSize  = OffStrings + (UINT32) Strings.Bytes.size ();

    pOut->clear ();
    PushBe32 (pOut, FDT_MAGIC);
    PushBe32 (pOut, TotalSize);
    PushBe32 (pOut, OffStruct);
    PushBe32 (pOut, OffStrings);
    PushBe32 (pOut, OffRsv);
    PushBe32 (pOut, 17);                                     // version
    PushBe32 (pOut, 16);                                     // last compatible version
    PushBe32 (pOut, Tree.BootCpuId);
    PushBe32 (pOut, (UINT32) Strings.Bytes.size ());
    PushBe32 (pOut, (UINT32) Struct.size ());
    pOut->insert (pOut->end (), Rsv.begin (), Rsv.end ());
    AlignTo (pOut, 4);
    pOut->insert (pOut->end (), Struct.begin (), Struct.end ());
    pOut->insert (pOut->end (), Strings.Bytes.begin (), Strings.Bytes.end ());
}

// ===========================================================================
//  DTS (textual) -- emit (decompile).
// ===========================================================================

// Does a property value look like one or more printable, NUL-terminated strings? (dtc uses
// the same heuristic to choose between "string", <cells>, and [bytes] rendering.)
static bool
LooksLikeStrings (std::vector<UINT8> CONST &V)
{
    if (V.empty () || V.back () != 0) { return false; }
    bool AnyPrintable = false;
    for (size_t I = 0; I < V.size (); I++) {
        UINT8 C = V[I];
        if (C == 0) { continue; }
        if (C < 0x20 || C > 0x7e) { return false; }
        AnyPrintable = true;
    }
    return AnyPrintable;
}

static void
AppendEscaped (std::string *pOut, std::string CONST &S)
{
    for (char C : S) {
        if (C == '"' || C == '\\') { pOut->push_back ('\\'); }
        pOut->push_back (C);
    }
}

static void
EmitProp (DT_PROP CONST &P, std::string *pOut)
{
    *pOut += P.Name;
    if (P.Value.empty ()) {
        *pOut += ";\n";
        return;
    }
    *pOut += " = ";
    if (LooksLikeStrings (P.Value)) {
        bool First = true;
        size_t I = 0;
        while (I < P.Value.size ()) {
            std::string S ((CHAR8 CONST *) &P.Value[I]);
            if (!First) { *pOut += ", "; }
            *pOut += "\"";
            AppendEscaped (pOut, S);
            *pOut += "\"";
            First = false;
            I += S.size () + 1;
        }
    } else if (P.Value.size () % 4 == 0) {
        *pOut += "<";
        for (size_t I = 0; I < P.Value.size (); I += 4) {
            if (I != 0) { *pOut += " "; }
            char Buf[16];
            std::snprintf (Buf, sizeof (Buf), "0x%x", Be32 (&P.Value[I]));
            *pOut += Buf;
        }
        *pOut += ">";
    } else {
        *pOut += "[";
        for (size_t I = 0; I < P.Value.size (); I++) {
            if (I != 0) { *pOut += " "; }
            char Buf[8];
            std::snprintf (Buf, sizeof (Buf), "%02x", P.Value[I]);
            *pOut += Buf;
        }
        *pOut += "]";
    }
    *pOut += ";\n";
}

static void
EmitNode (DtNode CONST &Node, std::string *pOut, int Depth)
{
    std::string Indent (Depth * 4, ' ');
    *pOut += Indent;
    *pOut += Depth == 0 ? "/" : Node.Name;
    *pOut += " {\n";
    std::string Inner ((Depth + 1) * 4, ' ');
    for (DT_PROP CONST &P : Node.Props) {
        *pOut += Inner;
        EmitProp (P, pOut);
    }
    for (DtNode CONST &C : Node.Children) {
        if (!Node.Props.empty () || &C != &Node.Children.front ()) { *pOut += "\n"; }
        EmitNode (C, pOut, Depth + 1);
    }
    *pOut += Indent;
    *pOut += "};\n";
}

static void
EmitDts (DeviceTree CONST &Tree, std::string *pOut)
{
    *pOut += "/dts-v1/;\n\n";
    for (std::pair<UINT64, UINT64> CONST &R : Tree.MemReserve) {
        char Buf[64];
        std::snprintf (Buf, sizeof (Buf), "/memreserve/ 0x%llx 0x%llx;\n",
                       (unsigned long long) R.first, (unsigned long long) R.second);
        *pOut += Buf;
    }
    if (!Tree.MemReserve.empty ()) { *pOut += "\n"; }
    EmitNode (Tree.Root, pOut, 0);
}

// ===========================================================================
//  DTS (textual) -- parse (compile), with label/&ref phandle resolution.
// ===========================================================================

// A pending "&label" / "&{/path}" reference: at this byte offset in this node's property the
// target node's phandle must be written once all phandles are assigned.
typedef struct _DT_FIXUP {
    std::string  NodePath;     // absolute path of the node holding the property
    std::string  PropName;
    size_t       ByteOffset;   // where in the property value the 4-byte phandle goes
    std::string  Target;       // label name, or absolute path when TargetIsPath
    bool         TargetIsPath;
} DT_FIXUP;

typedef struct _DTS_PARSER {
    CHAR8 CONST                       *p;
    CHAR8 CONST                       *End;
    std::string                       *pError;
    std::map<std::string, std::string> Labels;     // label -> absolute node path
    std::vector<DT_FIXUP>              Fixups;
} DTS_PARSER;

static void
SkipWs (DTS_PARSER *pP)
{
    for (;;) {
        while (pP->p < pP->End && (unsigned char) *pP->p <= ' ') { pP->p++; }
        if (pP->p + 1 < pP->End && pP->p[0] == '/' && pP->p[1] == '/') {
            while (pP->p < pP->End && *pP->p != '\n') { pP->p++; }
            continue;
        }
        if (pP->p + 1 < pP->End && pP->p[0] == '/' && pP->p[1] == '*') {
            pP->p += 2;
            while (pP->p + 1 < pP->End && !(pP->p[0] == '*' && pP->p[1] == '/')) { pP->p++; }
            if (pP->p + 1 < pP->End) { pP->p += 2; }
            continue;
        }
        break;
    }
}

static bool
IsNameChar (char C)
{
    return std::isalnum ((unsigned char) C) || C == ',' || C == '.' || C == '_' ||
           C == '+' || C == '-' || C == '@' || C == '#' || C == '?';
}

static std::string
ReadName (DTS_PARSER *pP)
{
    std::string S;
    while (pP->p < pP->End && IsNameChar (*pP->p)) { S.push_back (*pP->p++); }
    return S;
}

static bool
Expect (DTS_PARSER *pP, char C)
{
    SkipWs (pP);
    if (pP->p < pP->End && *pP->p == C) { pP->p++; return true; }
    if (pP->pError->empty ()) {
        char Buf[48];
        std::snprintf (Buf, sizeof (Buf), "expected '%c' in device-tree source", C);
        *pP->pError = Buf;
    }
    return false;
}

// Parse one integer cell (decimal or 0x hex).
static UINT64
ReadInt (DTS_PARSER *pP)
{
    SkipWs (pP);
    char *pEnd = nullptr;
    UINT64 V = (UINT64) std::strtoull (pP->p, &pEnd, 0);
    if (pEnd != nullptr && pEnd > pP->p) { pP->p = pEnd; }
    return V;
}

static bool ParseNode (DTS_PARSER *pP, DtNode *pNode, std::string CONST &Path);

// Parse a property value (the right-hand side of "name = ...;") into raw bytes, recording any
// reference fixups against NodePath/PropName.
static bool
ParseValue (DTS_PARSER *pP, std::vector<UINT8> *pOut, std::string CONST &NodePath, std::string CONST &PropName)
{
    for (;;) {
        SkipWs (pP);
        if (pP->p >= pP->End) { *pP->pError = "unterminated property value"; return false; }
        char C = *pP->p;
        if (C == '"') {
            pP->p++;
            while (pP->p < pP->End && *pP->p != '"') {
                if (*pP->p == '\\' && pP->p + 1 < pP->End) {
                    pP->p++;
                    char E = *pP->p++;
                    switch (E) {
                        case 'n': pOut->push_back ('\n'); break;
                        case 't': pOut->push_back ('\t'); break;
                        case 'r': pOut->push_back ('\r'); break;
                        case '0': pOut->push_back (0); break;
                        default:  pOut->push_back ((UINT8) E); break;
                    }
                } else {
                    pOut->push_back ((UINT8) *pP->p++);
                }
            }
            if (!Expect (pP, '"')) { return false; }
            pOut->push_back (0);                             // NUL-terminate the string
        } else if (C == '<') {
            pP->p++;                                         // a cell array of 32-bit big-endian cells
            for (;;) {
                SkipWs (pP);
                if (pP->p >= pP->End) { *pP->pError = "unterminated cell array"; return false; }
                if (*pP->p == '>') { pP->p++; break; }
                if (*pP->p == '&') {                          // phandle reference -> resolved later
                    pP->p++;
                    DT_FIXUP F;
                    F.NodePath = NodePath;
                    F.PropName = PropName;
                    F.ByteOffset = pOut->size ();
                    if (pP->p < pP->End && *pP->p == '{') {  // &{/path}
                        pP->p++;
                        std::string Pth;
                        while (pP->p < pP->End && *pP->p != '}') { Pth.push_back (*pP->p++); }
                        Expect (pP, '}');
                        F.Target = Pth;
                        F.TargetIsPath = true;
                    } else {
                        F.Target = ReadName (pP);            // &label
                        F.TargetIsPath = false;
                    }
                    pP->Fixups.push_back (std::move (F));
                    PushBe32 (pOut, 0);                      // placeholder, patched in pass 2
                } else {
                    PushBe32 (pOut, (UINT32) ReadInt (pP));
                }
            }
        } else if (C == '&') {
            pP->p++;                                         // bare phandle reference == <&ref>: one cell
            DT_FIXUP F;
            F.NodePath = NodePath;
            F.PropName = PropName;
            F.ByteOffset = pOut->size ();
            if (pP->p < pP->End && *pP->p == '{') {
                pP->p++;
                std::string Pth;
                while (pP->p < pP->End && *pP->p != '}') { Pth.push_back (*pP->p++); }
                Expect (pP, '}');
                F.Target = Pth;
                F.TargetIsPath = true;
            } else {
                F.Target = ReadName (pP);
                F.TargetIsPath = false;
            }
            pP->Fixups.push_back (std::move (F));
            PushBe32 (pOut, 0);                              // placeholder, patched in pass 2
        } else if (C == '[') {
            pP->p++;                                         // a raw byte string
            for (;;) {
                SkipWs (pP);
                if (pP->p >= pP->End) { *pP->pError = "unterminated byte string"; return false; }
                if (*pP->p == ']') { pP->p++; break; }
                char *pEnd = nullptr;
                UINT64 B = (UINT64) std::strtoull (pP->p, &pEnd, 16);
                if (pEnd == pP->p) { *pP->pError = "bad hex byte"; return false; }
                pP->p = pEnd;
                pOut->push_back ((UINT8) B);
            }
        } else {
            *pP->pError = "unexpected token in property value";
            return false;
        }
        SkipWs (pP);
        if (pP->p < pP->End && *pP->p == ',') { pP->p++; continue; }   // concatenated values
        break;
    }
    return true;
}

// Parse the body of a node (between '{' and '}').
static bool
ParseNodeBody (DTS_PARSER *pP, DtNode *pNode, std::string CONST &Path)
{
    for (;;) {
        SkipWs (pP);
        if (pP->p >= pP->End) { *pP->pError = "unterminated node"; return false; }
        if (*pP->p == '}') { pP->p++; return Expect (pP, ';'); }

        // A label ("foo:") may precede a child node.
        std::string Label;
        CHAR8 CONST *pSave = pP->p;
        std::string First = ReadName (pP);
        SkipWs (pP);
        if (pP->p < pP->End && *pP->p == ':') {
            pP->p++;
            Label = First;
            SkipWs (pP);
            First = ReadName (pP);
            SkipWs (pP);
        }

        if (pP->p < pP->End && *pP->p == '{') {              // child node
            pP->p++;
            DtNode *pChild = pNode->AddChild (First);
            std::string ChildPath = (Path == "/") ? ("/" + First) : (Path + "/" + First);
            if (!Label.empty ()) { pP->Labels[Label] = ChildPath; }
            if (!ParseNodeBody (pP, pChild, ChildPath)) { return false; }
        } else if (pP->p < pP->End && *pP->p == '=') {       // property with a value
            pP->p++;
            std::vector<UINT8> Value;
            if (!ParseValue (pP, &Value, Path, First)) { return false; }
            if (!Expect (pP, ';')) { return false; }
            pNode->SetProp (First, Value);
        } else if (pP->p < pP->End && *pP->p == ';') {       // empty (boolean) property
            pP->p++;
            pNode->SetPropEmpty (First);
        } else {
            (void) pSave;
            *pP->pError = "expected '{', '=' or ';' after name in device-tree source";
            return false;
        }
    }
}

// Resolve a path to the node it names, or nullptr.
static DtNode *
NodeByPath (DtNode *pRoot, std::string CONST &Path)
{
    if (Path == "/" || Path.empty ()) { return pRoot; }
    DtNode *pCur = pRoot;
    size_t I = 0;
    while (I < Path.size ()) {
        while (I < Path.size () && Path[I] == '/') { I++; }
        size_t J = I;
        while (J < Path.size () && Path[J] != '/') { J++; }
        if (J == I) { break; }
        std::string Comp = Path.substr (I, J - I);
        pCur = pCur->FindChild (Comp);
        if (pCur == nullptr) { return nullptr; }
        I = J;
    }
    return pCur;
}

// The highest phandle currently assigned anywhere in the tree.
static void
MaxPhandle (DtNode CONST &Node, UINT32 *pMax)
{
    std::string Key = "phandle";
    if (DT_PROP CONST *P = Node.FindProp (Key)) {
        if (P->Value.size () >= 4) {
            UINT32 V = Be32 (&P->Value[0]);
            if (V > *pMax) { *pMax = V; }
        }
    }
    for (DtNode CONST &C : Node.Children) { MaxPhandle (C, pMax); }
}

static bool
ParseDts (CHAR8 CONST *pText, size_t Len, DeviceTree *pTree, std::string *pError)
{
    DTS_PARSER P;
    P.p = pText;
    P.End = pText + Len;
    P.pError = pError;

    SkipWs (&P);
    if (P.p + 9 <= P.End && std::strncmp (P.p, "/dts-v1/", 8) == 0) {   // optional version tag
        P.p += 8;
        Expect (&P, ';');
    }
    // /memreserve/ <addr> <size>; entries.
    for (;;) {
        SkipWs (&P);
        if (P.p + 12 <= P.End && std::strncmp (P.p, "/memreserve/", 12) == 0) {
            P.p += 12;
            UINT64 Addr = ReadInt (&P);
            UINT64 Size = ReadInt (&P);
            if (!Expect (&P, ';')) { return false; }
            pTree->MemReserve.push_back (std::make_pair (Addr, Size));
            continue;
        }
        break;
    }
    SkipWs (&P);
    if (P.p >= P.End || *P.p != '/') { *pError = "device-tree source has no root node"; return false; }
    P.p++;                                                   // the root "/"
    if (!Expect (&P, '{')) { return false; }
    std::string RootPath = "/";
    if (!ParseNodeBody (&P, &pTree->Root, RootPath)) { return false; }

    // Pass 2: assign phandles to referenced nodes and patch every reference's cell.
    UINT32 NextPhandle = 0;
    MaxPhandle (pTree->Root, &NextPhandle);
    for (DT_FIXUP CONST &F : P.Fixups) {
        std::string TargetPath = F.TargetIsPath ? F.Target : std::string ();
        if (!F.TargetIsPath) {
            auto It = P.Labels.find (F.Target);
            if (It == P.Labels.end ()) {
                *pError = "unresolved reference: &" + F.Target;
                return false;
            }
            TargetPath = It->second;
        }
        DtNode *pTarget = NodeByPath (&pTree->Root, TargetPath);
        if (pTarget == nullptr) { *pError = "reference target not found: " + TargetPath; return false; }
        std::string Key = "phandle";
        DT_PROP *pPh = pTarget->FindProp (Key);
        if (pPh == nullptr || pPh->Value.size () < 4) {
            pTarget->SetPropU32 (Key, ++NextPhandle);
            pPh = pTarget->FindProp (Key);
        }
        UINT32 Ph = Be32 (&pPh->Value[0]);
        DtNode *pHolder = NodeByPath (&pTree->Root, F.NodePath);
        if (pHolder == nullptr) { continue; }
        DT_PROP *pProp = pHolder->FindProp (F.PropName);
        if (pProp == nullptr || F.ByteOffset + 4 > pProp->Value.size ()) { continue; }
        pProp->Value[F.ByteOffset + 0] = (UINT8) (Ph >> 24);
        pProp->Value[F.ByteOffset + 1] = (UINT8) (Ph >> 16);
        pProp->Value[F.ByteOffset + 2] = (UINT8) (Ph >> 8);
        pProp->Value[F.ByteOffset + 3] = (UINT8) Ph;
    }
    return true;
}

// ===========================================================================
//  Format detection, load, emit, overlay.
// ===========================================================================

DT_FORMAT
DeviceTree::DetectFormat (UINT8 CONST *pData, size_t Len)
{
    if (Len >= 4 && Be32 (pData) == FDT_MAGIC) { return DtFormatFdtBlob; }
    // Textual DTS: a "/dts-v1/" tag or a leading "/" root, possibly after whitespace/comments.
    for (size_t I = 0; I < Len && I < 256; I++) {
        UINT8 C = pData[I];
        if (C == ' ' || C == '\t' || C == '\r' || C == '\n') { continue; }
        if (C == '/') {
            if (I + 8 <= Len && std::strncmp ((CHAR8 CONST *) pData + I, "/dts-v1/", 8) == 0) { return DtFormatFdtSource; }
            if (I + 2 <= Len && (pData[I + 1] == '*' || pData[I + 1] == '/')) { continue; }   // comment
            return DtFormatFdtSource;                        // a root "/ {"
        }
        break;
    }
    return DtFormatUnknown;
}

bool
DeviceTree::LoadBytes (UINT8 CONST *pData, size_t Len, DT_FORMAT Fmt, std::string *pError)
{
    if (Fmt == DtFormatUnknown) { Fmt = DetectFormat (pData, Len); }
    switch (Fmt) {
        case DtFormatFdtBlob:   return ReadFdt (pData, Len, this, pError);
        case DtFormatFdtSource: return ParseDts ((CHAR8 CONST *) pData, Len, this, pError);
        case DtFormatAppleBinary:
        case DtFormatAppleText:
        case DtFormatOpenFirmware:
            *pError = "this device-tree format reader is not implemented yet";
            return false;
        default:
            *pError = "unrecognised device-tree format";
            return false;
    }
}

bool
DeviceTree::Load (CHAR8 CONST *pPath, std::string *pError)
{
    std::FILE *pF = std::fopen (pPath, "rb");
    if (pF == nullptr) { *pError = std::string ("cannot open ") + pPath; return false; }
    std::fseek (pF, 0, SEEK_END);
    long Size = std::ftell (pF);
    std::fseek (pF, 0, SEEK_SET);
    std::vector<UINT8> Bytes (Size > 0 ? (size_t) Size : 0);
    if (!Bytes.empty () && std::fread (Bytes.data (), 1, Bytes.size (), pF) != Bytes.size ()) {
        std::fclose (pF);
        *pError = std::string ("cannot read ") + pPath;
        return false;
    }
    std::fclose (pF);
    return LoadBytes (Bytes.data (), Bytes.size (), DtFormatUnknown, pError);
}

bool
DeviceTree::EmitBytes (DT_FORMAT Fmt, std::vector<UINT8> *pOut, std::string *pError) CONST
{
    if (Fmt == DtFormatFdtBlob) {
        WriteFdt (*this, pOut);
        return true;
    }
    *pError = "binary emission for this format is not implemented yet";
    return false;
}

bool
DeviceTree::EmitText (DT_FORMAT Fmt, std::string *pOut, std::string *pError) CONST
{
    if (Fmt == DtFormatFdtSource) {
        EmitDts (*this, pOut);
        return true;
    }
    *pError = "text emission for this format is not implemented yet";
    return false;
}

bool
DeviceTree::Save (CHAR8 CONST *pPath, DT_FORMAT Fmt, std::string *pError) CONST
{
    std::vector<UINT8> Bytes;
    std::string Text;
    UINT8 CONST *pData = nullptr;
    size_t Len = 0;
    if (Fmt == DtFormatFdtBlob || Fmt == DtFormatAppleBinary) {
        if (!EmitBytes (Fmt, &Bytes, pError)) { return false; }
        pData = Bytes.data ();
        Len = Bytes.size ();
    } else {
        if (!EmitText (Fmt, &Text, pError)) { return false; }
        pData = (UINT8 CONST *) Text.data ();
        Len = Text.size ();
    }
    std::FILE *pF = std::fopen (pPath, "wb");
    if (pF == nullptr) { *pError = std::string ("cannot write ") + pPath; return false; }
    if (Len != 0 && std::fwrite (pData, 1, Len, pF) != Len) {
        std::fclose (pF);
        *pError = std::string ("short write to ") + pPath;
        return false;
    }
    std::fclose (pF);
    return true;
}

// Merge node pFrag's properties and children onto pBase (properties overwrite, children
// recurse, new children are created) -- the core of fragment composition.
static void
MergeNode (DtNode *pBase, DtNode CONST &Frag)
{
    for (DT_PROP CONST &P : Frag.Props) {
        pBase->SetProp (P.Name, P.Value);
    }
    for (DtNode CONST &FC : Frag.Children) {
        DtNode *pBC = pBase->AddChild (FC.Name);
        MergeNode (pBC, FC);
    }
}

bool
DeviceTree::ApplyOverlay (DeviceTree CONST &Fragment, std::string *pError)
{
    (void) pError;
    MergeNode (&Root, Fragment.Root);
    for (std::pair<UINT64, UINT64> CONST &R : Fragment.MemReserve) { MemReserve.push_back (R); }
    return true;
}

} // namespace LibCPU
