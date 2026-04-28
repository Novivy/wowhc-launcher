#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <ctime>

#pragma comment(lib, "bcrypt.lib")

#include "google_drive.h"

#ifndef GDRIVE_CLIENT_ID
#define GDRIVE_CLIENT_ID ""
#endif
#ifndef GDRIVE_CLIENT_SECRET
#define GDRIVE_CLIENT_SECRET ""
#endif

static constexpr char kClientId[]     = GDRIVE_CLIENT_ID;
static constexpr char kClientSecret[] = GDRIVE_CLIENT_SECRET;

static std::string s_cachedFolderId;

static std::wstring ToWide(const std::string& s)
{
    std::wstring w; w.reserve(s.size());
    for (unsigned char c : s) w += (wchar_t)c;
    return w;
}

// ── Crypto helpers ────────────────────────────────────────────────────────────
static std::string Base64Url(const unsigned char* data, size_t len)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int b = (unsigned int)data[i] << 16;
        if (i+1 < len) b |= (unsigned int)data[i+1] << 8;
        if (i+2 < len) b |= data[i+2];
        out += T[(b >> 18) & 63];
        out += T[(b >> 12) & 63];
        if (i+1 < len) out += T[(b >>  6) & 63];
        if (i+2 < len) out += T[b & 63];
    }
    return out;
}

static bool ComputeSHA256(const unsigned char* in, size_t inLen, unsigned char out[32])
{
    BCRYPT_ALG_HANDLE  hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cb = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                          (PUCHAR)&objLen, sizeof(DWORD), &cb, 0);
        std::vector<BYTE> obj(objLen);
        if (BCryptCreateHash(hAlg, &hHash, obj.data(), objLen, nullptr, 0, 0) == 0) {
            BCryptHashData(hHash, (PUCHAR)in, (ULONG)inLen, 0);
            ok = (BCryptFinishHash(hHash, out, 32, 0) == 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

static void GenRandomBytes(unsigned char* out, size_t len)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RNG_ALGORITHM, nullptr, 0);
    BCryptGenRandom(hAlg, out, (ULONG)len, 0);
    BCryptCloseAlgorithmProvider(hAlg, 0);
}

static std::string UrlEncode(const std::string& s)
{
    std::string out;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~')
            out += (char)c;
        else { out += '%'; out += hex[c>>4]; out += hex[c&0xf]; }
    }
    return out;
}

// ── Tiny JSON field extractor ──────────────────────────────────────────────────
static std::string GJsonStr(const std::string& json, const std::string& key)
{
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos]=='\\' && pos+1 < json.size()) { pos++; val += json[pos]; }
        else val += json[pos];
        pos++;
    }
    return val;
}

static int GJsonInt(const std::string& json, const std::string& key)
{
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    while (pos < json.size() && !isdigit((unsigned char)json[pos])) pos++;
    return atoi(json.c_str() + pos);
}

// ── Token storage ──────────────────────────────────────────────────────────────
struct GToken {
    std::string accessToken;
    std::string refreshToken;
    long long   expiresAt = 0;
};

static std::wstring TokenPath(const std::wstring& configDir)
{
    return configDir + L"\\gdrive_tokens.json";
}

static GToken LoadToken(const std::wstring& configDir)
{
    GToken t;
    std::ifstream f(TokenPath(configDir));
    if (!f.is_open()) return t;
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    t.accessToken  = GJsonStr(json, "access_token");
    t.refreshToken = GJsonStr(json, "refresh_token");
    auto ep = json.find("\"expires_at\"");
    if (ep != std::string::npos) {
        ep = json.find(':', ep) + 1;
        while (ep < json.size() && !isdigit((unsigned char)json[ep])) ep++;
        t.expiresAt = atoll(json.c_str() + ep);
    }
    // Restore cached folder ID so GDrive_HasFolder() works after restart
    std::string fid = GJsonStr(json, "folder_id");
    if (!fid.empty()) s_cachedFolderId = fid;
    return t;
}

