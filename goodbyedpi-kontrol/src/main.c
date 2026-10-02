/*
 * GoodbyeDPI Kontrol
 *
 * GoodbyeDPI (Türkiye sürümü 0.2.3rc3) dosyalarını kendi içinde taşıyan,
 * GoodbyeDPI'ı tek tıkla açıp kapatmaya yarayan küçük bir Windows uygulaması.
 *
 *  - goodbyedpi.exe, WinDivert.dll ve WinDivert64.sys exe'nin içine gömülüdür,
 *    ilk açılışta %LOCALAPPDATA%\GoodbyeDPI-Kontrol klasörüne çıkarılır.
 *  - GoodbyeDPI konsol penceresi olmadan, gizli olarak çalıştırılır.
 *  - Simge durumuna küçültülünce sistem tepsisine gizlenir.
 *  - Uygulama kapanınca (çökse bile, Job Object sayesinde) GoodbyeDPI da kapanır.
 *  - Kurulu "GoodbyeDPI" Windows hizmetini (sürekli açık kalmasının sebebi)
 *    algılar ve kaldırmayı önerir.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600
#define SECURITY_WIN32

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <security.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "resource.h"

#define APP_NAME        L"GoodbyeDPI Kontrol"
#define WND_CLASS       L"GoodbyeDPIKontrolWnd"
#define MUTEX_NAME      L"Local\\GoodbyeDPIKontrolTekOrnek"
#define DATA_DIR_NAME   L"GoodbyeDPI-Kontrol"
#define SERVICE_NAME    L"GoodbyeDPI"
#define TASK_NAME       L"GoodbyeDPI Kontrol"
#define GDPI_EXE_NAME   L"goodbyedpi.exe"

#define WM_APP_TRAY     (WM_APP + 1)
#define WM_APP_SHOW     (WM_APP + 2)

#define ID_TIMER        1
#define ID_COMBO        100
#define ID_TOGGLE       101
#define ID_CHK_AUTO     102
#define ID_CHK_BOOT     103
#define ID_TRAY_TOGGLE  200
#define ID_TRAY_SHOW    201
#define ID_TRAY_EXIT    202

typedef struct {
    const wchar_t *name;
    const wchar_t *args;
    BOOL dns_redirect;
} Preset;

/* Orijinal paketteki .cmd dosyalarıyla birebir aynı parametreler. */
static const Preset kPresets[] = {
    { L"Varsayılan (turkey_dnsredir) - önerilen",
      L"-5 --set-ttl 5 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253", TRUE },
    { L"Alternatif 1 (Superonline)", L"--set-ttl 3", FALSE },
    { L"Alternatif 2 (Superonline)", L"-5", FALSE },
    { L"Alternatif 3 (Superonline)",
      L"--set-ttl 3 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253", TRUE },
    { L"Alternatif 4 (Superonline)",
      L"-5 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253", TRUE },
    { L"Alternatif 5 (Superonline)",
      L"-9 --dns-addr 77.88.8.8 --dns-port 1253 --dnsv6-addr 2a02:6b8::feed:0ff --dnsv6-port 1253", TRUE },
    { L"Alternatif 6 (Superonline)", L"-9", FALSE },
};
#define PRESET_COUNT ((int)(sizeof(kPresets) / sizeof(kPresets[0])))

enum { ST_UNKNOWN = -1, ST_OFF = 0, ST_ON, ST_EXTERNAL };

static HINSTANCE g_inst;
static HWND g_hwnd, g_status, g_combo, g_hint, g_toggle, g_chkAuto, g_chkBoot, g_info;
static HFONT g_font, g_fontBig, g_fontBtn;
static HICON g_icoApp, g_icoAppSm, g_icoOn, g_icoOff;
static HBRUSH g_bgBrush;
static NOTIFYICONDATAW g_nid;
static UINT g_msgTaskbarCreated;
static int g_dpi = 96;
static int g_state = ST_UNKNOWN;
static BOOL g_balloonShown;

