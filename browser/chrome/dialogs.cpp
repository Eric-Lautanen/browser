#include "dialogs.hpp"
#include "../../css/css_values.hpp"
#include <commctrl.h>
#include <windows.h>
#include <string>
#include <optional>
#include <cstdio>

namespace browser {

struct DateDlgCtx {
    HWND cal = nullptr;
    bool ok = false;
    SYSTEMTIME chosen{};
    WNDPROC orig = nullptr;
};

static LRESULT CALLBACK DateDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<DateDlgCtx*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            if (ctx) {
                SendMessage(ctx->cal, MCM_GETCURSEL, 0, (LPARAM)&ctx->chosen);
                ctx->ok = true;
            }
            DestroyWindow(hwnd);
            return 0;
        } else if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
    }
    WNDPROC orig = ctx ? ctx->orig : DefWindowProc;
    return CallWindowProc(orig, hwnd, msg, wp, lp);
}

std::optional<std::string> show_date_picker(HWND parent, const std::string& cur_val) {
    SYSTEMTIME st{}; bool have_date=false;
    if (cur_val.size()>=10) { int y,m,d; if(sscanf(cur_val.c_str(),"%d-%d-%d",&y,&m,&d)==3){st.wYear=(WORD)y; st.wMonth=(WORD)m; st.wDay=(WORD)d; have_date=true;}}
    if(!have_date) GetLocalTime(&st);
    HWND dlg = CreateWindowEx(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST, "STATIC", "Pick a date", WS_POPUP|WS_CAPTION|WS_VISIBLE, 100,100,240,220, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if(!dlg) return std::nullopt;
    HWND cal = CreateWindowEx(0, MONTHCAL_CLASSA, "", WS_CHILD|WS_VISIBLE|MCS_NOTODAYCIRCLE, 4,4,220,160, dlg, (HMENU)1001, GetModuleHandle(nullptr), nullptr);
    SendMessage(cal, MCM_SETCURSEL, 0, (LPARAM)&st);
    CreateWindowEx(0,"BUTTON","OK",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,60,170,60,24,dlg,(HMENU)IDOK,GetModuleHandle(nullptr),nullptr);
    CreateWindowEx(0,"BUTTON","Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,130,170,60,24,dlg,(HMENU)IDCANCEL,GetModuleHandle(nullptr),nullptr);
    auto* ctx = new DateDlgCtx{cal,false,st, (WNDPROC)GetWindowLongPtr(dlg, GWLP_WNDPROC)};
    SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR)ctx);
    SetWindowLongPtr(dlg, GWLP_WNDPROC, (LONG_PTR)DateDlgProc);
    MSG m;
    while(IsWindow(dlg)){ BOOL ret=GetMessage(&m,nullptr,0,0); if(!ret||ret==-1) break; if(!IsDialogMessage(dlg,&m)){TranslateMessage(&m); DispatchMessage(&m);}}
    std::optional<std::string> res;
    if(ctx->ok){
        char buf[16]; snprintf(buf,sizeof(buf),"%04d-%02d-%02d",ctx->chosen.wYear,ctx->chosen.wMonth,ctx->chosen.wDay);
        res = buf;
    }
    delete ctx;
    return res;
}

struct TimeDlgCtx {
    HWND hour=nullptr, min=nullptr;
    bool ok=false;
    WNDPROC orig=nullptr;
};

static LRESULT CALLBACK TimeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    auto* ctx = reinterpret_cast<TimeDlgCtx*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if(msg==WM_COMMAND && LOWORD(wp)==IDOK){
        if(ctx){
            char hb[8], mb[8]; GetWindowTextA(ctx->hour,hb,8); GetWindowTextA(ctx->min,mb,8);
            int hh=atoi(hb), mm=atoi(mb);
            if(hh>=0&&hh<24&&mm>=0&&mm<60) ctx->ok=true;
        }
        DestroyWindow(hwnd); return 0;
    } else if(msg==WM_COMMAND && LOWORD(wp)==IDCANCEL){ DestroyWindow(hwnd); return 0; }
    WNDPROC orig = ctx ? ctx->orig : DefWindowProc;
    return CallWindowProc(orig,hwnd,msg,wp,lp);
}

