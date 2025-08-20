// registry_key.hpp
#ifndef REGISTRY_KEY_HPP
#define REGISTRY_KEY_HPP

#include <string>
#include <system_error>
#include <memory>
#include <vector>

#include <windows.h>
#include <winreg.h>

#pragma comment(lib, "advapi32.lib")

namespace framework {
    // RAII wrapper for HKEY
    class registry_key {
    private:
        HKEY hKey_ = nullptr;
        bool is_valid_ = false;

        // Helper: throw system_error with Windows error
        [[noreturn]] static void throw_error(const std::string& context) {
            DWORD last_error = GetLastError();
            std::error_code ec(static_cast<int>(last_error), std::system_category());
            throw std::system_error(ec, context);
        }

        static LONG safe_RegCreateKeyEx(HKEY hKeyParent, const std::wstring& subkey,
            PHKEY phKey, DWORD* pDisposition) {
            return RegCreateKeyExW(hKeyParent, subkey.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, phKey, pDisposition);
        }

        static LONG safe_RegOpenKeyEx(HKEY hKeyParent, const std::wstring& subkey, PHKEY phKey) {
            return RegOpenKeyExW(hKeyParent, subkey.c_str(), 0, KEY_ALL_ACCESS, phKey);
        }

        static std::wstring to_wide(const std::string& str) {
            if (str.empty()) return std::wstring();
            int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
            if (size == 0) throw_error("MultiByteToWideChar failed");
            std::wstring wstr(size, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], size);
            return wstr;
        }

        static std::string to_narrow(const std::wstring& wstr) {
            if (wstr.empty()) return std::string();
            int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
            if (size == 0) throw_error("WideCharToMultiByte failed");
            std::string str(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0], size, nullptr, nullptr);
            return str;
        }

    public:
        // Default constructor: null key
        registry_key() = default;

        // Open or create key
        explicit registry_key(HKEY hKeyParent, const std::string& subkey) {
            open_or_create(hKeyParent, subkey);
        }

        // Prevent copying (HKEY is not copyable)
        registry_key(const registry_key&) = delete;
        registry_key& operator=(const registry_key&) = delete;

        // Allow moving
        registry_key(registry_key&& other) noexcept
            : hKey_(other.hKey_), is_valid_(other.is_valid_) {
            other.hKey_ = nullptr;
            other.is_valid_ = false;
        }

        registry_key& operator=(registry_key&& other) noexcept {
            if (this != &other) {
                close();
                hKey_ = other.hKey_;
                is_valid_ = other.is_valid_;
                other.hKey_ = nullptr;
                other.is_valid_ = false;
            }
            return *this;
        }

        ~registry_key() {
            close();
        }

        void close() {
            if (hKey_) {
                RegCloseKey(hKey_);
                hKey_ = nullptr;
                is_valid_ = false;
            }
        }

        bool is_open() const { return is_valid_; }

        // Open or create subkey
        void open_or_create(HKEY hKeyParent, const std::string& subkey_str) {
            close();
            std::wstring subkey = to_wide(subkey_str);

            LONG ret = safe_RegCreateKeyEx(hKeyParent, subkey, &hKey_, nullptr);
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to create/open registry key: " + subkey_str);
            }
            is_valid_ = true;
        }

        // Open existing key (no create)
        void open(HKEY hKeyParent, const std::string& subkey_str) {
            close();
            std::wstring subkey = to_wide(subkey_str);

            LONG ret = safe_RegOpenKeyEx(hKeyParent, subkey, &hKey_);
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to open registry key: " + subkey_str);
            }
            is_valid_ = true;
        }

        // Write DWORD
        void write_dword(const std::string& name, DWORD value) {
            ensure_open();
            std::wstring wname = to_wide(name);
            LONG ret = RegSetValueExW(hKey_, wname.c_str(), 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&value), sizeof(value));
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to write DWORD value: " + name);
            }
        }

        // Read DWORD
        DWORD read_dword(const std::string& name) {
            ensure_open();
            std::wstring wname = to_wide(name);
            DWORD type = 0;
            DWORD data = 0;
            DWORD size = sizeof(data);
            LONG ret = RegQueryValueExW(hKey_, wname.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&data), &size);
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to read DWORD value: " + name);
            }
            if (type != REG_DWORD) {
                throw std::runtime_error("Registry value is not REG_DWORD");
            }
            return data;
        }

        // Write string (REG_SZ)
        void write_string(const std::string& name, const std::string& value) {
            ensure_open();
            std::wstring wname = to_wide(name);
            std::wstring wvalue = to_wide(value);
            LONG ret = RegSetValueExW(hKey_, wname.c_str(), 0, REG_SZ,
                reinterpret_cast<const BYTE*>(wvalue.c_str()), static_cast<DWORD>((wvalue.size() + 1) * sizeof(wchar_t)));
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to write string value: " + name);
            }
        }

        // Read string (REG_SZ)
        std::string read_string(const std::string& name) {
            ensure_open();
            std::wstring wname = to_wide(name);
            DWORD type = 0;
            DWORD size = 0;
            LONG ret = RegQueryValueExW(hKey_, wname.c_str(), nullptr, &type, nullptr, &size);
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to query string value size: " + name);
            }
            if (type != REG_SZ) {
                throw std::runtime_error("Registry value is not REG_SZ");
            }

            std::vector<wchar_t> buffer(size / sizeof(wchar_t));
            ret = RegQueryValueExW(hKey_, wname.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to read string value: " + name);
            }

            return to_narrow(std::wstring(buffer.data(), size / sizeof(wchar_t)));
        }

        // Optional: delete value
        void delete_value(const std::string& name) {
            ensure_open();
            std::wstring wname = to_wide(name);
            LONG ret = RegDeleteValueW(hKey_, wname.c_str());
            if (ret != ERROR_SUCCESS && ret != ERROR_FILE_NOT_FOUND) {
                throw_error("Failed to delete value: " + name);
            }
        }

        // Optional: delete key (use with care)
        static void delete_key(HKEY hKeyParent, const std::string& subkey) {
            std::wstring wsubkey = to_wide(subkey);
            LONG ret = RegDeleteKeyW(hKeyParent, wsubkey.c_str());
            if (ret != ERROR_SUCCESS) {
                throw_error("Failed to delete key: " + subkey);
            }
        }

    private:
        void ensure_open() {
            if (!is_valid_) {
                throw std::runtime_error("Registry key is not open");
            }
        }
    };
}
#endif // REGISTRY_KEY_HPP