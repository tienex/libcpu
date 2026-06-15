/* Exercises the device-tree public C API from C (not C++), proving the LC-prefixed surface is
   genuinely C-callable. Builds a small machine tree with the mutation calls, emits it as DTS
   text and as an FDT blob, reloads the blob, and navigates it back -- so create, mutate, emit
   (text + binary), parse, and navigate are all covered. Prints RESULT: PASS / FAIL. */

#include "LibCPU/DeviceTree.h"
#include <stdio.h>
#include <string.h>

int
main (void)
{
    LCDeviceTreeRef Tree = LCDeviceTreeCreate ();
    LCDtNodeRef Root = LCDeviceTreeGetRoot (Tree);
    LCDtNodeSetPropString (Root, "model", "C-API Machine");

    LCDtNodeRef Cpu = LCDtNodeAddChild (Root, "cpu@0");
    LCDtNodeSetPropString (Cpu, "device_type", "cpu");
    LCDtNodeSetPropU32 (Cpu, "reg", 0);

    /* Emit as DTS text and check the obvious content is there. */
    CHAR8 *pDts = LCDeviceTreeCopyText (Tree, LCDtFormatFdtSource);
    int Ok = pDts != NULL &&
             strstr (pDts, "model = \"C-API Machine\"") != NULL &&
             strstr (pDts, "cpu@0") != NULL;

    /* Emit as an FDT blob, reload it, and navigate back to cpu@0's device_type. */
    UINTN Len = 0;
    VOID *pBlob = LCDeviceTreeCopyBytes (Tree, LCDtFormatFdtBlob, &Len);
    Ok = Ok && pBlob != NULL && Len > 0;

    LCDeviceTreeRef Tree2 = LCDeviceTreeCreateFromBytes (pBlob, Len, LCDtFormatFdtBlob);
    Ok = Ok && Tree2 != NULL;
    if (Tree2 != NULL) {
        LCDtNodeRef Cpu2 = LCDtNodeFindChild (LCDeviceTreeGetRoot (Tree2), "cpu@0");
        UINTN VLen = 0;
        CONST VOID *pVal = Cpu2 != NULL ? LCDtNodeGetPropValue (Cpu2, "device_type", &VLen) : NULL;
        Ok = Ok && Cpu2 != NULL && pVal != NULL && VLen == 4 && memcmp (pVal, "cpu", 4) == 0;
    }

    printf ("RESULT: %s  (C API: create/mutate/emit-text/emit-blob/parse/navigate)\n", Ok ? "PASS" : "FAIL");

    LCDeviceTreeFreeBuffer (pDts);
    LCDeviceTreeFreeBuffer (pBlob);
    LCDeviceTreeRelease (Tree);
    LCDeviceTreeRelease (Tree2);
    return Ok ? 0 : 1;
}
