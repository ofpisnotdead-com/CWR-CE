
#include <ctype.h>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Framework/LogFlags.hpp>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/select.h>
#endif
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/time.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/GlobalAlive.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <curl/curl.h>
#include <mutex>
#include <vector>
#if defined _WIN32
#include <winInet.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#endif

#define BUFFER_SIZE 256
#define AGENT_NAME "BI Agent"

namespace
{
struct CurlDownloadBuffer
{
    std::vector<char>* out;
    size_t maxSize;
    bool exceeded;
};

enum class CurlDownloadStatus
{
    Failed,
    Succeeded,
    Rejected
};

size_t WriteCurlDownloadChunk(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* buffer = static_cast<CurlDownloadBuffer*>(userdata);
    const size_t bytes = size * nmemb;
    if (size != 0 && bytes / size != nmemb)
    {
        buffer->exceeded = true;
        return 0;
    }
    if (buffer->maxSize > 0 && (buffer->out->size() > buffer->maxSize || bytes > buffer->maxSize - buffer->out->size()))
    {
        buffer->exceeded = true;
        return 0;
    }
    buffer->out->insert(buffer->out->end(), ptr, ptr + bytes);
    return bytes;
}

CurlDownloadStatus DownloadFileWithCurl(const char* url, const char* proxyServer, size_t maxSize,
                                        std::vector<char>& out)
{
    out.clear();
    if (url == nullptr || url[0] == 0)
    {
        return CurlDownloadStatus::Failed;
    }

    static std::once_flag curlInitFlag;
    std::call_once(curlInitFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        return CurlDownloadStatus::Failed;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, AGENT_NAME);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CurlDownloadBuffer buffer{&out, maxSize, false};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCurlDownloadChunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    if (proxyServer != nullptr && proxyServer[0] != 0)
    {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxyServer);
    }
    if (const char* caFile = std::getenv("SSL_CERT_FILE"); caFile != nullptr && caFile[0] != 0)
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caFile);
    }

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    if (result == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    }
    else if (buffer.exceeded)
    {
        LOG_WARN(Network, "XML download '{}' exceeded max size {}", url, maxSize);
    }
    else
    {
        LOG_WARN(Network, "XML download '{}' failed: {}", url, curl_easy_strerror(result));
    }

    curl_easy_cleanup(curl);
    if (result != CURLE_OK || statusCode < 200 || statusCode >= 300)
    {
        if (result == CURLE_OK)
        {
            LOG_WARN(Network, "XML download '{}' returned HTTP {}", url, statusCode);
        }
        out.clear();
        return buffer.exceeded ? CurlDownloadStatus::Rejected : CurlDownloadStatus::Failed;
    }
    if (maxSize > 0 && out.size() > maxSize)
    {
        LOG_WARN(Network, "XML download '{}' exceeded max size {} > {}", url, out.size(), maxSize);
        out.clear();
        return CurlDownloadStatus::Rejected;
    }
    return CurlDownloadStatus::Succeeded;
}
} // namespace

#if defined _WIN32

// HTTP/FTP source wrapped as a stream.
class QIHTTPStream
{
  private:
    HMODULE _library;
    HINTERNET _hSession;
    HINTERNET _hFile;
    char _buffer[BUFFER_SIZE];
    DWORD _pos, _len;
    bool _fail, _eof;

  public:
    QIHTTPStream()
    {
        _pos = 0;
        _len = 0;
        _hSession = nullptr;
        _hFile = nullptr;
        _fail = false;
        _eof = false;
        _library = LoadLibrary("winInet.dll");
    }
    ~QIHTTPStream()
    {
        close();
        FreeLibrary(_library);
    }

    // Open an HTTP/FTP URL. proxyServer is "address:port" or null.
    void open(const char* url, const char* proxyServer);
    void close();
    // Read one char; -1 on error/EOF.
    int get();
    void unget();

    bool fail() const { return _fail; }
    bool eof() const { return _eof; }
};

