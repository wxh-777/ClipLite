#ifndef CLIPLITE_APP_LIFECYCLE_H
#define CLIPLITE_APP_LIFECYCLE_H

#include <windows.h>

class AppLifecycle {
public:
    AppLifecycle() = default;
    ~AppLifecycle();

    AppLifecycle(const AppLifecycle&) = delete;
    AppLifecycle& operator=(const AppLifecycle&) = delete;

    bool acquireSingleInstance(const wchar_t* name);
    bool alreadyRunning() const { return mutex_ && lastError_ == ERROR_ALREADY_EXISTS; }
    DWORD lastError() const { return lastError_; }
    void releaseSingleInstance();

private:
    HANDLE mutex_ = nullptr;
    DWORD lastError_ = ERROR_SUCCESS;
};

#endif
