#include "DriveHelpers.h"

#include <QtGlobal>

#include <vector>

#if defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/sysctl.h>
#elif defined(Q_OS_LINUX)
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

const char kHelper[] = "makemkvcon";
const char kHelperMode[] = "guiserver";

struct Process
{
    pid_t pid = 0;
    pid_t parent = 0;
};

#if defined(Q_OS_MACOS)

// Zweites Argument des Prozesses (KERN_PROCARGS2: Anzahl, Programmpfad, Füllbytes, Argumente)
std::string secondArgument(pid_t pid)
{
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = 0;
    if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0 || size < sizeof(int))
        return {};
    std::vector<char> buf(size);
    if (sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0 || size < sizeof(int))
        return {};
    int argc = 0;
    std::memcpy(&argc, buf.data(), sizeof(int));
    if (argc < 2)
        return {};
    const char *p = buf.data() + sizeof(int), *end = buf.data() + size;
    while (p < end && *p) // Programmpfad
        ++p;
    while (p < end && !*p)
        ++p;
    while (p < end && *p) // argv[0]
        ++p;
    if (p < end)
        ++p;
    return p < end ? std::string(p, strnlen(p, size_t(end - p))) : std::string();
}

std::vector<Process> helpers()
{
    std::vector<Process> out;
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0)
        return out;
    std::vector<pid_t> pids(size_t(count) + 64);
    const int n = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    const uid_t uid = getuid();
    for (int i = 0; i < n; ++i) {
        struct proc_bsdinfo info;
        if (proc_pidinfo(pids[size_t(i)], PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != int(sizeof(info)))
            continue;
        if (info.pbi_uid != uid || std::strcmp(info.pbi_comm, kHelper) != 0)
            continue;
        if (secondArgument(pids[size_t(i)]) != kHelperMode)
            continue;
        out.push_back({pids[size_t(i)], pid_t(info.pbi_ppid)});
    }
    return out;
}

bool orphaned(const Process &p)
{
    return p.parent == 1;
}

#else // Linux

// "pid (name) Zustand Elternprozess …"; der Name kann Leerzeichen und Klammern enthalten
bool readStat(pid_t pid, std::string *name, pid_t *parent)
{
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/stat", int(pid));
    FILE *f = std::fopen(path, "r");
    if (!f)
        return false;
    char buf[1024];
    const size_t got = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[got] = 0;
    const char *open = std::strchr(buf, '(');
    const char *close = std::strrchr(buf, ')');
    if (!open || !close || close < open)
        return false;
    name->assign(open + 1, size_t(close - open - 1));
    char state = 0;
    int ppid = 0;
    if (std::sscanf(close + 1, " %c %d", &state, &ppid) != 2)
        return false;
    *parent = pid_t(ppid);
    return true;
}

std::string secondArgument(pid_t pid)
{
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/cmdline", int(pid));
    FILE *f = std::fopen(path, "r");
    if (!f)
        return {};
    char buf[2048];
    const size_t got = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[got] = 0;
    const size_t first = strnlen(buf, got);
    return first + 1 < got ? std::string(buf + first + 1) : std::string();
}

std::vector<Process> helpers()
{
    std::vector<Process> out;
    DIR *dir = opendir("/proc");
    if (!dir)
        return out;
    const uid_t uid = getuid();
    while (const dirent *e = readdir(dir)) {
        char *end = nullptr;
        const long pid = std::strtol(e->d_name, &end, 10);
        if (pid <= 0 || *end)
            continue;
        std::string name;
        pid_t parent = 0;
        if (!readStat(pid_t(pid), &name, &parent) || name != kHelper)
            continue;
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%ld", pid);
        struct stat st;
        if (stat(path, &st) != 0 || st.st_uid != uid)
            continue;
        if (secondArgument(pid_t(pid)) != kHelperMode)
            continue;
        out.push_back({pid_t(pid), parent});
    }
    closedir(dir);
    return out;
}

// Ohne Programm: der Elternprozess ist init oder die Sitzungsverwaltung, die Waisen übernimmt
bool orphaned(const Process &p)
{
    if (p.parent == 1)
        return true;
    std::string name;
    pid_t grand = 0;
    return readStat(p.parent, &name, &grand) && (name == "systemd" || name == "init");
}

#endif

int end(bool orphans, const std::vector<qint64> &keep = {})
{
    const pid_t self = getpid();
    std::vector<pid_t> ended;
    for (const Process &p : helpers()) {
        if (orphans ? !orphaned(p) : p.parent != self)
            continue;
        if (std::find(keep.begin(), keep.end(), qint64(p.pid)) != keep.end())
            continue;
        if (kill(p.pid, SIGTERM) == 0)
            ended.push_back(p.pid);
    }
    if (!ended.empty()) {
        // wer das Ende nicht annimmt (steht im Laufwerk), wird kurz darauf beendet
        std::thread([ended, orphans, self] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            for (const Process &p : helpers()) {
                for (const pid_t pid : ended) {
                    if (p.pid == pid && (orphans ? orphaned(p) : p.parent == self))
                        kill(pid, SIGKILL);
                }
            }
        }).detach();
    }
    return int(ended.size());
}

} // namespace

int DriveHelpers::endOrphans()
{
    if (qEnvironmentVariableIsSet("LUMEN_KEEP_HELPERS")) // Entwickler-Hilfe: nichts beenden
        return 0;
    const int n = end(true);
    if (n > 0)
        qWarning("Lumen: %d Hilfsprozess(e) ohne Programm beendet (%s, hielt das Laufwerk)", n, kHelper);
    return n;
}

std::vector<qint64> DriveHelpers::own()
{
    std::vector<qint64> out;
    const pid_t self = getpid();
    for (const Process &p : helpers()) {
        if (p.parent == self)
            out.push_back(qint64(p.pid));
    }
    return out;
}

int DriveHelpers::endOwn(const std::vector<qint64> &keep)
{
    return end(false, keep);
}

#else

int DriveHelpers::endOrphans() { return 0; }
std::vector<qint64> DriveHelpers::own() { return {}; }
int DriveHelpers::endOwn(const std::vector<qint64> &) { return 0; }

#endif