static wchar_t g_dir[MAX_PATH];
static wchar_t g_gdpiPath[MAX_PATH];
static wchar_t g_iniPath[MAX_PATH];
static wchar_t g_logPath[MAX_PATH];
static wchar_t g_selfPath[MAX_PATH];

static HANDLE g_job;
static HANDLE g_proc;
static DWORD g_pid;
static BOOL g_expectRunning;

static int g_preset;
static BOOL g_autoStart;
static BOOL g_bootTask;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void JoinPath(wchar_t *out, const wchar_t *dir, const wchar_t *name)
{
    _snwprintf(out, MAX_PATH, L"%ls\\%ls", dir, name);
    out[MAX_PATH - 1] = 0;
}

static void ErrorBox(const wchar_t *msg)
{
    MessageBoxW(g_hwnd, msg, APP_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

/* ------------------------------------------------------------------ */
/* Ayarlar                                                             */
/* ------------------------------------------------------------------ */

static void LoadSettings(void)
{
    g_preset = (int)GetPrivateProfileIntW(L"Ayarlar", L"Yontem", 0, g_iniPath);
    if (g_preset < 0 || g_preset >= PRESET_COUNT)
        g_preset = 0;
    g_autoStart = GetPrivateProfileIntW(L"Ayarlar", L"OtomatikBaslat", 0, g_iniPath) != 0;
    g_bootTask = GetPrivateProfileIntW(L"Ayarlar", L"AcilisGorevi", 0, g_iniPath) != 0;
}

static void SaveSettings(void)
{
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%d", g_preset);
    WritePrivateProfileStringW(L"Ayarlar", L"Yontem", buf, g_iniPath);
    WritePrivateProfileStringW(L"Ayarlar", L"OtomatikBaslat", g_autoStart ? L"1" : L"0", g_iniPath);
    WritePrivateProfileStringW(L"Ayarlar", L"AcilisGorevi", g_bootTask ? L"1" : L"0", g_iniPath);
}

/* ------------------------------------------------------------------ */
/* Gömülü dosyaları çıkarma                                            */
/* ------------------------------------------------------------------ */

static BOOL FileHasContent(const wchar_t *path, const void *data, DWORD size)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;

    BOOL same = FALSE;
    LARGE_INTEGER fsz;
    if (GetFileSizeEx(f, &fsz) && fsz.QuadPart == size) {
        BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
        DWORD rd = 0;
        if (buf && ReadFile(f, buf, size, &rd, NULL) && rd == size)
            same = memcmp(buf, data, size) == 0;
        if (buf)
            HeapFree(GetProcessHeap(), 0, buf);
    }
    CloseHandle(f);
    return same;
}

static BOOL ExtractResource(int id, const wchar_t *name)
{
    HRSRC res = FindResourceW(g_inst, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res)
        return FALSE;
    HGLOBAL h = LoadResource(g_inst, res);
    DWORD size = SizeofResource(g_inst, res);
    const void *data = h ? LockResource(h) : NULL;
    if (!data || !size)
        return FALSE;

    wchar_t path[MAX_PATH];
    JoinPath(path, g_dir, name);
    if (FileHasContent(path, data, size))
        return TRUE;

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD wr = 0;
    BOOL ok = WriteFile(f, data, size, &wr, NULL) && wr == size;
    CloseHandle(f);
    if (!ok)
        DeleteFileW(path);
    return ok;
}

static BOOL ExtractAll(void)
{
    BOOL ok = TRUE;
    ok &= ExtractResource(IDR_WINDIVERT_SYS, L"WinDivert64.sys");
    ok &= ExtractResource(IDR_WINDIVERT_DLL, L"WinDivert.dll");
    ok &= ExtractResource(IDR_GDPI_EXE, GDPI_EXE_NAME);
    return ok;
}

static BOOL InitPaths(void)
{
    wchar_t base[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)))
        return FALSE;
    JoinPath(g_dir, base, DATA_DIR_NAME);
    CreateDirectoryW(g_dir, NULL);
    JoinPath(g_gdpiPath, g_dir, GDPI_EXE_NAME);
    JoinPath(g_iniPath, g_dir, L"ayarlar.ini");
    JoinPath(g_logPath, g_dir, L"goodbyedpi.log");
    GetModuleFileNameW(NULL, g_selfPath, MAX_PATH);
    return GetFileAttributesW(g_dir) != INVALID_FILE_ATTRIBUTES;
}