static void SaveToken(const std::wstring& configDir, const GToken& t)
{
    std::ofstream f(TokenPath(configDir), std::ios::trunc);
    if (!f.is_open()) return;
    f << "{\"access_token\":\"" << t.accessToken
      << "\",\"refresh_token\":\"" << t.refreshToken
      << "\",\"expires_at\":" << t.expiresAt;
    if (!s_cachedFolderId.empty())
        f << ",\"folder_id\":\"" << s_cachedFolderId << "\"";
    f << "}";
}

// ── WinHTTP helper ─────────────────────────────────────────────────────────────
struct GResp {
    DWORD       status = 0;
    std::string body;
    std::string location;
};

static bool GCrackUrl(const std::wstring& url,
    std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& ssl)
{
    wchar_t hbuf[512]={}, pbuf[4096]={};
    URL_COMPONENTS uc={}; uc.dwStructSize=sizeof(uc);
    uc.lpszHostName=hbuf; uc.dwHostNameLength=512;
    uc.lpszUrlPath=pbuf;  uc.dwUrlPathLength=4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    host=hbuf; path=pbuf; port=uc.nPort;
    ssl=(uc.nScheme==INTERNET_SCHEME_HTTPS);
    return true;
}

static GResp GRequest(const std::wstring& method, const std::wstring& url,
    const std::string& bearer, const std::string& contentType,
    const void* body, DWORD bodyLen,
    const std::wstring& extraHdrs = L"")
{
    GResp resp;
    std::wstring host, path;
    INTERNET_PORT port; bool ssl;
    if (!GCrackUrl(url, host, path, port, ssl)) return resp;

    HINTERNET hS = WinHttpOpen(L"WOWHCLauncher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return resp;

    HINTERNET hC = WinHttpConnect(hS, host.c_str(), port, 0);
    if (!hC) { WinHttpCloseHandle(hS); return resp; }

    DWORD flags = WINHTTP_FLAG_REFRESH | (ssl ? WINHTTP_FLAG_SECURE : 0u);
    HINTERNET hR = WinHttpOpenRequest(hC, method.c_str(), path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return resp; }

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hR, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    std::wstring hdrs = extraHdrs;
    if (!bearer.empty())
        hdrs += L"Authorization: Bearer " + ToWide(bearer) + L"\r\n";
    if (!contentType.empty())
        hdrs += L"Content-Type: " + ToWide(contentType) + L"\r\n";
    if (!hdrs.empty())
        WinHttpAddRequestHeaders(hR, hdrs.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                  (LPVOID)body, bodyLen, bodyLen, 0) != 0;
    ok = ok && (WinHttpReceiveResponse(hR, nullptr) != 0);

    if (ok) {
        DWORD sc=0, sl=sizeof(sc);
        WinHttpQueryHeaders(hR,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sl, WINHTTP_NO_HEADER_INDEX);
        resp.status = sc;

        wchar_t loc[4096]={};
        DWORD ll = sizeof(loc);
        if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX, loc, &ll, WINHTTP_NO_HEADER_INDEX)
                && loc[0]) {
            int n = WideCharToMultiByte(CP_UTF8, 0, loc, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                resp.location.resize((size_t)n - 1);
                WideCharToMultiByte(CP_UTF8, 0, loc, -1,
                    resp.location.data(), n, nullptr, nullptr);
            }
        }
        DWORD avail=0, rd=0;
        while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
            std::vector<char> buf(avail+1);
            WinHttpReadData(hR, buf.data(), avail, &rd);
            resp.body.append(buf.data(), rd);
        }
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return resp;
}