// winInet entry points resolved at runtime via GetProcAddress.
typedef HINTERNET(CALLBACK* INTERNET_OPEN)(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
typedef BOOL(CALLBACK* INTERNET_CANONICALIZE_URL)(LPCSTR, LPSTR, LPDWORD, DWORD);
typedef BOOL(CALLBACK* INTERNET_CLOSE_HANDLE)(HINTERNET);
typedef BOOL(CALLBACK* INTERNET_READ_FILE)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef HINTERNET(CALLBACK* INTERNET_OPEN_URL)(HINTERNET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD);

// Note: use async reading instead.
void QIHTTPStream::open(const char* url, const char* proxyServer)
{
// GetProcAddress returns generic FARPROC, needs casting to specific function types
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"

#ifdef NET_LOG_HTTP
#ifdef NET_LOG_BRIEF
    NetLog("HTTP('%s','%s')", url, proxyServer ? proxyServer : "<direct>");
#else
    NetLog("Fetching URL: '%s' via proxy: %s", url, proxyServer ? proxyServer : "<direct>");
#endif
#endif

    if (!_library)
    {
        _fail = true;
        return;
    }

    char proxyOverride[256] = "<local>";
    if (proxyServer && !proxyServer[0])
    {
        proxyServer = nullptr;
    }
    if (!proxyServer)
    {
        // retrieve proxy from registry
        HKEY key;
        BYTE bufferServer[256];
        BYTE bufferOverride[256];
        DWORD sizeServer = sizeof(bufferServer);
        DWORD sizeOverride = sizeof(bufferOverride);
        if (::RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0,
                           KEY_READ, &key) == ERROR_SUCCESS)
        {
            if (::RegQueryValueEx(key, "ProxyServer", nullptr, nullptr, bufferServer, &sizeServer) == ERROR_SUCCESS)
            {
                proxyServer = (char*)bufferServer;
            }
            if (::RegQueryValueEx(key, "ProxyOverride", nullptr, nullptr, bufferOverride, &sizeOverride) ==
                ERROR_SUCCESS)
            {
                lstrcpynA(proxyOverride, reinterpret_cast<LPCSTR>(bufferOverride), sizeof(proxyOverride));
            }
            ::RegCloseKey(key);
        }
    }

    INTERNET_OPEN internetOpen = (INTERNET_OPEN)GetProcAddress(_library, "InternetOpenA");
    if (!internetOpen)
    {
        _fail = true;
        return;
    }
    const DWORD accessType = proxyServer ? INTERNET_OPEN_TYPE_PROXY : INTERNET_OPEN_TYPE_PRECONFIG;
    _hSession = internetOpen(AGENT_NAME, accessType, proxyServer, proxyServer ? proxyOverride : nullptr, 0);
    if (!_hSession)
    {
        _fail = true;
        DWORD error = GetLastError();
        LOG_DEBUG(Core, "InternetOpen: Error {:x}", error);
        return;
    }

    INTERNET_CANONICALIZE_URL internetCanonicalizeUrl =
        (INTERNET_CANONICALIZE_URL)GetProcAddress(_library, "InternetCanonicalizeUrlA");
    if (!internetCanonicalizeUrl)
    {
        _fail = true;
        return;
    }
    char name[256];
    DWORD size = 256;
    if (!internetCanonicalizeUrl(url, name, &size, 0))
    {
        _fail = true;
        DWORD error = GetLastError();
        LOG_DEBUG(Core, "InternetCanonicalizeUrl: Error {:x}", error);
        return;
    }

#ifdef NET_LOG_HTTP
#ifdef NET_LOG_BRIEF
    NetLog("HTTP:can('%s',%u)", name, (unsigned)size);
#else
    NetLog("Canonical URL: '%s' should be %u chars", name, (unsigned)size);
#endif
#endif

    INTERNET_OPEN_URL internetOpenUrl = (INTERNET_OPEN_URL)GetProcAddress(_library, "InternetOpenUrlA");
    if (!internetOpenUrl)
    {
        _fail = true;
        return;
    }
    _hFile = internetOpenUrl(_hSession, url, nullptr, -1, INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD, 0);
    if (!_hFile)
    {
        _fail = true;
        DWORD error = GetLastError();
        LOG_DEBUG(Core, "InternetOpenUrl: Error {:x}", error);
        return;
    }
#ifdef NET_LOG_HTTP
#ifdef NET_LOG_BRIEF
    NetLog("HTTP:ok");
#else
    NetLog("OK opening URL");
#endif
#endif
}