/* ------------------------------------------------------------------ */
/* Süreç / hizmet yardımcıları                                         */
/* ------------------------------------------------------------------ */

/* exceptPid dışındaki goodbyedpi.exe süreçlerini bulur; kill ise sonlandırır. */
static BOOL ScanOtherGdpi(DWORD exceptPid, BOOL kill)
{
    BOOL found = FALSE;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return FALSE;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == exceptPid || _wcsicmp(pe.szExeFile, GDPI_EXE_NAME) != 0)
                continue;
            found = TRUE;
            if (!kill)
                break;
            HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            if (p) {
                TerminateProcess(p, 0);
                WaitForSingleObject(p, 2000);
                CloseHandle(p);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static BOOL WaitServiceStopped(SC_HANDLE svc)
{
    SERVICE_STATUS ss;
    for (int i = 0; i < 50; i++) {
        if (!QueryServiceStatus(svc, &ss))
            return FALSE;
        if (ss.dwCurrentState == SERVICE_STOPPED)
            return TRUE;
        Sleep(100);
    }
    return FALSE;
}

/* GoodbyeDPI hizmetini durdurur; removeIt ise kaldırır. Hizmet yoksa FALSE döner. */
static BOOL ControlGdpiService(BOOL removeIt, BOOL *removed)
{
    if (removed)
        *removed = FALSE;
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm)
        return FALSE;
    DWORD access = SERVICE_STOP | SERVICE_QUERY_STATUS | (removeIt ? DELETE : 0);
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, access);
    if (!svc) {
        CloseServiceHandle(scm);
        return FALSE;
    }

    SERVICE_STATUS ss;
    if (QueryServiceStatus(svc, &ss) && ss.dwCurrentState != SERVICE_STOPPED) {
        ControlService(svc, SERVICE_CONTROL_STOP, &ss);
        WaitServiceStopped(svc);
    }
    if (removeIt && DeleteService(svc) && removed)
        *removed = TRUE;

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return TRUE;
}

static BOOL GdpiServiceExists(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm)
        return FALSE;
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (svc)
        CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return svc != NULL;
}

static DWORD RunHidden(wchar_t *cmdline)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return (DWORD)-1;
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

/* ------------------------------------------------------------------ */
/* Windows açılışında başlatma (Görev Zamanlayıcı)                     */
/* ------------------------------------------------------------------ */

static void SchtasksPath(wchar_t *out)
{
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    JoinPath(out, sys, L"schtasks.exe");
}

static BOOL BootTaskExists(void)
{
    wchar_t exe[MAX_PATH], cmd[1024];
    SchtasksPath(exe);
    _snwprintf(cmd, 1024, L"\"%ls\" /Query /TN \"%ls\"", exe, TASK_NAME);
    cmd[1023] = 0;
    return RunHidden(cmd) == 0;
}

static void XmlEscape(wchar_t *out, size_t cap, const wchar_t *in)
{
    size_t n = 0;
    for (; *in && n + 6 < cap; in++) {
        const wchar_t *rep = NULL;
        if (*in == L'&') rep = L"&amp;";
        else if (*in == L'<') rep = L"&lt;";
        else if (*in == L'>') rep = L"&gt;";
        if (rep) {
            while (*rep)
                out[n++] = *rep++;
        } else {
            out[n++] = *in;
        }
    }
    out[n] = 0;
}