// Streaming PUT for large file uploads
static GResp GUploadFileStream(const std::string& sessionUri,
    const std::wstring& filePath, DWORD64 fileSize,
    GDriveProgressCb onProgress)
{
    GResp resp;
    std::wstring wUri = ToWide(sessionUri);
    std::wstring host, path;
    INTERNET_PORT port; bool ssl;
    if (!GCrackUrl(wUri, host, path, port, ssl)) return resp;

    HINTERNET hS = WinHttpOpen(L"WOWHCLauncher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return resp;

    DWORD timeout = 600000;
    WinHttpSetOption(hS, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hS, WINHTTP_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));

    HINTERNET hC = WinHttpConnect(hS, host.c_str(), port, 0);
    if (!hC) { WinHttpCloseHandle(hS); return resp; }

    DWORD flags = WINHTTP_FLAG_REFRESH | (ssl ? WINHTTP_FLAG_SECURE : 0u);
    HINTERNET hR = WinHttpOpenRequest(hC, L"PUT", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return resp; }

    char rangeBuf[64];
    snprintf(rangeBuf, sizeof(rangeBuf),
             "bytes 0-%llu/%llu", (unsigned long long)(fileSize-1),
             (unsigned long long)fileSize);
    std::wstring rangeW(rangeBuf, rangeBuf + strlen(rangeBuf));
    std::wstring hdrs = L"Content-Type: video/mp4\r\nContent-Range: " + rangeW + L"\r\n";
    WinHttpAddRequestHeaders(hR, hdrs.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return resp;
    }

    if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, (DWORD)fileSize, 0)) {
        CloseHandle(hFile);
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return resp;
    }

    const DWORD CHUNK = 4u * 1024u * 1024u;
    std::vector<char> buf(CHUNK);
    DWORD64 sent = 0;
    DWORD   read = 0, written = 0;
    bool ok = true;
    while (ok && ReadFile(hFile, buf.data(), CHUNK, &read, nullptr) && read > 0) {
        if (!WinHttpWriteData(hR, buf.data(), read, &written)) { ok = false; break; }
        sent += written;
        if (onProgress) onProgress(sent, fileSize);
    }
    CloseHandle(hFile);

    if (ok && WinHttpReceiveResponse(hR, nullptr)) {
        DWORD sc=0, sl=sizeof(sc);
        WinHttpQueryHeaders(hR,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sl, WINHTTP_NO_HEADER_INDEX);
        resp.status = sc;
        DWORD avail=0, rd=0;
        while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
            std::vector<char> b(avail+1);
            WinHttpReadData(hR, b.data(), avail, &rd);
            resp.body.append(b.data(), rd);
        }
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return resp;
}

// ── Token refresh ──────────────────────────────────────────────────────────────
static bool RefreshToken(const std::wstring& configDir, GToken& token)
{
    std::string body =
        "client_id="     + UrlEncode(kClientId)           +
        "&client_secret="+ UrlEncode(kClientSecret)       +
        "&refresh_token="+ UrlEncode(token.refreshToken)  +
        "&grant_type=refresh_token";

    auto resp = GRequest(L"POST", L"https://oauth2.googleapis.com/token",
        "", "application/x-www-form-urlencoded",
        body.c_str(), (DWORD)body.size());

    if (resp.status != 200) return false;
    std::string newAccess = GJsonStr(resp.body, "access_token");
    if (newAccess.empty()) return false;

    token.accessToken = newAccess;
    int ei = GJsonInt(resp.body, "expires_in");
    if (ei <= 0) ei = 3600;
    token.expiresAt = (long long)time(nullptr) + ei - 60;
    SaveToken(configDir, token);
    return true;
}

static std::string LoadResourceBase64(int id)
{
    HRSRC   hRes  = FindResourceW(nullptr, MAKEINTRESOURCEW(id), L"PNG");
    if (!hRes) return {};
    HGLOBAL hGlob = LoadResource(nullptr, hRes);
    if (!hGlob) return {};
    DWORD   size  = SizeofResource(nullptr, hRes);
    const unsigned char* data = (const unsigned char*)LockResource(hGlob);
    if (!data || size == 0) return {};
    static const char B64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (DWORD i = 0; i < size; i += 3) {
        unsigned int b = (unsigned int)data[i] << 16;
        if (i+1 < size) b |= (unsigned int)data[i+1] << 8;
        if (i+2 < size) b |= data[i+2];
        out += B64[(b >> 18) & 63];
        out += B64[(b >> 12) & 63];
        out += (i+1 < size) ? B64[(b >>  6) & 63] : '=';
        out += (i+2 < size) ? B64[b & 63]          : '=';
    }
    return out;
}

