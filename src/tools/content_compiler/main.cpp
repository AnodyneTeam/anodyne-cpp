#include <stdio.h>
#include "dialog_compiler.hpp"

int main(int argc, char **argv)
{
    printf("Content compiler exec from %s\n", argv[0]);

    DialogCompiler dc;
    if (dc.Load("../../old/Content/Dialogue", "EN"))
    {
        dc.Compile();
        dc.DebugPrint();
    }
    else
    {
        printf("Dialog OVER!\n");
    }

    return 0;
}
