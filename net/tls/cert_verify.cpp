#include "cert_verify.hpp"

#include "../crypto/bignum.hpp"
#include "../crypto/ecc.hpp"
#include "../crypto/sha.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

namespace browser::net::tls {

    // ---------------------------------------------------------------------------
    // Minimal DER reader (definite-length forms only, as used by X.509)
    // ---------------------------------------------------------------------------

    namespace der {

        struct Elem {
            u8 tag = 0;
            std::size_t content_off = 0;
            std::size_t content_len = 0;
        };

        static bool next(const u8 *d, std::size_t size, std::size_t off, Elem &out) {
            if (off + 2 > size)
                return false;
            out.tag = d[off];
            u8 first = d[off + 1];
            std::size_t hdr = 2;
            u64 len = 0;
            if (first < 0x80) {
                len = first;
            } else {
                u8 n = static_cast<u8>(first & 0x7F);
                if (n == 0 || n > 4 || off + 2 + n > size)
                    return false;
                for (u8 i = 0; i < n; i++) len = (len << 8) | d[off + 2 + i];
                hdr += n;
            }
            if (off + hdr + len > size)
                return false;
            out.content_off = off + hdr;
            out.content_len = static_cast<std::size_t>(len);
            return true;
        }

        static bool children(const u8 *d, std::size_t size, const Elem &parent, std::vector<Elem> &out) {
            std::size_t off = parent.content_off;
            std::size_t end = parent.content_off + parent.content_len;
            while (off < end) {
                Elem e;
                if (!next(d, size, off, e))
                    return false;
                out.push_back(e);
                off = e.content_off + e.content_len;
            }
            return off == end;
        }

    }  // namespace der

