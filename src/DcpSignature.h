#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// XML-Signaturen von KDMs (SMPTE ST 430-3, XML-DSig mit RSA-SHA256):
//   - jede Referenz (#ID_AuthenticatedPublic, #ID_AuthenticatedPrivate) wird
//     kanonisiert (C14N) und ihr Digest mit dem signierten Wert verglichen
//   - die SignedInfo wird kanonisiert und gegen das Zertifikat des Unterzeichners
//     geprüft; die mitgelieferte Zertifikatskette wird bis zur Wurzel verifiziert
// Kanonisierung über libxml2, Kryptografie über OpenSSL.
namespace DcpSignature {

bool available();

struct Result
{
    bool present = false;     // ds:Signature vorhanden
    bool valid = false;       // Digests + Signatur korrekt
    bool chainValid = false;  // mitgelieferte Kette bis zu einer selbstsignierten Wurzel
    QString signer;           // Subjekt des Unterzeichners
    QString error;
};

Result verify(const QByteArray &xml);

// Signiert ein XML-Dokument wie ein KDM-Ersteller (für Tests und eigene KDMs):
// Referenzen auf alle Elemente mit den angegebenen Id-Werten; die Kette (PEM,
// Unterzeichner zuerst) wird in KeyInfo eingebettet.
bool sign(const QByteArray &xml, const QStringList &ids, const QByteArray &keyPem, const QByteArray &chainPem,
          QByteArray *out, QString *error);

} // namespace DcpSignature
