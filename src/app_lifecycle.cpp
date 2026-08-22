#include "app_lifecycle.h"

AppLifecycle::~AppLifecycle() {
    releaseSingleInstance();
}

bool AppLifecycle::acquireSingleInstance(const wchar_t* name) {
    releaseSingleInstance();
    if (!name || !*name) return false;
    mutex_ = CreateMutexW(nullptr, TRUE, name);
    lastError_ = GetLastError();
    return mutex_ != nullptr;
}

void AppLifecycle::releaseSingleInstance() {
    if (!mutex_) return;
    if (lastError_ != ERROR_ALREADY_EXISTS) ReleaseMutex(mutex_);
    CloseHandle(mutex_);
    mutex_ = nullptr;
    lastError_ = ERROR_SUCCESS;
}