void QIHTTPStream::close()
{
    if (!_library)
    {
        return;
    }

    INTERNET_CLOSE_HANDLE internetCloseHandle = (INTERNET_CLOSE_HANDLE)GetProcAddress(_library, "InternetCloseHandle");
    if (!internetCloseHandle)
    {
        return;
    }

    if (_hFile)
    {
        internetCloseHandle(_hFile);
        _hFile = nullptr;
    }
    if (_hSession)
    {
        internetCloseHandle(_hSession);
        _hSession = nullptr;
    }
}

int QIHTTPStream::get()
{
    if (_pos >= _len)
    {
        if (_eof || _fail)
        {
            return EOF;
        }
        if (!_library)
        {
            _fail = true;
            return EOF;
        }
        INTERNET_READ_FILE internetReadFile = (INTERNET_READ_FILE)GetProcAddress(_library, "InternetReadFile");
        if (!internetReadFile)
        {
            _fail = true;
            return EOF;
        }
        if (!internetReadFile(_hFile, _buffer, BUFFER_SIZE, &_len))
        {
            _fail = true;
            DWORD error = GetLastError();
            LOG_DEBUG(Core, "InternetReadFile: Error {:x}", error);
            return EOF;
        }
        if (_len == 0)
        {
            _eof = true;
            return EOF;
        }
        _pos = 0;
    }
    return (unsigned char)_buffer[_pos++];
}

#pragma clang diagnostic pop

void QIHTTPStream::unget()
{
    if (_pos == 0)
    {
        Fail("No char to unget");
    }
    else
    {
        _pos--;
    }
}

#else

#define HTTP_LOG
#define HTTP_SUPER_LOG

// HTTP/FTP source wrapped as a stream.
class QIHTTPStream
{
  protected:
    char* ptr;
    char* beginPtr;
    char* endPtr;

  public:
    bool ok;

    QIHTTPStream();

    ~QIHTTPStream();

    // Open an HTTP/FTP URL (FTP via proxy only). proxyServer is "address:port" or null.
    void open(const char* url, const char* proxyServer);

    void close();

    // Pointer to the downloaded data; sets size.
    const unsigned char* getData(unsigned& size);

    // Read one char; -1 on error/EOF.
    int get();

    void unget();

    bool fail() const { return !ok; }
    bool eof() const { return (ptr >= endPtr); }
};

QIHTTPStream::QIHTTPStream()
{
    ok = false;
    ptr = beginPtr = endPtr = nullptr;
}

QIHTTPStream::~QIHTTPStream()
{
    close();
}

static bool httpInt = false;

// SIGPIPE handler (connection refused).
void handlePipe(int sig)
{
    httpInt = true;
}

