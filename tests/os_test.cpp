// LumenOS: Einrichtung einer Fernbedienung (InputMapper, mit einem Gerät ohne Gerät) und das
// Auswerten von MakeMKVs Fortschrittsmeldungen (RipManager).
#include "InputMapper.h"
#include "RipManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QVariant>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    if (!ok)
        ++failures;
}

// Was ein Signal gemeldet hat (ohne QtTest: die Paket-Builds bringen es nicht überall mit)
struct Spy : QList<QVariantList>
{
    int count() const { return int(size()); }
    // Ereignisse verarbeiten, bis etwas angekommen ist
    void wait(int ms)
    {
        QElapsedTimer t;
        t.start();
        while (isEmpty() && t.elapsed() < ms)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString store = dir.filePath(QStringLiteral("remotes.json"));

    // --- eine neue Fernbedienung
    {
        InputMapper m(false);
        m.setStorePath(store);
        Spy wanted, actions, finished;
        QObject::connect(&m, &InputMapper::setupWanted, [&](const QString &id, const QString &name) { wanted.append({id, name}); });
        QObject::connect(&m, &InputMapper::action, [&](const QString &name, bool repeat) { actions.append({name, repeat}); });
        QObject::connect(&m, &InputMapper::wizardFinished, [&](const QString &id, bool saved) { finished.append({id, saved}); });
        m.fakeDevice(QStringLiteral("pad"));
        check(wanted.count() == 1 && m.devices().size() == 1 && !m.devices().first().toMap().value("mapped").toBool(),
              QStringLiteral("ein Gerät ohne Zuordnung meldet sich zur Einrichtung"));
        m.fakeButton(QStringLiteral("pad"), 1);
        check(actions.isEmpty() && wanted.count() == 2, QStringLiteral("eine Taste darauf löst nichts aus, bietet die Einrichtung wieder an"));

        m.startWizard(QStringLiteral("fake:pad"));
        check(m.wizardActive() && m.wizardStep() == 0 && m.wizardSteps().size() == InputMapper::actions().size(),
              QStringLiteral("Einrichtung beginnt mit der ersten von %1 Funktionen").arg(InputMapper::actions().size()));
        m.fakeButton(QStringLiteral("pad"), 11); // hoch
        m.fakeButton(QStringLiteral("pad"), 12); // runter
        m.fakeButton(QStringLiteral("pad"), 12); // dieselbe Taste noch einmal
        check(m.wizardStep() == 2 && m.wizardTaken() == QLatin1String("down"), QStringLiteral("eine schon vergebene Taste wird abgelehnt und genannt"));
        m.skipStep();
        check(m.wizardStep() == 2, QStringLiteral("Pflichtfunktionen lassen sich nicht auslassen"));
        m.fakeButton(QStringLiteral("pad"), 13); // links
        m.fakeButton(QStringLiteral("pad"), 14); // rechts
        m.fakeButton(QStringLiteral("pad"), 15); // ok
        check(m.wizardActive() && actions.isEmpty(), QStringLiteral("während der Einrichtung löst keine Taste etwas aus"));
        m.fakeButton(QStringLiteral("pad"), 16); // zurück – letzte Pflichtfunktion
        check(m.wizardStep() == InputMapper::kRequired && m.wizardSteps().at(InputMapper::kRequired).toMap().value("optional").toBool(),
              QStringLiteral("nach den Pflichtfunktionen folgen die wahlfreien"));
        m.fakeButton(QStringLiteral("pad"), 16); // "Zurück" lässt "Menü" aus
        check(m.wizardStep() == InputMapper::kRequired + 1, QStringLiteral("die Taste für Zurück lässt eine wahlfreie Funktion aus"));
        m.fakeButton(QStringLiteral("pad"), 20); // Wiedergabe/Pause
        m.fakeButton(QStringLiteral("pad"), 15); // "OK" schließt ab
        check(!m.wizardActive() && finished.count() == 1 && finished.first().at(1).toBool(), QStringLiteral("die Taste für OK schließt die Einrichtung ab"));
        check(m.devices().first().toMap().value("mapped").toBool() && QFile::exists(store), QStringLiteral("die Zuordnung ist gespeichert"));

        // --- danach sind es Funktionen
        m.fakeButton(QStringLiteral("pad"), 11);
        m.fakeButton(QStringLiteral("pad"), 15);
        m.fakeButton(QStringLiteral("pad"), 20);
        m.fakeButton(QStringLiteral("pad"), 99); // nicht zugeordnet
        QStringList got;
        for (const auto &a : actions)
            got << a.first().toString();
        check(got == QStringList{QStringLiteral("up"), QStringLiteral("ok"), QStringLiteral("playpause")},
              QStringLiteral("zugeordnete Tasten lösen ihre Funktion aus: %1").arg(got.join(QLatin1Char(' '))));
    }
    // --- beim nächsten Start kennt Lumen das Gerät
    {
        InputMapper m(false);
        m.setStorePath(store);
        Spy wanted, actions;
        QObject::connect(&m, &InputMapper::setupWanted, [&](const QString &id, const QString &name) { wanted.append({id, name}); });
        QObject::connect(&m, &InputMapper::action, [&](const QString &name, bool repeat) { actions.append({name, repeat}); });
        check(m.devices().size() == 1 && !m.devices().first().toMap().value("connected").toBool(),
              QStringLiteral("ein eingerichtetes Gerät steht in der Liste, auch wenn es nicht angeschlossen ist"));
        m.fakeDevice(QStringLiteral("pad"));
        m.fakeButton(QStringLiteral("pad"), 14);
        check(wanted.isEmpty() && actions.count() == 1 && actions.first().first().toString() == QLatin1String("right"),
              QStringLiteral("es arbeitet sofort, ohne neue Einrichtung"));
        // neu einrichten und abbrechen: die alte Zuordnung bleibt
        m.startWizard(QStringLiteral("fake:pad"));
        m.fakeButton(QStringLiteral("pad"), 31);
        m.cancelWizard();
        actions.clear();
        m.fakeButton(QStringLiteral("pad"), 31);
        m.fakeButton(QStringLiteral("pad"), 11);
        check(actions.count() == 1 && actions.first().first().toString() == QLatin1String("up"), QStringLiteral("eine abgebrochene Einrichtung lässt die alte Zuordnung stehen"));
        m.forget(QStringLiteral("fake:pad"));
        actions.clear();
        m.fakeButton(QStringLiteral("pad"), 11);
        check(actions.isEmpty() && !m.devices().first().toMap().value("mapped").toBool(), QStringLiteral("vergessen: die Tasten lösen nichts mehr aus"));
    }

    // --- MakeMKV: Zeilen der Ausgabe von "makemkvcon -r"
    {
        check(RipManager::fields(QStringLiteral("5011,0,1,\"Operation \\\"x\\\", done\",\"%1\",\"a,b\"")) ==
                  QStringList{QStringLiteral("5011"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("Operation \"x\", done"), QStringLiteral("%1"), QStringLiteral("a,b")},
              QStringLiteral("Felder mit Kommas und Anführungszeichen im Text"));
        RipManager rip;
        rip.parseLine(QStringLiteral("PRGC:5057,0,\"Saving to MKV file\""));
        rip.parseLine(QStringLiteral("PRGV:1200,32768,65536"));
        check(qAbs(rip.progress() - 0.5) < 0.001 && rip.status().contains(QLatin1String("Saving to MKV file")) && rip.status().contains(QLatin1String("50 %")),
              QStringLiteral("Fortschritt und Schritt: %1").arg(rip.status()));
        rip.parseLine(QStringLiteral("MSG:5005,0,1,\"3 titles saved\",\"%1 titles saved\",\"3\""));
        rip.parseLine(QStringLiteral("something else entirely"));
        check(rip.status().contains(QLatin1String("50 %")), QStringLiteral("andere Zeilen ändern nichts"));
    }
    // --- und ein ganzer Lauf mit einem Stellvertreter für makemkvcon
#ifndef Q_OS_WIN
    {
        const QString fake = dir.filePath(QStringLiteral("makemkvcon"));
        QFile f(fake);
        f.open(QIODevice::WriteOnly);
        f.write("#!/bin/sh\n"
                "echo 'MSG:1005,0,1,\"MakeMKV started\",\"%1 started\",\"MakeMKV\"'\n"
                "echo 'PRGC:5018,0,\"Scanning CD-ROM devices\"'\n"
                "echo 'PRGV:0,0,65536'\n"
                "for last; do :; done\n"            // letztes Argument: der Zielordner
                "echo 'PRGC:5057,0,\"Saving to MKV file\"'\n"
                "echo 'PRGV:65536,65536,65536'\n"
                "if [ \"$FAIL\" = 1 ]; then echo 'MSG:5010,0,0,\"Failed to open disc\",\"Failed to open disc\"'; exit 1; fi\n"
                "echo film > \"$last/title_t00.mkv\"\n"
                "echo 'MSG:5005,0,1,\"1 titles saved\",\"%1 titles saved\",\"1\"'\n");
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        qputenv("LUMEN_MAKEMKVCON", fake.toLocal8Bit());
        RipManager rip;
        Spy finished;
        QObject::connect(&rip, &RipManager::finished, [&](bool ok, const QString &folder) { finished.append({ok, folder}); });
        check(rip.available(), QStringLiteral("MakeMKV gefunden"));
        const QString library = dir.filePath(QStringLiteral("library"));
        QDir().mkpath(library);
        rip.start(QStringLiteral("/dev/sr0"), library, QStringLiteral("Ein Film: Teil 2"));
        check(rip.running(), QStringLiteral("Kopie läuft"));
        finished.wait(10000);
        check(finished.count() == 1 && finished.first().first().toBool() && !rip.running() && rip.lastError().isEmpty(),
              QStringLiteral("Kopie beendet: %1").arg(rip.status()));
        check(QFile::exists(library + QStringLiteral("/Ein Film Teil 2/title_t00.mkv")), QStringLiteral("im Ordner mit dem Namen der Disc (ohne Zeichen, die kein Dateiname verträgt)"));
        // dieselbe Disc noch einmal: ein zweiter Ordner
        finished.clear();
        rip.start(QStringLiteral("/dev/sr0"), library, QStringLiteral("Ein Film: Teil 2"));
        finished.wait(10000);
        check(QFile::exists(library + QStringLiteral("/Ein Film Teil 2 (2)/title_t00.mkv")), QStringLiteral("eine zweite Kopie bekommt einen eigenen Ordner"));
        // MakeMKV scheitert: Meldung, kein leerer Ordner
        qputenv("FAIL", "1");
        finished.clear();
        rip.start(QStringLiteral("/dev/sr0"), library, QStringLiteral("Kaputt"));
        finished.wait(10000);
        check(finished.count() == 1 && !finished.first().first().toBool() && rip.lastError() == QLatin1String("Failed to open disc")
                  && !QFile::exists(library + QStringLiteral("/Kaputt")),
              QStringLiteral("gescheitert: MakeMKVs Meldung wird gezeigt, kein leerer Ordner bleibt (%1)").arg(rip.lastError()));
    }
#endif

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