// ── OAuth PKCE flow ────────────────────────────────────────────────────────────
static bool DoOAuth(const std::wstring& configDir, GToken& outToken,
                    GDriveStatusCb onStatus)
{
    unsigned char verBytes[64];
    GenRandomBytes(verBytes, 64);
    std::string codeVerifier  = Base64Url(verBytes, 64);
    unsigned char hash[32];
    ComputeSHA256((const unsigned char*)codeVerifier.c_str(),
                  codeVerifier.size(), hash);
    std::string codeChallenge = Base64Url(hash, 32);

    // Start localhost server on random port
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) { WSACleanup(); return false; }

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);

    sockaddr_in actual = {}; int slen = sizeof(actual);
    getsockname(srv, (sockaddr*)&actual, &slen);
    int port = ntohs(actual.sin_port);

    std::string redirectUri = "http://localhost:" + std::to_string(port);

    std::string authUrl =
        "https://accounts.google.com/o/oauth2/v2/auth"
        "?client_id="             + UrlEncode(kClientId) +
        "&redirect_uri="          + UrlEncode(redirectUri) +
        "&response_type=code"
        "&scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.file"
        "&code_challenge="        + codeChallenge +
        "&code_challenge_method=S256"
        "&access_type=offline"
        "&prompt=consent";

    std::wstring authUrlW = ToWide(authUrl);
    ShellExecuteW(nullptr, L"open", authUrlW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if (onStatus) onStatus(L"Waiting for Google sign-in in your browser...");

    fd_set fds; FD_ZERO(&fds); FD_SET(srv, &fds);
    timeval tv = {120, 0};
    if (select(0, &fds, nullptr, nullptr, &tv) <= 0) {
        closesocket(srv); WSACleanup(); return false;
    }

    SOCKET client = accept(srv, nullptr, nullptr);
    closesocket(srv);
    if (client == INVALID_SOCKET) { WSACleanup(); return false; }

    char reqBuf[8192] = {};
    recv(client, reqBuf, sizeof(reqBuf)-1, 0);

    std::string logoB64 = LoadResourceBase64(202);
    std::string html =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
        "<!DOCTYPE html><html><body style='font-family:sans-serif;"
        "text-align:center;padding:60px;background:#16161a;color:#d2d2d7'>";
    if (!logoB64.empty())
        html += "<img src='data:image/png;base64," + logoB64 +
                "' style='width:120px;height:120px;border-radius:50%;"
                "margin-bottom:20px;display:block;margin-left:auto;margin-right:auto'>";
    html += "<h2 style='margin:0 0 12px'>Signed in successfully!</h2>"
            "<p style='color:#888'>You can close this tab and return to the WOW HC Launcher.</p>"
            "</body></html>";
    send(client, html.c_str(), (int)html.size(), 0);
    closesocket(client);
    WSACleanup();

    // Parse authorization code from "GET /?code=...&..."
    std::string req(reqBuf);
    auto pos = req.find("code=");
    if (pos == std::string::npos) return false;
    pos += 5;
    auto end = req.find_first_of("& \r\n", pos);
    std::string authCode = req.substr(pos, end == std::string::npos
                                           ? std::string::npos : end - pos);
    if (authCode.empty()) return false;

    if (onStatus) onStatus(L"Completing Google sign-in...");

    std::string tokenBody =
        "code="          + UrlEncode(authCode)     +
        "&client_id="    + UrlEncode(kClientId)    +
        "&client_secret="+ UrlEncode(kClientSecret)+
        "&redirect_uri=" + UrlEncode(redirectUri)  +
        "&grant_type=authorization_code"
        "&code_verifier="+ codeVerifier;

    auto resp = GRequest(L"POST", L"https://oauth2.googleapis.com/token",
        "", "application/x-www-form-urlencoded",
        tokenBody.c_str(), (DWORD)tokenBody.size());

    if (resp.status != 200) return false;
    outToken.accessToken  = GJsonStr(resp.body, "access_token");
    outToken.refreshToken = GJsonStr(resp.body, "refresh_token");
    if (outToken.accessToken.empty()) return false;

    int ei = GJsonInt(resp.body, "expires_in");
    if (ei <= 0) ei = 3600;
    outToken.expiresAt = (long long)time(nullptr) + ei - 60;
    SaveToken(configDir, outToken);
    return true;
}

