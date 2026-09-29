#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// Kryptografie für verschlüsselte DCPs (OpenSSL):
//
//   Zertifikatskette (SMPTE ST 430-2) ─ Lumen erzeugt Root/Intermediate/Leaf.
//     Das Leaf-Zertifikat geht an den Verleih bzw. KDM-Ersteller; der KDM wird
//     genau für dieses Zertifikat ausgestellt.
//   KDM (SMPTE ST 430-1 / Interop) ─ die Inhaltsschlüssel sind per RSA-OAEP mit
//     dem öffentlichen Schlüssel des Leaf-Zertifikats verschlüsselt; nur der
//     passende private Schlüssel kann sie auspacken.
//   Essenz (SMPTE ST 429-6) ─ AES-128-CBC je KLV-Triplet.
//
// Es gibt keinen Weg an einem KDM vorbei: ohne gültigen, an dieses Gerät
// adressierten KDM (oder vom Inhaber selbst eingetragene Schlüssel) bleibt ein
// verschlüsseltes DCP unlesbar.
namespace DcpCrypto {

bool available();

struct Identity
{
    bool valid = false;
    QString dir;
    QString subject;
    QString issuer;
    QString serial;        // dezimal
    QString thumbprint;    // Base64(SHA-1(tbsCertificate))
    QString dnQualifier;
    QDateTime notBefore;
    QDateTime notAfter;
    QString leafFile;      // leaf.pem
    QString chainFile;     // chain.pem (Leaf, Intermediate, Root)
    QString keyFile;       // leaf.key
};

Identity loadIdentity(const QString &dir);
// Neue Kette (RSA 2048, SHA-256, 10 Jahre) im Ordner anlegen
bool createIdentity(const QString &dir, const QString &organisation, QString *error);
// Vorhandenes Leaf-Zertifikat + privaten Schlüssel (PEM) übernehmen, z. B. aus DCP-o-matic
bool importIdentity(const QString &dir, const QString &certFile, const QString &keyFile, QString *error);

struct ContentKey
{
    QString keyId;       // UUID
    QByteArray key;      // 16 Byte
    QString type;        // MDIK, MDAK, MDSK, … (SMPTE)
    QString cplId;
    QDateTime notBefore;
    QDateTime notAfter;
    bool validAt(const QDateTime &t) const
    {
        return (!notBefore.isValid() || t >= notBefore) && (!notAfter.isValid() || t <= notAfter);
    }
};

struct Kdm
{
    QString file;
    QString cplId;
    QString title;
    QString annotation;
    QString recipientSerial;
    QString recipientSubject;
    QDateTime notBefore;
    QDateTime notAfter;
    bool smpte = true;
    QList<ContentKey> keys;
    QStringList keyIds;  // öffentlich gelistete Schlüssel-IDs
    QString error;
};

// KDM lesen und mit dem privaten Schlüssel (PEM) auspacken
Kdm decryptKdm(const QString &file, const QString &privateKeyFile);

// AES-128-CBC ohne Padding (für die Essenz-Entschlüsselung, wiederverwendbar)
class AesCbc
{
public:
    AesCbc();
    ~AesCbc();
    AesCbc(const AesCbc &) = delete;
    AesCbc &operator=(const AesCbc &) = delete;
    bool setKey(const QByteArray &key);
    // len muss ein Vielfaches von 16 sein; iv wird fortgeschrieben
    bool decrypt(uchar iv[16], const uchar *in, uchar *out, int len);
    bool encrypt(uchar iv[16], const uchar *in, uchar *out, int len);

private:
    void *m_ctx = nullptr;
    QByteArray m_key;
};

QString uuidFromBytes(const uchar *b);
QByteArray uuidToBytes(const QString &uuid);

} // namespace DcpCrypto
