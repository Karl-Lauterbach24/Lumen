#include "DcpCrypto.h"
#include "DcpSignature.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>

#ifdef LUMEN_HAVE_OPENSSL
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

#include <cstring>
#include <memory>

namespace DcpCrypto {

QString uuidFromBytes(const uchar *b)
{
    const QByteArray hex = QByteArray(reinterpret_cast<const char *>(b), 16).toHex();
    return QStringLiteral("%1-%2-%3-%4-%5").arg(QString::fromLatin1(hex.mid(0, 8)), QString::fromLatin1(hex.mid(8, 4)),
                                                 QString::fromLatin1(hex.mid(12, 4)), QString::fromLatin1(hex.mid(16, 4)),
                                                 QString::fromLatin1(hex.mid(20, 12)));
}

QByteArray uuidToBytes(const QString &uuid)
{
    QString s = uuid.trimmed().toLower();
    if (s.startsWith(QLatin1String("urn:uuid:")))
        s = s.mid(9);
    s.remove(QLatin1Char('-'));
    const QByteArray b = QByteArray::fromHex(s.toLatin1());
    return b.size() == 16 ? b : QByteArray();
}

namespace {

QString localName(const QDomElement &e)
{
    const QString t = e.tagName();
    const int i = t.indexOf(QLatin1Char(':'));
    return i >= 0 ? t.mid(i + 1) : t;
}

// Erstes Nachfahren-Element mit diesem Namen (ohne Namensraum)
QDomElement find(const QDomElement &root, const QString &name)
{
    for (QDomElement c = root.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (localName(c) == name)
            return c;
        const QDomElement d = find(c, name);
        if (!d.isNull())
            return d;
    }
    return {};
}

void findAll(const QDomElement &root, const QString &name, QList<QDomElement> *out)
{
    for (QDomElement c = root.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (localName(c) == name)
            out->append(c);
        else
            findAll(c, name, out);
    }
}

QDateTime parseTime(const QString &s)
{
    QDateTime t = QDateTime::fromString(s.trimmed(), Qt::ISODateWithMs);
    if (!t.isValid())
        t = QDateTime::fromString(s.trimmed(), Qt::ISODate);
    return t;
}

} // namespace

#ifdef LUMEN_HAVE_OPENSSL

namespace {

struct Free
{
    void operator()(EVP_PKEY *p) const { EVP_PKEY_free(p); }
    void operator()(X509 *p) const { X509_free(p); }
    void operator()(BIO *p) const { BIO_free_all(p); }
    void operator()(EVP_PKEY_CTX *p) const { EVP_PKEY_CTX_free(p); }
    void operator()(BIGNUM *p) const { BN_free(p); }
};
template <typename T> using Ptr = std::unique_ptr<T, Free>;

QString sslError()
{
    char buf[256] = {};
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

Ptr<X509> loadCert(const QByteArray &pem)
{
    Ptr<BIO> bio(BIO_new_mem_buf(pem.constData(), int(pem.size())));
    return Ptr<X509>(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

Ptr<EVP_PKEY> loadKey(const QByteArray &pem)
{
    Ptr<BIO> bio(BIO_new_mem_buf(pem.constData(), int(pem.size())));
    return Ptr<EVP_PKEY>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

QByteArray certPem(X509 *x)
{
    Ptr<BIO> bio(BIO_new(BIO_s_mem()));
    PEM_write_bio_X509(bio.get(), x);
    char *data = nullptr;
    const long n = BIO_get_mem_data(bio.get(), &data);
    return QByteArray(data, int(n));
}

QByteArray keyPem(EVP_PKEY *k)
{
    Ptr<BIO> bio(BIO_new(BIO_s_mem()));
    PEM_write_bio_PrivateKey(bio.get(), k, nullptr, nullptr, 0, nullptr, nullptr);
    char *data = nullptr;
    const long n = BIO_get_mem_data(bio.get(), &data);
    return QByteArray(data, int(n));
}

QString nameString(X509_NAME *n)
{
    Ptr<BIO> bio(BIO_new(BIO_s_mem()));
    X509_NAME_print_ex(bio.get(), n, 0, XN_FLAG_RFC2253);
    char *data = nullptr;
    const long len = BIO_get_mem_data(bio.get(), &data);
    return QString::fromUtf8(data, int(len));
}

QDateTime asn1Time(const ASN1_TIME *t)
{
    struct tm tm {};
    if (!t || ASN1_TIME_to_tm(t, &tm) != 1)
        return {};
    return QDateTime(QDate(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday), QTime(tm.tm_hour, tm.tm_min, tm.tm_sec), QTimeZone::UTC);
}

// SMPTE 430-2: dnQualifier = Base64(SHA-1(RSAPublicKey-DER))
QString dnQualifierFor(EVP_PKEY *key)
{
    unsigned char *der = nullptr;
    const int n = i2d_PublicKey(key, &der);
    if (n <= 0)
        return {};
    unsigned char md[20];
    unsigned int mdLen = 0;
    EVP_Digest(der, size_t(n), md, &mdLen, EVP_sha1(), nullptr);
    OPENSSL_free(der);
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(md), int(mdLen)).toBase64());
}

QString thumbprint(X509 *x)
{
    unsigned char *der = nullptr;
    const int n = i2d_re_X509_tbs(x, &der);
    if (n <= 0)
        return {};
    unsigned char md[20];
    unsigned int mdLen = 0;
    EVP_Digest(der, size_t(n), md, &mdLen, EVP_sha1(), nullptr);
    OPENSSL_free(der);
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(md), int(mdLen)).toBase64());
}

QString serialString(X509 *x)
{
    Ptr<BIGNUM> bn(ASN1_INTEGER_to_BN(X509_get_serialNumber(x), nullptr));
    if (!bn)
        return {};
    char *dec = BN_bn2dec(bn.get());
    const QString s = QString::fromLatin1(dec);
    OPENSSL_free(dec);
    return s;
}

bool addExt(X509 *cert, X509 *issuer, int nid, const char *value)
{
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
    if (!ext)
        return false;
    const bool ok = X509_add_ext(cert, ext, -1) == 1;
    X509_EXTENSION_free(ext);
    return ok;
}

Ptr<X509> makeCert(EVP_PKEY *key, EVP_PKEY *signKey, X509 *issuer, const QString &org, const QString &cn, int caPathLen)
{
    Ptr<X509> x(X509_new());
    X509_set_version(x.get(), 2);
    // Positive Zufalls-Seriennummer (64 Bit)
    unsigned char rnd[8];
    RAND_bytes(rnd, sizeof(rnd));
    rnd[0] &= 0x7f;
    Ptr<BIGNUM> bn(BN_bin2bn(rnd, sizeof(rnd), nullptr));
    BN_to_ASN1_INTEGER(bn.get(), X509_get_serialNumber(x.get()));
    X509_gmtime_adj(X509_getm_notBefore(x.get()), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x.get()), 60L * 60 * 24 * 3650);
    X509_set_pubkey(x.get(), key);

