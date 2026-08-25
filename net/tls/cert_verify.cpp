#include "cert_verify.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

namespace browser::net::tls {

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

        DWORD cbDecoded = 0;
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                 szOID_SUBJECT_ALT_NAME2,
                                 ext->Value.pbData,
                                 ext->Value.cbData,
                                 CRYPT_DECODE_ALLOC_FLAG,
                                 nullptr,
                                 nullptr,
                                 &cbDecoded)) {
            return 0;
        }

        std::vector<u8> decoded(cbDecoded);
        bool matched = false;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                szOID_SUBJECT_ALT_NAME2,
                                ext->Value.pbData,
                                ext->Value.cbData,
                                0,
                                nullptr,
                                decoded.data(),
                                &cbDecoded)) {
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

        auto finish =
            [&](HCERTSTORE hs, HCERTSTORE hc, HCERTSTORE hp, std::vector<PCCERT_CONTEXT> &ctxs, PCCERT_CHAIN_CONTEXT ch) {
                cleanup_cert_stores(hs, hc, hp, ctxs, ch);
            };

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
