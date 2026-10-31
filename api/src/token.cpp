#include <cutenes/api/token.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <vector>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace cutenes::api::token {

namespace {

constexpr std::size_t kTokenBytes = 32;

bool randomBytes(std::uint8_t* out, std::size_t size)
{
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(size),
               BCRYPT_USE_SYSTEM_PREFERRED_RNG)
        == 0;
#elif defined(__linux__)
    std::size_t done = 0;
    while (done < size) {
        const ssize_t n = getrandom(out + done, size - done, 0);
        if (n <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
#else
    arc4random_buf(out, size);
    return true;
#endif
}

std::string contents(const std::string& url, const std::string& token)
{
    return nlohmann::json{{"url", url}, {"token", token}}.dump() + "\n";
}

#if defined(_WIN32)

// The current user's SID, owned by the returned buffer.
std::vector<std::uint8_t> currentUser()
{
    HANDLE process = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process)) {
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(process, TokenUser, nullptr, 0, &size);
    std::vector<std::uint8_t> buffer(size);
    const BOOL ok = GetTokenInformation(process, TokenUser, buffer.data(), size, &size);
    CloseHandle(process);
    return ok ? buffer : std::vector<std::uint8_t>{};
}

PSID sidOf(std::vector<std::uint8_t>& user)
{
    return reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
}

#endif

} // namespace

std::string generate()
{
    std::uint8_t bytes[kTokenBytes];
    if (!randomBytes(bytes, kTokenBytes)) {
        return {};
    }
    static const char* const hex = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out += hex[b >> 4];
        out += hex[b & 0x0F];
    }
    return out;
}

bool constantTimeEquals(const std::string& a, const std::string& b)
{
    // Length is not secret (the token length is fixed); the contents are.
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<unsigned char>(diff | (a[i] ^ b[i]));
    }
    return diff == 0;
}

std::filesystem::path defaultDirectory()
{
    namespace fs = std::filesystem;
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        return fs::path(local) / "cutenes";
    }
    return fs::temp_directory_path() / "cutenes";
#else
    const char* home = std::getenv("HOME");
    const fs::path base = home != nullptr ? fs::path(home) : fs::temp_directory_path();
#if defined(__APPLE__)
    return base / "Library" / "Application Support" / "cutenes";
#else
    if (const char* state = std::getenv("XDG_STATE_HOME"); state != nullptr && *state != '\0') {
        return fs::path(state) / "cutenes";
    }
    return base / ".local" / "state" / "cutenes";
#endif
#endif
}

bool writeFile(const std::filesystem::path& path, const std::string& url,
    const std::string& tokenValue, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "cannot create " + path.parent_path().string() + ": " + ec.message();
            return false;
        }
    }
    // Replace, never reuse, a previous file: the new one is created with
    // owner-only access from the start.
    fs::remove(path, ec);
    const std::string text = contents(url, tokenValue);
#if defined(_WIN32)
    auto user = currentUser();
    if (user.empty()) {
        error = "cannot read the current user's SID";
        return false;
    }
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sidOf(user));
    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) {
        error = "cannot build the token file ACL";
        return false;
    }
    SECURITY_DESCRIPTOR descriptor;
    InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE);
    SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    SECURITY_ATTRIBUTES attributes{sizeof attributes, &descriptor, FALSE};
    const HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, &attributes,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(acl);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot create " + path.string();
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok || written != text.size()) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        error = "cannot create " + path.string();
        return false;
    }
    // The umask can only remove bits; set the mode exactly anyway.
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n <= 0) {
            ::close(fd);
            error = "cannot write " + path.string();
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return true;
#endif
}

bool isOwnerOnly(const std::filesystem::path& path)
{
#if defined(_WIN32)
    auto user = currentUser();
    if (user.empty()) {
        return false;
    }
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(path.wstring().c_str(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor)
        != ERROR_SUCCESS) {
        return false;
    }
    bool ownerOnly = dacl != nullptr;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(descriptor, &control, &revision);
    ownerOnly = ownerOnly && (control & SE_DACL_PROTECTED) != 0;
    for (DWORD i = 0; ownerOnly && i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace)) {
            ownerOnly = false;
            break;
        }
        const auto* header = static_cast<ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            ownerOnly = false;
            break;
        }
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        ownerOnly = EqualSid(&allowed->SidStart, sidOf(user)) != 0;
    }
    LocalFree(descriptor);
    return ownerOnly;
#else
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        return false;
    }
    return info.st_uid == ::getuid() && (info.st_mode & 077) == 0;
#endif
}

} // namespace cutenes::api::token