    X509_NAME *name = X509_get_subject_name(x.get());
    const QByteArray o = org.toUtf8(), ou = QByteArrayLiteral("lumen.player"), c = cn.toUtf8(), dnq = dnQualifierFor(key).toLatin1();
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_UTF8, reinterpret_cast<const unsigned char *>(o.constData()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_UTF8, reinterpret_cast<const unsigned char *>(ou.constData()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char *>(c.constData()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "dnQualifier", MBSTRING_ASC, reinterpret_cast<const unsigned char *>(dnq.constData()), -1, -1, 0);
    X509_set_issuer_name(x.get(), issuer ? X509_get_subject_name(issuer) : name);

    X509 *iss = issuer ? issuer : x.get();
    if (caPathLen >= 0) {
        const QByteArray bc = QStringLiteral("critical,CA:TRUE,pathlen:%1").arg(caPathLen).toLatin1();
        addExt(x.get(), iss, NID_basic_constraints, bc.constData());
        addExt(x.get(), iss, NID_key_usage, "keyCertSign,cRLSign");
    } else {
        addExt(x.get(), iss, NID_basic_constraints, "critical,CA:FALSE");
        addExt(x.get(), iss, NID_key_usage, "digitalSignature,keyEncipherment");
    }
    addExt(x.get(), iss, NID_subject_key_identifier, "hash");
    addExt(x.get(), iss, NID_authority_key_identifier, "keyid:always");
    if (X509_sign(x.get(), signKey, EVP_sha256()) <= 0)
        return {};
    return x;
}

} // namespace

bool available() { return true; }

Identity loadIdentity(const QString &dir)
{
    Identity id;
    id.dir = dir;
    id.leafFile = QDir(dir).filePath(QStringLiteral("leaf.pem"));
    id.chainFile = QDir(dir).filePath(QStringLiteral("chain.pem"));
    id.keyFile = QDir(dir).filePath(QStringLiteral("leaf.key"));
    Ptr<X509> leaf = loadCert(readFile(id.leafFile));
    Ptr<EVP_PKEY> key = loadKey(readFile(id.keyFile));
    if (!leaf || !key)
        return id;
    id.valid = X509_check_private_key(leaf.get(), key.get()) == 1;
    id.subject = nameString(X509_get_subject_name(leaf.get()));
    id.issuer = nameString(X509_get_issuer_name(leaf.get()));
    id.serial = serialString(leaf.get());
    id.thumbprint = thumbprint(leaf.get());
    id.dnQualifier = dnQualifierFor(key.get());
    id.notBefore = asn1Time(X509_get0_notBefore(leaf.get()));
    id.notAfter = asn1Time(X509_get0_notAfter(leaf.get()));
    return id;
}

bool createIdentity(const QString &dir, const QString &organisation, QString *error)
{
    QDir().mkpath(dir);
    Ptr<EVP_PKEY> rootKey(EVP_RSA_gen(2048));
    Ptr<EVP_PKEY> interKey(EVP_RSA_gen(2048));
    Ptr<EVP_PKEY> leafKey(EVP_RSA_gen(2048));
    if (!rootKey || !interKey || !leafKey) {
        if (error)
            *error = QStringLiteral("RSA-Schlüssel konnten nicht erzeugt werden: ") + sslError();
        return false;
    }
    const QString org = organisation.isEmpty() ? QStringLiteral("Lumen") : organisation;
    Ptr<X509> root = makeCert(rootKey.get(), rootKey.get(), nullptr, org, QStringLiteral(".lumen.smpte-430-2.ROOT"), 3);
    Ptr<X509> inter = root ? makeCert(interKey.get(), rootKey.get(), root.get(), org, QStringLiteral(".lumen.smpte-430-2.INTERMEDIATE"), 2) : Ptr<X509>();
    Ptr<X509> leaf = inter ? makeCert(leafKey.get(), interKey.get(), inter.get(), org, QStringLiteral("CS.lumen.smpte-430-2.LEAF"), -1) : Ptr<X509>();
    if (!leaf) {
        if (error)
            *error = QStringLiteral("Zertifikate konnten nicht signiert werden: ") + sslError();
        return false;
    }
    const QByteArray leafPem = certPem(leaf.get()), interPem = certPem(inter.get()), rootPem = certPem(root.get());
    bool ok = writeFile(QDir(dir).filePath(QStringLiteral("root.pem")), rootPem)
              && writeFile(QDir(dir).filePath(QStringLiteral("intermediate.pem")), interPem)
              && writeFile(QDir(dir).filePath(QStringLiteral("leaf.pem")), leafPem)
              && writeFile(QDir(dir).filePath(QStringLiteral("chain.pem")), leafPem + interPem + rootPem)
              && writeFile(QDir(dir).filePath(QStringLiteral("leaf.key")), keyPem(leafKey.get()));
    if (ok) {
        QFile::setPermissions(QDir(dir).filePath(QStringLiteral("leaf.key")), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        // Die CA-Schlüssel werden nicht benötigt (keine weiteren Zertifikate) und nicht gespeichert
    } else if (error) {
        *error = QStringLiteral("Zertifikate konnten nicht gespeichert werden (%1)").arg(dir);
    }
    return ok;
}

bool importIdentity(const QString &dir, const QString &certFile, const QString &keyFile, QString *error)
{
    const QByteArray certData = readFile(certFile);
    const QByteArray keyData = readFile(keyFile);
    Ptr<X509> cert = loadCert(certData);
    Ptr<EVP_PKEY> key = loadKey(keyData);
    if (!cert || !key) {
        if (error)
            *error = QStringLiteral("Zertifikat oder Schlüssel ist kein gültiges PEM");
        return false;
    }
    if (X509_check_private_key(cert.get(), key.get()) != 1) {
        if (error)
            *error = QStringLiteral("Privater Schlüssel passt nicht zum Zertifikat");
        return false;
    }
    QDir().mkpath(dir);
    const QByteArray leafPem = certPem(cert.get());
    // Weitere Zertifikate der Datei (Kette) mit übernehmen
    QByteArray chain = certData.contains("BEGIN CERTIFICATE") ? certData : leafPem;
    const bool ok = writeFile(QDir(dir).filePath(QStringLiteral("leaf.pem")), leafPem)
                    && writeFile(QDir(dir).filePath(QStringLiteral("chain.pem")), chain)
                    && writeFile(QDir(dir).filePath(QStringLiteral("leaf.key")), keyPem(key.get()));
    if (!ok && error)
        *error = QStringLiteral("Speichern fehlgeschlagen (%1)").arg(dir);
    return ok;
}

Kdm decryptKdm(const QString &file, const QString &privateKeyFile)
{
    Kdm kdm;
    kdm.file = file;
    QDomDocument doc;
    {
        QFile f(file);
        const QByteArray data = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        if (data.isEmpty() || !doc.setContent(data)) {
            kdm.error = QStringLiteral("KDM ist kein gültiges XML");
            return kdm;
        }
        // XML-Signatur (Aussteller, Unverändertheit) prüfen
        const DcpSignature::Result sig = DcpSignature::verify(data);
        kdm.signed_ = sig.present;
        kdm.signatureValid = sig.valid;
        kdm.chainValid = sig.chainValid;
        kdm.signer = sig.signer;
        kdm.signatureError = sig.error;
        if (sig.present && !sig.valid && DcpSignature::available()) {
            kdm.error = QStringLiteral("KDM-Signatur ungültig (%1) – Schlüssel werden nicht verwendet").arg(sig.error);
            return kdm;
        }
    }
    const QDomElement root = doc.documentElement();
    if (localName(root) != QLatin1String("DCinemaSecurityMessage")) {
        kdm.error = QStringLiteral("Keine KDM-Datei (DCinemaSecurityMessage erwartet)");
        return kdm;
    }
    kdm.smpte = !root.attribute(QStringLiteral("xmlns")).contains(QLatin1String("digicine"));
    kdm.cplId = find(root, QStringLiteral("CompositionPlaylistId")).text().trimmed().toLower().remove(QStringLiteral("urn:uuid:"));
    kdm.title = find(root, QStringLiteral("ContentTitleText")).text().trimmed();
    kdm.annotation = find(root, QStringLiteral("AnnotationText")).text().trimmed();
    kdm.notBefore = parseTime(find(root, QStringLiteral("ContentKeysNotValidBefore")).text());
    kdm.notAfter = parseTime(find(root, QStringLiteral("ContentKeysNotValidAfter")).text());
    const QDomElement recipient = find(root, QStringLiteral("Recipient"));
    kdm.recipientSerial = find(recipient, QStringLiteral("X509SerialNumber")).text().trimmed();
    kdm.recipientSubject = find(recipient, QStringLiteral("X509SubjectName")).text().trimmed();
    QList<QDomElement> typed;
    findAll(root, QStringLiteral("TypedKeyId"), &typed);
    for (const QDomElement &t : std::as_const(typed))
        kdm.keyIds << find(t, QStringLiteral("KeyId")).text().trimmed().toLower().remove(QStringLiteral("urn:uuid:"));

    Ptr<EVP_PKEY> key = loadKey(readFile(privateKeyFile));
    if (!key) {
        kdm.error = QStringLiteral("Kein privater Schlüssel – zuerst ein Zertifikat für Lumen erzeugen oder importieren");
        return kdm;
    }

    QList<QDomElement> ciphers;
    findAll(find(root, QStringLiteral("AuthenticatedPrivate")), QStringLiteral("CipherValue"), &ciphers);
    int failed = 0;
    for (const QDomElement &c : std::as_const(ciphers)) {
        const QByteArray enc = QByteArray::fromBase64(c.text().simplified().remove(QLatin1Char(' ')).toLatin1());
        Ptr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(key.get(), nullptr));
        size_t outLen = 0;
        if (!ctx || EVP_PKEY_decrypt_init(ctx.get()) <= 0 || EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0
            || EVP_PKEY_decrypt(ctx.get(), nullptr, &outLen, reinterpret_cast<const unsigned char *>(enc.constData()), size_t(enc.size())) <= 0) {
            ++failed;
            continue;
        }
        QByteArray out(int(outLen), '\0');
        if (EVP_PKEY_decrypt(ctx.get(), reinterpret_cast<unsigned char *>(out.data()), &outLen,
                             reinterpret_cast<const unsigned char *>(enc.constData()), size_t(enc.size())) <= 0) {
            ++failed;
            continue;
        }
        out.resize(int(outLen));
        const auto *b = reinterpret_cast<const uchar *>(out.constData());
        ContentKey k;
        int keyIdOff = 0, timeOff = 0, keyOff = 0;
        if (out.size() == 138) { // SMPTE: mit Schlüsseltyp
            k.type = QString::fromLatin1(out.mid(52, 4));
            keyIdOff = 56; timeOff = 72; keyOff = 122;
        } else if (out.size() == 134) { // Interop
            keyIdOff = 52; timeOff = 68; keyOff = 118;
        } else {
            ++failed;
            continue;
        }
        k.cplId = uuidFromBytes(b + 36);
        k.keyId = uuidFromBytes(b + keyIdOff);
        k.notBefore = parseTime(QString::fromLatin1(out.mid(timeOff, 25)));
        k.notAfter = parseTime(QString::fromLatin1(out.mid(timeOff + 25, 25)));
        k.key = out.mid(keyOff, 16);
        kdm.keys.append(k);
    }
    ERR_clear_error();
    if (kdm.keys.isEmpty())
        kdm.error = ciphers.isEmpty() ? QStringLiteral("KDM enthält keine Schlüssel")
                                      : QStringLiteral("KDM ist nicht für dieses Zertifikat ausgestellt (Schlüssel lassen sich nicht auspacken)");
    else if (failed)
        kdm.error = QStringLiteral("%1 von %2 Schlüsseln nicht lesbar").arg(failed).arg(ciphers.size());
    return kdm;
}

AesCbc::AesCbc() : m_ctx(EVP_CIPHER_CTX_new()) {}
AesCbc::~AesCbc() { EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(m_ctx)); }

bool AesCbc::setKey(const QByteArray &key)
{
    if (key.size() != 16)
        return false;
    m_key = key;
    return true;
}

bool AesCbc::decrypt(uchar iv[16], const uchar *in, uchar *out, int len)
{
    auto *ctx = static_cast<EVP_CIPHER_CTX *>(m_ctx);
    if (!ctx || m_key.size() != 16 || len % 16)
        return false;
    if (len == 0)
        return true;
    int n = 0;
    uchar nextIv[16];
    std::memcpy(nextIv, in + len - 16, 16); // CBC: letzter Chiffreblock = nächster IV
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, reinterpret_cast<const uchar *>(m_key.constData()), iv) != 1)
        return false;
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    const bool ok = EVP_DecryptUpdate(ctx, out, &n, in, len) == 1 && n == len;
    std::memcpy(iv, nextIv, 16);
    return ok;
}