static BOOL RegisterBootTask(void)
{
    wchar_t user[256] = L"", userXml[512] = L"", userTag[600] = L"", selfXml[MAX_PATH * 5];
    ULONG ulen = 256;
    if (GetUserNameExW(NameSamCompatible, user, &ulen) && user[0]) {
        XmlEscape(userXml, 512, user);
        _snwprintf(userTag, 600, L"<UserId>%ls</UserId>", userXml);
        userTag[599] = 0;
    }
    XmlEscape(selfXml, MAX_PATH * 5, g_selfPath);

    static wchar_t xml[8192];
    _snwprintf(xml, 8192,
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>GoodbyeDPI Kontrol uygulamasını oturum açılışında başlatır.</Description></RegistrationInfo>\r\n"
        L"  <Triggers><LogonTrigger><Enabled>true</Enabled>%ls</LogonTrigger></Triggers>\r\n"
        L"  <Principals><Principal id=\"Author\">%ls<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
        L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
        L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
        L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
        L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
        L"    <Enabled>true</Enabled>\r\n"
        L"    <Hidden>false</Hidden>\r\n"
        L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
        L"    <Priority>7</Priority>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\"><Exec><Command>%ls</Command><Arguments>/tray</Arguments></Exec></Actions>\r\n"
        L"</Task>\r\n",
        userTag, userTag, selfXml);
    xml[8191] = 0;

    wchar_t xmlPath[MAX_PATH];
    JoinPath(xmlPath, g_dir, L"gorev.xml");
    HANDLE f = CreateFileW(xmlPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    WCHAR bom = 0xFEFF;
    DWORD wr;
    BOOL ok = WriteFile(f, &bom, sizeof(bom), &wr, NULL) &&
              WriteFile(f, xml, (DWORD)(wcslen(xml) * sizeof(wchar_t)), &wr, NULL);
    CloseHandle(f);

    if (ok) {
        wchar_t exe[MAX_PATH], cmd[1024];
        SchtasksPath(exe);
        _snwprintf(cmd, 1024, L"\"%ls\" /Create /TN \"%ls\" /XML \"%ls\" /F", exe, TASK_NAME, xmlPath);
        cmd[1023] = 0;
        ok = RunHidden(cmd) == 0;
    }
    DeleteFileW(xmlPath);
    return ok;
}

static BOOL DeleteBootTask(void)
{
    wchar_t exe[MAX_PATH], cmd[1024];
    SchtasksPath(exe);
    _snwprintf(cmd, 1024, L"\"%ls\" /Delete /TN \"%ls\" /F", exe, TASK_NAME);
    cmd[1023] = 0;
    return RunHidden(cmd) == 0;
}

/* ------------------------------------------------------------------ */
/* GoodbyeDPI başlat / durdur                                          */
/* ------------------------------------------------------------------ */

static void UpdateUI(BOOL force);

static BOOL IsOurGdpiRunning(void)
{
    return g_proc && WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT;
}

static void AntivirusHint(wchar_t *out, size_t cap)
{
    _snwprintf(out, cap,
        L"\r\n\r\nAntivirüs programınız WinDivert dosyalarını engelliyor olabilir. "
        L"Şu klasörü antivirüs dışlamalarına ekleyip tekrar deneyin:\r\n%ls", g_dir);
    out[cap - 1] = 0;
}

static BOOL StartGdpi(void)
{
    if (IsOurGdpiRunning())
        return TRUE;

    /* Başka yerden açılmış GoodbyeDPI (hizmet / .cmd) çakışmasın. */
    ControlGdpiService(FALSE, NULL);
    ScanOtherGdpi(0, TRUE);

    wchar_t msg[2048], hint[1024];
    if (!ExtractAll()) {
        AntivirusHint(hint, 1024);
        _snwprintf(msg, 2048, L"GoodbyeDPI dosyaları çıkarılamadı.%ls", hint);
        msg[2047] = 0;
        ErrorBox(msg);
        return FALSE;
    }

    wchar_t cmd[1024];
    _snwprintf(cmd, 1024, L"\"%ls\" %ls", g_gdpiPath, kPresets[g_preset].args);
    cmd[1023] = 0;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE log = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = log;
        si.hStdError = log;
    }

    BOOL ok = CreateProcessW(g_gdpiPath, cmd, NULL, NULL, log != INVALID_HANDLE_VALUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, g_dir, &si, &pi);
    DWORD err = GetLastError();
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);

    if (!ok) {
        AntivirusHint(hint, 1024);
        _snwprintf(msg, 2048, L"GoodbyeDPI başlatılamadı (hata kodu %lu).%ls", err, hint);
        msg[2047] = 0;
        ErrorBox(msg);
        return FALSE;
    }

    if (g_job)
        AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    g_proc = pi.hProcess;
    g_pid = pi.dwProcessId;
    g_expectRunning = TRUE;
    UpdateUI(TRUE);
    return TRUE;
}