    // OIDs
    static const u8 OID_RSA_ENCRYPTION[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    static const u8 OID_EC_PUBLIC_KEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    static const u8 OID_PRIME256V1[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};

    static bool oid_equals(const u8 *d, std::size_t size, const u8 *oid, std::size_t oid_len) {
        return size == oid_len && std::memcmp(d, oid, oid_len) == 0;
    }

    // Extract SubjectPublicKeyInfo from a DER certificate.
    bool extract_certificate_public_key(const std::vector<u8> &cert, PublicKey &out) {
        const u8 *d = cert.data();
        std::size_t size = cert.size();
        der::Elem root;
        if (!der::next(d, size, 0, root) || root.tag != 0x30)
            return false;
        std::vector<der::Elem> top;
        if (!der::children(d, size, root, top) || top.empty())
            return false;
        if (top[0].tag != 0x30)
            return false;

        // TBSCertificate children; skip optional [0] EXPLICIT version.
        std::vector<der::Elem> tbs;
        if (!der::children(d, size, top[0], tbs))
            return false;
        std::size_t i = 0;
        if (!tbs.empty() && tbs[0].tag == 0xA0)
            i = 1;
        // Order per RFC 5280: serialNumber, signature, issuer, validity, subject, SPKI.
        if (tbs.size() < i + 6 || tbs[i].tag != 0x02 || tbs[i + 1].tag != 0x30 || tbs[i + 5].tag != 0x30)
            return false;
        der::Elem spki = tbs[i + 5];

        std::vector<der::Elem> spki_kids;
        if (!der::children(d, size, spki, spki_kids) || spki_kids.size() != 2 || spki_kids[0].tag != 0x30 ||
            spki_kids[1].tag != 0x03)
            return false;

        std::vector<der::Elem> alg;
        if (!der::children(d, size, spki_kids[0], alg) || alg.empty() || alg[0].tag != 0x06)
            return false;
        const u8 *oid = d + alg[0].content_off;
        std::size_t oid_len = alg[0].content_len;

        // BIT STRING content: one unused-bits byte then key bytes.
        if (spki_kids[1].content_len < 1)
            return false;
        const u8 *key = d + spki_kids[1].content_off + 1;
        std::size_t key_len = spki_kids[1].content_len - 1;

        if (oid_equals(oid, oid_len, OID_RSA_ENCRYPTION, sizeof(OID_RSA_ENCRYPTION))) {
            der::Elem rsa_seq;
            if (!der::next(key, key_len, 0, rsa_seq) || rsa_seq.tag != 0x30)
                return false;
            std::vector<der::Elem> rsa_kids;
            if (!der::children(key, key_len, rsa_seq, rsa_kids) || rsa_kids.size() != 2)
                return false;
            out.kind = PubKeyKind::RSA;
            out.n.assign(key + rsa_kids[0].content_off, key + rsa_kids[0].content_off + rsa_kids[0].content_len);
            // DER INTEGER carries a redundant leading 0x00 when the high bit is set;
            // the modulus must be exactly its minimal big-endian encoding.
            while (out.n.size() > 1 && out.n.front() == 0x00) out.n.erase(out.n.begin());
            return !out.n.empty();
        }
        if (oid_equals(oid, oid_len, OID_EC_PUBLIC_KEY, sizeof(OID_EC_PUBLIC_KEY))) {
            if (alg.size() < 2 || alg[1].tag != 0x06)
                return false;
            const u8 *curve = d + alg[1].content_off;
            if (!oid_equals(curve, alg[1].content_len, OID_PRIME256V1, sizeof(OID_PRIME256V1)))
                return false;
            if (key_len < 1 || key[0] != 0x04)
                return false;
            out.kind = PubKeyKind::EC_P256;
            out.point.assign(key, key + key_len);
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // MGF1 + RSASSA-PSS verification (SHA-256, salt length 32 â€” RFC 8446 Â§4.2.3)
    // ---------------------------------------------------------------------------

    static std::vector<u8> mgf1_sha256(const u8 *seed, std::size_t seed_len, std::size_t mask_len) {
        std::vector<u8> mask;
        mask.reserve(mask_len + 32);
        for (u32 counter = 0; mask.size() < mask_len; counter++) {
            std::vector<u8> input(seed, seed + seed_len);
            input.push_back(static_cast<u8>((counter >> 24) & 0xFF));
            input.push_back(static_cast<u8>((counter >> 16) & 0xFF));
            input.push_back(static_cast<u8>((counter >> 8) & 0xFF));
            input.push_back(static_cast<u8>(counter & 0xFF));
            auto h = crypto::SHA256::hash(input.data(), input.size());
            mask.insert(mask.end(), h.begin(), h.end());
        }
        mask.resize(mask_len);
        return mask;
    }

    static bool verify_rsa_pss_sha256(const std::vector<u8> &modulus_be,
                                      const std::vector<u8> &signature,
                                      const std::vector<u8> &content) {
        constexpr std::size_t HLEN = 32;
        constexpr std::size_t SLEN = 32;

        crypto::BigNum n = crypto::BigNum::from_bytes(modulus_be.data(), modulus_be.size());
        crypto::BigNum e(65537);
        crypto::BigNum s = crypto::BigNum::from_bytes(signature.data(), signature.size());
        if (s.compare(n) >= 0)
            return false;

        std::size_t k = modulus_be.size();
        auto m = s.mod_exp(e, n).to_bytes(k);
        if (m.size() != k || m[k - 1] != 0xBC)
            return false;

        std::size_t em_bits = modulus_be.size() * 8 - 1;
        std::size_t em_len = (em_bits + 7) / 8;
        if (k < HLEN + SLEN + 2)
            return false;
        const u8 *masked_db = m.data();
        std::size_t db_len = k - HLEN - 1;
        const u8 *h = m.data() + db_len;

        std::vector<u8> db = mgf1_sha256(h, HLEN, db_len);
        for (std::size_t i = 0; i < db_len; i++) db[i] ^= masked_db[i];
        std::size_t top_bits = 8 * em_len - em_bits;
        db[0] &= static_cast<u8>(0xFF >> top_bits);

        // DB must be 0x00* || 0x01 || salt
        std::size_t idx = 0;
        while (idx < db_len && db[idx] == 0x00) idx++;
        if (idx + 1 + SLEN != db_len || db[idx] != 0x01)
            return false;
        const u8 *salt = db.data() + idx + 1;

        auto m_hash = crypto::SHA256::hash(content.data(), content.size());
        std::vector<u8> m_prime(8 + HLEN + SLEN, 0);
        std::memcpy(m_prime.data() + 8, m_hash.data(), HLEN);
        std::memcpy(m_prime.data() + 8 + HLEN, salt, SLEN);
        auto h_prime = crypto::SHA256::hash(m_prime.data(), m_prime.size());
        return std::memcmp(h, h_prime.data(), HLEN) == 0;
    }

    // ---------------------------------------------------------------------------
    // ECDSA P-256 verification over SHA-256
    // ---------------------------------------------------------------------------

    std::vector<u8> make_certificate_verify_content(const std::string &context,
                                                    const std::vector<u8> &transcript_hash) {
        std::vector<u8> content(64, 0x20);
        content.insert(content.end(), context.begin(), context.end());
        content.push_back(0x00);
        content.insert(content.end(), transcript_hash.begin(), transcript_hash.end());
        return content;
    }

    CVResult verify_server_certificate_verify(const std::vector<std::vector<u8>> &cert_chain,
                                              u16 sig_alg,
                                              const std::vector<u8> &signature,
                                              const std::vector<u8> &transcript_hash) {
        if (sig_alg != 0x0804 && sig_alg != 0x0403)
            return CVResult::UNSUPPORTED_ALGORITHM;
        if (cert_chain.empty())
            return CVResult::NO_PUBLIC_KEY;

        PublicKey pk;
        if (!extract_certificate_public_key(cert_chain[0], pk))
            return CVResult::NO_PUBLIC_KEY;

        static const char server_ctx[] = "TLS 1.3, server CertificateVerify";
        auto content = make_certificate_verify_content(server_ctx, transcript_hash);

        if (sig_alg == 0x0804) {
            if (pk.kind != PubKeyKind::RSA)
                return CVResult::INVALID_SIGNATURE;
            return verify_rsa_pss_sha256(pk.n, signature, content) ? CVResult::VALID : CVResult::INVALID_SIGNATURE;
        }

        if (pk.kind != PubKeyKind::EC_P256)
            return CVResult::INVALID_SIGNATURE;

        // ecdsa_secp256r1_sha256 hashes the full CertificateVerify content.
        auto digest = crypto::SHA256::hash(content.data(), content.size());
        // e = leftmost min(bitlen(order), 256) bits of the digest; both are 256 bits here.
        auto curve = crypto::EllipticCurve::secp256r1();
        crypto::BigNum e = crypto::BigNum::from_bytes(digest.data(), digest.size());

        der::Elem seq;
        if (!der::next(signature.data(), signature.size(), 0, seq) || seq.tag != 0x30)
            return CVResult::INVALID_SIGNATURE;
        std::vector<der::Elem> ints;
        if (!der::children(signature.data(), signature.size(), seq, ints) || ints.size() != 2)
            return CVResult::INVALID_SIGNATURE;
        for (auto &el : ints) {
            if (el.tag != 0x02 || el.content_len == 0 || el.content_len > 33)
                return CVResult::INVALID_SIGNATURE;
        }
        crypto::BigNum rr = crypto::BigNum::from_bytes(signature.data() + ints[0].content_off, ints[0].content_len);
        crypto::BigNum ss = crypto::BigNum::from_bytes(signature.data() + ints[1].content_off, ints[1].content_len);
        if (rr.is_zero() || ss.is_zero() || rr.compare(curve.order) >= 0 || ss.compare(curve.order) >= 0)
            return CVResult::INVALID_SIGNATURE;

        crypto::ECPoint q{crypto::BigNum::from_bytes(pk.point.data() + 1, 32),
                          crypto::BigNum::from_bytes(pk.point.data() + 33, 32),
                          false};
        if (!curve.is_on_curve(q))
            return CVResult::INVALID_SIGNATURE;

        crypto::BigNum w = ss.mod_inverse(curve.order);
        crypto::BigNum u1 = e.mod_mul(w, curve.order);
        crypto::BigNum u2 = rr.mod_mul(w, curve.order);
        crypto::ECPoint p1 = curve.point_mul(curve.generator, u1);
        crypto::ECPoint p2 = curve.point_mul(q, u2);
        crypto::ECPoint result = curve.point_add(p1, p2);
        if (result.is_infinity)
            return CVResult::INVALID_SIGNATURE;
        crypto::BigNum v = result.x.mod_mul(crypto::BigNum(1), curve.order);
        return v.compare(rr) == 0 ? CVResult::VALID : CVResult::INVALID_SIGNATURE;
    }

    static std::string to_lower(const std::string &s) {
        std::string r(s.size(), 0);
        for (size_t i = 0; i < s.size(); i++) r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        return r;
    }

    // Wildcard matches exactly ONE leftmost label: *.example.com matches a.example.com
    // but never a.b.example.com or example.com (N-S2).
    static bool check_hostname_match(const std::string &pattern, const std::string &hostname) {
        std::string lc_pat = to_lower(pattern);
        std::string lc_host = to_lower(hostname);
        if (lc_pat == lc_host)
            return true;
        if (lc_pat.rfind("*.", 0) == 0) {
            std::string suffix = lc_pat.substr(1);  // ".example.com"
            if (lc_host.size() > suffix.size() &&
                lc_host.compare(lc_host.size() - suffix.size(), suffix.size(), suffix) == 0) {
                std::string label = lc_host.substr(0, lc_host.size() - suffix.size());
                if (label.find('.') == std::string::npos && !label.empty())
                    return true;
            }
        }
        return false;
    }

    // Returns: 1 = SAN present and hostname matched, 0 = SAN present, no match,
    // -1 = no SAN extension at all (CN fallback then permitted).
    static int check_san(PCCERT_CONTEXT cert_ctx, const std::string &hostname) {
        PCERT_EXTENSION ext = CertFindExtension(
            szOID_SUBJECT_ALT_NAME2, cert_ctx->pCertInfo->cExtension, cert_ctx->pCertInfo->rgExtension);
        if (!ext) {
            ext = CertFindExtension(
                szOID_SUBJECT_ALT_NAME, cert_ctx->pCertInfo->cExtension, cert_ctx->pCertInfo->rgExtension);
        }
        if (!ext)
            return -1;

        // Two-call pattern: pvInfo=NULL yields the required buffer size.
        DWORD cbDecoded = 0;
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                 szOID_SUBJECT_ALT_NAME2,
                                 ext->Value.pbData,
                                 ext->Value.cbData,
                                 0,
                                 nullptr,
                                 nullptr,
                                 &cbDecoded) ||
            cbDecoded == 0) {
            return 0;
        }

        // CRYPT_DECODE_NOCOPY_FLAG keeps sub-pointers inside our own buffer.
        std::vector<u8> decoded(cbDecoded);
        DWORD cbOut = static_cast<DWORD>(decoded.size());
        bool matched = false;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                szOID_SUBJECT_ALT_NAME2,
                                ext->Value.pbData,
                                ext->Value.cbData,
                                CRYPT_DECODE_NOCOPY_FLAG,
                                nullptr,
                                decoded.data(),
                                &cbOut)) {
            CERT_ALT_NAME_INFO *altInfo = reinterpret_cast<CERT_ALT_NAME_INFO *>(decoded.data());
            for (DWORD i = 0; i < altInfo->cAltEntry; i++) {
                if (altInfo->rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_DNS_NAME) {
                    std::wstring wdns(altInfo->rgAltEntry[i].pwszDNSName);
                    int len = WideCharToMultiByte(CP_UTF8, 0, wdns.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string dns(len - 1, 0);
                        WideCharToMultiByte(CP_UTF8, 0, wdns.c_str(), -1, &dns[0], len, nullptr, nullptr);
                        if (check_hostname_match(dns, hostname)) {
                            matched = true;
                            break;
                        }
                    }
                }
            }
        }
        return matched ? 1 : 0;
    }

    static bool check_cn(PCCERT_CONTEXT cert_ctx, const std::string &hostname) {
        char cn_buf[256];
        DWORD cn_len = sizeof(cn_buf);
        if (!CertGetNameStringA(cert_ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, cn_buf, cn_len)) {
            return false;
        }
        return check_hostname_match(cn_buf, hostname);
    }

    static void cleanup_cert_stores(HCERTSTORE &hs,
                                    HCERTSTORE &hc,
                                    HCERTSTORE &hp,
                                    std::vector<PCCERT_CONTEXT> &ctxs,
                                    PCCERT_CHAIN_CONTEXT &chain) {
        for (auto &c : ctxs) CertFreeCertificateContext(c);
        ctxs.clear();
        if (chain)
            CertFreeCertificateChain(chain);
        if (hp)
            CertCloseStore(hp, 0);
        if (hs)
            CertCloseStore(hs, 0);
        if (hc)
            CertCloseStore(hc, 0);
    }

    // Fail-closed validator (N-S2): any inability to establish trust yields a
    // non-VALID result. Only a chain built to a trusted root with valid
    // signatures, valid validity window and matching hostname passes.
    void validate_certificate_chain(const std::vector<std::vector<u8>> &cert_chain,
                                    const std::string &hostname,
                                    CertValidationResult &out_result) {
        out_result.result = CertResult::MALFORMED;
        out_result.detail.clear();

        auto finish = [&](HCERTSTORE hs,
                          HCERTSTORE hc,
                          HCERTSTORE hp,
                          std::vector<PCCERT_CONTEXT> &ctxs,
                          PCCERT_CHAIN_CONTEXT ch) { cleanup_cert_stores(hs, hc, hp, ctxs, ch); };

        if (cert_chain.empty()) {
            out_result.detail = "empty certificate chain";
            return;
        }

        HCERTSTORE hRootStore = nullptr;
        HCERTSTORE hCaStore = nullptr;
        HCERTSTORE hPeerStore = nullptr;
        std::vector<PCCERT_CONTEXT> cert_contexts;
        PCCERT_CHAIN_CONTEXT pChainContext = nullptr;

        hRootStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, (HCRYPTPROV)0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"ROOT");
        hCaStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, (HCRYPTPROV)0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"CA");
        hPeerStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, (HCRYPTPROV)0, 0, nullptr);
        if (!hPeerStore) {
            out_result.detail = "certificate store unavailable";
            finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        for (auto &der : cert_chain) {
            PCCERT_CONTEXT ctx = CertCreateCertificateContext(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
            if (!ctx) {
                out_result.detail = "certificate decode failed";
                finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
                return;
            }
            cert_contexts.push_back(ctx);
            CertAddCertificateContextToStore(hPeerStore, ctx, CERT_STORE_ADD_ALWAYS, nullptr);
        }

        PCCERT_CONTEXT leaf_ctx = cert_contexts[0];

        FILETIME now_time;
        GetSystemTimeAsFileTime(&now_time);

        if (CompareFileTime(&now_time, &leaf_ctx->pCertInfo->NotBefore) < 0) {
            out_result.result = CertResult::EXPIRED;
            out_result.detail = "certificate not yet valid (notBefore)";
            finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }
        if (CompareFileTime(&now_time, &leaf_ctx->pCertInfo->NotAfter) > 0) {
            out_result.result = CertResult::EXPIRED;
            out_result.detail = "certificate has expired (notAfter)";
            finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        int san = check_san(leaf_ctx, hostname);
        if (san < 0) {
            // No SAN extension: fall back to CN (legacy certificates).
            if (!check_cn(leaf_ctx, hostname)) {
                out_result.result = CertResult::HOST_MISMATCH;
                out_result.detail = "hostname does not match certificate CN";
                finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
                return;
            }
        } else if (san == 0) {
            out_result.result = CertResult::HOST_MISMATCH;
            out_result.detail = "hostname does not match certificate SAN";
            finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        CERT_CHAIN_PARA chain_params = {};
        chain_params.cbSize = sizeof(CERT_CHAIN_PARA);
        chain_params.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        chain_params.RequestedUsage.Usage.cUsageIdentifier = 0;

        if (!CertGetCertificateChain(nullptr,
                                     leaf_ctx,
                                     nullptr,
                                     hPeerStore,
                                     &chain_params,
                                     CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY,
                                     nullptr,
                                     &pChainContext)) {
            out_result.result = CertResult::UNTRUSTED;
            out_result.detail = "chain building failed";
            finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        if (pChainContext->TrustStatus.dwErrorStatus != 0) {
            DWORD err = pChainContext->TrustStatus.dwErrorStatus;
            if (err & CERT_TRUST_IS_UNTRUSTED_ROOT) {
                out_result.result = CertResult::UNTRUSTED;
                out_result.detail = "untrusted root";
            } else if (err & CERT_TRUST_IS_PARTIAL_CHAIN) {
                out_result.result = CertResult::UNTRUSTED;
                out_result.detail = "partial chain";
            } else if (err & CERT_TRUST_IS_NOT_TIME_VALID) {
                out_result.result = CertResult::EXPIRED;
                out_result.detail = "chain contains expired certificate";
            } else if (err & CERT_TRUST_IS_NOT_SIGNATURE_VALID) {
                out_result.result = CertResult::UNTRUSTED;
                out_result.detail = "chain signature invalid";
            } else {
                out_result.result = CertResult::UNTRUSTED;
                out_result.detail = "chain validation failed";
            }
            finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
            return;
        }

        out_result.result = CertResult::VALID;
        out_result.detail.clear();
        finish(hRootStore, hCaStore, hPeerStore, cert_contexts, pChainContext);
    }

}  // namespace browser::net::tls
