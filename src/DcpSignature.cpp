#include "DcpSignature.h"
#include "Tr.h"

#if defined(LUMEN_HAVE_LIBXML2) && defined(LUMEN_HAVE_OPENSSL)
#include <libxml/c14n.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>
#include <vector>
#endif

namespace DcpSignature {

#if defined(LUMEN_HAVE_LIBXML2) && defined(LUMEN_HAVE_OPENSSL)

namespace {

struct Doc
{
    xmlDocPtr d = nullptr;
    explicit Doc(const QByteArray &xml)
    {
        d = xmlReadMemory(xml.constData(), int(xml.size()), "kdm.xml", nullptr, XML_PARSE_NONET);
    }
    ~Doc()
    {
        if (d)
            xmlFreeDoc(d);
    }
};

QString text(xmlNodePtr n)
{
    xmlChar *c = xmlNodeGetContent(n);
    const QString s = c ? QString::fromUtf8(reinterpret_cast<const char *>(c)) : QString();
    xmlFree(c);
    return s;
}

QString attr(xmlNodePtr n, const char *name)
{
    xmlChar *v = xmlGetProp(n, BAD_CAST name);
    const QString s = v ? QString::fromUtf8(reinterpret_cast<const char *>(v)) : QString();
    xmlFree(v);
    return s;
}

bool isName(xmlNodePtr n, const char *local)
{
    return n && n->type == XML_ELEMENT_NODE && xmlStrcmp(n->name, BAD_CAST local) == 0;
}

xmlNodePtr find(xmlNodePtr n, const char *local)
{
    for (xmlNodePtr c = n ? n->children : nullptr; c; c = c->next) {
        if (isName(c, local))
            return c;
        if (xmlNodePtr d = find(c, local))
            return d;
    }
    return nullptr;
}

void findAll(xmlNodePtr n, const char *local, std::vector<xmlNodePtr> *out)
{
    for (xmlNodePtr c = n ? n->children : nullptr; c; c = c->next) {
        if (isName(c, local))
            out->push_back(c);
        findAll(c, local, out);
    }
}

xmlNodePtr findById(xmlNodePtr n, const QString &id)
{
    for (xmlNodePtr c = n ? n->children : nullptr; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE)
            continue;
        for (const char *a : {"Id", "ID", "id"})
            if (attr(c, a) == id)
                return c;
        if (xmlNodePtr d = findById(c, id))
            return d;
    }
    return nullptr;
}

// Kanonische Form eines Teilbaums im Dokumentkontext (geerbte Namensräume inklusive)
QByteArray c14n(xmlDocPtr doc, xmlNodePtr node, const QString &algorithm)
{
    const bool comments = algorithm.endsWith(QLatin1String("#WithComments"));
    int mode = XML_C14N_1_0;
    if (algorithm.contains(QLatin1String("exc-c14n")))
        mode = XML_C14N_EXCLUSIVE_1_0;
    else if (algorithm.contains(QLatin1String("xml-c14n11")))
        mode = XML_C14N_1_1;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx)
        return {};
    ctx->node = node;
    const char *expr = comments ? "(.//. | .//@* | .//namespace::*)"
                                : "(.//. | .//@* | .//namespace::*)[not(self::comment())]";
    xmlXPathObjectPtr set = xmlXPathEvalExpression(BAD_CAST expr, ctx);
    QByteArray out;
    xmlChar *buf = nullptr;
    if (set && set->nodesetval) {
        const int n = xmlC14NDocDumpMemory(doc, set->nodesetval, mode, nullptr, comments ? 1 : 0, &buf);
        if (n >= 0 && buf)
            out = QByteArray(reinterpret_cast<const char *>(buf), n);
    }
    xmlFree(buf);
    xmlXPathFreeObject(set);
    xmlXPathFreeContext(ctx);
    return out;
}

const EVP_MD *digestFor(const QString &uri)
{
    if (uri.endsWith(QLatin1String("sha256")))
        return EVP_sha256();
    if (uri.endsWith(QLatin1String("sha384")))
        return EVP_sha384();
    if (uri.endsWith(QLatin1String("sha512")))
        return EVP_sha512();
    if (uri.endsWith(QLatin1String("sha1")))
        return EVP_sha1();
    return nullptr;
}

QByteArray digest(const EVP_MD *md, const QByteArray &data)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(data.constData(), size_t(data.size()), out, &len, md, nullptr);
    return QByteArray(reinterpret_cast<const char *>(out), int(len));
}

struct X509Free { void operator()(X509 *x) const { X509_free(x); } };
using X509Ptr = std::unique_ptr<X509, X509Free>;

QString subjectOf(X509 *x)
{
    BIO *bio = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(bio, X509_get_subject_name(x), 0, XN_FLAG_RFC2253);
    char *data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    const QString s = QString::fromUtf8(data, int(n));
    BIO_free(bio);
    return s;
}