static void StopGdpi(void)
{
    g_expectRunning = FALSE;
    if (g_proc) {
        TerminateProcess(g_proc, 0);
        WaitForSingleObject(g_proc, 3000);
        CloseHandle(g_proc);
        g_proc = NULL;
        g_pid = 0;
    }
    ControlGdpiService(FALSE, NULL);
    ScanOtherGdpi(0, TRUE);
    UpdateUI(TRUE);
}

static void ReadLogTail(wchar_t *out, int cap)
{
    out[0] = 0;
    HANDLE f = CreateFileW(g_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    char buf[1500];
    LARGE_INTEGER sz;
    if (GetFileSizeEx(f, &sz) && sz.QuadPart > (LONGLONG)sizeof(buf) - 1) {
        LARGE_INTEGER pos;
        pos.QuadPart = sz.QuadPart - (LONGLONG)sizeof(buf) + 1;
        SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
    }
    DWORD rd = 0;
    if (ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL)) {
        buf[rd] = 0;
        int n = MultiByteToWideChar(CP_ACP, 0, buf, -1, out, cap);
        if (n <= 0)
            out[0] = 0;
    }
    CloseHandle(f);
}

/* Zamanlayıcıdan çağrılır: GoodbyeDPI kendi kendine kapandıysa bildirir. */
static void CheckProcess(void)
{
    if (!g_proc || WaitForSingleObject(g_proc, 0) != WAIT_OBJECT_0)
        return;

    DWORD code = 0;
    GetExitCodeProcess(g_proc, &code);
    CloseHandle(g_proc);
    g_proc = NULL;
    g_pid = 0;
    if (!g_expectRunning)
        return;
    g_expectRunning = FALSE;
    UpdateUI(TRUE);

    static wchar_t out[1600], msg[4096], hint[1024];
    ReadLogTail(out, 1600);
    AntivirusHint(hint, 1024);
    _snwprintf(msg, 4096, L"GoodbyeDPI beklenmedik şekilde kapandı (çıkış kodu %lu).\r\n\r\n%ls%ls%ls",
               code, out[0] ? L"Çıktı:\r\n" : L"", out, hint);
    msg[4095] = 0;
    ErrorBox(msg);
}

/* ------------------------------------------------------------------ */
/* Sistem tepsisi                                                      */
/* ------------------------------------------------------------------ */

static void TraySetTip(const wchar_t *tip)
{
    wcsncpy(g_nid.szTip, tip, sizeof(g_nid.szTip) / sizeof(g_nid.szTip[0]) - 1);
}