static bool EnsureToken(const std::wstring& configDir, GToken& token,
                        GDriveStatusCb onStatus)
{
    token = LoadToken(configDir);
    if (!token.accessToken.empty()) {
        if ((long long)time(nullptr) < token.expiresAt) return true;
        if (onStatus) onStatus(L"Refreshing Google Drive token...");
        if (RefreshToken(configDir, token)) return true;
    }
    return DoOAuth(configDir, token, onStatus);
}

// ── Drive: ensure folder ──────────────────────────────────────────────────────
static std::string EnsureFolder(const std::string& accessToken,
                                 GDriveStatusCb onStatus)
{
    if (!s_cachedFolderId.empty()) return s_cachedFolderId;

    std::string q = "name = 'WOW HC Replays' "
                    "and mimeType = 'application/vnd.google-apps.folder' "
                    "and trashed = false";
    std::wstring searchUrl =
        std::wstring(L"https://www.googleapis.com/drive/v3/files?q=") +
        ToWide(UrlEncode(q)) +
        L"&fields=files(id)&spaces=drive";

    auto resp = GRequest(L"GET", searchUrl, accessToken, "", nullptr, 0);
    if (resp.status == 200) {
        std::string id = GJsonStr(resp.body, "id");
        if (!id.empty()) { s_cachedFolderId = id; return id; }
    }

    if (onStatus) onStatus(L"Creating 'WOW HC Replays' folder...");
    std::string body =
        "{\"name\":\"WOW HC Replays\","
        "\"mimeType\":\"application/vnd.google-apps.folder\"}";
    auto cr = GRequest(L"POST",
        L"https://www.googleapis.com/drive/v3/files",
        accessToken, "application/json", body.c_str(), (DWORD)body.size());
    if (cr.status == 200 || cr.status == 201) {
        s_cachedFolderId = GJsonStr(cr.body, "id");
        return s_cachedFolderId;
    }
    return {};
}

// ── Drive: initiate resumable upload ─────────────────────────────────────────
static std::string InitiateResumable(const std::string& accessToken,
    const std::string& fileName, const std::string& folderId, DWORD64 fileSize)
{
    std::string meta =
        "{\"name\":\"" + fileName + "\","
        "\"parents\":[\"" + folderId + "\"]}";

    char szBuf[32]; snprintf(szBuf, sizeof(szBuf), "%llu",
                             (unsigned long long)fileSize);
    std::wstring extra =
        std::wstring(L"X-Upload-Content-Type: video/mp4\r\n"
                     L"X-Upload-Content-Length: ") +
        std::wstring(szBuf, szBuf + strlen(szBuf)) + L"\r\n";

    auto resp = GRequest(L"POST",
        L"https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable",
        accessToken, "application/json; charset=UTF-8",
        meta.c_str(), (DWORD)meta.size(), extra);

    return (resp.status == 200) ? resp.location : std::string{};
}

// ── Drive: set anyone-with-link permission ────────────────────────────────────
static bool SetAnyoneCanView(const std::string& accessToken,
                              const std::string& fileId)
{
    std::string body = "{\"role\":\"reader\",\"type\":\"anyone\"}";
    std::wstring url =
        std::wstring(L"https://www.googleapis.com/drive/v3/files/") +
        ToWide(fileId) +
        L"/permissions";
    auto resp = GRequest(L"POST", url, accessToken, "application/json",
                         body.c_str(), (DWORD)body.size());
    return (resp.status == 200 || resp.status == 201);
}

// ── Drive: get web view link ──────────────────────────────────────────────────
static std::wstring GetWebViewLink(const std::string& accessToken,
                                    const std::string& fileId)
{
    std::wstring url =
        std::wstring(L"https://www.googleapis.com/drive/v3/files/") +
        ToWide(fileId) +
        L"?fields=webViewLink";
    auto resp = GRequest(L"GET", url, accessToken, "", nullptr, 0);
    if (resp.status != 200) return {};
    return ToWide(GJsonStr(resp.body, "webViewLink"));
}