std::optional<std::string> show_time_picker(HWND parent, const std::string& cur_val){
    int h=0, mi=0; bool have=false;
    if(cur_val.size()>=5 && sscanf(cur_val.c_str(),"%d:%d",&h,&mi)==2) have=true;
    if(!have){ SYSTEMTIME st; GetLocalTime(&st); h=st.wHour; mi=st.wMinute; }
    HWND dlg = CreateWindowEx(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,"STATIC","Pick a time",WS_POPUP|WS_CAPTION|WS_VISIBLE,120,120,200,120,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!dlg) return std::nullopt;
    CreateWindowEx(0,"STATIC","Hour:",WS_CHILD|WS_VISIBLE,12,14,40,18,dlg,nullptr,GetModuleHandle(nullptr),nullptr);
    HWND hour_edit = CreateWindowEx(0,"EDIT","",WS_CHILD|WS_VISIBLE|ES_NUMBER|WS_BORDER,56,12,40,22,dlg,(HMENU)2001,GetModuleHandle(nullptr),nullptr);
    CreateWindowEx(0,"STATIC","Min:",WS_CHILD|WS_VISIBLE,12,44,40,18,dlg,nullptr,GetModuleHandle(nullptr),nullptr);
    HWND min_edit = CreateWindowEx(0,"EDIT","",WS_CHILD|WS_VISIBLE|ES_NUMBER|WS_BORDER,56,42,40,22,dlg,(HMENU)2002,GetModuleHandle(nullptr),nullptr);
    CreateWindowEx(0,"BUTTON","OK",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,110,12,60,24,dlg,(HMENU)IDOK,GetModuleHandle(nullptr),nullptr);
    CreateWindowEx(0,"BUTTON","Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,110,42,60,24,dlg,(HMENU)IDCANCEL,GetModuleHandle(nullptr),nullptr);
    char hbuf[8], mbuf[8]; snprintf(hbuf,sizeof(hbuf),"%d",h); snprintf(mbuf,sizeof(mbuf),"%d",mi);
    SetWindowTextA(hour_edit,hbuf); SetWindowTextA(min_edit,mbuf);
    auto* ctx = new TimeDlgCtx{hour_edit,min_edit,false,(WNDPROC)GetWindowLongPtr(dlg,GWLP_WNDPROC)};
    SetWindowLongPtr(dlg,GWLP_USERDATA,(LONG_PTR)ctx);
    SetWindowLongPtr(dlg,GWLP_WNDPROC,(LONG_PTR)TimeDlgProc);
    MSG m; while(IsWindow(dlg)){BOOL ret=GetMessage(&m,nullptr,0,0); if(!ret||ret==-1) break; if(!IsDialogMessage(dlg,&m)){TranslateMessage(&m); DispatchMessage(&m);}}
    std::optional<std::string> res;
    if(ctx->ok){
        char hb[8], mb2[8]; GetWindowTextA(ctx->hour,hb,8); GetWindowTextA(ctx->min,mb2,8);
        int hh=atoi(hb), mm=atoi(mb2); char buf[16]; snprintf(buf,sizeof(buf),"%02d:%02d",hh,mm); res=buf;
    }
    delete ctx;
    return res;
}

bool show_file_picker(HWND parent, std::string& out_path){
    wchar_t wbuf[260*64]={0};
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=parent; ofn.lpstrFilter=L"All Files\0*.*\0"; ofn.lpstrFile=wbuf; ofn.nMaxFile=260*64; ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
    if(!GetOpenFileNameW(&ofn)) return false;
    int len=WideCharToMultiByte(CP_UTF8,0,wbuf,-1,nullptr,0,nullptr,nullptr);
    if(len>0){ std::string path(len-1,'\0'); WideCharToMultiByte(CP_UTF8,0,wbuf,-1,&path[0],len,nullptr,nullptr); out_path=path; return true; }
    return false;
}

bool show_color_picker(HWND parent, std::string cur_val, std::string& out_color){
    COLORREF cust[16]={0};
    COLORREF cur=RGB(0,0,0);
    if(!cur_val.empty() && cur_val[0]=='#' && cur_val.size()>=7){
        auto c = browser::css::Color::from_hex(cur_val);
        cur=RGB(c.r,c.g,c.b);
    }
    CHOOSECOLORW cc{}; cc.lStructSize=sizeof(cc); cc.hwndOwner=parent; cc.rgbResult=cur; cc.lpCustColors=cust; cc.Flags=CC_RGBINIT|CC_FULLOPEN;
    if(!ChooseColorW(&cc)) return false;
    char buf[16]; snprintf(buf,sizeof(buf),"#%02x%02x%02x",GetRValue(cc.rgbResult),GetGValue(cc.rgbResult),GetBValue(cc.rgbResult));
    out_color=buf; return true;
}

} // namespace browser