static void TrayAdd(void)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = (g_state == ST_OFF || g_state == ST_UNKNOWN) ? g_icoOff : g_icoOn;
    TraySetTip(APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrayUpdate(HICON icon, const wchar_t *tip)
{
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    g_nid.hIcon = icon;
    TraySetTip(tip);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayBalloon(const wchar_t *title, const wchar_t *text)
{
    g_nid.uFlags = NIF_INFO;
    wcsncpy(g_nid.szInfoTitle, title, sizeof(g_nid.szInfoTitle) / sizeof(g_nid.szInfoTitle[0]) - 1);
    wcsncpy(g_nid.szInfo, text, sizeof(g_nid.szInfo) / sizeof(g_nid.szInfo[0]) - 1);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.szInfo[0] = 0;
    g_nid.szInfoTitle[0] = 0;
}

static void ShowMainWindow(void)
{
    ShowWindow(g_hwnd, SW_SHOW);
    if (IsIconic(g_hwnd))
        ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static void ShowTrayMenu(void)
{
    HMENU m = CreatePopupMenu();
    BOOL running = g_state == ST_ON || g_state == ST_EXTERNAL;
    AppendMenuW(m, MF_STRING, ID_TRAY_TOGGLE, running ? L"GoodbyeDPI'ı Kapat" : L"GoodbyeDPI'ı Aç");
    AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Pencereyi Göster");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Çıkış (GoodbyeDPI da kapanır)");
    SetMenuDefaultItem(m, ID_TRAY_SHOW, FALSE);

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

/* ------------------------------------------------------------------ */
/* Arayüz                                                              */
/* ------------------------------------------------------------------ */

static void UpdateHint(void)
{
    SetWindowTextW(g_hint, kPresets[g_preset].dns_redirect
        ? L"Bu yöntem DNS yönlendirmesi içerir (Yandex DNS 77.88.8.8:1253). Ayrıca DNS ayarı gerekmez."
        : L"Bu yöntem DNS yönlendirmesi yapmaz: Windows DNS ayarınızı değiştirin (ör. 77.88.8.8 veya 1.1.1.1).");
}

static void UpdateUI(BOOL force)
{
    int st;
    if (IsOurGdpiRunning())
        st = ST_ON;
    else if (ScanOtherGdpi(g_pid, FALSE))
        st = ST_EXTERNAL;
    else
        st = ST_OFF;

    if (st == g_state && !force)
        return;
    g_state = st;

    const wchar_t *text, *tip;
    switch (st) {
    case ST_ON:
        text = L"● GoodbyeDPI AÇIK";
        tip = L"GoodbyeDPI: AÇIK";
        break;
    case ST_EXTERNAL:
        text = L"● GoodbyeDPI AÇIK (dışarıdan)";
        tip = L"GoodbyeDPI: AÇIK (başka yerden başlatılmış)";
        break;
    default:
        text = L"● GoodbyeDPI KAPALI";
        tip = L"GoodbyeDPI: KAPALI";
        break;
    }
    SetWindowTextW(g_status, text);
    SetWindowTextW(g_toggle, st == ST_OFF ? L"AÇ" : L"KAPAT");
    InvalidateRect(g_status, NULL, TRUE);
    TrayUpdate(st == ST_OFF ? g_icoOff : g_icoOn, tip);
}

static void ToggleGdpi(void)
{
    UpdateUI(FALSE);
    if (g_state == ST_OFF)
        StartGdpi();
    else
        StopGdpi();
}

static HWND MakeCtl(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, HFONT font)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                             g_hwnd, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static void CreateControls(void)
{
    g_status = MakeCtl(L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE, 16, 12, 328, 36, 0, g_fontBig);

    MakeCtl(L"STATIC", L"Yöntem:", SS_LEFT, 16, 62, 60, 20, 0, g_font);
    g_combo = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 76, 58, 268, 240, ID_COMBO, g_font);
    for (int i = 0; i < PRESET_COUNT; i++)
        SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)kPresets[i].name);
    SendMessageW(g_combo, CB_SETCURSEL, (WPARAM)g_preset, 0);

    g_hint = MakeCtl(L"STATIC", L"", SS_LEFT, 16, 90, 328, 32, 0, g_font);
    UpdateHint();

    g_toggle = MakeCtl(L"BUTTON", L"AÇ", BS_PUSHBUTTON | WS_TABSTOP, 16, 130, 328, 46, ID_TOGGLE, g_fontBtn);

    g_chkAuto = MakeCtl(L"BUTTON", L"Uygulama açılınca GoodbyeDPI'ı otomatik başlat",
                        BS_AUTOCHECKBOX | WS_TABSTOP, 16, 190, 328, 22, ID_CHK_AUTO, g_font);
    SendMessageW(g_chkAuto, BM_SETCHECK, g_autoStart ? BST_CHECKED : BST_UNCHECKED, 0);

    g_chkBoot = MakeCtl(L"BUTTON", L"Windows açılışında bu uygulamayı tepside başlat",
                        BS_AUTOCHECKBOX | WS_TABSTOP, 16, 214, 328, 22, ID_CHK_BOOT, g_font);

    g_info = MakeCtl(L"STATIC",
                     L"Simge durumuna küçültünce tepsiye gizlenir ve çalışmaya devam eder. "
                     L"Pencereyi (X) kapatınca GoodbyeDPI da kapanır.",
                     SS_LEFT, 16, 244, 328, 34, 0, g_font);
}