// ── Public API ─────────────────────────────────────────────────────────────────
bool GDrive_HasFolder()
{
    return !s_cachedFolderId.empty();
}

std::wstring GDrive_GetFolderUrl()
{
    if (!s_cachedFolderId.empty())
        return L"https://drive.google.com/drive/folders/" +
               ToWide(s_cachedFolderId);
    return L"https://drive.google.com/drive/my-drive";
}

void GDrive_Disconnect(const std::wstring& configDir)
{
    DeleteFileW(TokenPath(configDir).c_str());
    s_cachedFolderId.clear();
}

bool GDrive_HasToken(const std::wstring& configDir)
{
    GToken t = LoadToken(configDir);
    return !t.accessToken.empty() && !t.refreshToken.empty();
}

bool GDrive_HasClientId()
{
    return kClientId[0] != '\0';
}

GDriveResult GDrive_Upload(const std::wstring& filePath,
                            const std::wstring& configDir,
                            GDriveProgressCb    onProgress,
                            GDriveStatusCb      onStatus)
{
    GDriveResult result;

    if (!GDrive_HasClientId()) {
        result.error    = GDriveError::ApiError;
        result.errorMsg = L"Google Drive is not configured in this build.";
        return result;
    }

    // Get file size
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fa)) {
        result.error    = GDriveError::ApiError;
        result.errorMsg = L"Cannot access the selected file.";
        return result;
    }
    DWORD64 fileSize = ((DWORD64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;

    // Ensure valid token
    GToken token;
    if (!EnsureToken(configDir, token, onStatus)) {
        result.error    = GDriveError::AuthFailed;
        result.errorMsg = L"Google sign-in failed or timed out.";
        return result;
    }

    // Find or create upload folder
    if (onStatus) onStatus(L"Checking Google Drive folder...");
    std::string folderId = EnsureFolder(token.accessToken, onStatus);
    if (folderId.empty()) {
        result.error    = GDriveError::ApiError;
        result.errorMsg = L"Could not create 'WOW HC Replays' folder in Google Drive.";
        return result;
    }
    // Persist folder ID so GDrive_HasFolder() returns true on next launch
    SaveToken(configDir, token);

    // Extract filename (narrow)
    std::wstring wName = filePath.substr(filePath.rfind(L'\\') + 1);
    std::string fileName;
    fileName.reserve(wName.size());
    for (wchar_t c : wName) fileName += (char)c;

    // Initiate resumable upload session
    if (onStatus) onStatus(L"Starting upload...");
    std::string sessionUri = InitiateResumable(token.accessToken,
                                               fileName, folderId, fileSize);
    if (sessionUri.empty()) {
        result.error    = GDriveError::NetworkError;
        result.errorMsg = L"Could not initiate upload session. Check your connection.";
        return result;
    }

    // Stream file
    if (onStatus) onStatus(L"Uploading...");
    auto uploadResp = GUploadFileStream(sessionUri, filePath, fileSize, onProgress);
    if (uploadResp.status != 200 && uploadResp.status != 201) {
        result.error    = GDriveError::NetworkError;
        result.errorMsg = L"Upload failed (HTTP " +
                          std::to_wstring(uploadResp.status) + L").";
        return result;
    }

    std::string fileId = GJsonStr(uploadResp.body, "id");
    if (fileId.empty()) {
        result.error    = GDriveError::ApiError;
        result.errorMsg = L"Upload succeeded but could not get file ID.";
        return result;
    }

    // Set public sharing
    if (onStatus) onStatus(L"Setting share permissions...");
    SetAnyoneCanView(token.accessToken, fileId);

    // Get share link
    result.shareLink = GetWebViewLink(token.accessToken, fileId);
    if (result.shareLink.empty()) {
        // Fallback: construct link manually
        result.shareLink = L"https://drive.google.com/file/d/" +
                           ToWide(fileId) +
                           L"/view?usp=sharing";
    }

    return result;
}