bool AesCbc::encrypt(uchar iv[16], const uchar *in, uchar *out, int len)
{
    auto *ctx = static_cast<EVP_CIPHER_CTX *>(m_ctx);
    if (!ctx || m_key.size() != 16 || len % 16)
        return false;
    if (len == 0)
        return true;
    int n = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, reinterpret_cast<const uchar *>(m_key.constData()), iv) != 1)
        return false;
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    const bool ok = EVP_EncryptUpdate(ctx, out, &n, in, len) == 1 && n == len;
    std::memcpy(iv, out + len - 16, 16);
    return ok;
}

#else // ohne OpenSSL: nur unverschlüsselte DCPs

bool available() { return false; }
Identity loadIdentity(const QString &dir) { Identity i; i.dir = dir; return i; }
bool createIdentity(const QString &, const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("Ohne OpenSSL gebaut");
    return false;
}
bool importIdentity(const QString &, const QString &, const QString &, QString *error) { return createIdentity({}, {}, error); }
Kdm decryptKdm(const QString &file, const QString &)
{
    Kdm k;
    k.file = file;
    k.error = QStringLiteral("Ohne OpenSSL gebaut – verschlüsselte DCPs werden nicht unterstützt");
    return k;
}
AesCbc::AesCbc() = default;
AesCbc::~AesCbc() = default;
bool AesCbc::setKey(const QByteArray &) { return false; }
bool AesCbc::decrypt(uchar *, const uchar *, uchar *, int) { return false; }
bool AesCbc::encrypt(uchar *, const uchar *, uchar *, int) { return false; }

#endif

} // namespace DcpCrypto