bool verifyWith(X509 *cert, const EVP_MD *md, const QByteArray &data, const QByteArray &sig)
{
    EVP_PKEY *key = X509_get0_pubkey(cert);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = key && ctx && EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, key) == 1
              && EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char *>(sig.constData()), size_t(sig.size()),
                                  reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size())) == 1;
    EVP_MD_CTX_free(ctx);
    ERR_clear_error();
    return ok;
}

// Signaturzertifikat -> ausstellende Zertifikate der Kette bis zu einer selbstsignierten Wurzel
bool chainOk(X509 *leaf, const std::vector<X509Ptr> &certs)
{
    X509 *cur = leaf;
    for (int depth = 0; depth < 10; ++depth) {
        if (X509_check_issued(cur, cur) == X509_V_OK && X509_verify(cur, X509_get0_pubkey(cur)) == 1)
            return true;
        X509 *issuer = nullptr;
        for (const auto &c : certs)
            if (c.get() != cur && X509_check_issued(c.get(), cur) == X509_V_OK && X509_verify(cur, X509_get0_pubkey(c.get())) == 1)
                issuer = c.get();
        if (!issuer)
            return false;
        cur = issuer;
    }
    return false;
}

} // namespace

bool available() { return true; }

Result verify(const QByteArray &xml)
{
    Result r;
    Doc doc(xml);
    if (!doc.d) {
        r.error = LTR("XML nicht lesbar");
        return r;
    }
    xmlNodePtr root = xmlDocGetRootElement(doc.d);
    xmlNodePtr sigEl = find(root, "Signature");
    if (!sigEl) {
        r.error = LTR("KDM ist nicht signiert");
        return r;
    }
    r.present = true;
    xmlNodePtr signedInfo = find(sigEl, "SignedInfo");
    xmlNodePtr sigValue = find(sigEl, "SignatureValue");
    if (!signedInfo || !sigValue) {
        r.error = LTR("Signatur unvollständig");
        return r;
    }

    // 1. Referenzen
    std::vector<xmlNodePtr> refs;
    findAll(signedInfo, "Reference", &refs);
    if (refs.empty()) {
        r.error = LTR("Signatur ohne Referenzen");
        return r;
    }
    for (xmlNodePtr ref : refs) {
        const QString uri = attr(ref, "URI");
        if (!uri.startsWith(QLatin1Char('#'))) {
            r.error = LTR("Nicht unterstützte Referenz %1").arg(uri);
            return r;
        }
        xmlNodePtr target = findById(root, uri.mid(1));
        if (!target) {
            r.error = LTR("Referenziertes Element %1 fehlt").arg(uri);
            return r;
        }
        QString algo = QStringLiteral("http://www.w3.org/TR/2001/REC-xml-c14n-20010315");
        std::vector<xmlNodePtr> transforms;
        findAll(ref, "Transform", &transforms);
        for (xmlNodePtr t : transforms) {
            const QString a = attr(t, "Algorithm");
            if (a.contains(QLatin1String("c14n")))
                algo = a;
            else if (!a.endsWith(QLatin1String("enveloped-signature"))) {
                r.error = LTR("Nicht unterstützte Transformation %1").arg(a);
                return r;
            }
        }
        const EVP_MD *md = digestFor(attr(find(ref, "DigestMethod"), "Algorithm"));
        if (!md) {
            r.error = LTR("Unbekanntes Digest-Verfahren");
            return r;
        }
        const QByteArray expected = QByteArray::fromBase64(text(find(ref, "DigestValue")).simplified().remove(QLatin1Char(' ')).toLatin1());
        if (digest(md, c14n(doc.d, target, algo)) != expected) {
            r.error = LTR("Inhalt verändert: Prüfsumme von %1 stimmt nicht").arg(uri);
            return r;
        }
    }

    // 2. SignedInfo gegen das Zertifikat des Unterzeichners
    const QString c14nAlgo = attr(find(signedInfo, "CanonicalizationMethod"), "Algorithm");
    const QString sigAlgo = attr(find(signedInfo, "SignatureMethod"), "Algorithm");
    const EVP_MD *md = digestFor(sigAlgo);
    if (!md) {
        r.error = LTR("Unbekanntes Signaturverfahren %1").arg(sigAlgo);
        return r;
    }
    const QByteArray canon = c14n(doc.d, signedInfo, c14nAlgo);
    const QByteArray sig = QByteArray::fromBase64(text(sigValue).simplified().remove(QLatin1Char(' ')).toLatin1());
    std::vector<xmlNodePtr> certNodes;
    findAll(sigEl, "X509Certificate", &certNodes);
    std::vector<X509Ptr> certs;
    for (xmlNodePtr c : certNodes) {
        const QByteArray der = QByteArray::fromBase64(text(c).simplified().remove(QLatin1Char(' ')).toLatin1());
        const unsigned char *p = reinterpret_cast<const unsigned char *>(der.constData());
        if (X509 *x = d2i_X509(nullptr, &p, long(der.size())))
            certs.emplace_back(x);
    }
    for (const auto &c : certs) {
        if (verifyWith(c.get(), md, canon, sig)) {
            r.valid = true;
            r.signer = subjectOf(c.get());
            r.chainValid = chainOk(c.get(), certs);
            if (!r.chainValid)
                r.error = LTR("Signatur gültig, Zertifikatskette unvollständig");
            return r;
        }
    }
    r.error = certs.empty() ? LTR("Kein Zertifikat des Unterzeichners im KDM") : LTR("Signatur ungültig");
    return r;
}