void QIHTTPStream::open(const char* url, const char* proxyServer)
{
    close();
    if (!url || !url[0])
        return;
    LOG_DEBUG(Core, "Fetching URL: '{}' via proxy: {}", url, proxyServer ? proxyServer : "<direct>");
#ifdef NET_LOG_HTTP
#ifdef NET_LOG_BRIEF
    NetLog("HTTP('%s','%s')", url, proxyServer ? proxyServer : "<direct>");
#else
    NetLog("Fetching URL: '%s' via proxy: %s", url, proxyServer ? proxyServer : "<direct>");
#endif
#endif
    struct sockaddr_in server; // [host:port] a connection will be established to
    memset(&server, 0, sizeof(server));
    server.sin_port = htons(80);
    char localHost[512]; // host to be asked
    char localUrl[512];  // requested URL
    char* port;
    struct hostent* hos; // resolved host-name
    if (proxyServer && proxyServer[0])
    {
        // I'll determine connecting host first:
        port = (char*)strchr(proxyServer, ':');
        if (port)
        { // colon found!
            if (isdigit(port[1]))
                server.sin_port = htons(atoi(port + 1));
            port[0] = (char)0;
            hos = gethostbyname(proxyServer);
            port[0] = ':';
        }
        else
        { // proxy-port should be 80
            hos = gethostbyname(proxyServer);
        }
        if (!hos)
            return;
        memcpy((char*)&server.sin_addr, (char*)hos->h_addr, hos->h_length);
        // and then check the URL-format:
        if (memcmp(url, "http://", 7) && memcmp(url, "ftp://", 6))
            snprintf(localUrl, sizeof(localUrl), "http://%s", url);
        else
            strncpy(localUrl, url, sizeof(localUrl));
        localUrl[sizeof(localUrl) - 1] = (char)0;
    }
    else
    { // direct connection to a HTTP server
        const char* host = strchr(url, ':');
        if (!host)
            host = url;
        else
        {
            if (host[1] == '/' && host[2] == '/')
                host += 3;
            else
                host = url;
        }
        port = (char*)strchr(host, ':');
        char* absolute = (char*)strchr(host, '/');
        if (port && (!absolute || port < absolute) && isdigit(port[1]))
        {
            server.sin_port = htons(atoi(port + 1));
            memcpy(localHost, host, port - host);
            localHost[port - host] = (char)0;
        }
        else
        { // no colon!
            if (absolute)
            {
                memcpy(localHost, host, absolute - host);
                localHost[absolute - host] = (char)0;
            }
            else
                snprintf(localHost, sizeof(localHost), "%s", (const char*)host);
        }
        hos = gethostbyname(localHost);
        if (!hos)
            return;
        memcpy((char*)&server.sin_addr, (char*)hos->h_addr, hos->h_length);
        if (absolute)
        {
            strncpy(localUrl, absolute, sizeof(localUrl));
            localUrl[sizeof(localUrl) - 1] = (char)0;
        }
        else
            snprintf(localUrl, sizeof(localUrl), "%s", (const char*)"/");
    }

#ifdef HTTP_LOG
    LOG_DEBUG(Core, "HTTP server: {:08x}:{} ({}), url: '{}'", ntohl(*((unsigned*)&server.sin_addr)),
              ntohs(server.sin_port), (unsigned)server.sin_family, localUrl);
#endif
#ifdef NET_LOG_HTTP
#ifdef NET_LOG_BRIEF
    NetLog("HTTP(%08x:%u,%u,'%s')", ntohl(*((unsigned*)&server.sin_addr)), ntohs(server.sin_port),
           (unsigned)server.sin_family, localUrl);
#else
    NetLog("HTTP server: %08x:%u (%u), url: '%s'", ntohl(*((unsigned*)&server.sin_addr)), ntohs(server.sin_port),
           (unsigned)server.sin_family, localUrl);
#endif
#endif

    // handle the SIGPIPE (for connection-refuse):
    signal(SIGPIPE, handlePipe);
    httpInt = false;
    // make the HTTP request:
    char req[4096];
    int sock, i;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        LOG_DEBUG(Core, "HTTP: Cannot get a socket!");
        return;
    }
    server.sin_family = AF_INET;
    if (fcntl(sock, F_SETFL, FNDELAY) < 0)
    {
        LOG_DEBUG(Core, "HTTP: Cannot set FNDELAY mode!");
        ::close(sock);
        return;
    }
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0 && // initiate the connection
        errno != EINPROGRESS)
    {
        LOG_DEBUG(Core, "HTTP: Connection refused (errno={})!", errno);
        ::close(sock);
        return;
    }

    // check the timeout:
    struct timeval ti;
    ti.tv_sec = 5;
    ti.tv_usec = 0; // are 5 seconds enough?
    fd_set mask;
    FD_ZERO(&mask);
    FD_SET(sock, &mask);
    if (!select(sock + 1, nullptr, &mask, nullptr, &ti))
    {
        LOG_DEBUG(Core, "HTTP: Connection timeout!");
        ::close(sock);
        return;
    }
    if (fcntl(sock, F_SETFL, 0) < 0)
    {
        LOG_DEBUG(Core, "HTTP: Cannot set FDELAY mode!");
        ::close(sock);
        return;
    }

    // assembly the request string:
    snprintf(req, sizeof(req), "%s", (const char*)"GET ");
    ptr = req + 4;
    port = localUrl;
    while (port[0] && ptr < req + sizeof(req))
        if (port[0] == ' ' || port[0] == '<' || port[0] == '>' || port[0] == '"' || port[0] == '#' || port[0] == '%' ||
            port[0] == '{' || port[0] == '}' || port[0] == '|' || port[0] == '\\' || port[0] == '^' || port[0] == '~' ||
            port[0] == '[' || port[0] == ']' || port[0] == '`')
        { // percent-escape
            ptr += snprintf(ptr, req + sizeof(req) - ptr, "%%%02X", (unsigned)port[0]);
            port++;
        }
        else
            *ptr++ = *port++;
    if (proxyServer && proxyServer[0])
        snprintf(ptr, req + sizeof(req) - ptr,
                 " HTTP/1.0\r\n"
                 "User-Agent: BI Agent/1.99\r\n"
                 "\r\n");
    else
        snprintf(ptr, req + sizeof(req) - ptr,
                 " HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "User-Agent: BI Agent/1.99\r\n"
                 "\r\n",
                 localHost);
    req[sizeof(req) - 1] = (char)0;
    int msgLen = strlen(req);