static void InitFonts(void)
{
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    LOGFONTW lf = ncm.lfMessageFont;
    g_font = CreateFontIndirectW(&lf);

    lf.lfHeight = -MulDiv(15, g_dpi, 72);
    lf.lfWeight = FW_BOLD;
    g_fontBig = CreateFontIndirectW(&lf);

    lf.lfHeight = -MulDiv(12, g_dpi, 72);
    g_fontBtn = CreateFontIndirectW(&lf);
}

static void OnStartupChecks(void)
{
    if (GdpiServiceExists()) {
        int r = MessageBoxW(g_hwnd,
            L"Bilgisayarınızda \"GoodbyeDPI\" Windows hizmeti kurulu.\r\n"
            L"Bu hizmet Windows her açıldığında GoodbyeDPI'ı otomatik başlattığı için "
            L"GoodbyeDPI sürekli açık kalıyor.\r\n\r\n"
            L"Hizmet kaldırılsın mı? (Önerilir)\r\n"
            L"Sonrasında GoodbyeDPI'ı bu uygulamadan istediğiniz zaman açıp kapatabilirsiniz.",
            APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
        if (r == IDYES) {
            BOOL removed = FALSE;
            ControlGdpiService(TRUE, &removed);
            ScanOtherGdpi(0, TRUE);
            if (!removed)
                ErrorBox(L"GoodbyeDPI hizmeti kaldırılamadı. Orijinal paketteki service_remove.cmd dosyasını "
                         L"yönetici olarak çalıştırmayı deneyin.");
        }
    }

    /* Windows açılış görevi: varsa exe yolu değişmiş olabilir, güncelle. */
    if (g_bootTask && BootTaskExists())
        RegisterBootTask();
    else if (g_bootTask) {
        g_bootTask = FALSE; /* görev Windows tarafında silinmiş */
        SaveSettings();
    }
    SendMessageW(g_chkBoot, BM_SETCHECK, g_bootTask ? BST_CHECKED : BST_UNCHECKED, 0);

    UpdateUI(TRUE);
    if (g_autoStart && g_state == ST_OFF)
        StartGdpi();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated) {
        TrayAdd();
        UpdateUI(TRUE);
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        CreateControls();
        TrayAdd();
        UpdateUI(TRUE);
        SetTimer(hwnd, ID_TIMER, 1000, NULL);
        return 0;

    case WM_TIMER:
        if (wp == ID_TIMER) {
            CheckProcess();
            UpdateUI(FALSE);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TOGGLE:
        case ID_TRAY_TOGGLE:
            ToggleGdpi();
            break;
        case ID_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_combo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < PRESET_COUNT && sel != g_preset) {
                    g_preset = sel;
                    SaveSettings();
                    UpdateHint();
                    if (IsOurGdpiRunning()) {
                        StopGdpi();
                        StartGdpi();
                    }
                }
            }
            break;
        case ID_CHK_AUTO:
            g_autoStart = SendMessageW(g_chkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            break;
        case ID_CHK_BOOT: {
            BOOL want = SendMessageW(g_chkBoot, BM_GETCHECK, 0, 0) == BST_CHECKED;
            HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
            BOOL ok = want ? RegisterBootTask() : DeleteBootTask();
            SetCursor(old);
            if (!ok) {
                SendMessageW(g_chkBoot, BM_SETCHECK, want ? BST_UNCHECKED : BST_CHECKED, 0);
                ErrorBox(want ? L"Windows açılış görevi oluşturulamadı."
                              : L"Windows açılış görevi silinemedi.");
            } else {
                g_bootTask = want;
                SaveSettings();
            }
            if (ok && want) {
                MessageBoxW(hwnd,
                    L"Uygulama artık Windows oturumu açıldığında tepside başlayacak.\r\n\r\n"
                    L"GoodbyeDPI'ın da otomatik açılmasını istiyorsanız "
                    L"\"Uygulama açılınca GoodbyeDPI'ı otomatik başlat\" seçeneğini de işaretleyin.\r\n\r\n"
                    L"Not: Bu exe dosyasını başka bir klasöre taşırsanız, yeni yerinden bir kez açmanız yeterli.",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case ID_TRAY_SHOW:
            ShowMainWindow();
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd))
                ShowWindow(hwnd, SW_MINIMIZE);
            else
                ShowMainWindow();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        }
        return 0;

    case WM_APP_SHOW:
        ShowMainWindow();
        return 0;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            ShowWindow(hwnd, SW_HIDE);
            if (!g_balloonShown) {
                g_balloonShown = TRUE;
                TrayBalloon(APP_NAME, L"Tepside çalışmaya devam ediyor. Açıp kapatmak için simgeye sağ tıklayın.");
            }
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        SetBkMode(dc, TRANSPARENT);
        if (ctl == g_status) {
            COLORREF c = g_state == ST_ON ? RGB(22, 163, 74)
                       : g_state == ST_EXTERNAL ? RGB(217, 119, 6)
                       : RGB(220, 38, 38);
            SetTextColor(dc, c);
        } else if (ctl == g_hint || ctl == g_info) {
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        return (LRESULT)g_bgBrush;
    }

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wp)
            StopGdpi();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        StopGdpi();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    (void)prev;
    g_inst = inst;

    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(WND_CLASS, NULL);
        if (other)
            PostMessageW(other, WM_APP_SHOW, 0, 0);
        return 0;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    InitFonts();
    g_bgBrush = GetSysColorBrush(COLOR_WINDOW);

    if (!InitPaths()) {
        MessageBoxW(NULL, L"Uygulama veri klasörü oluşturulamadı.", APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }
    ExtractAll(); /* hata olursa başlatırken tekrar denenir ve bildirilir */
    LoadSettings();

    /* Bu uygulama kapanırsa (çökme dahil) GoodbyeDPI da kapansın. */
    g_job = CreateJobObjectW(NULL, NULL);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        ZeroMemory(&li, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }

    g_icoApp = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    g_icoAppSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    g_icoOn = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_ON), IMAGE_ICON,
                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    g_icoOff = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_OFF), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = g_icoApp;
    wc.hIconSm = g_icoAppSm;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, S(360), S(290) };
    AdjustWindowRect(&rc, style, FALSE);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hwnd = CreateWindowExW(0, WND_CLASS, APP_NAME, style, x, y, w, h, NULL, NULL, inst, NULL);
    if (!hwnd)
        return 1;

    BOOL startInTray = cmdline && (wcsstr(cmdline, L"/tray") || wcsstr(cmdline, L"-tray"));
    if (!startInTray) {
        ShowWindow(hwnd, show == SW_HIDE ? SW_SHOWNORMAL : show);
        UpdateWindow(hwnd);
    }

    OnStartupChecks();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    if (g_job)
        CloseHandle(g_job);
    if (mutex)
        CloseHandle(mutex);
    return (int)m.wParam;
}
