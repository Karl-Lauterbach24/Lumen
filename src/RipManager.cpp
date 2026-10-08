#include "RipManager.h"
#include "Tr.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

RipManager::RipManager(QObject *parent)
    : QObject(parent)
{
}

RipManager::~RipManager()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

QString RipManager::program() const
{
    const QString custom = qEnvironmentVariable("LUMEN_MAKEMKVCON"); // Tests
    if (!custom.isEmpty())
        return custom;
    QString found = QStandardPaths::findExecutable(QStringLiteral("makemkvcon"));
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(QStringLiteral("makemkvcon"), {QStringLiteral("/usr/local/bin"), QStringLiteral("/opt/makemkv/bin"),
                                                                              QStringLiteral("/Applications/MakeMKV.app/Contents/MacOS")});
    return found;
}

bool RipManager::available() const
{
    return !program().isEmpty();
}

QStringList RipManager::fields(const QString &text)
{
    QStringList out;
    QString current;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('\\') && i + 1 < text.size())
                current += text.at(++i);
            else if (c == QLatin1Char('"'))
                quoted = false;
            else
                current += c;
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(',')) {
            out << current;
            current.clear();
        } else {
            current += c;
        }
    }
    out << current;
    return out;
}

void RipManager::start(const QString &device, const QString &folder, const QString &name)
{
    if (m_process || device.isEmpty() || folder.isEmpty())
        return;
    const QString exe = program();
    if (exe.isEmpty()) {
        m_error = LTR("MakeMKV ist nicht eingerichtet.");
        emit changed();
        return;
    }
    // Ordner mit dem Namen der Disc; gibt es ihn schon, eine Nummer anhängen
    QString label = name.trimmed();
    if (label.isEmpty())
        label = QFileInfo(device).fileName();
    label.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral(" "));
    label = label.simplified();
    if (label.isEmpty())
        label = QStringLiteral("Disc");
    QString dir = QDir(folder).filePath(label);
    for (int n = 2; QFileInfo::exists(dir) && !QDir(dir).isEmpty(); ++n)
        dir = QDir(folder).filePath(QStringLiteral("%1 (%2)").arg(label).arg(n));
    if (!QDir().mkpath(dir)) {
        m_error = LTR("Der Ordner „%1“ lässt sich nicht anlegen.").arg(dir);
        emit changed();
        return;
    }
    m_target = dir;
    m_error.clear();
    m_lastMessage.clear();
    m_step.clear();
    m_saved = 0;
    m_progress = -1;
    m_cancelled = false;
    m_status = LTR("MakeMKV liest die Disc …");
    m_buffer.clear();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_buffer += m_process->readAll();
        for (int end; (end = m_buffer.indexOf('\n')) >= 0;) {
            parseLine(QString::fromUtf8(m_buffer.left(end)).trimmed());
            m_buffer.remove(0, end + 1);
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0 && m_saved > 0;
        done(ok, m_cancelled ? LTR("Kopieren abgebrochen.")
                 : ok ? QString()
                 : !m_lastMessage.isEmpty() ? m_lastMessage : LTR("MakeMKV hat ohne Ergebnis geendet."));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false, LTR("MakeMKV ließ sich nicht starten."));
    });
    // Ein Abbild oder Ordner statt eines Laufwerks: makemkvcon nennt die Quelle anders
    const QFileInfo source(device);
    const QString from = source.isDir() ? QStringLiteral("file:") + device : source.isFile() ? QStringLiteral("iso:") + device : QStringLiteral("dev:") + device;
    m_process->start(exe, {QStringLiteral("-r"), QStringLiteral("--progress=-same"), QStringLiteral("--minlength=120"), QStringLiteral("mkv"), from,
                           QStringLiteral("all"), dir});
    emit changed();
}

void RipManager::parseLine(const QString &line)
{
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return;
    const QString kind = line.left(colon);
    const QStringList f = fields(line.mid(colon + 1));
    if (kind == QLatin1String("PRGV") && f.size() >= 3) {
        // Stand des laufenden Schritts, Stand insgesamt, Höchstwert
        const double max = f.at(2).toDouble();
        m_progress = max > 0 ? std::clamp(f.at(1).toDouble() / max, 0.0, 1.0) : -1;
    } else if ((kind == QLatin1String("PRGC") || kind == QLatin1String("PRGT")) && f.size() >= 3) {
        if (kind == QLatin1String("PRGC"))
            m_step = f.at(2);
    } else if (kind == QLatin1String("MSG") && f.size() >= 4) {
        m_lastMessage = f.at(3);
        // 5005: "N titles saved"; 5036/5037 u. a.: einzelner Titel gespeichert – die Zahl steht im Text
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^(\\d+) titles? saved")).match(m_lastMessage);
        if (m.hasMatch())
            m_saved = m.captured(1).toInt();
    } else {
        return;
    }
    QString status = m_step.isEmpty() ? LTR("MakeMKV liest die Disc …") : m_step;
    if (m_progress >= 0)
        status += QStringLiteral("  ·  %1 %").arg(int(m_progress * 100));
    if (status != m_status) {
        m_status = status;
        emit changed();
    }
}

void RipManager::cancel()
{
    if (!m_process)
        return;
    m_cancelled = true;
    m_process->terminate();
    QProcess *p = m_process;
    QTimer::singleShot(4000, p, [p] {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
}

void RipManager::done(bool ok, const QString &error)
{
    if (!m_process)
        return;
    m_process->deleteLater();
    m_process = nullptr;
    m_error = error;
    m_status = ok ? LTR("Fertig: %1").arg(QFileInfo(m_target).fileName()) : QString();
    m_progress = -1;
    // Ein leerer Ordner bleibt nicht zurück
    if (!ok && QDir(m_target).isEmpty())
        QDir().rmdir(m_target);
    emit changed();
    emit finished(ok, m_target);
}
