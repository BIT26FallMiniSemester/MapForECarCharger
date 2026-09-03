#ifndef WEBENGINESETUP_H
#define WEBENGINESETUP_H

#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QString>
#include <QtGlobal>

// 只使用「当前程序链接的那套 Qt」自带的 QtWebEngineProcess。
// 若去找另一套 Qt5/Qt6 的进程，Chromium IPC 会对不上，出现
// VALIDATION_ERROR_MESSAGE_HEADER_UNKNOWN_METHOD，渲染进程被杀掉。
inline QString qtWebEngineProcessPath()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString fromQt = QLibraryInfo::path(QLibraryInfo::LibraryExecutablesPath)
                           + QStringLiteral("/QtWebEngineProcess");
#else
    const QString fromQt = QLibraryInfo::location(QLibraryInfo::LibraryExecutablesPath)
                           + QStringLiteral("/QtWebEngineProcess");
#endif
    if (QFileInfo::exists(fromQt))
        return fromQt;
    return {};
}

inline void setupWebEngineEnvironment()
{
#ifdef Q_OS_LINUX
    // 这台虚拟机会把 Qt6 程序配到 Qt5 的 QtWebEngineProcess，一创建内嵌网页就 IPC 失败。
    if (qEnvironmentVariableIntValue("CHARGING_USE_WEBENGINE") != 1)
        return;
#endif
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_DISABLE_SANDBOX"))
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");

    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (flags.isEmpty())
        flags = "--no-sandbox --disable-gpu --disable-dev-shm-usage";
    else if (!flags.contains("disable-dev-shm-usage"))
        flags += " --disable-dev-shm-usage";
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);

    const QString process = qtWebEngineProcessPath();
    if (!process.isEmpty())
        qputenv("QTWEBENGINEPROCESS_PATH", QFile::encodeName(process));
    else
        qunsetenv("QTWEBENGINEPROCESS_PATH");
}

inline bool webEngineProcessAvailable()
{
    return !qtWebEngineProcessPath().isEmpty();
}

// 这台 Ubuntu 虚拟机里 libQt6WebEngine 与 QtWebEngineProcess 的 Chromium ABI 不一致，
// 创建 QWebEngineView 就会刷 UNKNOWN_METHOD 并杀掉渲染进程。
// Linux 默认改为系统浏览器打开腾讯路线；若以后组件对齐了，可 export CHARGING_USE_WEBENGINE=1。
inline bool shouldEmbedWebEngine()
{
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIntValue("CHARGING_USE_WEBENGINE") != 1)
        return false;
#endif
    return webEngineProcessAvailable();
}

#endif
