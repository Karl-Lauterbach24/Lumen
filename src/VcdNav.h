#pragma once

#include "OpticalMedia.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

// Video-CD 2.0 / SVCD Wiedergabesteuerung (PBC, "Playback Control"):
// LOT/PSD aus dem Abbild bzw. vom Laufwerk, Listen nach dem White Book:
//   Play List       – Elemente nacheinander, danach Wartezeit, dann "Next"
//   Selection List  – Menübild (meist Standbild-Segment), Auswahl per Zifferntaste
//                     (Basisnummer BSN), Next/Previous/Return/Default, Zeitablauf
//   End List        – Ende der Steuerung
// Elemente: Track (2–99), Einsprungpunkt (100–599), Segment (1000–2979).
// Gespielt wird jeweils ein Sektorbereich über "lumenvcd://"; das Ende eines
// Elements meldet der Player (itemFinished).
class VcdNav : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool selection READ selection NOTIFY stateChanged)
    Q_PROPERTY(int lid READ lid NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
    explicit VcdNav(QObject *parent = nullptr);

    static bool hasPbc(const QString &source);
    bool start(const QString &source);
    void stop();

    bool active() const { return m_active; }
    bool selection() const;
    int lid() const { return m_lid; }
    // {lid, type, choices (BSN…BSN+NOS-1), prev, next, return, default, item, still, waiting}
    QVariantMap state() const;

    // "enter" (Default), "next", "prev", "return"/"menu", "0"…"9"
    Q_INVOKABLE bool key(const QString &name);
    // Aktuelles Element zu Ende gespielt (keep-open: eof erreicht)
    void itemFinished();

signals:
    void playRequested(const QString &url, bool still);
    void stateChanged();
    void stopped();

private:
    struct List
    {
        int type = 0;
        int lid = 0;
        int prev = 0xffff, next = 0xffff, ret = 0xffff, def = 0xffff, timeout = 0xffff;
        int wait = 0;          // Sekunden, -1 = unendlich
        int loop = 1;          // 0 = unendlich
        int bsn = 1;
        QList<int> items;      // Play List: Elemente; Selection: [Menübild]
        QList<int> choices;    // Selection: Listen-Offsets je Auswahl
        int changePic = 0;     // End List
    };

    bool parse(int offsetValue, List *out) const;
    bool go(int offsetValue);
    void enter();
    void playItem(int itemId);
    void listDone();
    QString itemUrl(int itemId, bool *still) const;
    static int waitSeconds(int code);

    Optical::VcdDisc m_disc;
    QString m_source;
    bool m_active = false;
    int m_mult = 8;
    int m_lid = 0;
    List m_list;
    int m_item = 0;
    int m_loops = 0;
    bool m_still = false;
    bool m_waiting = false;
    QTimer m_waitTimer;
    QTimer m_digitTimer;
    QString m_digits;
};