#ifdef HTTP_SUPER_LOG
    LOG_DEBUG(Core, "HTTP request: {}", req);
#endif

    // send the request string:
    int sent;
    ptr = req;
    while (msgLen)
    {
        ti.tv_sec = 5;
        ti.tv_usec = 0; // are 5 seconds enough?
        FD_ZERO(&mask);
        FD_SET(sock, &mask);
        if (!select(sock + 1, nullptr, &mask, nullptr, &ti))
        {
            LOG_DEBUG(Core, "HTTP: Error sending data (timeout)!");
            ::close(sock);
            return;
        }
        sent = send(sock, ptr, msgLen, 0);
        if (sent < 0 || httpInt)
        {
            LOG_DEBUG(Core, "HTTP: Error sending data (errno={})!", errno);
            ::close(sock);
            return;
        }
        ptr += sent;
        msgLen -= sent;
    }

    // ... and receive the response:
    ptr = req;
    int left = sizeof(req);
    endPtr = nullptr;
    int errCounter = 12;
    do
    {
        ti.tv_sec = 8;
        ti.tv_usec = 0; // 8 seconds ... to be sure (for proxy-caches)
        FD_ZERO(&mask);
        FD_SET(sock, &mask);
        if (!select(sock + 1, &mask, nullptr, nullptr, &ti) || httpInt)
        {
            LOG_DEBUG(Core, "HTTP: Error receiving data (timeout)!");
            ::close(sock);
            ptr = endPtr = nullptr;
            return;
        }
        msgLen = recv(sock, ptr, left, 0); // receive at least the response header
        if (msgLen < 0)
        {
            LOG_DEBUG(Core, "HTTP: Error receiving response header ({})!", errno);
            if (--errCounter < 0)
                break;
        }
        else
        {
            ptr += msgLen;
            left -= msgLen;
        }
    } while (!(endPtr = strstr(req, "\r\n\r\n")) && left > 0);

    // look for "Content-Length:" header:
    if (endPtr)
        endPtr[0] = (char)0;
    else
    {
        LOG_DEBUG(Core, "HTTP: Response header is too long!");
        ::close(sock);
        ptr = endPtr = nullptr;
        return;
    }
    port = strcasestr(req, "\r\nContent-length:");
    if (!port)
    {
        LOG_DEBUG(Core, "HTTP: Missing Content-length field (header len={})!", endPtr - req);
#ifdef HTTP_SUPER_LOG
        LOG_DEBUG(Core, "{}", req);
#endif
        ::close(sock);
        ptr = endPtr = nullptr;
        return;
    }

    // prepare the data-array:
    int totalLen = atoi(port + 17);
    endPtr += 4;
    beginPtr = (char*)malloc(totalLen);
    memcpy(beginPtr, endPtr, ptr - endPtr);
    ptr = beginPtr + (ptr - endPtr);
    endPtr = beginPtr + totalLen;
    left = endPtr - ptr;

    // read the rest of data:
    errCounter = 12;
    do
    {
        ti.tv_sec = 5;
        ti.tv_usec = 0; // are 5 seconds enough?
        FD_ZERO(&mask);
        FD_SET(sock, &mask);
        if (!select(sock + 1, &mask, nullptr, nullptr, &ti))
        {
            LOG_DEBUG(Core, "HTTP: Truncating data to {} bytes", ptr - beginPtr);
            endPtr = ptr;
            break;
        }
        msgLen = recv(sock, ptr, left, 0); // receive rest of the data
        if (msgLen < 0)
        {
            LOG_DEBUG(Core, "HTTP: Error receiving data ({})!", errno);
            if (--errCounter < 0)
            {
                endPtr = ptr;
                break;
            }
        }
        else
        {
            ptr += msgLen;
            left -= msgLen;
        }
    } while (left);

    ::close(sock);
    LOG_DEBUG(Core, "HTTP: OK loading URL '{}' ({} bytes)", url, endPtr - beginPtr);
