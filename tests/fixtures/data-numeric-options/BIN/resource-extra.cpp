#include "../../../../packages/Demo/BIN/resource-extra.cpp"

class RscOptionsPageNumberEntryFallback
{
    controls[]={"Border","Panel","Title","Entry","Apply","Cancel"};

    class Border: OptTplDlgBorder { idc=9482; };
    class Panel: OptTplDlgPanel { idc=9483; };
    class Title: OptTplDlgTitle { idc=9480; };
    class Entry
    {
        access=ACCESS_LOCKED;
        type=25;
        idc=9481;
        style=ST_CENTER;
        selection="display";
        angle=0;
        colorText[]=OPT_C_CRT_GREEN;
        colorSelection[]={0.3,1,0.3,0.4};
        font=OPT_FONT_CRT;
        size=1.00;
        autocomplete="";
        x=OPT_DLG_TEXT_X;
        y=OPT_DLG_COUNT_Y;
        w=OPT_DLG_TEXT_W;
        h=OPT_DLG_TEXT_H;
        text="";
    };
    class Apply: OptTplDlgBtn0 { idc=9401; default=0; text="$STR_DISP_MAIN_OPT_DISPLAY_APPLY"; };
    class Cancel: OptTplDlgBtn1 { idc=9402; default=0; text="$STR_DISP_OPT_CAP_CANCEL"; };
};