bool sign(const QByteArray &xml, const QStringList &ids, const QByteArray &keyPem, const QByteArray &chainPem,
          QByteArray *out, QString *error)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    BIO *kb = BIO_new_mem_buf(keyPem.constData(), int(keyPem.size()));
    EVP_PKEY *key = PEM_read_bio_PrivateKey(kb, nullptr, nullptr, nullptr);
    BIO_free(kb);
    if (!key)
        return fail(LTR("Privater Schlüssel nicht lesbar"));
    QString certsXml;
    BIO *cb = BIO_new_mem_buf(chainPem.constData(), int(chainPem.size()));
    while (X509 *x = PEM_read_bio_X509(cb, nullptr, nullptr, nullptr)) {
        unsigned char *der = nullptr;
        const int n = i2d_X509(x, &der);
        certsXml += QStringLiteral("<ds:X509Data><ds:X509Certificate>%1</ds:X509Certificate></ds:X509Data>")
                        .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(der), n).toBase64()));
        OPENSSL_free(der);
        X509_free(x);
    }
    BIO_free(cb);
    ERR_clear_error();

    const QString refC14n = QStringLiteral("http://www.w3.org/TR/2001/REC-xml-c14n-20010315");
    QString refs;
    {
        Doc doc(xml);
        if (!doc.d) {
            EVP_PKEY_free(key);
            return fail(LTR("XML nicht lesbar"));
        }
        for (const QString &id : ids) {
            xmlNodePtr n = findById(xmlDocGetRootElement(doc.d), id);
            if (!n) {
                EVP_PKEY_free(key);
                return fail(LTR("Element %1 fehlt").arg(id));
            }
            refs += QStringLiteral("<ds:Reference URI=\"#%1\"><ds:DigestMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#sha256\"/>"
                                   "<ds:DigestValue>%2</ds:DigestValue></ds:Reference>")
                        .arg(id, QString::fromLatin1(digest(EVP_sha256(), c14n(doc.d, n, refC14n)).toBase64()));
        }
    }
    const QString signature = QStringLiteral(
        "<ds:Signature xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"><ds:SignedInfo>"
        "<ds:CanonicalizationMethod Algorithm=\"http://www.w3.org/TR/2001/REC-xml-c14n-20010315#WithComments\"/>"
        "<ds:SignatureMethod Algorithm=\"http://www.w3.org/2001/04/xmldsig-more#rsa-sha256\"/>%1</ds:SignedInfo>"
        "<ds:SignatureValue>@@SIG@@</ds:SignatureValue><ds:KeyInfo>%2</ds:KeyInfo></ds:Signature>").arg(refs, certsXml);
    QByteArray withSig = xml;
    const int close = int(withSig.lastIndexOf("</"));
    if (close < 0) {
        EVP_PKEY_free(key);
        return fail(LTR("Kein Wurzelelement"));
    }
    withSig.insert(close, signature.toUtf8());

    Doc doc(withSig);
    xmlNodePtr si = doc.d ? find(xmlDocGetRootElement(doc.d), "SignedInfo") : nullptr;
    const QByteArray canon = si ? c14n(doc.d, si, QStringLiteral("http://www.w3.org/TR/2001/REC-xml-c14n-20010315#WithComments")) : QByteArray();
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t len = 0;
    QByteArray sig;
    if (ctx && EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1
        && EVP_DigestSign(ctx, nullptr, &len, reinterpret_cast<const unsigned char *>(canon.constData()), size_t(canon.size())) == 1) {
        sig.resize(int(len));
        if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char *>(sig.data()), &len,
                           reinterpret_cast<const unsigned char *>(canon.constData()), size_t(canon.size())) != 1)
            sig.clear();
        sig.resize(int(len));
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    if (canon.isEmpty() || sig.isEmpty())
        return fail(LTR("Signieren fehlgeschlagen"));
    withSig.replace("@@SIG@@", sig.toBase64());
    *out = withSig;
    return true;
}

#else

bool available() { return false; }
Result verify(const QByteArray &)
{
    Result r;
    r.error = LTR("Ohne libxml2/OpenSSL gebaut – Signatur nicht geprüft");
    return r;
}
bool sign(const QByteArray &, const QStringList &, const QByteArray &, const QByteArray &, QByteArray *, QString *error)
{
    if (error)
        *error = LTR("Ohne libxml2/OpenSSL gebaut");
    return false;
}

#endif

} // namespace DcpSignature