#ifdef NET_LOG_HTTP
#ifdef NET_LOG_BRIEF
    NetLog("HTTP('%s',%u)", url, endPtr - beginPtr);
#else
    NetLog("HTTP: OK loading URL '%s' (%u bytes)", url, endPtr - beginPtr);
#endif
#endif

    ok = true;
    ptr = beginPtr;
}

void QIHTTPStream::close()
{
    if (beginPtr)
        free(beginPtr);
    ptr = beginPtr = endPtr = nullptr;
    ok = false;
}

const unsigned char* QIHTTPStream::getData(unsigned& size)
{
    if (!ok || !beginPtr)
        return nullptr;
    size = endPtr - beginPtr;
#ifdef HTTP_LOG
    LOG_DEBUG(Core, "HTTP-getData: {} bytes", size);
#endif
    return (const unsigned char*)beginPtr;
}

int QIHTTPStream::get()
{
    if (!ok || !ptr || ptr >= endPtr)
        return -1;
    return *ptr++;
}

void QIHTTPStream::unget()
{
    if (beginPtr && ptr > beginPtr)
        ptr--;
}

#endif

#ifdef _WIN32

class BufferedWrite
{
    enum
    {
        BufferSize = 1024
    };
    char _buffer[BufferSize];
    int _used;
    HANDLE _handle;

  public:
    explicit BufferedWrite(HANDLE handle)
    {
        _handle = handle;
        _used = 0;
    }
    ~BufferedWrite() { Flush(); }
    int Put(char c)
    {
        int err = 0;
        if (_used >= BufferSize)
        {
            err = Flush();
        }
        _buffer[_used++] = c;
        return err;
    }
    int Flush()
    {
        if (_used <= 0)
        {
            return 0;
        }
        DWORD w;
        WriteFile(_handle, _buffer, _used, &w, nullptr);
        int ret = (_used == static_cast<int>(w) ? 0 : -1);
        _used = 0;
        return ret;
    }
};

bool DownloadFile(const char* url, const char* filename, const char* proxyServer, size_t maxSize)
{
    std::vector<char> payload;
    const CurlDownloadStatus curlStatus = DownloadFileWithCurl(url, proxyServer, maxSize, payload);
    if (curlStatus == CurlDownloadStatus::Succeeded)
    {
        LocalPath(fn, filename);
        FILE* file = fopen(fn, "wb");
        if (file == nullptr)
        {
            return false;
        }
        const size_t written = fwrite(payload.data(), 1, payload.size(), file);
        fclose(file);
        if (written != payload.size())
        {
            DeleteFile(filename);
            return false;
        }
        return true;
    }
    if (curlStatus == CurlDownloadStatus::Rejected)
    {
        return false;
    }

    QIHTTPStream in;
    in.open(url, proxyServer);
    // On failure, don't create the file.
    if (in.fail())
    {
        return false;
    }
    HANDLE out = CreateFile(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    BufferedWrite buf(out);
    bool ok = true;
    size_t downloaded = 0;
    int c = in.get();
    while (!in.eof() && !in.fail())
    {
        if (maxSize > 0 && downloaded >= maxSize)
        {
            ok = false;
            break;
        }
        if (buf.Put(c) < 0)
        {
            ok = false;
            break;
        }
        ++downloaded;
        c = in.get();
    }
    buf.Flush();
    CloseHandle(out);
    if (in.fail())
    {
        ok = false;
    }
    if (!ok)
    {
        DeleteFile(filename);
    }

    return ok;
}

#else

bool DownloadFile(const char* url, const char* filename, const char* proxyServer, size_t maxSize)
{
    std::vector<char> payload;
    const CurlDownloadStatus curlStatus = DownloadFileWithCurl(url, proxyServer, maxSize, payload);
    if (curlStatus == CurlDownloadStatus::Succeeded)
    {
        LocalPath(fn, filename);
        FILE* file = fopen(fn, "wb");
        if (file == nullptr)
        {
            return false;
        }
        const size_t written = fwrite(payload.data(), 1, payload.size(), file);
        fclose(file);
        if (written != payload.size())
        {
            ::unlink(filename);
            return false;
        }
        return true;
    }
    if (curlStatus == CurlDownloadStatus::Rejected)
    {
        return false;
    }

    QIHTTPStream in;
    in.open(url, proxyServer);
    if (in.fail())
        return false;
    LocalPath(fn, filename);
    int file = ::open(fn, O_CREAT | O_WRONLY | O_TRUNC, S_IREAD | S_IWRITE);
    if (!file)
        return false;
    unsigned size;
    const unsigned char* data = in.getData(size);
    if (!data)
    {
        ::close(file);
        return false;
    }
    if (maxSize > 0 && size > maxSize)
    {
        ::close(file);
        ::unlink(filename);
        return false;
    }
    int sizeWritten = ::write(file, data, size);
    ::close(file);
    return (sizeWritten == size);
}

#endif

// Returns memory acquired by GlobalAlloc; caller must GlobalFree it when done.

#ifdef _WIN32

char* DownloadFile(const char* url, size_t& size, const char* proxyServer, size_t maxSize)
{
    size = 0;
    QIHTTPStream in;
    in.open(url, proxyServer);
    // On failure, don't allocate.
    if (in.fail())
    {
        return nullptr;
    }
    int allocated = 64 * 1024;
    int usedSize = 0;
    char* mem = (char*)GlobalAlloc(GMEM_FIXED, allocated);
    if (mem == nullptr)
    {
        return nullptr;
    }
    int c = in.get();
    while (!in.eof() && !in.fail())
    {
        if (maxSize > 0 && static_cast<size_t>(usedSize) >= maxSize)
        {
            GlobalFree(mem);
            return nullptr;
        }
        if (usedSize >= allocated)
        {
            char* newMem = (char*)GlobalAlloc(GMEM_FIXED, allocated * 2);
            if (!newMem)
            {
                GlobalFree(mem);
                return nullptr;
            }
            memcpy(newMem, mem, usedSize);
            GlobalFree(mem);
            mem = newMem;
            allocated *= 2;
        }
        mem[usedSize++] = c;
        c = in.get();
    }
    if (in.fail())
    {
        GlobalFree(mem);
        return nullptr;
    }
    size = usedSize;
    return mem;
}

#else

char* DownloadFile(const char* url, size_t& size, const char* proxyServer, size_t maxSize)
{
    std::vector<char> payload;
    const CurlDownloadStatus curlStatus = DownloadFileWithCurl(url, proxyServer, maxSize, payload);
    if (curlStatus == CurlDownloadStatus::Succeeded)
    {
        size = payload.size();
        char* copy = static_cast<char*>(malloc(size));
        if (copy)
            memcpy(copy, payload.data(), size);
        return copy;
    }
    if (curlStatus == CurlDownloadStatus::Rejected)
    {
        size = 0;
        return nullptr;
    }

    QIHTTPStream in;
    in.open(url, proxyServer);
    size = 0;
    if (in.fail())
        return nullptr;
    unsigned s;
    const unsigned char* data = in.getData(s);
    if (!data)
        return nullptr;
    if (maxSize > 0 && s > maxSize)
        return nullptr;
    size = s;
    char* copy = (char*)malloc(size);
    if (copy)
        memcpy(copy, data, size);
    return copy;
}

#endif

// SAXParser and related parsing.

#include <Poseidon/Network/XML/Xml.hpp>

typedef QIStream Stream;

int XMLAttributes::Add(RString name, RString value)
{
    int index = AutoArray<XMLAttribute>::Add();
    XMLAttribute& attribute = Set(index);
    attribute.name = name;
    attribute.value = value;
    return index;
}

const XMLAttribute* XMLAttributes::Find(RString name) const
{
    for (int i = 0; i < Size(); i++)
    {
        if (Get(i).name == name)
        {
            return &Get(i);
        }
    }
    return nullptr;
}

#define ISSPACE(c) ((c) >= 0 && (c) <= 32)

static void SkipSpaces(Stream& in)
{
    int c = in.get();
    while (!in.eof() && !in.fail() && ISSPACE(c))
    {
        c = in.get();
    }
    if (!in.eof() && !in.fail())
    {
        in.unget();
    }
}

static char ReadChar(Stream& in)
{
    char buf[256];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && c != ';')
    {
        if (len < static_cast<int>(sizeof(buf)) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    buf[len] = 0;

    if (buf[0] == '#')
    {
        return atoi(buf + 1);
    }
    else if (stricmp(buf, "amp") == 0)
    {
        return '&';
    }
    else if (stricmp(buf, "apos") == 0)
    {
        return '\'';
    }
    else if (stricmp(buf, "quot") == 0)
    {
        return '\"';
    }
    else if (stricmp(buf, "lt") == 0)
    {
        return '<';
    }
    else if (stricmp(buf, "gt") == 0)
    {
        return '>';
    }
    Fail("Unknown entity");
    return '?';
}

static RString ReadPropertyName(Stream& in)
{
    char buf[256];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && !ISSPACE(c) && c != '=')
    {
        if (len < static_cast<int>(sizeof(buf)) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    if (!in.eof() && !in.fail())
    {
        in.unget();
    }
    buf[len] = 0;
    return buf;
}

static RString ReadPropertyValue(Stream& in)
{
    SkipSpaces(in);
    int c = in.get();
    if (c != '=')
    {
        in.unget();
        return "";
    }

    SkipSpaces(in);
    c = in.get();
    if (c != '"' && c != '\'')
    {
        in.unget();
        return "";
    }

    int term = c;

    char buf[256];
    int len = 0;
    c = in.get();
    if (c == '&')
    {
        c = ReadChar(in);
    }
    while (!in.eof() && !in.fail() && c != term)
    {
        if (len < static_cast<int>(sizeof(buf)) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
        if (c == '&')
        {
            c = ReadChar(in);
        }
    }
    buf[len] = 0;
    return buf;
}

static RString ReadTag(Stream& in)
{
    char buf[256];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && isalnum(c))
    {
        if (len < static_cast<int>(sizeof(buf)) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    if (!in.eof() && !in.fail())
    {
        in.unget();
    }
    buf[len] = 0;
    return buf;
}

static void SkipTag(Stream& in)
{
    int c = in.get();
    while (!in.eof() && !in.fail() && c != '>')
    {
        c = in.get();
    }
}

static RString ReadText(Stream& in)
{
    SkipSpaces(in);

    char buf[2048];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && c != '<')
    {
        if (c == 0x0a)
        {
            // avoid spaces at line end
            while (len > 0 && buf[len - 1] == ' ')
            {
                len--;
            }
            // avoid CR LF at begin of the text
            if (len == 0)
            {
                goto ReadTextContinue;
            }
        }
        if (c != 0x0d) // avoid CR LF -> 2 spaces (CRLF and LF are handled well)
        {
            if (ISSPACE(c))
            {
                c = ' ';
            }
            else if (c == '&')
            {
                c = ReadChar(in);
            }
            if (len < static_cast<int>(sizeof(buf)) - 1)
            {
                buf[len++] = c;
            }
        }
    ReadTextContinue:
        c = in.get();
    }
    if (!in.eof() && !in.fail())
    {
        in.unget();
    }
    // RTRIM
    while (len > 0 && ISSPACE(buf[len - 1]))
    {
        len--;
    }
    buf[len] = 0;
    return buf;
}

bool SAXParser::Parse(QIStream& in)
{
    _abort = false;
    OnStartDocument();

    int c;
    while (!_abort)
    {
        I_AM_ALIVE();

        c = in.get();
        if (in.eof())
        {
            OnEndDocument();
            return true;
        }
        if (in.fail())
        {
            return false;
        }

        if (c == '<')
        {
            c = in.get();
            if (c == '/')
            {
                RString tag = ReadTag(in);
                OnEndElement(tag);
            }
            else if (isalnum(c))
            {
                in.unget();
                RString tag = ReadTag(in);

                // check for attributes
                XMLAttributes attributes;
                SkipSpaces(in);
                c = in.get();
                in.unget();
                while (c != '>' && c != '/')
                {
                    RString name = ReadPropertyName(in);
                    if (name.GetLength() == 0)
                    {
                        return false;
                    }
                    RString value = ReadPropertyValue(in);
                    attributes.Add(name, value);
                    SkipSpaces(in);
                    c = in.get();
                    in.unget();
                }
                OnStartElement(tag, attributes);
                if (c == '/')
                {
                    OnEndElement(tag);
                    in.get();
                    c = in.get();
                    in.unget();
                }
                NET_ERROR(c == '>');
            }
            SkipTag(in);
        }
        else
        {
            // text
            in.unget();
            RString text = ReadText(in);
            if (text.GetLength() > 0)
            {
                OnCharacters(text);
            }
        }
    }
    return true;
}
